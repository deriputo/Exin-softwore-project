# 舵机和开机呼吸灯 PWM（STM32F103 + SPL）

采用方案 1：TIM1_CH1/PA8 驱动舵机，TIM3_CH1/PA6 驱动呼吸灯。
当前v0.21.0开机灯按用户确认改为低有效：3.3V→限流电阻→LED正极，LED负极→PA6。保留约3秒从慢到快呼吸后熄灭，不常亮。
TIM2 由公共时间基准使用，TIM4/PB6/PB7 现用于旋转编码器。
PA9/PA10 预留给串口，PB8/PB9 预留给重映射后的 I2C1。
这些是默认接线安排，请对照实物确认。

## PWM 参数

两个 PWM 定时器都分频到 1 MHz，1 个计数对应 1 us。
初始化读取实际 APB1/APB2 时钟并处理定时器倍频；不支持的配置返回 ERROR。

| 功能 | 引脚 | 定时器 | 频率 | ARR | CCR |
| --- | --- | --- | --- | --- | --- |
| 舵机 | PA8 | TIM1_CH1 | 50 Hz | 19999 | 500..2500 |
| 呼吸灯 | PA6 | TIM3_CH1 | 1 kHz | 999 | 0..1000 |

常见 72 MHz 定时器时钟下，PSC 都为 71；两种频率由不同 ARR 产生。
频率 = TIM 时钟 / ((PSC+1)*(ARR+1))。
PWM1 高电平有效时，占空比 = CCR / (ARR+1)。
LED 的 CCR=1000 大于 ARR=999，对应持续有效输出，即 100% 亮度。

舵机使用高电平有效 PWM：

| 目标角度 | 脉宽 | 占空比 |
| --- | --- | --- |
| 0 度 | 500 us | 2.5% |
| 90 度 | 1500 us | 7.5% |
| 180 度 | 2500 us | 12.5% |

角度到脉宽采用线性映射，并四舍五入到整数 us；这是命令映射，实物角度需要校准。
PWM_Init 默认输出 90 度对应的 1500 us；输入角度和脉宽都做限幅。
舵机电源接独立供电，供电 GND 与 STM32 GND 共地，信号线接 PA8。
LED 使用限流电阻，当前pwm_config.h为LED_PWM_ACTIVE_LOW=1，低电平点亮。TIM3输出反相，亮度0时PA6为高电平，灯灭；亮度100%时PA6为低电平，灯亮。亮度值仍表示点亮比例，无需反转呼吸计算。
GPIO/通道绑定在 pwm.c，不能只更改一个引脚名称就换到任意 GPIO。

TIM1 是高级定时器，必须开启主输出 MOE，代码已调用 TIM_CtrlPWMOutputs。
ARR、CCR 开启预装载，修改 CCR 在下一次更新事件生效，减少周期中途修改产生的异常脉冲。
舵机新命令最多等待一个 20 ms 周期生效，查询接口返回最近请求的脉宽。
初始化使用默认复用映射，并拥有 TIM1 和 TIM3 的全部定时器资源。

## 呼吸效果

- PWM 载波固定 1 kHz，不通过改变载波频率改变呼吸速度。
- 呼吸频率在 3000 ms 内从 0.5 Hz 连续增加到 2 Hz。
- 对变频曲线积分得到相位，再用平滑三次曲线将相位转换成亮度。
- 最后 300 ms 乘以平滑渐暗包络，在 3 秒结束时熄灭，避免任意亮度处突然截止。
- 结束后 LED 的有效占空比为 0，但 1 kHz 定时器仍运行。
- 动画是一次性的，可以重新调用 LEDPWM_BreathStart 重播。

核心按经过的绝对时间计算曲线，不依赖 PWM_Process 的调用次数。
主循环偶尔漏更新不会拖长动画；恢复后会直接计算当前时间应有的亮度。
主循环阻塞时硬件仍输出之前的 PWM，占空比不会自动更新，因此保持主循环非阻塞才能获得平滑呼吸。
不要在中断中执行曲线计算，计算使用整数和 uint64_t，不依赖浮点库。

## 接口

```c
ServoPWM_SetAngle(120);       /* 0..180 度，越界限幅。 */
ServoPWM_SetPulseUs(1500);    /* 500..2500 us，越界限幅。 */
LEDPWM_SetBrightness(500);   /* 0..1000，500 表示 50%；取消当前呼吸动画。 */
LEDPWM_BreathStart(now_ms);  /* 从零亮度开始一次 3 秒动画。 */
PWM_Process(now_ms);        /* 在主循环持续调用，最多每 5 ms 更新一次 CCR。 */
LEDPWM_BreathStop();        /* 手动立即熄灭；自动结束已包含渐暗过程。 */
```

调用接口前先完成 PWM_Init；所有 PWM 接口应从主循环调用。
now_ms 使用 TIM2 的 Timebase_GetMs，计数器正常回绕时可继续运行。
PWM_Init 不自动启动动画，以便由应用决定初始化完成后的启动时刻。

## 工程接入

加入 key.c、key_core.c、timebase.c、pwm.c、pwm_core.c；使用编码器时再加入 encoder.c、encoder_core.c，并添加对应 Drivers 子目录为头文件路径。
SPL 需要 GPIO/RCC/TIM/misc 源码，以及已有 CMSIS、启动文件和系统时钟配置。
初始化顺序：Key_Init → PWM_Init → Encoder_Init（使用时）→ Timebase_Init(Key_ScanTick) → LEDPWM_BreathStart。
完整调用示例见 examples/key_pwm_main.c；不要与工程已有 main 函数同时加入。
原先按键模块内的 TIM2_IRQHandler 已迁移到 timebase.c，按键消抖和长短按规则保持一致。

## 验证

tests/pwm_core_test.c 覆盖频率计算、脉宽/占空比边界、角度限幅、亮度限幅、
呼吸频率单调增加、曲线连续性和 3 秒结束归零。
tests/key_core_test.c 用于回归原有按键逻辑。
这些是主机测试，不能替代 SPL 工程编译和上板测量。

上板用示波器检查 PA8：周期 20 ms，0/90/180 度时脉宽分别为 0.5/1.5/2.5 ms；
PA6：PWM 周期 1 ms，占空比平滑变化，动画结束后保持 LED 熄灭电平。

参考：
- https://www.st.com/resource/en/datasheet/stm32f103t8.pdf
- https://www.st.com/resource/en/reference_manual/cd00171190-stm32f101-103-105-107-stm32f100-series-armbased-32bit-mcus-stmicroelectronics.pdf
