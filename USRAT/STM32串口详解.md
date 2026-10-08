# STM32 串口详解

## 一、串口是什么，能做什么

**串口（UART，通用异步收发器）** 是一种**异步串行通信**外设：数据一位一位地按顺序发送（串行），没有独立时钟线、靠双方约定的**波特率**对齐节奏（异步），能发（TX）也能收（RX）。它是 STM32 内部的一个外设模块，通过两根引脚（TX/RX）和外界交换数据。

串口能做什么：



| 场景    | 具体用途                                    |
| ----- | --------------------------------------- |
| 调试打印  | `printf("温度=%d", temp)` 输出到电脑串口助手，看程序状态 |
| 传感器通信 | GPS、激光雷达、蓝牙模块等大量用串口输出数据                 |
| 设备对接  | 对接单片机 / PC / 工业设备，传输命令和数据               |
| 固件升级  | 很多设备通过串口 Bootloader 烧写程序                |

对初学者来说，串口调试打印是第一个用途 —— 它相当于单片机的 "眼睛"，让你看到程序内部发生了什么。

本文用到的硬件：主控 **STM32F411RET6**（Cortex-M4，100MHz），串口 **USART1**（PA9=TX，PA10=RX），联调工具 **USB 转 TTL 模块 + VOFA+**，开发环境 **STM32CubeMX + Keil MDK**。

## 二、硬件准备与接线

材料清单：



| 材料                         | 用途            |
| -------------------------- | ------------- |
| STM32F411 开发板              | 主控，本文用 USART1 |
| USB 转 TTL 模块（CH340/CP2102） | 连接电脑和单片机      |
| USB 线 x2                   | 供电 + 连接模块     |
| 杜邦线 x3                     | TX/RX/GND 接线  |
| VOFA+                      | 串口调试上位机       |

接线表：



```
STM32开发板              USB转TTL模块
  PA9 (USART1_TX)  ────→   RX
  PA10 (USART1_RX) ────→   TX
  GND               ────→   GND
```

两个关键点：**TX 接 RX、RX 接 TX（交叉）**—— 发送对接收、接收对发送，接成 TX-TX 或 RX-RX 通信不了；**必须共地（GND 接 GND）**——TX/RX 是电压信号，收发双方必须以同一个地为基准判断高低电平，不共地会收不到或乱码。

通信参数约定：**波特率 115200、数据位 8、校验位 None、停止位 1**，简写为 **"115200-8-N-1"**。VOFA+ 和单片机代码里必须完全一致，否则乱码。

## 三、串口核心原理

UART 是异步通信，没有时钟线，收发双方靠约定的波特率对齐。一个字节被拆成一帧传输：



```
  起始位    数据位(8bit)        停止位
  ┌────┐ ┌──────────────────┐ ┌───┐
  │  0 │ │ D0 D1 ... D7     │ │ 1 │
  └────┘ └──────────────────┘ └───┘
```

一帧 = 1 起始位（低电平，告诉接收方 "我要开始了"）+ 8 数据位（低位先发）+ 1 停止位（高电平，回到空闲）。这就是 "8N1" 的含义。

**波特率 = 每秒传输的 bit 数**。115200 bps 换算成字节 ≈ 11520 字节 / 秒。因为异步通信没有时钟线，接收方只能靠波特率推算每个 bit 的宽度，所以**双方波特率必须严格一致**（误差一般要求 <2%），否则接收方在错误的时刻采样 → 乱码。

**同步 vs 异步（通信方式）**：异步（UART）没有时钟线、靠波特率对齐、2 根线（TX+RX）；同步（SPI/I2C）有独立时钟线（SCLK）、靠时钟线对齐、3 根线以上。

**TTL vs RS232 vs RS485（电气电平标准）**：



| 标准    | 电平 / 方式       | 传输距离   | 场景      |
| ----- | ------------- | ------ | ------- |
| TTL   | 0\~3.3V/5V 单端 | <1 米   | 单片机直接输出 |
| RS232 | ±12V 单端       | \~15 米 | 老式电脑串口  |
| RS485 | 差分信号          | 1200 米 | 工业现场    |

本文用的就是 **UART（异步）+ TTL（电平）** 的组合，最基础最常用。

**三种收发方式**：



