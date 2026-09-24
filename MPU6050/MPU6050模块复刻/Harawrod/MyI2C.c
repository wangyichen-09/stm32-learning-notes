#include "stm32f10x.h"                  // Device header
#include "Delay.h"
#include "MPU6050_Reg.h"


/*
 * ==================== 软件模拟I2C驱动 ====================
 * 硬件连接：PB10 = SCL（时钟线，主机控制）
 *          PB11 = SDA（数据线，双向）
 * 总线要求：SDA/SCL外接4.7k上拉电阻到3.3V（开漏输出必须）
 *
 * 核心原理：
 *   1. 开漏输出：引脚只能拉低(写0)，高电平靠外部上拉电阻
 *   2. SCL是节拍：主机每打一个脉冲(低->高->低)，传输1个比特
 *   3. 铁律：SCL低期间SDA可变（换数据），SCL高期间SDA必须稳定（采样）
 * ========================================================
 */


// 定义：PB11 = SDA，PB10 = SCL
void MyI2C_Init(void)
{
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB,ENABLE);   // ① 开GPIOB时钟（所有外设必须先开时钟，否则引脚不工作）
	GPIO_InitTypeDef GPIO_InitStruct;
	GPIO_InitStruct.GPIO_Mode =  GPIO_Mode_Out_OD;        // ② 开漏输出（I2C要求！软件模拟用普通开漏，不用AF_OD复用开漏）
	GPIO_InitStruct.GPIO_Pin = GPIO_Pin_10 | GPIO_Pin_11; // ③ PB10=SCL，PB11=SDA，两个引脚一起配置
	GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;          // ④ 50MHz翻转速度
	GPIO_Init(GPIOB,&GPIO_InitStruct);                     // ⑤ 写入寄存器生效（注意&：传结构体地址）
	GPIO_SetBits(GPIOB,GPIO_Pin_10 | GPIO_Pin_11);         // ⑥ 默认输出高电平=释放总线（I2C空闲时SDA/SCL都为高）
}

// 读SDA电平（主机读取从机放到SDA上的数据）
// 返回值：0=SDA为低电平，1=SDA为高电平
uint8_t MyI2C_R_SDA(void)
{
	uint8_t BitValue;
	BitValue = GPIO_ReadInputDataBit(GPIOB,GPIO_Pin_11);   // 读PB11引脚实际电平（开漏模式下读IDR寄存器即引脚真实电压）
	Delay_us(10);                                          // 延时10us，控制总线速度，避免过快导致从机反应不过来
	return BitValue;
}

// 写SCL电平（主机控制时钟节拍）
// BitValue=0：SCL拉低（换数据窗口，从机/主机可以改变SDA）
// BitValue=1：SCL释放为高（采样窗口，从机读取SDA上的数据）
void MyI2C_W_SCL(uint8_t BitValue)
{
	GPIO_WriteBit(GPIOB,GPIO_Pin_10,(BitAction) BitValue);  // 写PB10（SCL）：0=拉低，1=释放（开漏输出高）
	Delay_us(10);                                          // 每个动作延时10us，控制I2C速度
}

// 写SDA电平（主机把数据放到数据线上）
// BitValue=0：SDA拉低（输出0）
// BitValue=1：SDA释放（输出1，即松手，让从机驱动SDA或靠上拉为高）
void MyI2C_W_SDA(uint8_t BitValue)
{
	GPIO_WriteBit(GPIOB,GPIO_Pin_11,(BitAction) BitValue);  // 写PB11（SDA）：0=拉低，1=释放
	Delay_us(10);
}

// I2C起始条件（告诉从机：我要开始通信了）
// 时序：SCL高电平期间，SDA由高变低（下降沿）
void MyI2C_Start(void)
{
	 MyI2C_W_SDA(1);     // ① 先释放SDA（确保SDA为高，从空闲状态开始）
	 MyI2C_W_SCL(1);     // ② SCL拉高（总线进入高电平状态）
	 MyI2C_W_SDA(0);     // ③ SCL高期间，把SDA拉低 = 起始信号（从机检测到这个下降沿就知道主机要说话了）
	 MyI2C_W_SCL(0);     // ④ SCL拉低，为后续传输数据腾出"换数据窗口"（SCL低时才能改SDA）
}

// I2C停止条件（告诉从机：通信结束，总线释放）
// 时序：SCL高电平期间，SDA由低变高（上升沿）
void MyI2C_Stop(void)
{
	 MyI2C_W_SDA(0);     // ① 先把SDA拉低（从数据传输状态进入停止前的准备）
	 MyI2C_W_SCL(1);     // ② SCL拉高
	 MyI2C_W_SDA(1);     // ③ SCL高期间，把SDA释放为高 = 停止信号（从机检测到这个上升沿就知道通信结束）
}

// 发送一个字节（主机发，从机收）
// Byte：要发送的字节（0x00~0xFF）
// 原理：从最高位bit7开始，SCL低时放数据，SCL高时让从机采样，循环8次
void MyI2C_SendByte(uint8_t Byte)
{
	uint8_t i;
	for(i = 0; i < 8; i++)                  // 循环8次，发8位
	{
		MyI2C_W_SDA(Byte & (0x80 >> i));    // ① SCL低期间，用探针(0x80>>i)取出bit7~bit0放到SDA上
		MyI2C_W_SCL(1);                     // ② SCL拉高：从机在SCL高期间读取这一位
		MyI2C_W_SCL(0);                     // ③ SCL拉低：准备发下一位
	}
}

