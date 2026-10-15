#ifndef KEY_H
#define KEY_H

#include "stm32f10x.h"
#include "key_core.h"

/* Initializes GPIO and state only. Start Timebase_Init(Key_ScanTick) afterward. */
ErrorStatus Key_Init(void);
uint8_t Key_GetEvent(KeyEvent *event);
uint8_t Key_GetPressedMask(void);
uint32_t Key_GetDroppedCount(void);
/* Call exactly once per 5 ms from the common timebase ISR. */
void Key_ScanTick(void);

#endif
