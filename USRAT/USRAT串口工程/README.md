# USRAT 串口工程

基于 STM32F411RET6 + HAL 库的 USART1 + DMA 接收示例工程。

## 工程说明
- 主控：STM32F411RET6
- 串口：USART1（PA9=TX，PA10=RX）
- 波特率：115200-8-N-1
- 接收方式：DMA2_Stream2 + IDLE 空闲中断（不定长接收）
- 发送：printf 重定向（fputc）

## 打开方式
1. 用 STM32CubeMX 打开 `USRAT.ioc` 可查看/重新生成配置
2. 用 Keil MDK 打开 `MDK-ARM/USRAT.uvprojx` 编译下载
3. 串口助手（VOFA+）115200-8-N-1 联调
