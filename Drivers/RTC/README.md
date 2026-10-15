# 内置 RTC 时钟与三个闹钟（STM32F103 + SPL）

已确认使用 PC14/PC15 上的 32.768 kHz 晶振（LSE），不自动切换 LSI。
RTC 使用独立硬件秒计数，不依赖主循环、TIM2 的软件秒累加或 OLED 刷新。
LSE 32768 Hz，RTC 分频寄存器为 32767，得到每秒一次计数。
STM32F103 的 RTC 是 32 位秒计数器，不是直接带时分秒字段的日历外设。

## 默认行为

- 普通上电/普通复位调用 RTCClock_Init，时间为 23:59:55，三个闹钟默认 OFF、00:00:00。
- 经过 Power 模块预备的真正 Standby 唤醒，恢复原有 LSE 秒计数、三个闹钟和应用状态，不重新写 CNT 或分频器。
- 读时用 counter % 86400 转换时分秒；23:59:59 下一秒显示 00:00:00，不每天清零硬件 CNT。
- 不实现日期。使用 10 个 16 位 BKP 寄存器临时保存 Standby 恢复信息，不在普通复位/断电重启后恢复。
- 使用 LSE 独立运行，但普通软件启动仍重设时间；不能宣称支持电池备份下的普通掉电保时恢复。
- 模块拥有 RTC/闹钟和备份域时钟配置；若之前选择了其他 RTC 时钟源，切换到 LSE 需要重置备份域，已有备份寄存器会被清除。
- 接口从主循环调用；RTC_IRQHandler 只清标志并记录提示，不处理菜单、OLED 或等待寄存器写入。

## 非阻塞初始化和写入

```c
RTCClock_Init(Timebase_GetMs());
/* 主循环持续调用，不能只调用 Init 后等待它立即就绪。 */
RTCClock_Process(Timebase_GetMs());
status = RTCClock_GetStatus();
```

公共时间基准先启动，再初始化 RTC。状态机依次等待 LSE、同步标志、寄存器可写、分频器写入、计数器写入、闹钟和中断配置。
等待过程不使用 while 忙等，也不调用 SPL 的 RTC_WaitForSynchro/RTC_WaitForLastTask。
LSE 启动超时默认 5 秒，寄存器同步或写入超时默认 1 秒；超时进入 FAILED，其他模块可以继续运行。
配置位于 rtc_clock_config.h；慢起振的实物可根据测量调整超时，不通过无限等待掩盖问题。

状态：STARTING、READY、UPDATING_TIME、FAILED。
RTCClock_GetError 可区分 LSE、同步、寄存器写入超时。
寄存器写入期间保持主循环调度；GetTime 在设置时间未完成时返回 BUSY，不输出不确定的中间值。

```c
RTCClockTime time = {14, 30, 5};
result = RTCClock_SetTime(&time);
```

SetTime 的 OK 表示请求已接受。完成后状态回到 READY，GetTime 返回新时间。
如果请求返回 BUSY，应稍后重试，不应当作成功；未来串口 TIME 命令应在确认写入成功后回复 OK。
时、分、秒范围分别为 0..23、0..59、0..59，非法参数返回 BAD_ARGUMENT。
RTC 写计数器不会在本驱动中重新设置子秒分频相位，下一秒沿按当前 RTC 相位到来。
硬件高低半字读取使用一致性检查，避免低 16 位进位时得到拼接错误的计数。

## 三个逻辑闹钟，共用一个硬件比较器

STM32F103 RTC 只有一组闹钟比较寄存器，无法直接配置三个独立的硬件闹钟。
三个逻辑槽位编号 1..3，各自保存时分秒和启用状态。
模块计算最近的未来到点，将该秒计数写入 RTC_ALR；到点后安排下一次。
同秒触发的槽位一起记录在 active_mask，bit0/bit1/bit2 对应 ALARM1/2/3。
闹钟按每天重复处理，停止本次提醒不关闭启用状态。

硬件 ALR 中断只提示主循环。主循环同时按 RTC 秒计数检查上次采样到当前时间的区间，
即使比较寄存器更新稍晚或主循环错过精确目标秒，也不会仅因错过等号而漏掉提醒。
长期停掉主循环时不能实时闪灯；恢复后提醒已跨过的闹钟，多次历史到点合并为该槽位一次提醒。
显式 SetTime 会重新建立基准，不把倒拨时间误当成经过了几十年的时间，也不补响手动跳过的目标。
启用/设置闹钟恰好等于当前秒，或设置当前时间恰好命中已启用闹钟，会立即生成一次提醒。

```c
RTCClockTime target = {7, 30, 0};
RTCClockAlarm alarm;

RTCClock_SetAlarm(1, &target, 1);  /* 设置 ALARM1 并开启。 */
RTCClock_EnableAlarm(2, 0);      /* OFF：关闭启用状态，保留时间。 */
RTCClock_DeleteAlarm(3);         /* DEL：清零时间并关闭。 */
RTCClock_GetAlarm(1, &alarm);    /* 查询配置。 */
RTCClock_AcknowledgeAlarm();     /* 停止所有本次提醒，不改变启用状态。 */
```

修改、关闭或删除某个闹钟，会清除该槽位的旧提醒，不影响其他正在提醒的槽位。
GetAlarm/SetAlarm 等接口在初始化未完成时返回 NOT_READY，设置当前时间过程中返回 BUSY。
闹钟设置 OK 表示逻辑配置已保存；硬件比较器由 Process 异步更新，区间检查覆盖期间的到点事件。
所有槽位关闭时硬件比较值设在当前计数之前，不安排近期比较事件。

## 报警 LED 与按键