// 接收一个字节（从机发，主机收）
// 返回值：读到的字节（0x00~0xFF）
// 原理：先释放SDA让从机驱动，SCL高时读SDA，从bit7开始拼，循环8次
uint8_t MyI2C_ReceiveByte(void)
{
	uint8_t i, Byte = 0x00;
	MyI2C_W_SDA(1);                        // ① 关键：先释放SDA（松手），把线让给从机
	for(i = 0; i < 8; i++)
	{
		MyI2C_W_SCL(1);                    // ② SCL拉高：从机把这一位放到SDA上
		if(MyI2C_R_SDA() == 1)             // ③ 读SDA电平
		{
			Byte |= (0x80 >> i);           // 是1就把对应位拼进Byte
		}
		MyI2C_W_SCL(0);                    // ④ SCL拉低：从机准备下一位
	}
	return Byte;
}

// 发送应答位（主机发，从机收）
// AckBit=0：应答（ACK，继续通信）
// AckBit=1：非应答（NACK，结束通信）
void MyI2C_SendAck(uint8_t AckBit)
{
	MyI2C_W_SDA(AckBit);                   // ① 把应答位放到SDA上
	MyI2C_W_SCL(1);                        // ② SCL拉高：从机读取应答位
	MyI2C_W_SCL(0);                        // ③ SCL拉低：收尾
}

// 接收应答位（从机发，主机收）
// 返回值：0=从机应答成功，1=从机不应答（通信失败）
uint8_t MyI2C_ReceiveAck(void)
{
	uint8_t AckBit;
	MyI2C_W_SDA(1);                        // ① 先释放SDA，让从机拉
	MyI2C_W_SCL(1);                        // ② SCL拉高：从机在第9个时钟应答
	AckBit = MyI2C_R_SDA();                // ③ 读SDA：0=从机拉低了=应答，1=没拉低=不应答
	MyI2C_W_SCL(0);                        // ④ SCL拉低：收尾
	return AckBit;
}

// MPU6050写寄存器（应用层：通过I2C往指定寄存器写一个字节）
// reg：寄存器地址（如0x6B=PWR_MGMT_1电源管理）
// data：要写入的值
// 一帧流程：Start -> 设备地址+写位 -> ACK -> 寄存器地址 -> ACK -> 数据 -> ACK -> Stop
void MPU6050_WriteReg(uint8_t reg, uint8_t data)
{
	MyI2C_Start();                          // ① 起始
	MyI2C_SendByte(0x68 << 1);             // ② 发设备地址0x68左移1位（腾出bit0做写位，bit0=0=写）
	MyI2C_ReceiveAck();                     // ③ 等从机应答
	MyI2C_SendByte(reg);                    // ④ 发寄存器地址
	MyI2C_ReceiveAck();                     // ⑤ 等从机应答
	MyI2C_SendByte(data);                   // ⑥ 发数据
	MyI2C_ReceiveAck();                     // ⑦ 等从机应答
	MyI2C_Stop();                           // ⑧ 停止
}

uint8_t MPU6050_ReadReg(uint8_t reg)
{
	uint8_t data;
	
	// ① 起始
	MyI2C_Start();
	
	// ② 发设备地址+写位（0x68左移1位）
	MyI2C_SendByte(0x68 << 1);
	
	// ③ 等从机应答
	MyI2C_ReceiveAck();
	
	// ④ 发寄存器地址
	MyI2C_SendByte(reg);
	
	// ⑤ 等从机应答
	MyI2C_ReceiveAck();
	
	// ⑥ 重复起始（换方向）
	MyI2C_Start();
	
	// ⑦ 发设备地址+读位（0x68左移1位 | 1）
	MyI2C_SendByte(0x68 << 1 | 1);
	
	// ⑧ 等从机应答
	MyI2C_ReceiveAck();
	
	// ⑨ 读数据字节
	data =  MyI2C_ReceiveByte();
	MyI2C_SendAck(1);
	MyI2C_Stop();
	
	return data;
}

void MPU6050_Init(void)
{
	// 1. 唤醒芯片
	MPU6050_WriteReg(MPU6050_PWR_MGMT_1, 0x01);
	Delay_ms(10);
	
	// 2. 配置采样率
	MPU6050_WriteReg(MPU6050_SMPLRT_DIV, 0x09);
	
	// 3. 配置数字低通滤波
	MPU6050_WriteReg(MPU6050_CONFIG, 0x03);
	
	// 4. 配置陀螺仪量程
	MPU6050_WriteReg(MPU6050_GYRO_CONFIG, 0x00);
	
	// 5. 配置加速度量程
	MPU6050_WriteReg(MPU6050_ACCEL_CONFIG, 0x00);
}

void MPU6050_ReadRegs(uint8_t reg, uint8_t *buf, uint8_t len)
{
	uint8_t i;
	MyI2C_Start();
	MyI2C_SendByte(0x68 << 1);
	MyI2C_ReceiveAck();
	MyI2C_SendByte(reg);
	MyI2C_ReceiveAck();
	MyI2C_Start();
	MyI2C_SendByte(0x68 << 1 | 1);
	MyI2C_ReceiveAck();
	for(i = 0; i < len; i++)
	{
		buf[i] = MyI2C_ReceiveByte();
		if(i < len - 1) MyI2C_SendAck(0);
		else MyI2C_SendAck(1);
	}
	MyI2C_Stop();
}
