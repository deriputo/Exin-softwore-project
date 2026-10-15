# 第三方来源

工程主体、状态机、应用整合和测试为本任务编写。以下库与资料保留原作者归属：

- `Vendor/LocalSPL/Library`、`Vendor/LocalSPL/Start`：从用户提供的多级菜单工程复制的 ST STM32F10x 标准外设库与 Arm CMSIS，文件中的原始许可和版权声明保留。Keil 启动文件复制至 Project 后只调整栈大小为 2 KiB。
- `Vendor/STM32F10x_StdPeriph_Lib`：ST SPL 的公开镜像 `https://github.com/wajatimur/stm32f10x-stdperiph-lib`，GNU 启动文件来自其原始 ST 目录；保留第三方源文件中的许可。最终压缩包不包含镜像的 Git 数据。
- OLED 字库：用户指定的 `D:\BaiduNetdiskDownload\大灰狼的共享文件\多级菜单\2024_1_21_多级菜单_v1.8_STM32C8T6_标准库\2024_1_21_单片机多级菜单_v1.8\OLED`，保留江协科技作者归属。ASCII 点阵与汉字点阵保留；汉字索引改成相同 UTF-8 字节的 ASCII C 转义，兼容 AC5。显示层提供其常用绘制接口子集，GPIO 模拟 I²C 则替换为共享硬件 I²C 状态机。
- MPU6050 参考：用户指定的 `E:\STM32project\6-2 硬件收发IIC\hardware\MPU6050.c` 与配套寄存器头文件；本实现参考其配置思路，使用异步连续采样及新的状态机、校准和姿态估算。
- STM32 Autodebug：用户本机独立套件，由用户显式选择，用于 Keil 构建、设备枚举和固件契约检查；未复制到交付固件中。

本次没有修改用户提供的原始 OLED/MPU6050 工程目录。
