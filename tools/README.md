# USB / HC-04 BLE 上位机

入口`pc_monitor.py`；界面`monitor_ui.py`；DPI适配`monitor_dpi.py`；离线分析`analyze_trace.py`。
主程序需要Tkinter和requirements.txt内依赖。请用同一个Python安装依赖与启动程序：

```powershell
python -m pip install -r tools/requirements.txt
python tools/pc_monitor.py
```

以上在仓库根目录执行。推荐使用根README的独立虚拟环境流程。USB为115200/8N1，BLE是直接GATT连接，不是在COM口列表查找模块。HC-04到MCU的UART保持9600；默认FFE0服务、FFE1收发，UUID可展开配置。

1. 显式选设备并连接，不会自动改变舵机状态。
2. 点击HELP/IMU ?等查询，设备回复才代表MCU通信链路通了；大于号只表示电脑完成写入。
3. 点击TRACE ON才显示数据。USB约50Hz，BLE约5Hz。
4. 点击开始记录CSV选择保存位置，停止后可离线分析。
5. 关闭HOLD或TRACE需主动发送相应OFF；断开PC连接不会自动停止固件任务。

```powershell
python tools/analyze_trace.py 'C:/your/path/trace.csv'
```

CSV保存电脑接收时间与设备毫秒，字段：received_at, ms, target, measured, error, P, I, D, servo, saturated, fault。
损坏帧不用于绘图或记录。分析必须结合动作时间，持续转动时的误差不等于停稳后的超调；未同步的视频和CSV不能强行对齐。

界面为瑞士风格，支持系统DPI绘制；关闭旧进程后重新启动才加载新代码，不保证混合DPI多屏间自动重排。
说明：[BLE](../docs/PC_BLUETOOTH.md)、[界面](../docs/PC_SWISS_UI.md)、[高清](../docs/PC_HIDPI.md)、[离线分析](../docs/TRACE_ANALYSIS_TEST.md)。

tests/pc_monitor_test.py测试解析和模拟传输；tests/pc_monitor_ui_test.py测试真实Tk控件但不连接硬件。
tools目录中的screen_*是历史模型筛选脚本，autodebug_*是实验硬件辅助脚本，都不是上位机启动所必需的文件，不会被pc_monitor.py自动运行。
历史说明保留在docs/history/PC_README_before_github.md。
