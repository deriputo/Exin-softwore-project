# e芯：STM32 多功能时钟与单轴姿态控制

基于 **STM32F103C8T6、标准外设库 SPL、Keil5** 的嵌入式学习项目，包含RTC时钟/闹钟、OLED菜单、编码器、IMU姿态估计、SG90姿态闭环，以及支持USB串口和HC-04 BLE的Python上位机。

固件源码基线为 **v0.21.0-boot-led-active-low**。当前整理仅完善仓库说明、依赖声明和可移植性，不改变控制算法。完整调试历史见[版本记录](docs/VERSIONS.md)和[项目实现与复盘](docs/项目实现逻辑与调试复盘.md)。

## 当前功能

- 两个低电平按键：5ms扫描、20ms消抖、超过1秒长按。
- TIM4硬件编码器，菜单选择与参数编辑。
- LSE驱动RTC，三个每日逻辑闹钟；K1与K2重叠按下才能停止本次提醒，支持先按住一键再按另一键。
- OLED与IMU共用100kHz I²C；OLED分块差异刷新。
- SG90手动角度与连杆IMU单轴HOLD，含限幅、限速及故障锁存。
- PA6低有效开机灯，约3秒从慢到快呼吸后熄灭；PB0低有效报警灯。
- 60秒空闲熄屏/普通Sleep；第一次按键手势只用于唤醒。
- USB与蓝牙复用指令；上位机提供曲线、CSV、瑞士风格界面和系统DPI适配。

动态跟踪速度和误差仍有改进空间，不能把主机模拟测试通过当作完整实物验收。当前已移除SYS RESET复位诊断指令。

## 接线

各电源共地；舵机使用合适的独立5V供电。

| 功能 | STM32引脚 | 接线/参数 |
|---|---|---|
| K1 / K2 | PA0 / PA2 | 按键按下接GND，内部上拉 |
| 编码器A / B | PB6 / PB7 | 公共端GND，默认4计数/格 |
| OLED与IMU SCL / SDA | PB8 / PB9 | 两模块并联共享，3.3V，IMU AD0接GND |
| 舵机信号 | PA8 / TIM1_CH1 | 50Hz，500～2500µs，对应指令0～180° |
| 开机呼吸灯 | PA6 / TIM3_CH1 | 3.3V→限流电阻→LED正极，负极PA6；载波1kHz |
| 报警灯 | PB0 | 3.3V→限流电阻→LED正极，负极PB0 |
| USB串口 | PA9 TX / PA10 RX | 模块RX→PA9，TX→PA10；115200/8N1 |
| HC-04 UART | PB10 TX / PB11 RX | 模块RXD→PB10，TXD→PB11；9600/8N1 |
| RTC LSE | PC14 / PC15 | 板载32.768kHz晶振 |
| ST-Link | PA13 / PA14 | SWDIO / SWCLK，另接GND |

HC-04供电按模块额定电压，UART逻辑需适配3.3V。LED必须有限流；不要把裸LED直接跨接电源。

## 获取与构建

获取完整依赖时使用Git递归克隆；若已克隆，执行：

```bash
git clone --recurse-submodules https://github.com/deriputo/Exin-softwore-project.git
cd Exin-softwore-project
git submodule update --init --recursive
```

`Vendor/STM32F10x_StdPeriph_Lib`是固定版本的第三方子模块。GitHub的普通源码ZIP不会自动填充其内容；Keil主要使用已随仓库提供的`Vendor/LocalSPL`，可选GCC构建需要该子模块中的GNU启动文件。

**Keil5：** 打开`Project/eXin.uvprojx`，使用ARM Compiler 5、C99、MicroLIB，Rebuild后下载。当前时钟按8MHz HSE/72MHz配置。只使用工程登记的入口，不要把tests、examples或第三方示例的所有C文件一起加入目标。输出到`build/keil/`，生成物不跟踪。

**可选ARM GCC：** 安装GNU Arm Embedded工具链，在PowerShell中将ARM_GCC_BIN指向工具链bin目录，再运行：

```powershell
./tools/build.ps1
```

构建/烧录会驱动真实机构；通电前保持机械安装可靠，IMU随连杆固定。

## 上位机

使用带Tkinter的Python 3.9+，建议创建独立虚拟环境以免依赖随临时运行时更新丢失：

```powershell
python -m venv .venv
./.venv/Scripts/python.exe -m pip install -r tools/requirements.txt
./.venv/Scripts/python.exe tools/pc_monitor.py
```

