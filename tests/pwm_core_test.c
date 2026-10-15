#include <assert.h>
#include <stdio.h>
#include "pwm_core.h"

int main(void)
{
    uint32_t t;
    uint16_t previous_brightness = 0;
    uint16_t previous_frequency = 0;
    uint16_t peak = 0;

    assert(PWM_TIMER_TICK_HZ / SERVO_PERIOD_TICKS == 50);
    assert(PWM_TIMER_TICK_HZ / LED_PERIOD_TICKS == 1000);
    /* Duty basis points: 250 = 2.5%, 1250 = 12.5%. */
    assert(SERVO_MIN_PULSE_US * 10000u / SERVO_PERIOD_TICKS == 250);
    assert(SERVO_MAX_PULSE_US * 10000u / SERVO_PERIOD_TICKS == 1250);
    assert(PWMCore_AngleToPulseUs(0) == 500);
    assert(PWMCore_AngleToPulseUs(90) == 1500);
    assert(PWMCore_AngleToPulseUs(180) == 2500);
    assert(PWMCore_AngleToPulseUs(-90) == 500);
    assert(PWMCore_AngleToPulseUs(270) == 2500);
    assert(PWMCore_ClampPulseUs(0) == 500);
    assert(PWMCore_ClampPulseUs(65535) == 2500);
    assert(PWMCore_ClampPulseUs(1234) == 1234);
    for (t = 1; t <= 180; ++t) {
        assert(PWMCore_AngleToPulseUs((int16_t)t) > PWMCore_AngleToPulseUs((int16_t)(t - 1)));
    }

    assert(PWMCore_BrightnessToTicks(0) == 0);
    assert(PWMCore_BrightnessToTicks(500) == 500);
    assert(PWMCore_BrightnessToTicks(1000) == 1000);
    assert(PWMCore_BrightnessToTicks(65535) == 1000);
    assert(PWMCore_BreathFrequencyMilliHz(0) == 500);
    assert(PWMCore_BreathFrequencyMilliHz(1500) == 1250);
    assert(PWMCore_BreathFrequencyMilliHz(3000) == 2000);
    assert(PWMCore_BreathFrequencyMilliHz(0xffffffffu) == 2000);
    assert(PWMCore_BreathBrightness(0) == 0);
    assert(PWMCore_BreathBrightness(500) == 684);
    assert(PWMCore_BreathBrightness(3000) == 0);
    assert(PWMCore_BreathBrightness(3005) == 0);
    assert(PWMCore_BreathBrightness(0xffffffffu) == 0);

    for (t = 0; t <= LED_BREATH_DURATION_MS; t += 5) {
        uint16_t brightness = PWMCore_BreathBrightness(t);
        uint16_t frequency = PWMCore_BreathFrequencyMilliHz(t);
        int difference = (int)brightness - (int)previous_brightness;
        assert(brightness <= 1000);
        assert(frequency >= previous_frequency);
        assert(difference >= -60 && difference <= 60);
        if (brightness > peak) {
            peak = brightness;
        }
        previous_brightness = brightness;
        previous_frequency = frequency;
    }
    assert(peak >= 999);
    assert(PWMCore_BreathBrightness(2995) <= 2);
    puts("PASS: PWM periods, servo endpoints/clamps, brightness, frequency ramp, smooth finish");
    return 0;
}
