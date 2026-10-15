# 旋转编码器（STM32F103 + SPL，TIM4 编码器模式）

TIM4_CH1/PB6 接 A 相，TIM4_CH2/PB7 接 B 相。公共触点接 GND，两个输入内部上拉。
若使用带电路的模块，按模块接线说明接 3.3 V/GND，A/B 输出接 PB6/PB7。
编码器按压开关不使用，保持题目要求的两个独立按键。

## 资源安排

| 功能 | 资源 |
| --- | --- |
| 按键扫描与时间基准 | TIM2，每 5 ms 中断 |
| 舵机 | TIM1_CH1/PA8 |
| 开机呼吸灯 | TIM3_CH1/PA6 |
| 旋转编码器 | TIM4_CH1/PB6、TIM4_CH2/PB7 |
| USART1 预留 | PA9/PA10 |
| OLED/IMU I2C1 预留 | PB8/PB9（需 I2C1 重映射） |

TIM4 默认输入占用了先前预留的 PB6/PB7，所以未来 I2C1 模块改用 PB8/PB9，
初始化 I2C 时需要 GPIO_PinRemapConfig(GPIO_Remap_I2C1, ENABLE)。
当前编码器模块不擅自启用 I2C 重映射或初始化 I2C。
STM32F103C8T6 的 48 引脚封装没有 TIM4 重映射所需的 PD12/PD13，不能用它们避开冲突。
此模块拥有 TIM4 的全部定时器资源，不能同时拿 TIM4 其他通道生成独立 PWM。

## 硬件初始化

- GPIO_Mode_IPU：GPIO 输入模式，而不是 PWM 使用的复用推挽输出。
- PSC=0：不对编码器边沿计数分频。
- ARR=65535：使用全部 16 位计数范围。
- TIM_EncoderMode_TI12：对 TI1、TI2 两路的上升/下降边沿计数，得到四倍频。
- 两路 TIM_ICPolarity_Rising：在编码器模式中表示输入不反相，不表示只数上升沿。
- TIM_ICPSC_DIV1：两路输入捕获不分频。
- CKD=DIV4，ICFilter=15：使用较强的输入数字滤波。
- 不启用 TIM4 中断、DMA，也不添加 TIM4_IRQHandler。

定时器根据 A/B 相位关系自行更新 CNT 和方向，CPU 不需要在每个边沿上执行程序。
主循环定期读一次 CNT，只做整数差值和每格换算，不影响 TIM2 的计时方式。
它是上电/软件归零之后的相对位置，不能得知旋钮上电前的绝对角度。

## 原始计数和每格增量

硬件原始位置由 Encoder_GetCounter 返回，范围是 0..65535。
顺逆方向跨零时自然回绕，例如 65535→0 或 0→65535。
软件通过相邻两次计数的模 65536 差值，还原带正负号的位移：

```text
上次 CNT=65534，这次 CNT=2     -> 原始增量 +4
上次 CNT=2，这次 CNT=65534     -> 原始增量 -4
```

不能使用“读取 CNT 后清零”的方式：读取和清零之间的边沿可能丢失。
本驱动正常读取和软件归零都不写硬件 CNT，只有初始化时设置 CNT=0。

默认 ENCODER_COUNTS_PER_STEP=4：累计四个有向计数才返回一格。
不足一格的余数保留，随后继续累计，不会因快速轮询而丢失慢速旋转。
例如连续读到 +1、+1、+1、+1，前三次返回 0，第四次返回 +1 格。
中途反向时，有向余数相互抵消。一次读到 +12 计数则返回 +3 格，不能把它简化成 +1。
机械编码器每格可能有 2 个或 4 个计数；需要实测，不能仅根据四倍频模式断言每格固定为 4。

encoder_config.h 中可修改：

```c
#define ENCODER_COUNTS_PER_STEP 4u
#define ENCODER_DIRECTION       1  /* 顺时针返回负值时改成 -1，或交换 A/B。 */
#define ENCODER_INPUT_FILTER    15u
```