USB选择实际COM口。BLE先断开手机占用，开启电脑蓝牙，在上位机扫描选择HC-04；默认服务FFE0、收发FFE1。连接不会自动发控制指令，点击TRACE ON才开始曲线。断开上位机不会自动关闭板上HOLD。

USB TRACE约50Hz，BLE约5Hz。上位机依赖不属于MCU固件依赖。
详细步骤见[上位机说明](tools/README.md)、[BLE](docs/PC_BLUETOOTH.md)、[界面](docs/PC_SWISS_UI.md)、[DPI](docs/PC_HIDPI.md)。

## 指令

每条指令以实际CR、LF或CRLF结尾，语法区分大小写（HELP/help除外）。

```text
HELP
TIME(14:30:05)
ALARM1(14:31:00)
ALARM2 OFF
ALARM3 DEL
ALARM ?
IMU ?
IMU RAW ?
IMU FAULT ?
SERVO ?
SERVO SET 90
SERVO HOLD ON
SERVO HOLD OFF
SERVO HOLD ?
SERVO TRACE ON
SERVO TRACE OFF
SERVO TRACE ?
```

HOLD启用时拒绝手动SERVO SET。当前目标姿态通过OLED编码器编辑、K2短按确认；HOLD ON捕获当时姿态，并非无条件设为90°。故障锁存时保持最后PWM，传感器恢复不自动解除，需关闭HOLD后确认状态再开启。

## 当前控制与测量参数

当前IMU必须装在被控连杆上。机构安装映射(X,Y,Z)=(传感器X,Z,-Y)，选定Y倾角以90°为标定参考，角速度对应传感器GZ。上电校准时应底座水平、手动90°对应连杆竖直，保持静止完成200个合格样本。六轴IMU不提供无漂移绝对航向；单轴控制也不等于完整三维姿态保持。

| 项目 | 当前有效配置 |
|---|---|
| 算法 | 固定入口偏置PID姿态外环；历史DIRECT/FOLLOW未启用 |
| Kp / Ki / Kd | 0.7 / 8.0 / 0.005，最终覆盖见App_Init |
| 方向 / 周期 | -1 / 至少20ms且新样本 |
| 死区 / 平滑过渡 | 0.5° / 0.5～0.8° |
| 输出指令范围 / 限速 | 0～180° / 180°/s |
| D输入 / 限幅 | 校准角速度 / ±0.5°贡献 |
| IMU量程 | 加速度±2g，陀螺仪±1000°/s，32.8LSB/(°/s) |
| 采样 / 重力短时接续 | 10ms / 最长200ms，仍保留其他异常保护 |

先验证反馈方向、校准、供电和机械可达范围，再调增益。OUT是舵机指令，M才是IMU测量估计。详见[学习复盘](docs/项目实现逻辑与调试复盘.md)。

## 验证

Windows上安装GCC后，在PowerShell运行主机C测试，可用-Compiler指定gcc路径：

```powershell
./tools/test.ps1 -Compiler 'C:/path/to/gcc.exe'
python tests/pc_monitor_test.py
python tests/analyze_trace_test.py
python tests/pc_monitor_ui_test.py
```

C脚本定义23组测试；Tk界面检查需图形桌面。主机测试使用模拟硬件，不连接开发板，不能代替Keil构建、PWM波形、供电、功耗或实物动态性能验收。已有验证记录带各自版本和边界，本次文档整理不表示重新执行全部测试。

## 目录

| 目录 | 内容 |
|---|---|
| Application | 主入口、应用调度、菜单及应用配置 |
| Drivers | 按键、PWM、编码器、RTC、IMU、控制、I²C、OLED等 |
| Protocol | 指令解析、队列及异步应答 |
| Project | Keil/GCC配置、启动和链接支持 |
| Vendor | 第三方SPL/CMSIS及子模块 |
| tools | 上位机、CSV分析、构建/测试和历史实验工具 |
| tests | 主机测试与硬件替身，不进入固件 |
| examples | 独立参考示例，不是当前主入口 |
| docs | 版本历史、实测记录、操作说明和学习复盘 |

`build`、`tmp`、`release`、`backups`及本机环境不提交。`docs/experiments`里的受版本管理实验数据用于复盘。历史README归档在[docs/history](docs/history)，其中旧参数及绝对路径仅供追溯。

## 第三方来源

ST SPL、Arm CMSIS、OLED字库及IMU参考代码保留各自作者和文件原有许可，见[第三方说明](docs/THIRD_PARTY.md)。本仓库未对全部第三方内容统一重新授权。
