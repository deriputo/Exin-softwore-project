# I2C1 共享总线

硬件：STM32F103，标准外设库 SPL，I2C1 重映射至 **PB8=SCL、PB9=SDA**，100 kHz。杜邦线连接出现原始数据异常后，先降低速率增加信号裕量，改善程度需要实物验证。两条线需要外部上拉至 3.3 V；多个模块自带上拉时检查并联后的阻值。OLED 与 MPU6050 共用总线，默认 7 位地址分别为 `0x3C`、`0x68`，不是左移后的 `0x78`、`0xD0`。

```c
I2CBus_Init();
IMU_Init(now);
OLED_Init(now);
/* 主循环每次执行，不能只在 100ms 显示任务中调用。 */
I2CBus_Process(now);
IMU_Process(now);  /* 优先让 IMU 获取空闲总线 */
OLED_Process(now);
```

传输是合作式有限状态机，每次 `I2CBus_Process()` 检查标志并前进一步，不等待硬件标志。总线只接受一个在途请求，提交忙时返回 false，由设备驱动下一轮重试。请求和 tx/rx 缓冲区必须存活至 `result` 从 PENDING 改为终态。

支持纯写、纯读和“写寄存器地址 + repeated START + 连续读”。STM32F1 的 1 字节、2 字节以及最后 3 字节收尾分开实现；收尾中短暂关中断仅用于完成不可拆分的 ACK/ADDR/STOP/DR 操作，没有关闭中断等待硬件。

单笔传输超时为 30 ms，NACK、总线错误、仲裁丢失和溢出返回错误并重置 I2C 外设；不会无限卡住主循环。设备驱动 1 秒后重试。外部设备把 SDA 持续拉低时，外设重置未必能解除故障，程序仍会超时并报告失败；本模块不假称已完成物理总线恢复。

`I2CBus_Suspend()` 只有无在途请求时才成功。普通 PWR Sleep 不需要关闭总线，定时器和 I2C 外设仍运行。若执行更深电源模式，先暂停设备调度并等传输完成，之后 Suspend；唤醒后 Resume。

主机测试覆盖读长度 1～14、寄存器 repeated START、纯写、NACK、超时及计时回绕、读缓冲边界、中断状态恢复。模拟测试验证软件状态转换；实物还需要用逻辑分析仪确认 ACK/STOP 时序、上拉、电容和信号质量。

硬件时序依据：[STM32F10xxx RM0008](https://www.st.com/resource/en/reference_manual/cd00171190-stm32f101xx-stm32f102xx-stm32f103xx-stm32f105xx-and-stm32f107xx-advanced-arm-based-32-bit-mcus-stmicroelectronics.pdf)。
