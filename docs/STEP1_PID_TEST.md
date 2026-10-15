# 第1项：恢复 PID 的软件验收

本次固件启用 PID，控制轴 Pitch（IMU Y 姿态角），Kp=2.5、Ki=0.4、Kd=0.06、方向 +1。此参数尚未在正确机械安装下调试。第2项才进行机械安装，当前测试不能证明真实定角精度。

1. 断电后先拔下舵机三根线，保留 IMU 接线。下载新版 Project/eXin.uvprojx 工程；上电后将 IMU 静置到显示姿态角。
2. 进入 SERVO 页面，确认显示 MANUAL。长按 K2，预期显示 HOLD（不应显示 FOLLOW），T 为捕获的当前姿态，M 为实测角度。
3. 旋转编码器将 T 设置到 +10.0，先不要确认；此时是编辑值，不代表控制目标已经改变。电脑发送 SERVO HOLD ? 查看实际 target；短按 K2 后再查询，实际 target 应变为 +10.0。再设置并确认 0.0，查询也应同步。
4. 电脑串口使用 115200 8N1，所有命令追加回车换行。发送 SERVO TRACE ON，预期收到表头与 OK，随后约20行/秒的 H,... 数据，字段 target/measured/error/P/I/D/servo/saturated/fault。M 与目标不同且超出死区时 P 应产生对应修正；I/D 并不要求始终非零。
5. 发送 SERVO TRACE OFF，然后 SERVO HOLD OFF；页面应回到 MANUAL。再发送 SERVO SET 90，应返回 OK SERVO=90 HOLD=OFF。
6. 反馈：是否显示 HOLD；确认前后 target 是否正确；TRACE 是否持续输出；有无 FAULT。此阶段 OUT 可以变化，因为 IMU 尚未形成机械反馈，不据此判断定角是否成功。

测试结束保持 HOLD OFF，等待第2项机械安装指导。尚未安装和验证方向前，不接回舵机持续运行 HOLD。