| 方式  | CPU 参与  | 适合场景    |
| --- | ------- | ------- |
| 轮询  | 全程等待    | 简单、数据少  |
| 中断  | 每字节打断一次 | 少量、零星数据 |
| DMA | 几乎不参与   | 大量、连续数据 |

本文重点用的是 **轮询发送（printf）+ DMA 接收**：发送量小用阻塞简单，接收量大用 DMA 不占 CPU。

## 四、工程结构与 HAL 分层

CubeMX 生成的标准工程分三大块：



```
USRAT/
├── USRAT.ioc               ← CubeMX 配置文件
├── .mxproject              ← CubeMX 辅助缓存文件
├── Core/                   ★ 用户代码区（重点）
│   ├── Inc/                头文件
│   └── Src/                源文件
├── Drivers/                ST官方库（不要改）
└── MDK-ARM/                Keil工程
```

Core/ 是你写代码的地方，Drivers/ 是 ST 官方库只读不碰，MDK-ARM/ 是编译入口（双击 `.uvprojx` 打开）。

HAL 库把外设初始化拆成三层：**参数层** `MX_USART1_UART_Init()` 填结构体（波特率、数据位、校验），**底层层** `HAL_UART_MspInit()` 管时钟、引脚复用、DMA、NVIC 中断，**用户层** 是你的业务逻辑。调用关系：`MX_USART1_UART_Init()` → `HAL_UART_Init()`（把参数写进寄存器）→ `HAL_UART_MspInit()`（自动调用，配底层）。`HAL_UART_Init`**&#x20;管 "串口自己"，**`HAL_UART_MspInit`**&#x20;管 "串口周围"，两者合起来串口才能工作。**

main () 初始化顺序：



```
HAL_Init()               ① 总开关：HAL库全局初始化（SysTick时基）
SystemClock_Config()     ② 系统时钟：配置频率（100MHz）
MX_GPIO_Init()           ③ GPIO端口时钟使能
MX_DMA_Init()            ④ DMA2时钟 + Stream2中断
MX_USART1_UART_Init()    ⑤ 串口初始化（参数 + MspInit）
HAL_UART_Receive_DMA()   ⑥ 启动DMA接收
while(1)                 ⑦ 主循环
```

顺序不能乱：串口要用 GPIO 引脚和 DMA，所以 GPIO、DMA 必须先初始化；DMA 要在串口前（MspInit 里要绑定）；依赖的必须先就绪。

## 五、代码实现

串口初始化的本质是 "按顺序把硬件准备好"，整个过程 7 步：



```
① HAL_Init()               总开关：HAL库全局初始化（SysTick时基）
② SystemClock_Config()     系统时钟：配置频率（100MHz）
③ MX_GPIO_Init()           GPIO端口时钟使能
④ MX_DMA_Init()            DMA2时钟 + Stream2中断
⑤ MX_USART1_UART_Init()    串口配置（参数 + MspInit）
⑥ HAL_UART_Receive_DMA()   启动DMA接收
⑦ while(1)                 主循环
```

核心代码：



```
int main(void)
{
  HAL_Init();               // ① 总开关
  SystemClock_Config();     // ② 系统时钟（100MHz）
  MX_GPIO_Init();           // ③ GPIO端口通电
  MX_DMA_Init();            // ④ DMA2通电+中断
  MX_USART1_UART_Init();    // ⑤ 串口配置
  HAL_UART_Receive_DMA(&huart1, RXbuf, 50);   // ⑥ 启动接收
  __HAL_UART_ENABLE_IT(&huart1, UART_IT_IDLE); // 使能IDLE中断
  while (1) { }             // ⑦ 主循环
}
```

串口配置核心：



```
void MX_USART1_UART_Init(void)
{
  huart1.Instance = USART1;             // 用哪个串口
  huart1.Init.BaudRate = 115200;        // 波特率
  huart1.Init.WordLength = UART_WORDLENGTH_8B;  // 数据位8
  huart1.Init.StopBits = 1;             // 停止位1
  huart1.Init.Parity = NONE;            // 无校验
  huart1.Init.Mode = TX_RX;             // 收发都开
  HAL_UART_Init(&huart1);               // 写进硬件寄存器
}
```

`HAL_UART_Init` 内部自动调用 `HAL_UART_MspInit()` 配置底层（时钟 / 引脚 / DMA / 中断）。

**printf 重定向**：单片机没有屏幕，`printf` 默认写标准输出（不存在）。重定向 = 改写 printf 的底层函数 `fputc`，让字符改道走串口发出去。



