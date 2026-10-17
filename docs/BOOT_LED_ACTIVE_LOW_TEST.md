# 开机灯低电平呼吸与移除复位诊断

版本`v0.21.0-boot-led-active-low`。旧诊断版`v0.20.0-reset-diagnostics-test`及ZIP保留。

用户澄清：不是LED负极接GND，也不是电源常亮灯；正极接VCC，负极接PA6，继续开机呼吸后熄灭。

接线：**3.3V → 限流电阻 → LED正极；LED负极 → PA6**。裸LED不可省略限流电阻，电源与STM32共地；此处VCC使用3.3V。

仅LED_PWM_ACTIVE_LOW从0改1，使现有TIM3_CH1变为低电平有效。亮度0输出高电平，亮度100%输出低电平；1kHz载波、3000ms从0.5Hz到2Hz及末尾渐灭算法不变，不常亮。PA8舵机PWM极性及HOLD未改。PB0报警灯仍为原低有效配置。

按用户要求移除刚加入的启动RCC_CSR快照/清标志、App_CaptureResetFlags、SYS RESET ?解析/HELP/应答与相应诊断测试；对应应用/协议恢复v0.19.0双键消警基线，额外验证SYS RESET ?已拒绝。诊断文档作为历史记录保留，不表示当前功能。

## 用户测试

1. 断电改接LED，Keil Rebuild并下载，再复位观察。
2. 开机灯应呼吸约3秒，从慢到快，最后熄灭；正常运行和睡眠时不常亮。
3. 报警灯PB0只在闹钟活动时闪烁；两根手指分别按K1、K2可消警，单键不能消警，松开不跳页。
4. USB/蓝牙原指令仍可使用。SYS RESET ?已移除，发送应返回ERR SYNTAX。

软件检查：app_integration、protocol、pwm_core通过；主机检查覆盖原双键行为/协议和呼吸亮度计算，不等于实测PA6波形或Keil编译。用户负责本次编译烧录和实物验收。
