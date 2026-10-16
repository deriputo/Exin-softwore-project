# 单轴 IMU 姿态定角

v0.14.0新增PID死区平滑试验：deadband_transition_deg默认0兼容旧硬死区，应用设0.3°，0.5～0.8°误差使用三次smoothstep权重接入P/I；≥0.8°恢复原误差，D输入和保护不变。UI HOLD Y SOFT。此项针对边缘回调，有限模型存在少量尾段时间代价，实物未验收，详见../../docs/SMOOTH_DEADBAND_TEST.md。以下RATE说明为沿用的基础算法。

当前试验版v0.12.0-gyro-rate-response-test，OLED为HOLD Y RATE：P0.7、Ki8、D系数0.005，直接使用校准后的传感器Z轴角速度，速度贡献±0.5°，静止小噪声抑制。六项主机检查通过，未实物验收；100ms陀螺仪接续和保护保留。连续跟踪模型改善约17～22%，原wide延迟模型仍不合格，不能保证实物实时跟随。上一版v0.11.1及慢速验收版v0.10.1独立保存。 [当前参数、输入契约和验证](../../docs/GYRO_RATE_RESPONSE_TEST.md)。启用use_rate_sample时，ControlSample必须提供同一采样时刻、相同控制轴符号的angular_rate_dps和angular_rate_valid；旧角差分路径默认保留。

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
