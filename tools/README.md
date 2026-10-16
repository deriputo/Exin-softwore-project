# 可选 Python 上位机

当前活动源码：v0.11.0-imu-gyro-bridge-test，OLED为HOLD Y PIDR3。用户授权加速度暂时不可信时最长100ms陀螺仪接续，超时及真实通信/采样故障仍保护。RAW追加MODE/REFAGE/BRIDGE；五项主机检查通过，未实物验收。控制参数与积分修复不变；慢速验收标签v0.10.1完整保留。 [本版步骤](../docs/IMU_GYRO_BRIDGE_TEST.md)。

以下为历史记录。

当前活动源码为v0.10.2-pidr-slew-fix-test，OLED显示HOLD Y PIDR2。只修复PID在限速时整轮拒绝积分导致的大误差冻结，P0.7/Ki6/Kd0.005与保护不变。四项主机检查通过，实物待验收；wide模型仍不合格，不能视为全部高速问题解决。用户慢速验收版独立保存在v0.10.1-pidr-slow-accepted及ZIP。 [本轮说明](../docs/PIDR_SLEW_FIX_TEST.md)。

以下为历史记录。

当前活动源码：**v0.10.0-pidr-small-angle-test**，OLED显示HOLD Y PIDR。用户已明确批准独立小角度实物试验。使用固定偏置PID，P0.7/Ki6/Kd0.005；不是小积分方案，也不是DIRECT。fast假设288组最坏过冲1.127°，但原wide范围最坏19.415°未通过；当前仅允许先做约5°测试，实物仍以2°为验收上限，尚未实物验收。由用户Keil编译烧录。[本轮步骤](../docs/PIDR_SMALL_ANGLE_TEST.md)。

以下为历史记录。

当前待测为 **v0.9.1-tracking-2deg-test**：用户批准2°短暂过冲上限；位置系数0.30不变，估计时间常数0.10秒、速度贡献限幅6°。288组模型最坏过冲1.88°，12组连续跟踪模型均改善，但仍未达到紧贴目标。Keil编译烧录及实物验收由用户执行。详见docs/DIRECT_TRACKING_TEST.md。

以下为历史版本说明。

当前DIRECT+试验：TRACE P为位置项，I=0，D为受限速度反馈（±3°，到位死区内为0）。CSV格式不变，servo仍为指令值。以下I/D均为0的描述仅适用于v0.8.0。测试见../docs/DIRECT_RATE_TEST.md。

DIRECT试验模式保持CSV字段兼容。此模式P列为候选姿态误差位置补偿（非原PI的Kp项），I/D为0；target/measured/error/servo等含义不变。没有实际舵机轴角反馈，servo仍是指令。算法判别请看板端HOLD Y DIRECT标识，详见docs/DIRECT_COMPENSATION_TEST.md。

新增离线分析入口`analyze_trace.py`：读取本工具记录的CSV并导出HTML曲线/JSON指标，不连接串口、不改变固件。[用法和验收](../docs/TRACE_ANALYSIS_TEST.md)。

此工具用于电脑串口控制与姿态闭环演示，属于软件扩展，不是 STM32 固件依赖。不会自动选择、连接串口或发送舵机/TRACE/HOLD 命令。

安装 Python 3.9 或更新版本（带 Tkinter），在工程根目录执行：

```powershell
python -m pip install -r Tools/requirements.txt
python Tools/pc_monitor.py
```

1. 选择 USB 转串口实际 COM 号，点击连接。串口固定 115200、8N1，TX/RX 交叉接 PA10/PA9，GND 共地。
2. 可设置 TIME、三个 ALARM、整数舵机角度，或发送 `HELP`、`IMU ?` 等命令。参数按钮使用固件精确语法；原始命令输入框不偷偷修正大小写或空格。
3. 点击 HOLD ON 捕获当前姿态；HOLD 打开时固件拒绝手动 SERVO SET。HOLD OFF 关闭控制并清除故障。
4. 点击 TRACE ON 才开启 20 Hz 调试数据；曲线显示目标、实测、误差最近 10 秒，状态行显示输出、饱和、故障。
5. 点击开始记录 CSV 并选择保存路径。记录仅包含有效 TRACE 数据，第一列为电脑实际接收该行的本地时间（含时区），第二列为设备毫秒计数。停止记录会关闭文件。GUI 接收队列满时会显示丢失计数。

设备调试格式：

```text
H,ms,target,measured,error,P,I,D,servo,saturated,fault
H,1250,+15.0,+14.6,+0.4,+1.0,+13.3,-0.1,104.2,0,NONE
```

时间列为 32 位无符号毫秒计数；角度字段为十进制数；饱和为 0/1；故障为大写名称。表头和普通应答只显示在文本窗口，不被当成数据。NaN/Infinity、缺少字段、非法时间等损坏帧不会画图或写 CSV。

打开、关闭及读写串口均在后台线程执行，GUI 不等待通信。命令通过发送队列，接收消息通过有界队列传回。关闭上位机或断开连接不会自动改变舵机状态；需要停止 HOLD/TRACE 时先按相应 OFF 按钮。固件进入 Standby 后普通 USART RX 无法唤醒，应使用硬件定义的唤醒键或 RTC。

`tests/pc_monitor_test.py` 无需 pyserial、显示器或连接硬件，验证 TRACE 解析、非有限值防护、命令范围和 CSV 字段。GUI、COM 连接及真实数据刷新仍需在电脑和板卡上验证。
