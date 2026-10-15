# 代码版本与备份

工程根目录：`C:\Users\庄唐旭\Documents\ChatGPT\e芯工作室考核实操部分`。
Keil入口：`Project/eXin.uvprojx`。

| Git标签 | 含义 | 实物状态 |
|---|---|---|
| `v0.1.0-stable` | 2026-10-15当前稳定基线；完整PID、Pitch、方向-1、目标/输出0～180°，P/I/D=0.8/0.25/0.02，限速90°/s | 用户确认最新程序已烧录且一切正常；性能指标尚未量化 |
| `v0.2.0-slew180-test` | 第一项性能试验：仅把应用HOLD限速提高到180°/s；PID不变 | 主机测试通过；用户首轮反馈更快、无抖动/过冲/故障；补测T89.2、M89.3、恢复约0.28s，但到位后有来回摆动，待确认衰减或持续，不升级为稳定版 |
| `v0.2.1-imu-fault-test` | 新增IMU FAULT ?首次故障现场记录，继承180°/s限速，PID/保护不变 | app_integration与protocol主机测试通过；待用户编译烧录复现故障，实物原因未确定 |

当前分支为`codex/hold-response`。到位摆动测试期间又出现IMU_INVALID，优先用[首次故障记录版](IMU_FAULT_CAPTURE_TEST.md)定位原因，暂缓PID调优。每完成一项改进都单独提交、添加标签，记录软件检查和实物状态。试验版在用户验收前不称为稳定版；MCU编译和烧录由用户执行。

## 查看代码

- `Application/app.c`：应用调度、页面、串口命令。
- `Application/app_config.h`：应用参数和HOLD方向。
- `Drivers/Control/control.c`：PID、限幅、限速、故障保护。
- `Drivers/IMU/imu_core.c`：姿态解算、静止校准。

## 查看Git历史

在工程根目录执行：

```powershell
git log --oneline --decorate --all
git tag --list
git show v0.1.0-stable:Application/app_config.h
```

后续比较两版时使用 `git diff <旧标签> <新标签> -- Application Drivers`。

## 独立源码压缩包

每个已保存标签可导出源码ZIP，放入根目录`backups`。压缩包包含源码、项目文件、库、测试及文档。生成的固件、缓存、日志和临时文件不计入源码版本；忽略规则不会删除现有文件。

恢复旧版建议把相应ZIP解压到新目录，打开其中的Keil工程执行Rebuild后再下载，避免当前工程和旧版编译产物混用。不要覆盖当前工程中的未保存修改。

Git仓库和ZIP都保存在本机；如需防止磁盘故障，还应另行复制备份到其他磁盘。
