# OLED 显示驱动

默认 **SSD1306 128×64、4 针 I2C 接口、地址 0x3C**，与 IMU 共用 I2C1 PB8/PB9。`oled_config.h` 可改地址。SH1106 的列偏移、其他尺寸或 SPI 模块需要另外适配，不能仅改地址。

本模块依据用户给出的驱动目录改写：

`D:\BaiduNetdiskDownload\大灰狼的共享文件\多级菜单\2024_1_21_多级菜单_v1.8_STM32C8T6_标准库\2024_1_21_单片机多级菜单_v1.8\OLED`

原作者 **江协科技**，原驱动声明允许查看、使用、修改并用于自己的项目，所有权归原作者。原始 OLED.c 的 SSD1306 初始化、纵向 8 点页显存与字模布局被保留；原 `OLED_Data_ASCII.c` 的 95 个 6×8/8×16 字模和 `OLED_Data_Hanzi.c` 的 48 项汉字数据（含未知字形）被复制并仅规范初始化括号，保留版权说明。未复制完整图片库或 OLED_Patch_byAdam，因为当前 UI 不需要它们。

与参考工程的主要变化：

- 移除 GPIO 模拟 I2C、延时循环、timer.h 依赖和同步整屏 Update。
- 工作显存和发送快照各 1024 字节。绘图仅改 RAM，`OLED_Present()`/`OLED_Update()` 请求刷新。
- 每次传 16 字节，在 chunk 之间让 IMU 使用总线；一帧的快照不被下一帧绘图修改，避免混合不同画面数据。
- 初始化、熄屏、亮屏、错误重试全部通过 Process 推进，无硬件等待循环。
- 所有点、矩形和图片操作裁剪到 128×64；坐标左上角为 (0,0)。

基础调用：

```c
OLED_Init(now);
OLED_Clear();
OLED_DrawText(0, 0, "23:59:55");
OLED_DrawRect(0, 12, 128, 52);
OLED_Present();
/* 每轮主循环，位于 I2CBus_Process 与 IMU_Process 之后。 */
OLED_Process(now);
```

保留的原绘图接口子集：`OLED_ShowChar`、`OLED_ShowString`、`OLED_ShowChinese`、`OLED_ShowImage`、`OLED_Update`。字体参数 `OLED_6X8`、`OLED_8X16` 与原工程一致。`OLED_Init()` 的新签名需要传 `now_ms`；不兼容整个旧头文件。中文字符串用 UTF-8，只有原表已有的字符能显示，缺字显示方框问号；完整当前 UI 默认用 ASCII 保证全部标签可读。

`OLED_SetPower(false)` 请求 SSD1306 `0xAE`，已有 chunk 完成后优先熄屏；`OLED_IsPowered()` 在命令完成后返回 false。亮屏时自动重刷最新工作帧。这个 API 只控制屏幕显示，不替代 MCU 的 PWR Sleep/Standby。

构建必须加入：`oled.c`、`oled_gfx.c`、`OLED_Data_ASCII.c`、`OLED_Data_Hanzi.c`，以及共享总线 `i2c_bus.c`。不要同时编译参考工程的原 OLED.c。

主机测试覆盖原字库逐列一致性、非页对齐图片、UTF-8 缺字/截断、极端坐标裁剪、发送快照、共享 IMU 总线与熄屏保持 IMU 工作。上板需要确认屏幕型号、方向、地址、对比度及刷新效果。
