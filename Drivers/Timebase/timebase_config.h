#ifndef TIMEBASE_CONFIG_H
#define TIMEBASE_CONFIG_H

#define TIMEBASE_IRQ_PREEMPTION_PRIORITY 2u
#define TIMEBASE_IRQ_SUB_PRIORITY       0u
/* Set to 0 when TIM2_IRQHandler is already defined in stm32f10x_it.c. */
#define TIMEBASE_DEFINE_TIM2_IRQ_HANDLER 1

#endif
