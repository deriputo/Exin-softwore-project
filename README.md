# e 芯考核：STM32 多功能时钟与舵机控制

2026-10-16：稳定基线为Git标签`v0.1.0-stable`；当前`v0.6.5-pi05-no-d-test`保留Kp1/Kd0，仅恢复Ki_eff=0.5/[1+(e/8)²]，先验证稳定消除残差。纯比例对照已确认无明显摆动但存在残差，当前不是提速通过版。OLED、分析工具及保护保留，IMU_INVALID未解决。步骤见[恢复小积分测试](docs/PI05_NO_D_TEST.md)，版本见[VERSIONS](docs/VERSIONS.md)。

当前进度、最新固件验证状态见[当前进度与下次继续](docs/当前进度与下次继续.md)。当前已恢复题目要求的完整PID与0～180°目标/输出范围，取消开启位置±15°诊断限制；[操作与实物测试](docs/HOLD_EXAM_TEST.md)。IMU安装映射为机构(X,Y,Z)=芯片(X,Z,-Y)，控制机构Pitch；新固件下载和实物反馈方向、精度、扰动恢复验证待完成。

单一 STM32F103C8T6 标准外设库（SPL）工程，入口为 `Application/main.c`，Keil 工程为 `Project/eXin.uvprojx`。实现五个页面、RTC 时间与三个闹钟、编码器、MPU6050、舵机手动/0～180°连杆姿态定角、电脑与蓝牙串口和空闲电源管理。

**默认 60 秒无操作后进入普通 Sleep 并关闭 OLED。** TIM2 每 5 ms 唤醒处理后台任务，RTC、舵机 PWM、IMU 和串口继续工作；按键、编码器的原始计数变化或收到串口字节会重新计时并恢复显示。熄屏后的第一次按键只唤醒，整次按下、长按和松手均不触发页面操作；松手后再按才正常操作，同时按住的另一个键也会被消耗。报警和持续按住按键会阻止空闲熄屏。Sleep 不复位，不丢失编辑值。

## 工程与硬件

| 功能 | 引脚 / 外设 | 配置 |
|---|---|---|
| KEY1、KEY2 | PA0、PA2 | 按下接 GND，内部上拉 |
| 舵机信号 | PA8 / TIM1_CH1 | 50 Hz，500～2500 µs，0～180° |
| 开机呼吸 LED | PA6 / TIM3_CH1 | 1 kHz PWM 载波，高电平亮 |
| 闹钟 LED | PB0 | 已确认另一端接 3.3V：低电平亮、高电平灭，500 ms 亮 / 500 ms 灭 |
| 编码器 A、B | PB6、PB7 / TIM4 | TI12 四倍计数，默认 4 个计数一格 |
| OLED + MPU6050 | PB8=SCL、PB9=SDA / I2C1 重映射 | 100 kHz；7 位地址 0x3C、0x68 |
| 全局串口 | PA9=TX、PA10=RX / USART1 | 115200，8N1 |
| HC-04 蓝牙 | PB10=TX、PB11=RX / USART3 | 默认 9600，8N1；模块 TXD 接 PB11、RXD 接 PB10 |
| RTC 晶振 | PC14、PC15 | 已按用户确认使用 32.768 kHz LSE |
| 公共扫描时基 | TIM2 | 5 ms 中断；按键扫描与毫秒计数 |
| 系统时钟 | HSE + PLL | 工程默认 HSE 8 MHz、系统 72 MHz |

上述引脚是本工程的资源分配。实物接线须与配置头文件一致；若 HSE 不是 8 MHz，需要同步修改库的时钟配置和 HSE_VALUE。舵机由独立合适电源供电，与 STM32 共地；USB 转串口 TX 接 PA10、RX 接 PA9。I²C 两条线需上拉到 3.3 V。MPU6050 AD0 接低；AD0 接高时改 `imu_config.h` 地址为 0x69。

呼吸 LED 默认高有效；闹钟 LED 已按用户确认的接线配置为低有效。主程序入口首先把报警灯设为熄灭，RTC 初始化与无报警期间保持熄灭。编码器方向反了改 `encoder_config.h`；每格计数与机械编码器型号不一致时也在那里调整。

