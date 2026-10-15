# 公共时间基准（TIM2）

TIM2 每 5 ms 更新一次毫秒计数，再调用一个可配置的回调。
此模块拥有 TIM2 和默认的 TIM2_IRQHandler；不占用 TIM1、TIM3、TIM4。

```c
Key_Init();
if (Timebase_Init(Key_ScanTick) != SUCCESS) {
    /* 报告时钟配置错误。 */
}
now_ms = Timebase_GetMs();
```

按键必须在启用时间基准前初始化；回调在中断中运行，禁止延时和耗时操作。
只有调用 Timebase_Init 才会开始按键扫描，Key_Init 现在只配置 GPIO 和扫描状态。
如果工程已有 TIM2_IRQHandler，将 timebase_config.h 中的
TIMEBASE_DEFINE_TIM2_IRQ_HANDLER 改为 0，在已有中断函数中调用 Timebase_TimerIRQHandler。
中断优先级也在 timebase_config.h 配置，沿用应用已有的 NVIC 分组。

计数器为 uint32_t，约 49.7 天回绕。周期判断采用 `(uint32_t)(now - start)`，
不要使用 `now >= start + duration`，也不要在动画运行时重新初始化时间基准。
Timebase_GetMs 返回从本模块初始化开始的毫秒数，分辨率是 5 ms。
长期屏蔽中断会丢失更新周期，计数不再等于实际经过时间。

在常见 72 MHz TIM2 时钟下，PSC=71，ARR=4999。
模块读取 APB1 时钟，并在总线分频不为 1 时乘 2。
当前仅支持定时器时钟为 1 MHz 整数倍的配置，否则返回 ERROR。
将 timebase.c 加入 SPL 工程，并将此目录加入头文件搜索路径。
