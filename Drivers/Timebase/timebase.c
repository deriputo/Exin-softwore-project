#include "timebase.h"
#include "timebase_config.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_tim.h"
#include "misc.h"

static volatile uint32_t elapsed_ms;
static TimebaseTickCallback tick_callback;

ErrorStatus Timebase_Init(TimebaseTickCallback callback)
{
    RCC_ClocksTypeDef clocks;
    TIM_TimeBaseInitTypeDef timer;
    NVIC_InitTypeDef nvic;
    uint32_t timer_clock;
    uint32_t divider;

    RCC_GetClocksFreq(&clocks);
    timer_clock = clocks.PCLK1_Frequency;
    if ((RCC->CFGR & RCC_CFGR_PPRE1) != 0u) {
        timer_clock *= 2u;
    }
    if (timer_clock < 1000000u || timer_clock % 1000000u != 0u) {
        return ERROR;
    }
    divider = timer_clock / 1000000u;
    if (divider > 65536u) {
        return ERROR;
    }

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);
    NVIC_DisableIRQ(TIM2_IRQn);
    TIM_DeInit(TIM2);
    elapsed_ms = 0;
    tick_callback = callback;

    TIM_InternalClockConfig(TIM2);
    TIM_TimeBaseStructInit(&timer);
    timer.TIM_Prescaler = (uint16_t)(divider - 1u);
    timer.TIM_Period = (uint16_t)(TIMEBASE_PERIOD_MS * 1000u - 1u);
    timer.TIM_CounterMode = TIM_CounterMode_Up;
    timer.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInit(TIM2, &timer);
    TIM_SetCounter(TIM2, 0);
    TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
    NVIC_ClearPendingIRQ(TIM2_IRQn);

    /* Preserve the application's NVIC priority grouping. */
    nvic.NVIC_IRQChannel = TIM2_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = TIMEBASE_IRQ_PREEMPTION_PRIORITY;
    nvic.NVIC_IRQChannelSubPriority = TIMEBASE_IRQ_SUB_PRIORITY;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);
    TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);
    TIM_Cmd(TIM2, ENABLE);
    return SUCCESS;
}

uint32_t Timebase_GetMs(void)
{
    /* Aligned 32-bit loads are atomic on Cortex-M3. Wraps after ~49.7 days. */
    return elapsed_ms;
}

void Timebase_TimerIRQHandler(void)
{
    if (TIM_GetITStatus(TIM2, TIM_IT_Update) == RESET) {
        return;
    }
    TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
    elapsed_ms += TIMEBASE_PERIOD_MS;
    if (tick_callback != 0) {
        tick_callback();
    }
}

#if TIMEBASE_DEFINE_TIM2_IRQ_HANDLER
void TIM2_IRQHandler(void)
{
    Timebase_TimerIRQHandler();
}
#endif
