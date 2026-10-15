# STM32F103C8T6 目标构建支持

本目录提供项目自有配置，不改写 ST/Arm 原始库文件和版权声明。

- `stm32f10x_conf.h`：SPL 配置，包含工程使用的 GPIO/RCC/TIM/RTC/BKP/PWR/EXTI/I²C/USART/DMA/MISC。
- `stm32f103c8.ld`：GNU 链接脚本，Flash 64 KiB、SRAM 20 KiB，顶部固定保留 2 KiB 栈。
- `cmsis_gcc_compat.c`：旧 CMSIS v1.30 GCC 分支的 PRIMASK 读写定义。
- `syscalls.c`：newlib 内存/设备接口；没有隐式 UART 重定向，设备读写返回 errno 错误。

GNU 构建使用 `-mcpu=cortex-m3 -mthumb -mfloat-abi=soft`、`STM32F10X_MD` 和 `USE_STDPERIPH_DRIVER`。
头文件路径包含本目录、各自 Drivers 目录、Vendor/LocalSPL/Start 和 Vendor/LocalSPL/Library。
使用 `Vendor/LocalSPL/Start/system_stm32f10x.c` 与需要的 SPL 源文件。
GCC 启动文件来自原始 ST 库：

`Vendor/STM32F10x_StdPeriph_Lib/Libraries/CMSIS/CM3/DeviceSupport/ST/STM32F10x/startup/gcc_ride7/startup_stm32f10x_md.s`

启动文件初始化 .data/.bss、调用 SystemInit 与 main，向量表需保留在 Flash 开头。
链接时使用 `-TProject/stm32f103c8.ld`、`--gc-sections` 与链接 map 输出。
链接脚本 ASSERT 保证静态 RAM 区不侵入保留的栈；链接通过不代表实际运行时栈深度已经验证。
当前应用不使用动态分配；_sbrk 仍限制堆在 __HeapBase..__HeapLimit 内，不能侵入 2 KiB 栈保留区。
它不提供多线程/中断里的分配支持，若将来增加这类调用需补充同步与重入支持。
ELF 显式分离 Flash 的 R/X 段和 SRAM 的 R/W 段，避免产生 R/W/X LOAD 段。
原始启动不调用 C++ 构造器，若将来加入 C++ 或带构造器组件需补充启动处理。

旧 `core_cm3.c` 的 GCC 内联汇编不适合现代 GCC。目标工程仅加入本目录兼容文件，
不要同时加入旧 core_cm3.c，以免重复符号。Keil 构建使用其匹配的 CMSIS/启动支持，不加入兼容文件。

默认系统时钟配置依赖开发板 HSE 的 8 MHz 晶振；RTC 另用已确认的 32.768 kHz LSE。
若实物 HSE 不同，必须匹配 HSE_VALUE 和 SystemInit 时钟配置，再验证 TIM/USART/I²C 实测时序。
