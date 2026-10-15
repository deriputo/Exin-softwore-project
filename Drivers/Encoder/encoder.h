#ifndef ENCODER_H
#define ENCODER_H

#include "stm32f10x.h"
#include "encoder_core.h"

/* Owns TIM4 and PB6/PB7. Initialize after configuring the system clock. */
ErrorStatus Encoder_Init(void);
/* Main-loop only. Samples CNT once, updates positions, returns signed whole steps.
   Call regularly (e.g. every 5 ms). One consumer dispatches steps to the current page. */
int32_t Encoder_ReadDeltaSteps(void);
/* Current live hardware CNT, modulo 65536; not an absolute angle. */
uint16_t Encoder_GetCounter(void);
/* Logical positions from the last ReadDeltaSteps sample, since Init/ResetPosition. */
int64_t Encoder_GetPositionCounts(void);
int64_t Encoder_GetPositionSteps(void);
uint8_t Encoder_GetFaults(void);
/* Main-loop only. Discards previous travel and partial steps without clearing CNT. */
void Encoder_ResetPosition(void);

#endif
