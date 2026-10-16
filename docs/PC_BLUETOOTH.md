# 上位机 HC-04 BLE 测试版

版本：`v0.17.0-pc-ble-test`。仅修改电脑端，不需要编译或烧录。STM32固件、HOLD算法、IMU保护与v0.16.1完全一致。
旧版已保存为`v0.16.1-gyro1000-motion-snapshot`及同名ZIP。

## 打开

关闭旧上位机窗口，在PowerShell运行：

```powershell
& 'C:\Users\庄唐旭\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe' 'C:\Users\庄唐旭\Documents\ChatGPT\e芯工作室考核实操部分\tools\pc_monitor.py'
```

依赖安装（须使用启动上位机的同一Python）：

```powershell
& 'C:\Users\庄唐旭\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe' -m pip install -r 'C:\Users\庄唐旭\Documents\ChatGPT\e芯工作室考核实操部分\tools\requirements.txt'
```

## 连接与验收

1. 给开发板和HC-04供电，电脑开启蓝牙。手机小程序先断开HC-04，避免占用连接。不需要在COM口列表找BLE设备，也不需要主动创建虚拟串口。
2. 左上角从“USB串口”切到“蓝牙BLE”，点击“刷新 / 扫描”，约5秒后在列表选择自己的HC-04（按名称和地址确认）。应用只扫描，不能代替Windows打开或关闭蓝牙适配器。
3. 保持默认UUID：服务`0000ffe0-0000-1000-8000-00805f9b34fb`；接收、发送均`0000ffe1-0000-1000-8000-00805f9b34fb`。点击连接。默认值来自用户提供的汇承小程序`miniprogram/configs/index.js`，也与此前手机设置截图相同。其他固件可编辑完整UUID；程序会核对通知和写入属性，配置错误会显示在文本区。
4. 点击`HELP`、`IMU ?`、`SERVO ?`、`ALARM ?`，应看到对应回复。`>`表示电脑已完成写入，设备返回的`< OK`等才表示设备处理结果。
5. 测试时间/闹钟按钮并核对OLED；全部原始指令也可在输入框发送。舵机暂不测试，连接本身不会发送HOLD、角度或TRACE命令。
6. 如需验证曲线，手动点击`TRACE ON`，应收到数据并画图；蓝牙沿用固件5Hz，USB约50Hz。可以记录CSV并交给同一分析工具。不要把较稀疏的蓝牙记录当成50Hz数据。
7. 点击`TRACE OFF`再断开。切回USB串口、刷新并选COM口，115200/8N1连接，验证查询仍正常。一次只使用一种电脑端连接。
8. 测试连接后关闭模块电源：程序应显示断开或错误，不自动重连、不重放旧命令。恢复供电后重新扫描/连接。连接/断开不会自动停止板上已有HOLD或TRACE。

电脑必须有支持BLE的可用蓝牙适配器；没有设备时检查Windows蓝牙、模块供电、手机占用、距离。底部文本区会显示扫描/连接/UUID/写入错误。若协议属性要求认证而连接失败，再根据系统提示或Windows设置配对；本版不自动发起配对。

## 实现及验证边界

- Bleak运行在后台线程的独立asyncio循环，扫描、连接和IO不阻塞Tk；界面主线程通过队列处理回复、画图及CSV。
- BLE先订阅通知再接受发送；命令加CRLF，以最多20字节分片，片间30ms，适配现有9600波特率模块UART。通知跨包拼行，超长行丢弃后可恢复。
- 连接或扫描期间锁定设备类型/UUID；断开后的新连接创建新工作线程，不重放发送队列。发送失败无法证明设备未执行，须先查询实际状态，避免盲目重复控制指令。
- 自动测试覆盖分片回复/TRACE解析、长指令分片、两类写入属性、缺服务/缺属性、断线清理、连接期间取消、队列限额、超长行恢复及已有命令/CSV解析。测试使用模拟BLE服务，不能代替Windows适配器和实物HC-04验收。
- 接口依据：[Bleak客户端文档](https://bleak.readthedocs.io/en/latest/api/client.html)、[Windows后端](https://bleak.readthedocs.io/en/latest/backends/windows.html)。
