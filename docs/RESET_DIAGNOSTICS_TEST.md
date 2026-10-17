# 按键复位原因诊断版

版本：`v0.20.0-reset-diagnostics-test`。旧版`v0.19.0-alarm-two-key`保留Git和ZIP。
用户反馈即使不报警，先K1后K2也会重新出现开机动画，先K2时报警灯变暗；接线/通断检查未发现异常。当前根因未确定，本版用于采集证据，不代表故障已修好。

## 改动

- main一进入先保存RCC->CSR，再RCC_ClearFlag清硬件复位标志，然后原顺序初始化。已核对Keil所用LocalSPL的SystemInit不清CSR复位标志，RCC_ClearFlag函数已在工程内，无需增加Keil源文件。
- 快照存SRAM，不被App_Init或查询清除；每次重新启动覆盖为该次标志，不维护跨掉电历史。清硬件标志仅发生在启动保存之后，避免旧标志累积影响下一次复位判断，不改PWR的SBF和RTC备份状态。
- 新增只读命令`SYS RESET ?`，USB串口和BLE回复各归原通道；HELP中列出。无自动上报或新增控制命令。
- 保留双键消警、HOLD、IMU、电源管理和上位机。

示例（只说明格式，并非实测）：

```text
SYS RESET CSR=0x04000000 CAPTURED=1 PIN=1 POR_PDR=0 SOFT=0 IWDG=0 WWDG=0 LPWR=0 UPTIME_MS=3520 KEYS=0 ALARM=0
```

## 测试步骤

1. 用户Keil打开Project/eXin.uvprojx，Rebuild并烧录。下载/调试器可能引入复位，首次记录仅作基线。
2. 上位机USB连接（当前CH340为COM5，以刷新后的列表为准），原始命令输入`SYS RESET ?`，回车发送。保存完整回复。
3. 不设置闹钟，按此前方式先按K1再按K2，仅复现一次；松开两键，若出现开机动画等待约3秒。
4. **不要再按RESET、断电或重新下载**，直接再次发送`SYS RESET ?`。把前后两条完整回复一起提供。
5. 如果没有复位，也查询一次，比较UPTIME_MS是否继续增加。KEYS为当前消抖后状态：0全松、1仅K1、2仅K2、3双键。按键不稳时可用单键查询辅助，但不需要长时间重复复现。

UPTIME_MS是本次TIM计时启动后的毫秒数（约49.7天回绕），不是RTC时间。它不会因普通Sleep清零。CAPTURED=1表示启动快照调用已执行；0表示本次未捕获，不能当成“没有复位”。

## 解读边界

| 字段 | 为1的含义 |
|---|---|
| PIN | 外部NRST复位标志；可与其他标志同时存在，不能单凭此项认定某根线接错 |
| POR_PDR | 上电/掉电复位合并标志；不能区分正常上电和异常供电跌落，更不能作为电压测量 |
| SOFT | 软件复位标志，不直接指出具体代码位置 |
| IWDG / WWDG | 对应看门狗复位标志 |
| LPWR | 低功耗管理复位标志，不能解释成普通Sleep本来就会复位 |

全部标志并列返回，不猜单一原因。标志全0而又见开机动画时，仍需考虑未发生硬件复位的启动流程重入、调试器干预等，不能排除软件问题。此版不含HardFault现场记录，也不写Flash或备份寄存器保存历史。若需要定位短暂供电/NRST干扰，后续仍可能需要示波器。

参考：[ST RM0008，RCC_CSR定义](https://www.st.com/resource/en/reference_manual/cd00171190.pdf)。主机app_integration检查逐位/组合/零标志、App_Init保留快照、KEYS/ALARM、双串口应答与原双键行为；protocol检查严格新语法与旧解析，均通过。Keil实际编译和硬件复位标志待用户验收。
