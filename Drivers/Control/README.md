# 单轴 IMU 姿态定角

2026-10-14当前应用已关闭临时诊断，恢复P=0.8、I=0.25、D=0.02及0～180°目标/输出。`hold_output_span_deg` 当前为180°，不再限制捕获位置附近±15°。操作与实物验收见 `docs/HOLD_EXAM_TEST.md`；反馈方向仍需要真实运动确认。

当前应用已恢复Pitch/Y正向PID，目标0～180°，M=90+相对校准Y；用户已将IMU固定在连杆上且90°输出对应竖直。初调Kp=0.8、Ki=0.25、Kd=0.02、死区0.5°、D项80ms滤波。输出限幅限速、积分防饱和、传感器及时序故障保护继续有效。

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

无效 IMU、样本超过 100 ms、控制间隔超过 200 ms 都锁存故障并保持最后一次有效舵机输出，不在缺少测量时继续调节。`HOLD OFF` 清除锁存，之后需重新获得有效样本才能启动。

IMU 必须安装在被舵机驱动的机构上。上板先用手动角度确认作用轴、正负方向、运动范围和安装零位，再调整 `direction` 和 PID 参数。默认参数是可运行的初始值，主机模型测试不代表实物误差精度或扰动恢复性能；实际机架、舵机迟滞和电源会影响效果。

测试 `tests/control_test.c` 包括有效性、零误差保持、采样时效、故障锁存、限幅限速、积分防饱和、计时回绕及简化一阶机构受扰恢复。
