# 两个按键的非阻塞底层驱动（STM32F103 + SPL）

已确认：标准外设库 SPL，按下为低电平。
KEY1 使用 PA0，KEY2 按用户接线改为 PA2；两者按下接 GND，内部上拉。引脚配置位于 `key_config.h`。
TIM2 现由 Drivers/Timebase 公共时间基准管理，并调用 Key_ScanTick 扫描按键。
按键一端连接 GPIO，另一端连接 GND，GPIO 使用内部上拉。
这里只提供可集成的驱动，未包含完整 Keil 工程、SPL/CMSIS 和启动文件。

## 工作方式

- TIM2 每 5 ms 进入一次更新中断，同时扫描两个按键。
- 每次原始电平改变，重新开始消抖窗口。首次观察到变化后，必须再稳定 20 ms 才接受新状态。
- 两个按键分别维护状态，按下和松开都消抖，稳定状态可通过位掩码查询。
- 按下确认后开始累计持续时间；严格 `> 1000 ms` 时产生一次长按事件。
- 扫描分辨率为 5 ms，因此首次长按事件在确认按下后的 1005 ms 产生。
- 长按时间依据消抖后的状态计量，不是从第一次电气下降沿计量；物理动作与识别之间存在采样、消抖延迟。
- 中断只采样和生成事件，不延时，不操作 OLED，不执行菜单或保存动作。
- 主循环从 32 个事件的 FIFO 中取事件。队列满时丢弃新事件，并累计丢失数。
- 取事件时短暂关闭中断，并恢复原先的 PRIMASK，不会错误开启原本已关闭的中断。

| 手势 | 事件序列 |
| --- | --- |
| 短按 | PRESS → SHORT → RELEASE |
| 长按 | PRESS → LONG → RELEASE |
| 一直按住 | PRESS → LONG，继续按住不再重复 |
| 抖动未稳定 | 无状态切换事件 |

PRESS/RELEASE 是底层状态事件。普通页面用 SHORT 翻页/选项，用 LONG 保存。
短按只能在松开后确认，否则无法提前判断同一次按下是否会变成长按。
双键同时按下可通过 `(Key_GetPressedMask() & (KEY1_MASK | KEY2_MASK)) == (KEY1_MASK | KEY2_MASK)` 判断。

## 加入 SPL 工程

1. 加入 `key.c`、`key_core.c` 和 `Drivers/Timebase/timebase.c`，把两个目录加入头文件搜索路径。
2. 工程需要已有 GPIO、RCC、TIM、misc 的 SPL 源码，以及 CMSIS 和正确启动文件。
3. 在系统时钟配置完成后调用 `Key_Init()`，再调用 `Timebase_Init(Key_ScanTick)`，检查返回值为 SUCCESS。
4. 主循环持续取出并处理事件，不要把事件处理放入中断。
5. TIM2 由公共时间基准拥有，不要同时用它生成舵机/LED PWM。如果已存在 TIM2 中断处理函数，将 timebase_config.h 中的 `TIMEBASE_DEFINE_TIM2_IRQ_HANDLER` 改为 0，在已有函数中调用 `Timebase_TimerIRQHandler()`，避免重复定义。

```c
#include "key.h"
#include "timebase.h"

int main(void)
{
    KeyEvent event;

    /* 完成现有工程的时钟初始化及其他外设初始化。 */
    if (Key_Init() != SUCCESS) {
        /* 根据工程习惯报告初始化错误。 */
        while (1) { }
    }
    if (Timebase_Init(Key_ScanTick) != SUCCESS) {
        /* 报告不支持的 TIM2 时钟配置。 */
        while (1) { }
    }

    while (1) {
        while (Key_GetEvent(&event)) {
            if (event.type == KEY_EVENT_SHORT) {
                if (event.key == KEY_ID_1) {
                    /* 浏览状态：切换到下一个页面。 */
                } else {
                    /* 选择下一个参数。 */
                }
            } else if (event.type == KEY_EVENT_LONG) {
                if (event.key == KEY_ID_2) {
                    /* 保存当前参数；浏览页等其他状态按应用规则处理。 */
                }
            }
        }
        /* 时钟、OLED、串口等后台任务。 */
    }
}
```

## TIM2 参数

读取 PCLK1，并根据 APB1 分频判断定时器时钟是否需要乘 2。
将 TIM2 分频到 1 MHz，再设置 ARR = 4999，得到 5 ms 更新周期。
常见 72 MHz 定时器时钟对应 PSC = 71。不能把 PCLK1 的 36 MHz 直接当作这种配置下的 TIM2 时钟。
若定时器时钟不是 1 MHz 的整数倍，初始化返回 ERROR，避免静默使用错误计时周期。
公共时间基准不修改全局 NVIC 优先级分组；根据应用已有分组调整 `timebase_config.h` 的中断优先级。

## 与闹钟逻辑衔接

报警期间建议在 PRESS 事件停止当前提醒，使用户不必等到松开。
应用层同时记录“此次手势用于确认报警”，一直到对应 RELEASE 都忽略该键的 SHORT/LONG，防止停止报警后又翻页或保存。
双键确认时按同样规则消费两个键的手势。停止本次提醒与关闭闹钟启用状态属于应用层逻辑。
驱动不会自动执行报警、翻页或保存。

## 验证和限制

`tests/key_core_test.c` 直接测试生产扫描核心，不依赖 MCU。
覆盖消抖、松开抖动、短按、严格长按边界、长按不重复、双键独立、事件 FIFO 和溢出恢复。

```text
gcc -std=c99 -Wall -Wextra -Werror -pedantic -IDrivers/Key Drivers/Key/key_core.c tests/key_core_test.c -o tmp/key_core_test.exe
tmp/key_core_test.exe
```

主机测试不覆盖真实 GPIO、TIM2 时钟、中断和接线。需在 SPL 工程编译后上板确认。
主循环应及时消费事件；正常工作时 `Key_GetDroppedCount()` 应为 0。
长时间屏蔽中断会丢失定时器扫描周期，因为更新标志不是计数队列，因此也会影响长按计时。
