# PWR 电源管理：默认 Sleep，60 秒空闲关屏

按用户确认使用 Sleep。当前 KEY1 是 PA0 低电平按下，不改动接线。
默认 60 秒无人操作进入 UI 空闲状态，应用关闭 OLED；CPU 可在每轮主循环处理完就绪任务后执行 WFI。
Sleep 只停 CPU 执行，TIM1 舵机、TIM2 扫描、TIM3 PWM、TIM4 编码器、USART、I²C 和 LSE RTC 持续运行。
TIM2 每 5 ms 唤醒执行下一轮；USART/RTC/DMA 中断也可以唤醒。关屏后的首次按键/编码器/串口输入由应用唤醒 UI。
Sleep 不复位，不丢失 SRAM、时间、页面或舵机目标；IMU 采样和姿态控制继续调度。

## 接入

工程添加 power.c、power_core.c 和 Drivers/Power include 路径；SPL 使用 pwr、rcc、exti、gpio。
公共时间基准持续运行。PWR 不修改系统时钟或中断优先级分组。

```c
Power_Init(Timebase_GetMs());
RTCClock_Init(Timebase_GetMs()); /* 不提前清 SBF，保留 Standby 恢复机会。 */
Power_SetMode(POWER_MODE_SLEEP);
Power_SetIdleTimeout(60000u);

/* 主循环：调用所有非阻塞任务，处理输入，并将活动时间告知 Power。 */
Power_Touch(now_ms); /* 仅在用户活动时调用，不能每轮都调用。 */
Power_Process(now_ms, busy_flags);
/* 根据 Power_IsIdle() 通知 OLED；显示控制由应用负责。 */
Power_SleepUntilInterrupt(); /* 就绪任务清空后调用，不从中断调用。 */
```

POWER_BUSY_STARTUP/ALARM/HOLD/TRACE/SERIAL/DISPLAY/INPUT/FAULT 中任一标志抑制自动空闲。
这些标志保护 UI 与真正 Standby；应用仍可在一轮就绪工作完成后执行 CPU Sleep。
用户按下/松开、旋转编码器和串口接收均应调用 Power_Touch；时间自然变化、IMU 周期采样不应算用户活动。
SetIdleTimeout(0) 关闭自动空闲。毫秒差使用无符号减法，支持 TIM2 毫秒计数回绕。

Power_SleepUntilInterrupt 保存 PRIMASK，暂时禁止异常处理后执行 WFI，再恢复原始屏蔽状态。
已使能中断即使暂时被 PRIMASK 屏蔽，也能使 Cortex-M3 从 WFI 恢复，之后才执行处理函数。
若调用前已关闭中断则拒绝休眠，避免让调用者误以为会正常运行中断。最大正常扫描等待由 TIM2 的 5 ms 提供。

## 可选的真正 Standby

`Power_SetMode(POWER_MODE_STANDBY)` 可选，当前工程默认不启用。
进入真正 Standby 后 SRAM/普通外设停止，舵机 PWM 不再输出；唤醒是 CPU 复位。
PWR 模块有就绪检查、非阻塞预备/取消钩子、备份状态 CRC，以及最后进入前 RTC 待处理到点检查。
RTC 写入/启动未完成、报警、HOLD、TRACE、串口或显示未完成时不得进入。
应用应在钩子里完成 OLED 关屏与外设安全处理，不在中断中等待；`prepare` 返回 0 等待下一轮、1 表示就绪。
若 prepare 发起最后一笔 OLED 传输，单独 DISPLAY busy 会保留预备状态直至传输排空，用户活动取消预备。
`cancel` 用于恢复被预备操作停止的外设。若因待处理中断等条件未能进入，驱动恢复正常 SCR/PDDS/PRIMASK。

```c
Power_SetBackupData((uint32_t)servo_angle | ((uint32_t)page << 8));
Power_SetStandbyHooks(PrepareOutputs, RestoreOutputs);
Power_SetStandbyGuard(StandbyInputsStillQuiet);
```

Standby guard 在屏蔽中断后、快照写入前和最终 WFI 前各调用一次，只允许有界只读检查。
应用应再次检查 UART 硬件 RXNE/错误标志以及 RX 队列、按键物理/稳定状态、编码器 CNT 是否变化、
HOLD/TRACE 和总线事务是否仍空闲，避免主循环忙碌检查之后的新输入被 Standby 复位覆盖。
不要在 guard 等待传输、执行页面操作或改变中断屏蔽状态；返回 0 取消预备、保持正常运行。

当前 `POWER_STANDBY_ENABLE_PA0_WKUP=0`：真正 Standby 只由 RTC 已启用闹钟或 NRST 唤醒。
KEY1/KEY2、编码器、UART 在此模式下均不能作为普通外设唤醒源。没有已启用闹钟时需 NRST。
PA0/WKUP 是上升沿唤醒，EWUP 使能后强制输入下拉；现有低有效上拉 KEY1 与它不兼容。
官方文档明确：PA0 已经为高电平时打开 EWUP 也产生唤醒事件，因此不能直接启用现有接线。
只有另配 PA0 高有效唤醒按钮并迁走 KEY1 后，才能把 `POWER_STANDBY_ENABLE_PA0_WKUP` 改为 1；
即使启用，驱动会在 PA0 当前为高时拒绝进入。

Power 所有进入操作均在主循环进行。实际进入 Standby 前启用 EXTI17 RTC 事件并清 WUF，
清 WUF 后重新检查 RTC，避免在清标志前到点又被覆盖。RTC ALRF 在恢复逻辑补查之后才清除。
RTCClock_WasStandbyResume/GetResumeData 提供有 CRC 验证的恢复结果；Power_WasStandbyWake 仅报告来源。
普通上电/NRST 仍按考题默认时间 23:59:55，不能把此功能宣称为通用掉电保时。

## 验证范围

power_core_test：30 秒阈值、活动重置、busy 抑制、毫秒回绕、关闭自动空闲。
power_state_test：PRIMASK/WFI、默认模式、异步预备保留和取消、RTC 不就绪/进入前到点拒绝、
屏蔽中断后的两次应用输入检查、未进入时清理。
rtc_clock_state_test/rtc_backup_test：真正 Standby 快照还原、每日闹钟补查和 CRC。
上述为电脑行为替身测试，不能证明板上 PWR、晶振、电流、PWM 和实际唤醒可靠；必须完成 SPL 工程编译和上板验证。

上板应检查：60 秒关屏、按键旋钮串口唤醒、睡眠时持续 50 Hz 舵机 PWM、跨午夜 RTC、闹钟唤醒并点亮报警灯。
测量电流时同时考虑 OLED、IMU、舵机、供电模块和调试器，不能用 CPU Sleep 直接推断整板微安电流。

参考：[ST RM0008](https://www.st.com/resource/en/reference_manual/rm0008-stm32f103xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)，
[Arm Cortex-M3 Generic User Guide](https://documentation-service.arm.com/static/5ea823e69931941038df1af5)。
