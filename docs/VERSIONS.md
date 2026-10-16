# 代码版本与备份

工程根目录：`C:\Users\庄唐旭\Documents\ChatGPT\e芯工作室考核实操部分`。
Keil入口：`Project/eXin.uvprojx`。

| Git标签 | 含义 | 实物状态 |
|---|---|---|
| `v0.12.3-response-trace20-test` | 归档用户确认的手动阶跃；电脑TRACE改为20ms，实际控制参数不变 | 450组增大P/比例滤波候选未通过，不启用；本版用于更细响应采集 |
| `v0.12.2-motion-integral-experiment` | 运动积分增强实验及216组筛选；默认关闭，当前应用参数不变 | 未找到兼顾提速与恢复的候选，不作为新性能版；21项主机回归通过 |
| `v0.12.1-rate-trace13-snapshot` | 用户要求独立保存当前RATE，与v0.12.0控制源码相同，附trace13实测 | 本轮无故障，静止约±0.1°，连续运动RMS5.44°/最大12°，动态跟踪未达标 |
| `v0.12.0-gyro-rate-response-test` | 校准后角速度直接反馈、Ki8、D贡献限幅±0.5°，UI为HOLD Y RATE | 六项主机检查通过；有限倾斜模型0.943°，连续模型改善，wide不通过，未实物验收 |
| `v0.11.1-pidr3-lag-snapshot` | 用户要求单独保存当前PIDR3；固件源码与v0.11.0完全相同 | 用户反馈运动开始及停止后仍有明显滞后，未满足实时跟随；本次未改算法参数 |
| `v0.1.0-stable` | 2026-10-15当前稳定基线；完整PID、Pitch、方向-1、目标/输出0～180°，P/I/D=0.8/0.25/0.02，限速90°/s | 用户确认最新程序已烧录且一切正常；性能指标尚未量化 |
| `v0.2.0-slew180-test` | 第一项性能试验：仅把应用HOLD限速提高到180°/s；PID不变 | 主机测试通过；用户首轮反馈更快、无抖动/过冲/故障；补测T89.2、M89.3、恢复约0.28s，但到位后有来回摆动，待确认衰减或持续，不升级为稳定版 |
| `v0.2.1-imu-fault-test` | 新增IMU FAULT ?首次故障现场记录，继承180°/s限速，PID/保护不变 | 用户已捕获静止/运动时投影不足故障，根因尚未确定 |
| `v0.3.0-oled-dirty-test` | OLED以16字节块进行差异刷新，初始化/唤醒/通信恢复全屏重刷，额外RAM1088字节 | display_imu_state与app_integration主机测试通过；2026-10-16用户确认验收成功，仅代表本项显示优化通过，IMU故障仍未解决 |
| `v0.4.0-trace-analysis-test` | CSV离线分析，导出HTML曲线/JSON指标；默认±1°保持1秒，固件不变 | 5项分析及3项上位机测试通过，待真实记录验收 |
| `v0.5.0-ki050-test` | 仅将应用Ki从0.25提高至0.50，保留其他控制参数、OLED及分析工具 | control和app_integration测试通过，模型改善不等于实测，待用户编译烧录验收 |
| `v0.6.0-nonlinear-pi-test` | Kp1.6、Ki峰值2、误差尺度8° | 软件测试通过，但实物静止左右抽动，未通过验收 |
| `v0.6.1-nonlinear-i08-test` | 保留Kp1.6，Ki峰值2降至0.8 | 用户反馈静止仍左右摆动，未通过验收 |
| `v0.6.2-ki0-diagnostic` | Ki0、Kp1.6、Kd0.02静止对照 | 用户反馈静止仍左右摆动，尚不能确定P/D或测量波动根因 |
| `v0.6.3-kp10-ki0-diagnostic` | Kp1.0、Ki0、Kd0.02 | 用户反馈静止仍左右摆动 |
| `v0.6.4-p-only-diagnostic` | Kp1.0/Ki0/Kd0纯比例对照 | 用户确认水平及受扰后无明显摆动，受扰残差约5.9°；不算完整定角通过 |
| `v0.6.5-pi05-no-d-test` | Kp1/Kd0，Ki_eff=0.5/[1+(e/8)^2] | 用户确认后段停稳，小角度消差有效；大角度仍缓慢间歇挪动，提速未通过 |
| `v0.7.0-bounded-i-test` | Kp1/Kd0；积分增益0.4～1.0、尺度4° | 用户反馈间歇挪动减少、无过冲/持续摆动，但响应仍慢，性能未完全验收 |
| `v0.7.1-i-floor08-test` | 积分下限0.8，上限1.0、Kp1/Kd0 | 用户确认三次底座动作，仍感觉不够快；记录从峰值到1°约4秒，不满足期望 |
| `v0.8.0-direct-compensation-test` | 新增基于执行器位置估计的DIRECT模式，补偿系数0.5、时间常数0.30秒；旧PI保留 | control_direct/control/app_integration通过，288组模型检查通过，尚未实物验收 |
| `v0.8.1-tau008-failed` | 仅把模型时间常数改0.08秒、系数0.5不变 | 应用整合通过，直接补偿失配过冲检查失败：最坏21.70°，不推荐烧录 |
| `v0.8.2-tau030-restored` | 活动固件恢复v0.8.0的0.30秒/系数0.5，附失败记录 | control_direct及app_integration重新通过；没有实现新的提速 |
| `v0.11.0-imu-gyro-bridge-test` | 加速度不可信时限时100ms陀螺仪接续，真实通信/过期/饱和仍保护，UI为PIDR3 | 五项主机检查通过，未实物验收；慢速验收版保留 |
| `v0.10.2-pidr-slew-fix-test` | PID改为按可达窗口接受部分积分，修复大误差积分冻结；UI为PIDR2，参数不变 | 四项主机检查通过；wide模型仍不合格，实物待验收 |
| `v0.10.1-pidr-slow-accepted` | 与v0.10.0控制源码相同；用户要求独立保留 | 用户确认慢速操作验收通过；快速突变会停调控，IMU_INVALID另待排查，不算完整验收 |
| `v0.10.0-pidr-small-angle-test` | 用户批准独立小角度PIDR：P0.7/Ki6/Kd0.005，固定偏置，较快恒定积分 | fast假设288组1.127°通过；原wide范围19.415°不通过，尚未实物验收，仅先测约5° |
| `v0.9.1-tracking-2deg-test` | 2°过冲预算下跟进时间0.10秒、速度反馈贡献上限6°，位置系数0.30不变 | 三项主机检查通过；连续跟踪略改善，待用户实物验收 |
| `v0.9.0-direct-rate-test` | DIRECT增加50ms滤波的受限速度反馈，贡献±3°，死区内关闭；系数0.30/时间常数0.12秒 | 三项主机测试通过，288组失配最坏过冲1.29°；等待用户编译烧录和静止/5°验收 |

