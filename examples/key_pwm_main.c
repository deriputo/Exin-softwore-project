/* Integration example, not a complete firmware project.
   Add only one main.c to your Keil project. SystemInit must configure the clock. */
#include "key.h"
#include "timebase.h"
#include "pwm.h"
#include "encoder.h"
#include "rtc_clock.h"

#if KEY_SCAN_PERIOD_MS != TIMEBASE_PERIOD_MS
#error Key scan period must match the common timebase period
#endif

int main(void)
{
    KeyEvent event;
    uint32_t now_ms;
    uint32_t last_encoder_ms = 0;
    int32_t encoder_steps;
    uint8_t acknowledged_gestures = 0;
    uint8_t event_mask;

    if (Key_Init() != SUCCESS || PWM_Init() != SUCCESS || Encoder_Init() != SUCCESS) {
        while (1) { /* Initialization error: add application diagnostics here. */ }
    }
    if (Timebase_Init(Key_ScanTick) != SUCCESS) {
        while (1) { /* Unsupported TIM2 clock configuration. */ }
    }
    now_ms = Timebase_GetMs();
    RTCClock_Init(now_ms);
    LEDPWM_BreathStart(now_ms);

    while (1) {
        now_ms = Timebase_GetMs();
        PWM_Process(now_ms);
        RTCClock_Process(now_ms);
        if ((uint32_t)(now_ms - last_encoder_ms) >= 5u) {
            last_encoder_ms = now_ms;
            encoder_steps = Encoder_ReadDeltaSteps();
            if (encoder_steps != 0) {
                /* Pass the signed magnitude to the current page: +N/-N steps.
                   Do not reduce a multi-step result to just +1/-1. */
            }
        }
        while (Key_GetEvent(&event)) {
            event_mask = (uint8_t)(1u << event.key);
            if (event.type == KEY_EVENT_RELEASE) {
                acknowledged_gestures &= (uint8_t)~event_mask;
                continue;
            }
            if (acknowledged_gestures & event_mask) {
                continue;
            }
            if (RTCClock_GetActiveAlarmMask() != 0) {
                RTCClock_AcknowledgeAlarm();
                /* Consume this gesture, including both keys if both are held.
                   SHORT/LONG after acknowledgement must not also change pages/save. */
                acknowledged_gestures |= (uint8_t)(event_mask | Key_GetPressedMask());
                continue;
            }
            if (event.type == KEY_EVENT_SHORT) {
                /* Dispatch short presses according to the current page. */
            } else if (event.type == KEY_EVENT_LONG) {
                /* Save settings on KEY2 long press in the appropriate page. */
            }
        }
        /* Run other nonblocking application tasks here. */
    }
}