初始化时先不转动旋钮，在一个稳定卡位开始。
观察 Encoder_GetPositionCounts，慢转十格；净计数为 40 则每格设 4，为 20 则设 2。
若出现来回跳动，先检查接线和抖动，不能只通过改比例隐藏问题。

## 接口与调用规则

```c
Encoder_Init();                        /* 初始化 TIM4，软件位置置零。 */
steps = Encoder_ReadDeltaSteps();      /* 采样并更新位置，返回 +N/-N 格。 */
counter = Encoder_GetCounter();        /* 当前硬件 CNT，仅查询，不消耗增量。 */
counts = Encoder_GetPositionCounts();  /* 最近采样的累计有向原始计数，int64_t。 */
position = Encoder_GetPositionSteps(); /* 最近采样的累计有向整格数，int64_t。 */
faults = Encoder_GetFaults();          /* 故障位，通常为 0。 */
Encoder_ResetPosition();              /* 以当前 CNT 为基准，清除位置、余数和故障。 */
```

以上接口先初始化后使用，从主循环调用，不能在主循环与中断中同时操作扫描状态。
只有 Encoder_ReadDeltaSteps 会推进采样基准和累计位置；GetPosition 接口返回最近一次采样的结果。
一个统一的调用点读增量，再分发给当前页面，避免不同页面分别读取导致互相消耗增量。
建议每 5 ms 读一次；可用 Timebase_GetMs 在主循环安排，无需增加中断回调。
切换编辑项时如果不想把未满一格的余数带到新参数，可调用 Encoder_ResetPosition。
软件归零只建立相对基准，归零前已转过的位移会主动丢弃。

跨回绕还原的前提：两次采样间真实净位移必须严格小于 32768 个原始计数。
恰好 32768 时无法确定方向，本次不返回增量，清除余数，并记录 ENCODER_FAULT_AMBIGUOUS_DELTA。
超过半个计数范围的位移可能混淆成另一个方向，软件不能仅凭两个 CNT 值检测出来。
手动旋钮每 5 ms 采样通常远小于这个范围；不要长期停掉采样或将此方案直接用于高速电机。
软件位置使用 int64_t，极限处饱和并设置 ENCODER_FAULT_POSITION_LIMIT，避免有符号溢出。

## 滤波与机械抖动

定时器输入滤波可抑制很短的毛刺，但不能等同于按键的 20 ms 消抖。
在 TIM4 时钟 72 MHz、CKD=DIV4、ICFilter=15 下，fDTS=18 MHz，
滤波采样频率=fDTS/32=562.5 kHz，要求 8 次一致采样，时间尺度约 14 us。
PSC 不会把输入滤波变成毫秒级消抖；调低系统时钟会改变滤波时间尺度。
每格累计与正反余数抵消可以减少小幅跳动，但不能保证所有接触抖动都不生成完整一格。
若实物有明显误计数，先检查上拉、公共端、线长和供电，再根据编码器信号设计 RC/施密特整形。
不能对两相直接使用独立的 20 ms 按键消抖，这可能破坏相位关系并丢掉正常旋转。

## 工程接入与验证

将 encoder.c、encoder_core.c 加入 SPL 工程，把 Drivers/Encoder 加入头文件搜索路径。
需要已有 GPIO/RCC/TIM SPL 源码和 CMSIS。集成调用见 examples/key_pwm_main.c。
不新增定时器中断，不改动按键扫描周期，不重置公共时间基准。

tests/encoder_core_test.c 测试正反向、余数累计、快速多格、16 位回绕、比例配置、
软件归零、半范围歧义、位置限幅，以及十万次随机正反采样的计数守恒。
主机测试不验证真实 A/B 信号、滤波或 TIM4 寄存器，上板仍需验证。

上板验收：顺时针十格增加十格、逆时针十格回到零；慢转不漏格，快转不丢失增量幅度；
跨 CNT=0/65535 时方向正确；旋转时按键、舵机 PWM、呼吸动画和公共时间基准继续工作。

参考：
- https://www.st.com/resource/en/datasheet/stm32f103t8.pdf
- https://www.st.com/resource/en/reference_manual/rm0008-stm32f103xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf
