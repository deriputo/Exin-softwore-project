# USART1 非阻塞串口

- PA9 TX、PA10 RX，115200 baud、8 数据位、无校验、1 停止位。
- `Serial_Init()` 后调用 `Serial_ReadByte()` 在主循环读取，向协议模块送入字节。
- RXNE 中断只收字节；TXE 中断只发送字节，不在中断中解析协议或格式化数据。
- RX/TX 各一个 512 字节环形队列，实际可用 511 字节。`Serial_Write()` 全部入队才返回 1；空间不足返回 0，不等待且不发送半条消息。
- `Serial_Write()`、`Serial_ReadByte()`、状态查询均为主循环接口；短临界区保存并恢复原始 PRIMASK。
- 已定义 `USART1_IRQHandler()`。工程原有同名中断应删除，或移除本文件末尾包装函数并在统一中断文件调用 `Serial_IRQHandler()`。

硬件接线：USB 转串口 TX 接 PA10，RX 接 PA9，GND 共地，电平为 3.3 V。USART 不支持在 Standby 下监听普通 RX；待机唤醒必须遵循电源模块定义的唤醒引脚/RTC。

接收错误（ORE/FE/NE/PE）或 RX 队列溢出时，驱动清空未读输入、送出非法 NUL 标记，并丢弃后续字节直到 CR/LF。协议模块会拒绝当前受损命令，防止把缺字的命令当成有效设置。通过 `Serial_GetStats()` 查看接收、丢失和错误计数。

`Serial_TxIdle()` 同时检查队列为空与硬件 TC，确保最后停止位也已发出。`Serial_IsIdle()` 另外检查 RX 队列；应用仍需检查协议模块是否保留半条命令或异步设置请求。

需要 SPL `stm32f10x_usart.c`、GPIO、RCC、`misc.c`，包含此目录以及 `serial.c`、`serial_ring.c`。NVIC 分组由总工程设置，本模块不更改全局分组。

主机测试 `tests/serial_ring_test.c` 验证队列满、跨圈顺序及整条入队。UART 引脚、波特率和中断还需用板卡/串口工具验证。