## 页面操作

| 页面 | 编码器 | KEY2 短按 | KEY2 长按超过 1 秒 |
|---|---|---|---|
| 主菜单 | 选择 TIME / ALARM / IMU / SERVO | 无操作 | 无操作 |
| 时间设置 | 调整选中时、分、秒，边界循环 | 时→分→秒→时 | 保存后按新时间继续计时 |
| 闹钟 | 调整时间或 ON/OFF | 时→分→秒→ON/OFF→时 | 保存当前闹钟 |
| IMU | 无操作 | 无操作 | 重新静止校准；HOLD 开启时拒绝 |
| 舵机 MANUAL | 编辑 0～180° | 确认并执行 | 开启 HOLD，捕获当前姿态 |
| 舵机 HOLD Y | 编辑对水平面目标0～180°，一格1° | 确认目标角 | 开关定角并清故障 |

KEY1 短按：功能页按“TIME→ALARM→IMU→SERVO→主菜单”单向循环；主菜单按赛题进阶交互进入编码器选中的页面，默认选择 TIME。若希望完全固定五页循环，将 `APP_MENU_DIRECT_SELECT` 改为 0。闹钟页长按 KEY1 可选择 A1/A2/A3。

报警时 KEY1、KEY2 任意一个或同时按下，只停止本次提醒；整次按下、长按和松开动作均不执行翻页、选择或保存，闹钟仍保持启用，次日继续提醒。

冷启动时间 23:59:55，舵机 90°。时间保持 24 小时制，跨午夜正确进位。OLED 开机进度动画正常持续 1.2 秒，随后显示主菜单；显示器启动较晚时仍保留完整动画。OLED 故障超过 6 秒时允许继续操作后台功能，设备驱动自动重试。

开机 LED 在 3 秒内将呼吸频率从 0.5 Hz 连续提高到 2 Hz，最后 300 ms 平滑熄灭；PWM 载波与呼吸变化频率分别处理。这采用用户明确要求及题目详细条款的“从慢到快”，题目概览中的“慢→快→慢”表述不同。

## 串口协议

蓝牙手机端复用本节全部指令，电脑与手机分别收取各自回复。手机必须追加实际换行，不能添加教程演示包头。蓝牙 TRACE 为 5 Hz，电脑 TRACE 为 20 Hz，两路独立开关；手机收到指令也会唤醒 OLED。接线、供电、连接和验收详见 `Drivers/Bluetooth/README.md`。

以 CR、LF 或 CRLF 结束一行。除 HELP/help 外，命令大小写、空格和括号严格匹配；没有额外 PWR 命令。

```text
HELP
TIME(14:30:05)
ALARM1(07:30:00)
ALARM2(08:00:00)
ALARM3(09:00:00)
ALARM2 OFF
ALARM3 DEL
ALARM ?
IMU ?
SERVO ?
SERVO SET 120
SERVO HOLD ON
SERVO HOLD OFF
SERVO HOLD ?
SERVO TRACE ON
SERVO TRACE OFF
SERVO TRACE ?
```

TIME 应答在 RTC 写入完成后发送，后续命令不会越过这次改时；OLED 的时间保存也是异步的。OFF 保留闹钟时间，DEL 将时间清零并关闭。HOLD 开启时拒绝手动 SERVO SET；有效手动设置会同步舵机页面。IMU 查询和 OLED 姿态来自同一个样本，保留带符号的一位小数。

TRACE ON 输出字段表头与应答，随后每 50 ms 尝试发送一个完整记录。命令应答优先，串口容量不足时丢弃该次调试记录，不阻塞或积压控制任务。未收完的一行命令不会停止 TRACE。

```text
H,ms,target,measured,error,P,I,D,servo,saturated,fault
H,1250,15.0,14.6,0.4,1.0,0.0,-0.1,90.9,0,NONE
```

可选上位机为 `tools/pc_monitor.py`，提供串口命令、姿态曲线和 CSV 日志，说明在 `tools/README.md`。

