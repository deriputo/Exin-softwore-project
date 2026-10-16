# 单轴 IMU 姿态定角

当前活动源码：**v0.10.0-pidr-small-angle-test**，OLED显示HOLD Y PIDR。用户已明确批准独立小角度实物试验。使用固定偏置PID，P0.7/Ki6/Kd0.005；不是小积分方案，也不是DIRECT。fast假设288组最坏过冲1.127°，但原wide范围最坏19.415°未通过；当前仅允许先做约5°测试，实物仍以2°为验收上限，尚未实物验收。由用户Keil编译烧录。[本轮步骤](../../docs/PIDR_SMALL_ANGLE_TEST.md)。

以下为历史记录。

当前待测为 **v0.9.1-tracking-2deg-test**：用户批准2°短暂过冲上限；位置系数0.30不变，估计时间常数0.10秒、速度贡献限幅6°。288组模型最坏过冲1.88°，12组连续跟踪模型均改善，但仍未达到紧贴目标。Keil编译烧录及实物验收由用户执行。详见docs/DIRECT_TRACKING_TEST.md。

以下为历史版本说明。

当前v0.9.0为DIRECT+：位置系数0.30、估计时间常数0.12秒、50ms滤波的姿态速度反馈系数0.06秒、贡献限幅±3°。死区内P/D为0且保持最后指令。TRACE I=0、D为受限速度反馈；旧PI参数未启用。DIRECT估计状态在死区外可产生相当于积分的积累，并非完全无积分尾调。详见docs/DIRECT_RATE_TEST.md；以下DIRECT无D说明为v0.8.0历史记录。

2026-10-16最新应用已切到CONTROL_MODE_DIRECT试验模式，OLED显示HOLD Y DIRECT。基于上一轮限幅后指令的一阶位置估计（时间常数0.30秒）加0.5倍姿态误差补偿，方向-1；不是当前指令直接当实际位置，也不是原PID逐轮累加。到达0.5°死区后保持最后指令，既有故障/限幅/限速继续有效。旧PI/FOLLOW分支保留，以下PI参数为历史参考。完整说明和TRACE语义见docs/DIRECT_COMPENSATION_TEST.md，尚未实物验收。

2026-10-16应用试验参数为Kp1、Kd0、Ki_eff=0.8+0.2*e²/(e²+4²)，Pitch/Y轴，方向-1，目标/输出0～180°，M=90+相对校准Y。用户IMU安装在连杆上，手动90°对应竖直，当前版本待实物验收。测试和曲线见docs/I_FLOOR08_TEST.md。输出限幅限速、抗积分饱和、传感器及时序故障保护保留。

校准时要求底座水平且连杆在90°输出时竖直；首次实测只做80/90/100°及小扰动。FOLLOW分支保留但不启用。下文通用默认值用于核心测试，当前应用参数以Application/app_config.h和App_Init为准。


该模块不访问硬件；应用传入 IMU 的校准后 Roll/Pitch 与采样时间，取得 PWM 脉宽后调用 `ServoPWM_SetPulseUs()`。每个 `Control` 实例独立。`ControlSample.valid` 必须表示传感器已连接、完成静止校准并取得可靠姿态，不能仅表示 I²C 读取成功。

默认参数：Roll 轴、方向 +1、Kp=2.5、Ki=0.4、Kd=0.06、20 ms 控制周期、0.3° 死区、目标 ±60°、积分贡献 ±35°、舵机输出 0°～180°、限速 90°/s。所有值可以通过 `ControlConfig` 调整。参数不满足有效范围时初始化使用默认值。

接口：

```c
Control controller;
ControlSample sample;
Control_Init(&controller, 0, now_ms);
/* 主循环内，sample 由校准后的 IMU 提供 */
ServoPWM_SetPulseUs(Control_Update(&controller, &sample, now_ms));
```

- `Control_SetManualAngle()` 接受整数 0°～180°，HOLD 打开时返回 `CONTROL_HOLD_ACTIVE`。
- `Control_StartHold()` 捕获当前所选轴的姿态作为目标，并捕获当前舵机角度为控制偏置。零误差时保持原脉宽，避免进入闭环立即跳到中位。重复 ON 不重设目标。
- `Control_SetTarget()` 用于界面设置目标姿态；舵机角度与目标姿态是不同参数。
- `Control_StopHold()` 保留最后舵机输出、关闭闭环并清除故障。
- `Control_GetSnapshot()` 供 OLED 和 20 Hz TRACE 使用。

有效新鲜样本会在手动模式及故障锁存时继续更新实测角度和误差遥测；这不会改变舵机脉宽或自动清除故障。

PID 对测量值求导，改变目标不会产生微分冲击。积分有上下限；达到输出限位或输出限速、且误差继续向该方向推动时暂停积分。只对新的样本计算，每个控制周期最多计算一次。

integral_error_scale_deg默认0代表恒定Ki。大于0时，有效Ki=integral_min_ki+(ki-integral_min_ki)*e²/(e²+scale²)，其中ki为上限且0<=integral_min_ki<=ki。死区门控和抗饱和可让实际增量为0，不表示积分增益下限失效。

无效 IMU、样本超过 100 ms、控制间隔超过 200 ms 都锁存故障并保持最后一次有效舵机输出，不在缺少测量时继续调节。`HOLD OFF` 清除锁存，之后需重新获得有效样本才能启动。

IMU 必须安装在被舵机驱动的机构上。上板先用手动角度确认作用轴、正负方向、运动范围和安装零位，再调整 `direction` 和 PID 参数。默认参数是可运行的初始值，主机模型测试不代表实物误差精度或扰动恢复性能；实际机架、舵机迟滞和电源会影响效果。

测试 `tests/control_test.c` 包括有效性、零误差保持、采样时效、故障锁存、限幅限速、积分防饱和、计时回绕及简化一阶机构受扰恢复。
