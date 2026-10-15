#include "pwm_core.h"

/* Smoothstep: 3*x*x - 2*x*x*x, with x and result represented in 0..1000. */
static uint16_t SmoothStep(uint32_t x)
{
    return (uint16_t)((x * x * (3000u - 2u * x) + 500000u) / 1000000u);
}

uint16_t PWMCore_ClampPulseUs(uint16_t pulse_us)
{
    if (pulse_us < SERVO_MIN_PULSE_US) {
        return SERVO_MIN_PULSE_US;
    }
    if (pulse_us > SERVO_MAX_PULSE_US) {
        return SERVO_MAX_PULSE_US;
    }
    return pulse_us;
}

uint16_t PWMCore_AngleToPulseUs(int16_t angle_deg)
{
    if (angle_deg < 0) {
        angle_deg = 0;
    } else if (angle_deg > 180) {
        angle_deg = 180;
    }
    return (uint16_t)(SERVO_MIN_PULSE_US +
        ((uint32_t)angle_deg * (SERVO_MAX_PULSE_US - SERVO_MIN_PULSE_US) + 90u) / 180u);
}

uint16_t PWMCore_BrightnessToTicks(uint16_t brightness_permille)
{
    if (brightness_permille > 1000u) {
        brightness_permille = 1000u;
    }
    return (uint16_t)(((uint32_t)brightness_permille * LED_PERIOD_TICKS + 500u) / 1000u);
}

uint16_t PWMCore_BreathFrequencyMilliHz(uint32_t elapsed_ms)
{
    if (elapsed_ms >= LED_BREATH_DURATION_MS) {
        return LED_BREATH_END_MHZ;
    }
    return (uint16_t)(LED_BREATH_START_MHZ +
        (LED_BREATH_END_MHZ - LED_BREATH_START_MHZ) * elapsed_ms / LED_BREATH_DURATION_MS);
}

uint16_t PWMCore_BreathBrightness(uint32_t elapsed_ms)
{
    uint64_t phase;
    uint32_t triangle;
    uint32_t brightness;
    uint32_t fade;

    if (elapsed_ms >= LED_BREATH_DURATION_MS) {
        return 0;
    }

    /* Integrate f(t) = f0 + (f1-f0)*t/T. Phase units are millionths of a cycle.
       Absolute-time evaluation avoids drift and handles missed main-loop updates. */
    phase = (uint64_t)LED_BREATH_START_MHZ * elapsed_ms +
        (uint64_t)(LED_BREATH_END_MHZ - LED_BREATH_START_MHZ) * elapsed_ms * elapsed_ms /
        (2u * LED_BREATH_DURATION_MS);
    phase %= 1000000u;
    triangle = (uint32_t)(phase <= 500000u ? phase / 500u : (1000000u - phase) / 500u);
    brightness = SmoothStep(triangle);

    /* Smoothly end the one-shot animation; no arbitrary-phase cutoff at 3 s. */
    if (elapsed_ms > LED_BREATH_DURATION_MS - LED_BREATH_FADE_MS) {
        fade = (LED_BREATH_DURATION_MS - elapsed_ms) * 1000u / LED_BREATH_FADE_MS;
        brightness = (brightness * SmoothStep(fade) + 500u) / 1000u;
    }
    return (uint16_t)brightness;
}
