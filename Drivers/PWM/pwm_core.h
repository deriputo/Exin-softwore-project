#ifndef PWM_CORE_H
#define PWM_CORE_H

#include <stdint.h>

#define PWM_TIMER_TICK_HZ       1000000u
#define SERVO_PWM_HZ            50u
#define SERVO_PERIOD_TICKS      (PWM_TIMER_TICK_HZ / SERVO_PWM_HZ)
#define SERVO_MIN_PULSE_US      500u
#define SERVO_MAX_PULSE_US      2500u
#define LED_PWM_HZ              1000u
#define LED_PERIOD_TICKS        (PWM_TIMER_TICK_HZ / LED_PWM_HZ)
#define LED_BREATH_DURATION_MS  3000u
#define LED_BREATH_FADE_MS      300u
#define LED_BREATH_START_MHZ    500u  /* milli-Hz: 0.5 Hz */
#define LED_BREATH_END_MHZ      2000u /* milli-Hz: 2 Hz */

uint16_t PWMCore_ClampPulseUs(uint16_t pulse_us);
uint16_t PWMCore_AngleToPulseUs(int16_t angle_deg);
uint16_t PWMCore_BrightnessToTicks(uint16_t brightness_permille);
uint16_t PWMCore_BreathBrightness(uint32_t elapsed_ms);
uint16_t PWMCore_BreathFrequencyMilliHz(uint32_t elapsed_ms);

#endif
