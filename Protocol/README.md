# 串口协议解析与有序调度

核心只负责行解析、命令队列和响应重试，通过回调访问 RTC/IMU/舵机，所有接口在主循环调用。

```c
Protocol_Init(&protocol, AppCommandHandler, AppSerialWriter, &app);
while (Serial_ReadByte(&byte)) {
    Protocol_FeedByte(&protocol, byte);
}
Protocol_Process(&protocol, now_ms);
```

支持以下精确格式，除 `HELP`/`help` 外均为大写，不去除多余空格，不允许追加字符。

| 输入 | 解析的动作 |
|---|---|
| `HELP` 或 `help` | 帮助 |
| `TIME(23:59:55)` | 设置时间 |
| `ALARM1(06:30:00)` | 设置并启用闹钟 1（编号 1～3） |
| `ALARM1 OFF` | 关闭闹钟 |
| `ALARM1 DEL` | 删除闹钟 |
| `ALARM ?` | 查询三个闹钟 |
| `IMU ?` | 查询同源 X/Y/Z 姿态角 |
| `IMU RAW ?` | 扩展诊断：查询当前原始数据与有效性、校准状态 |
| `IMU FAULT ?` | 扩展诊断：查询最近一次HOLD首次故障快照；恢复和OFF不覆盖，成功重新ON或复位清除 |
| `SERVO ?` | 查询角度/状态 |
| `SERVO SET 90` | 整数角度 0～180 |
| `SERVO HOLD ON`、`SERVO HOLD OFF`、`SERVO HOLD ?` | 姿态保持控制与查询 |
| `SERVO TRACE ON`、`SERVO TRACE OFF`、`SERVO TRACE ?` | 调试流开关与查询 |

时间使用严格两位时分秒，小时 0～23，分秒 0～59；非法内容返回 `ERR SYNTAX`，不会调用应用处理器。CR、LF 和 CRLF 均可结束命令，空行忽略。95 字符以上或包含不可打印字节的行整体无效。

最多 8 个待处理命令。队列满时停止接收新命令，按原顺序完成此前命令，再返回 `ERR OVERFLOW n`；没有偷偷覆盖旧设置。输入错误不会阻塞主循环。

处理回调在首次调用收到 `first_call=1`。同步命令完成写好包含 CRLF 的响应并返回 `PROTOCOL_DONE`。异步 `TIME` 命令应在首次调用接受 RTC 设置请求，然后返回 `PROTOCOL_WAIT`，等硬件写入结束且 `RTCClock_GetStatus()==READY` 后才返回 `DONE`。后续命令始终等待，因此跟随的查询不会读到旧时间。

响应最大 383 字节，只有传输回调接受整条响应后才移除队列项；TX 暂忙会重试同一缓存，不会再次执行已完成的设置。`Protocol_IsBusy()` 包含队列、异步等待和半条输入，应用进入待机前需同时确认它与 UART 空闲。

`Protocol_FormatTenths()` 和 `Protocol_FloatToTenths()` 用整数小数格式化，避免链接浮点 `printf`。IMU 使用 `force_sign=1` 输出 `+12.3`、`-0.1`。应用自行根据实际状态组织规定的响应。

TRACE 的字段由应用统一定义并在流开始时发送 CSV 表头：`H,ms,target,measured,error,P,I,D,servo,saturated,fault`，角度/控制贡献以 0.1° 输出，数据周期 50 ms。发生 TX 背压时丢弃完整调试帧并计数，不能等待发送而拖慢采样。

测试 `tests/protocol_test.c` 检查合法/非法输入、边界、异步顺序、TX 背压、超长行、受损输入、队列溢出和带符号小数。
