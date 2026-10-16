# 上位机高DPI适配

版本：`v0.18.1-pc-hidpi`。前版瑞士风格界面已独立保存为`v0.18.0-pc-swiss-ui`及同名ZIP。

在创建Tk窗口前启用Windows系统DPI感知，让窗口按系统DPI原生绘制，减少Windows对低DPI窗口整体放大带来的文字模糊。界面使用Tk字体绘制；图表始终由Canvas按当前窗口尺寸绘制，不是放大的截图。

根据Tk报告的实际DPI同步调整像素布局、间距、输入控件留白、图表边距和线型。字体由Tk点数机制缩放，不重复放大；输入框字符宽度与日志行数也不重复缩放。初始和最小窗口尺寸按屏幕范围约束，保留左侧滚动区域及可折叠UUID设置。

## 生效方式

**关闭旧版窗口及离线预览窗口，再重新启动实际上位机**：

```powershell
& 'C:\Users\庄唐旭\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe' 'C:\Users\庄唐旭\Documents\ChatGPT\e芯工作室考核实操部分\tools\pc_monitor.py'
```

旧进程不会自动加载这项修改。无需重新安装依赖或烧录STM32。

检查标题、按钮和图表刻度在当前Windows缩放下是否更清楚，再按原流程连接USB/BLE、查询和开启TRACE。没有更改Windows显示设置、ClearType或系统分辨率，也没有修改通信、CSV或固件。

使用system-aware模式以兼容现有Tk 8.6布局，不承诺多显示器不同缩放之间拖动后自动重排或同等清晰。优先在系统DPI对应的主屏使用；清晰程度也受字体渲染、屏幕原生分辨率和系统兼容性覆盖影响。

验证：10项传输/解析测试与3项真实Tk离线界面检查通过；后者在创建窗口前启用本版DPI感知，检查小窗口BLE布局、按钮绑定、指标及日志，无硬件连接。

依据：[Microsoft DPI感知设置说明](https://learn.microsoft.com/en-us/windows/win32/hidpi/setting-the-default-dpi-awareness-for-a-process)。DPI感知必须在创建窗口前配置；本版使用SYSTEM_AWARE，旧Windows回退SetProcessDPIAware。