当前待测v0.12.0-gyro-rate-response-test；保留v0.11.1快照及v0.10.1慢速验收版。步骤见[GYRO_RATE_RESPONSE_TEST.md](GYRO_RATE_RESPONSE_TEST.md)，未实物验收。

历史记录：当前待测v0.11.0-imu-gyro-bridge-test，100ms限时接续已获用户授权，尚未实物验收；步骤见[IMU_GYRO_BRIDGE_TEST.md](IMU_GYRO_BRIDGE_TEST.md)。

历史记录：当前待测v0.10.2-pidr-slew-fix-test。慢速验收版v0.10.1已独立保留；见[本轮说明](PIDR_SLEW_FIX_TEST.md)。修复积分冻结，IMU_INVALID另查，未放宽保护。

历史记录：当前待测v0.10.0-pidr-small-angle-test，限第一轮约5°实物试验，已获用户明确批准；不能当作原wide范围通过。测试步骤见[PIDR_SMALL_ANGLE_TEST.md](PIDR_SMALL_ANGLE_TEST.md)。

历史记录：当前v0.9.1-tracking-2deg-test：用户批准2°过冲，时间常数0.10秒/速度贡献上限6°/位置系数0.30。完整288组最坏过冲1.88°，12组连续跟踪误差改善；尚未实物验收，未达到紧贴目标。MCU由用户编译烧录；见[测试说明](DIRECT_TRACKING_TEST.md)。

上一版：当前分支为`codex/hold-response`。当前待测v0.9.0-direct-rate-test：DIRECT+受限速度反馈，参数0.30/0.12秒/0.06秒/±3°。288组最坏过冲1.29°，尚未实物验收，MCU编译烧录由用户执行。IMU_INVALID仍未解决。测试见[DIRECT_RATE_TEST.md](DIRECT_RATE_TEST.md)。

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
