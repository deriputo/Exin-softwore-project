#ifndef TIMEBASE_H
#define TIMEBASE_H

#include "stm32f10x.h"

#define TIMEBASE_PERIOD_MS 5u

typedef void (*TimebaseTickCallback)(void);

/* Owns TIM2. Callback runs in the ISR: keep it short and nonblocking. */
ErrorStatus Timebase_Init(TimebaseTickCallback callback);
uint32_t Timebase_GetMs(void);
void Timebase_TimerIRQHandler(void);

#endif
