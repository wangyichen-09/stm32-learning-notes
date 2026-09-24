#include "stm32f10x.h"
#include "Delay.h"
#include "MPU6050_REG.h"

int main(void)
{
	uint8_t buf[14];
	int16_t AccX, AccY, AccZ;
	int16_t GyroX, GyroY, GyroZ;
	float accX_g, accY_g, accZ_g;
	float gyroX_dps, gyroY_dps, gyroZ_dps;
	
	MyI2C_Init();           // 初始化I2C引脚
	Delay_ms(100);
	
	MPU6050_Init();          // 初始化MPU6050（唤醒+配置）
	Delay_ms(10);
	
	while(1)
	{
		// 连续读14个字节（从0x3B开始）
		MPU6050_ReadRegs(MPU6050_ACCEL_XOUT_H, buf, 14);
		
		// 拼加速度数据（高8位左移8位 + 低8位 = 16位）
		AccX = (buf[0] << 8) | buf[1];
		AccY = (buf[2] << 8) | buf[3];
		AccZ = (buf[4] << 8) | buf[5];
		
		// 拼陀螺仪数据
		GyroX = (buf[8] << 8) | buf[9];
		GyroY = (buf[10] << 8) | buf[11];
		GyroZ = (buf[12] << 8) | buf[13];
		
		// 换算成实际值
		// 加速度：±2g量程，16384 LSB/g
		accX_g = AccX / 16384.0f;
		accY_g = AccY / 16384.0f;
		accZ_g = AccZ / 16384.0f;
		
		// 陀螺仪：±250°/s量程，131 LSB/(°/s)
		gyroX_dps = GyroX / 131.0f;
		gyroY_dps = GyroY / 131.0f;
		gyroZ_dps = GyroZ / 131.0f;
		
		Delay_ms(100);  // 100ms读一次（10Hz）
	}
}