## 姿态控制

2026-10-13 用户已将IMU固定在连杆上，实测输出90°时连杆竖直、相对Y≈0°，100°时Y≈+9.1°、80°时Y≈-10.7°。当前应用使用Pitch/Y正向PID闭环，目标范围0～180°，对水平面角度M=90°+相对校准Y。每次上电要求底座水平、90°输出时连杆竖直且机构静止；参考点必须正确。IMU重校准要求先关闭HOLD并设置舵机90°。HOLD ON捕获当前对地角度，编码器编辑T并短按K2确认。

默认初调参数Kp=0.8、Ki=0.25、Kd=0.02、0.5°死区、微分80ms低通、20ms更新，输出0～180°、90°/s限速、积分防饱和与故障保持。IMU的Y为单轴有符号重力倾角融合，可区分超过90°的两侧；控制所用M增加90°参考偏置。X/Z保持原显示用途，Z不是绝对磁航向。重力接近Y转轴方向时角度不可观测，会判无效并保持输出。两端角度与大幅底座倾斜可能超出舵机机械可达范围，需实机逐步确认。电脑TRACE恢复真实PID20Hz，蓝牙5Hz。

MPU6050 的 Roll/Pitch 用互补滤波；Yaw 是相对积分角，会随时间漂移。六轴传感器不能提供无漂移的绝对航向。

## 构建与验证

在 Keil MDK 中打开 `Project/eXin.uvprojx`，目标为 `eXin STM32F103C8`，使用 ARM Compiler 5、C99、MicroLIB。输出为 `build/keil/eXin.hex` 与带调试信息的 `eXin.axf`。源文件均已登记，不要同时加入 `examples/key_pwm_main.c`。

本次采用用户指定的 **stm32-autodebug** skill 进行 Keil 构建。当前电脑上的调用方式：

```powershell
Set-Location Project
& 'C:/Users/庄唐旭/Documents/Codex/embedded-toolkits/.venv/Scripts/python.exe' 'C:/Users/庄唐旭/Documents/Codex/embedded-toolkits/stm32-autodebug/run_autodebug.py' --project "$PWD/eXin.uvprojx" --no-flash --rebuild --json
```

主机软件测试，在工程根目录运行：

```powershell
./tools/test.ps1
python -m unittest discover -s tests -p '*_test.py' -v
```

17 组 C 测试覆盖按键、PWM、编码器、RTC、备份恢复、电源、串口队列、协议、PID、UI、I²C、IMU/OLED 状态机及真实应用整合；另有 3 项 Python 上位机测试。应用联调使用硬件替身，不代表实物验证。`docs/ui-preview.png` 来自真实应用绘制的模拟帧缓冲，不是板卡照片。

最新验证记录见 `docs/VALIDATION.md`。用户已解决烧录并开始实物测试；随后依据其 ID=0x70 模块与报警灯低有效接线修正驱动，新版已编译并通过主机测试，但尚未由本次操作烧录或完成实机验收。功耗、波形、IMU 与 PID 仍需实测，不能用主机模拟代替。

## 目录与扩展

`Application` 管理交互与调度；`Drivers` 各模块独立；`Protocol` 解析串口；`Project` 包含启动、链接和 Keil 配置；`Vendor` 保留 SPL/CMSIS；`tests` 是离线验证；`tools` 是构建、测试和上位机工具。

普通 Sleep 是交付默认模式。真正 Standby API 保留为可选扩展，但会停止 PWM、串口和主程序，唤醒后复位；不能直接替换 Sleep。现有 PA0 低有效 KEY1 与高有效 WKUP 接线不匹配，默认关闭 PA0 WKUP，只有 RTC/复位可唤醒该可选模式。Standby 会备份三个闹钟、舵机角度和页面，并在唤醒后补查闹钟；HOLD/TRACE/输入未完成时禁止进入。详见 `Drivers/Power/README.md`。

用户提供的 OLED 字库与 MPU6050 参考代码来源及适配说明在对应驱动 README；第三方来源记录见 `docs/THIRD_PARTY.md`。原始用户参考目录未修改。