```
#include <stdio.h>

int fputc(int ch, FILE *f)
{
  uint8_t data = (uint8_t)ch;
  HAL_UART_Transmit(&huart1, &data, 1, 0xFFFF);
  return ch;
}
```

两个必须：`#include <stdio.h>`；Keil 里勾选 **Use MicroLIB**（Options → Target → Use MicroLIB），不勾 printf 不重定向。使用：`printf("USART1 printf test OK!\r\n");`

**DMA + IDLE 不定长接收**：数据量大 → 用 DMA 自动搬，CPU 不参与；不知道一包多长 → 用 IDLE 空闲中断，数据停了说明一包收完。



```
HAL_UART_Receive_DMA(&huart1, RXbuf, 50);       // DMA接收50字节
__HAL_UART_ENABLE_IT(&huart1, UART_IT_IDLE);    // 使能IDLE中断
```

IDLE 中断处理（五步套路，缺一不可）：



```
void USART1_IRQHandler(void)
{
  HAL_UART_IRQHandler(&huart1);

  if(__HAL_UART_GET_FLAG(&huart1, UART_FLAG_IDLE) != RESET)  // ① 查IDLE
  {
    __HAL_UART_CLEAR_FLAG(&huart1, UART_FLAG_IDLE);          // ② 清IDLE（必须，否则死循环）
    HAL_UART_DMAStop(&huart1);                               // ③ 停DMA
    uint16_t len = 50 - __HAL_DMA_GET_COUNTER(&hdma_usart1_rx); // ④ 算长度
    printf("recv %d bytes\r\n", len);                        // ⑤ 处理数据
    HAL_UART_Receive_DMA(&huart1, RXbuf, 50);                // ⑥ 重启接收（必须，否则只收一次）
  }
}
```

收完后数据在 `RXbuf[0..len-1]`，直接使用：



```
if (len == 2 && RXbuf[0]=='o' && RXbuf[1]=='n')
{
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);  // 收到"on"亮灯
}
```

## 六、验证与调试

联调步骤：① 接线（PA9→模块 RX、PA10→模块 TX、GND→GND）② USB 转 TTL 插电脑，确认识别到 COM 口 ③ 打开 VOFA+，选对应 COM 口，波特率设 115200 ④ Keil 编译下载 ⑤ 复位看 printf 输出，发送区输入字符验证接收。

验证清单：



| 测试   | 操作               | 预期结果                          |
| ---- | ---------------- | ----------------------------- |
| 发送测试 | 复位开发板            | 串口显示 "USART1 printf test OK!" |
| 连续发送 | 观察串口             | 每 500ms 打印一条（如果主循环里有）         |
| 接收测试 | VOFA+ 发送 "hello" | 串口显示 "recv 5 bytes"           |

常见现象排查表：



| 现象         | 可能原因              | 检查方法                                               |
| ---------- | ----------------- | -------------------------------------------------- |
| 完全收不到      | 接线错误 / 没共地        | 检查 TX↔RX 交叉、GND 连接                                 |
| 乱码         | 波特率不一致            | VOFA+ 和代码波特率都设 115200                              |
| 能发不能收      | 没启动接收 / IDLE 中断没开 | 检查 `HAL_UART_Receive_DMA` 和 `__HAL_UART_ENABLE_IT` |
| 程序死机 / 卡死  | 中断里指针错误 / 标志没清    | 检查 IDLE 标志是否清除、`%s` 是否传了地址                         |
| printf 不输出 | MicroLIB 没勾       | Keil → Options → Target → Use MicroLIB             |

## 七、总结

核心要点回顾：



| 模块  | 核心内容                                     |
| --- | ---------------------------------------- |
| 原理  | UART 异步通信，一帧 = 起始位 + 数据位 + 停止位；波特率双方必须一致 |
| 接线  | TX↔RX 交叉、必须共地、参数 115200-8-N-1            |
| 工程  | CubeMX 三层结构：Init 参数层 / MspInit 底层 / 用户代码 |
| 初始化 | 总开关 → 时钟 → 通电 → 通电 → 配置 → 启动 → 循环        |
| 发送  | printf 重定向（fputc + MicroLIB）             |
| 接收  | DMA + IDLE 不定长接收（查→清→停→算→处理→重启）          |
| 调试  | VOFA+ 联调，按排查表逐项定位                        |