PB0，已按用户确认的 LED 另一端接 3.3V 的接线配置为低电平点亮、高电平熄灭，外接限流电阻；接线与有效电平在 rtc_clock_config.h 可改。main 的第一步调用 RTCClock_InitAlarmLED，在设置输出模式之前写入灭灯电平；RTC 初始化、无报警和消警后均保持熄灭。复位期间进入 main 之前的瞬态由硬件决定。
报警触发时立即进入亮相位，之后根据 TIM2 毫秒基准亮 500 ms、灭 500 ms，频率 1 Hz。
RTC 决定到点时刻；TIM2 仅提供灯的半秒闪烁节奏，不累计 RTC 时间。
需要主循环及时调用 Process；GPIO 报警 LED 不使用新的 PWM 定时器。

集成示例在 PRESS 事件停止提醒，并记住已用于确认报警的按键手势。
直到该键 RELEASE 前，忽略后续 SHORT/LONG，防止报警停止后同时翻页、选项或保存。
双键同时按住时两个手势都消费，兼容题目正文与评分表的停止操作。

## 工程接入和测试

加入 rtc_clock.c、rtc_core.c，添加 Drivers/RTC 头文件目录。
SPL 需要 rtc、bkp、pwr、rcc、gpio、misc 源码，继续使用现有 CMSIS、启动文件和系统时钟初始化。
普通运行使用 RTC_IRQn/RTC_IRQHandler。Power 模块在真正 Standby 之前配置 EXTI17 上升沿事件；
当前默认 Sleep 无需 EXTI17 或独立 RTCAlarm_IRQHandler。
如果工程已有 RTC_IRQHandler，将 RTC_CLOCK_DEFINE_IRQ_HANDLER 改为 0，并在已有函数中调用 RTCClock_IRQHandler。
模块不修改全局 NVIC 优先级分组，中断优先级在配置文件调整。

examples/key_pwm_main.c 已加入 RTC 初始化、持续处理和报警按键消费示例。
tests/rtc_core_test.c 覆盖一天 86400 个时刻的转换、跨午夜、最近闹钟、三个槽位、同秒触发、
延迟处理、每日重复、停止后不重复、OFF/DEL 和手动调时。
核心测试不替代完整 SPL 工程编译、晶振起振、寄存器写入、RTC 中断和 PB0 闪烁的上板验证。
tests/rtc_clock_state_test.c 使用 tests/rtc_fake 中的最小行为替身测试实际 rtc_clock.c，
覆盖异步初始化、写入串行化、LSE/同步/写入超时、硬件闹钟目标、500 ms 闪烁、
异步调时和未送达中断时的到点补查。替身不是 ST 头文件，也不证明真实 SPL 工程能编译或硬件工作正常。

## Sleep 与 Standby 恢复

默认 Sleep 保留 CPU/RAM 和外设计时，RTC 时间与逻辑闹钟无需序列化，所有 API 继续有效。
真正 Standby 唤醒相当于一次复位，SRAM 不保留。因此 RTCClock_Init 在清除 PWR 的 SBF 前先检查：
是否从 Standby 唤醒、RTC 是否仍使用已启用的 LSE、BKP 快照的版本/CRC/数据是否合法。
只有三项都满足才恢复，否则执行普通上电初始化。

```c
/* Power_Init 不清 SBF。RTCClock_Init 必须先于任何手动 PWR_ClearFlag(SB)。 */
RTCClock_Init(now_ms);
if (RTCClock_WasStandbyResume()) {
    app_data = RTCClock_GetResumeData();
}
```

快照用 BKP_DR1..DR10：版本/提交标记、CRC16、32 位最近秒计数、三个 18 位闹钟数据和应用自定义 32 位。
模块拥有这 10 个备份寄存器；不要同时用于其他业务。CRC 与标记可检测中途复位导致的不完整写入。
应用可以把舵机目标角度和页面编号打包到 app_data；保留内容由应用自行校验范围并恢复。
Power 在进入前调用 RTCClock_CanStandby/PrepareStandby，写入未完成、未同步到硬件的闹钟、待处理 ALRF、
活动提醒都会拒绝进入。取消预备时调用 RTCClock_CancelStandby，清除快照提交标记。

恢复时先以快照中的最近计数补查睡眠期间跨过的闹钟，再清 ALRF 和重排下一次闹钟。
因此硬件报警唤醒和启动延迟不会丢失提醒，同秒到点的多个槽位都能恢复。
即使快照存在，普通 NRST 不满足 SBF 条件，仍按 23:59:55 启动；这是本题当前的明确启动策略。

新增测试：rtc_backup_test 覆盖 86400 次三槽打包还原与 160 个单比特破坏；rtc_clock_state_test
增加 Standby 保留 CNT、补响两个闹钟、CRC 损坏、普通复位，以及进入前 RTC 到点竞态拒绝。

硬件计数器约 136 年回绕。本题不提供长期历法/计数回绕后的连续日期语义，需在溢出前重新设置时间。
初始化/调时/闹钟 API 不得绕过驱动直接写 RTC 寄存器，否则会破坏比较器调度和采样基准。

上板检查：从 23:59:55 计时跨午夜；在三槽配置最近、同秒、跨午夜闹钟；
报警期间任意按键只停灯；OFF 保留时间，DEL 清零；切页、旋转编码器、舵机 PWM 和呼吸动画期间 RTC 正常计时。

参考：
- https://www.st.com/resource/en/datasheet/stm32f103t8.pdf
- https://www.st.com/resource/en/reference_manual/rm0008-stm32f101xx-stm32f102xx-stm32f103xx-advanced-arm-based-32-bit-mcus-stmicroelectronics.pdf
