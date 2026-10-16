# 离线姿态性能分析

版本`v0.4.0-trace-analysis-test`。只新增电脑端工具、测试和文档，固件保持v0.3.0不变，无需烧录。

## 使用与验收

1. 用原`tools/pc_monitor.py`手动连接串口，确认HOLD开启、目标固定，点击TRACE ON及“开始记录CSV”。只做小幅扰动，恢复后继续记录至少2秒，再停止记录。如出现故障，不必为本项继续运行机构，报告会如实标记无效。
2. 在工程根目录PowerShell运行，将trace.csv替换为实际记录路径：

```powershell
& 'C:/Users/庄唐旭/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe' tools/analyze_trace.py trace.csv
```

生成同目录`trace_analysis.html`及`trace_analysis.json`。用浏览器打开HTML，不需联网或安装额外Python库。输出已存在时拒绝覆盖，用`--output another.html`另存。

3. 若扰动释放发生在记录开始后3.5秒，区间终点为8秒：

```powershell
& 'C:/Users/庄唐旭/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe' tools/analyze_trace.py trace.csv --start-s 3.5 --end-s 8 --output trial2.html
```

起止秒数相对于CSV第一条板端样本，非MCU开机时刻。需要自己确定目标确认或扰动释放时刻，软件不猜测。没有准确起点时，只能演示曲线，不能宣称测得恢复时间。CSV没有HOLD字段，需确认测试全程开启HOLD。

4. 用户检查报告能否打开、曲线是否对应实物、故障和饱和是否正确标注，再反馈验收结果。本项不会提升控制器性能或修复IMU_INVALID。

## 口径

- 默认±1°连续保持1秒，可用`--band-deg 0.5 --dwell-s 1.5`调整，仅改变分析。
- 恢复时间从选定起点算起，终点为最后一次进入误差带的首个采样点，此后直到记录结束不再离开且至少保持指定时长。记录不足或再次离开则不算稳定，不推算样本之间行为。
- 稳定段平均误差是该段`target-measured`的样本有符号均值；峰峰值为实测角最大值减最小值。它们来自IMU，非外部机械精度认证。
- 存在故障或超过150ms的数据间断时，该区间恢复/稳定指标作废；输出饱和单独标记。用`--gap-s`调整间断门限；蓝牙5Hz可设0.6，但时间分辨率较低。
- 目标变化超过0.2°拒绝混算，请选取固定目标区间。损坏CSV、非有限数、重复/复位/倒退时间拒绝分析，支持32位毫秒计数自然回绕。
- 板端20Hz时间分辨率约50ms，不宣称毫秒级响应精度。小于150ms的漏帧可能未标记，不能证明数据全部齐全。输入兼容上位机CSV，可带或不带电脑接收时间列。

## 验证与备份

5项分析测试和3项原上位机测试通过：合成0.5秒恢复、末尾再偏离、故障/间断、时间回绕/复位、目标变化、区间选择、命令行导出及覆盖保护。没有操作串口或采集实物记录，等待用户验收。

Git标签`v0.4.0-trace-analysis-test`，源码备份`backups/v0.4.0-trace-analysis-test.zip`。
