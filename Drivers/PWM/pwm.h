#ifndef PWM_H
#define PWM_H

#include "stm32f10x.h"
#include "pwm_core.h"

/* Owns TIM1/PA8 and TIM3/PA6. Starts servo at 90 degrees, LED off. */
ErrorStatus PWM_Init(void);
void ServoPWM_SetAngle(int16_t angle_deg);
void ServoPWM_SetPulseUs(uint16_t pulse_us);
/* Returns the requested width; hardware applies preload at the next PWM period. */
uint16_t ServoPWM_GetPulseUs(void);

/* All setters and PWM_Process run in the main loop, not in interrupts. */
void LEDPWM_SetBrightness(uint16_t brightness_permille);
void LEDPWM_BreathStart(uint32_t now_ms);
void LEDPWM_BreathStop(void);
uint8_t LEDPWM_IsBreathing(void);
/* Call often with Timebase_GetMs(); work is limited to once per 5 ms. */
void PWM_Process(uint32_t now_ms);

#endif
