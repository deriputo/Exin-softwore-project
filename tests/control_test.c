#include <assert.h>
#include <stdio.h>
#include "control.h"
#include "app_config.h"

static float Abs(float x) { return x < 0 ? -x : x; }
static ControlSample Sample(float roll, float pitch, uint32_t now)
{
    ControlSample sample;
    sample.roll_deg = roll;
    sample.pitch_deg = pitch;
    sample.timestamp_ms = now;
    sample.valid = 1;
    return sample;
}

/* Same 8deg base step and first-order actuator for old/new tuning. This tests
   relative recovery and safety; it does not predict real SG90 settling time. */
static uint32_t RecoveryMs(float kp, float ki, float slew)
{
    Control control;
    ControlConfig config;
    ControlSample sample;
    float plant = 90, previous = 90;
    uint32_t now, stable = 0;
    Control_DefaultConfig(&config);
    config.axis = CONTROL_AXIS_PITCH;
    config.direction = APP_HOLD_DIRECTION;
    config.target_min_deg = 0; config.target_limit_deg = 180;
    config.integral_limit_deg = 180;
    config.kp = kp; config.ki = ki; config.kd = APP_HOLD_KD;
    config.deadband_deg = APP_HOLD_DEADBAND_DEG;
    config.slew_deg_per_second = slew;
    Control_Init(&control, &config, 0);
    sample = Sample(0, plant, 0);
    assert(Control_StartHold(&control, &sample, 0) == CONTROL_OK);
    for (now = 20; now <= 60000; now += 20) {
        float base = now >= 1000 ? 8.0f : 0.0f;
        /* Observed physical mount: increasing output decreases ground Pitch. */
        plant += 0.08f * (base + 180.0f - control.state.output_angle_deg - plant);
        sample = Sample(0, plant, now);
        (void)Control_Update(&control, &sample, now);
        assert(control.state.fault == CONTROL_FAULT_NONE);
        assert(control.state.output_angle_deg >= 0 && control.state.output_angle_deg <= 180);
        assert(Abs(control.state.output_angle_deg - previous) <= slew * 0.02f + 0.001f);
        previous = control.state.output_angle_deg;
        if (now >= 1000 && Abs(plant - 90) <= 1.0f) stable += 20;
        else stable = 0;
        if (stable >= 1000) return now - 1000;
    }
    return 60000;
}

int main(void)
{
    Control control;
    ControlConfig config;
    ControlSample sample;
    uint32_t now;
    float previous;
    float plant = 0.0f;
    union { uint32_t bits; float number; } nan_value;

    Control_Init(&control, 0, 0);
    assert(control.state.pulse_us == 1500);
    assert(Control_SetManualAngle(&control, 0) == CONTROL_OK);
    assert(control.state.pulse_us == 500);
    assert(Control_SetManualAngle(&control, 180) == CONTROL_OK);
    assert(control.state.pulse_us == 2500);
    assert(Control_SetManualAngle(&control, -1) == CONTROL_BAD_ARGUMENT);
    assert(Control_SetManualAngle(&control, 181) == CONTROL_BAD_ARGUMENT);
    assert(Control_SetManualAngle(&control, 90) == CONTROL_OK);
    sample = Sample(5.5f, 20.0f, 0);
    assert(Control_Update(&control, &sample, 0) == 1500);
    assert(control.state.measured_deg == 5.5f && control.state.error_deg == -5.5f);
    sample = Sample(10.0f, 20.0f, 0);
    sample.valid = 0;
    assert(Control_StartHold(&control, &sample, 0) == CONTROL_SENSOR_UNAVAILABLE);
    sample.valid = 1;
    assert(Control_StartHold(&control, &sample, 101) == CONTROL_SENSOR_UNAVAILABLE);
    assert(Control_StartHold(&control, &sample, 0) == CONTROL_OK);
    assert(control.state.target_deg == 10.0f);
    assert(Control_SetManualAngle(&control, 120) == CONTROL_HOLD_ACTIVE);
    assert(Control_SetTarget(&control, 61.0f) == CONTROL_BAD_ARGUMENT);
    assert(Control_SetTarget(&control, -61.0f) == CONTROL_BAD_ARGUMENT);
    assert(Control_SetTarget(&control, 60.0f) == CONTROL_OK);
    previous = control.state.output_angle_deg;
    sample = Sample(10.0f, 20.0f, 20);
    (void)Control_Update(&control, &sample, 20);
    assert(control.state.output_angle_deg > previous);
    assert(control.state.output_angle_deg - previous <= 1.801f);
    assert(control.state.saturated);
    assert(Abs(control.state.i_term) < 0.0001f); /* slew anti-windup */
    assert(Control_StartHold(&control, &sample, 20) == CONTROL_OK);
    assert(control.state.target_deg == 60.0f); /* repeated ON does not recapture */
    previous = control.state.output_angle_deg;
    assert(Control_Update(&control, &sample, 40) == control.state.pulse_us);
    assert(previous == control.state.output_angle_deg); /* same sample isn't integrated */
    (void)Control_Update(&control, &sample, 121);
    assert(control.state.fault == CONTROL_FAULT_IMU_STALE);
    sample = Sample(10, 20, 140);
    (void)Control_Update(&control, &sample, 140);
    assert(control.state.fault == CONTROL_FAULT_IMU_STALE);
    sample.roll_deg = 25;
    (void)Control_Update(&control, &sample, 140);
    assert(control.state.measured_deg == 25 && control.state.fault == CONTROL_FAULT_IMU_STALE);
    assert(Control_StartHold(&control, &sample, 140) == CONTROL_FAULT_LATCHED);
    Control_StopHold(&control);
    assert(control.state.fault == CONTROL_FAULT_NONE);
    assert(!control.state.hold_enabled);
    assert(Control_StartHold(&control, &sample, 140) == CONTROL_OK);
    sample.valid = 0;
    (void)Control_Update(&control, &sample, 160);
    assert(control.state.fault == CONTROL_FAULT_IMU_INVALID);
    Control_StopHold(&control);
    nan_value.bits = 0x7fc00000u;
    sample = Sample(nan_value.number, 20, 160);
    assert(Control_StartHold(&control, &sample, 160) == CONTROL_SENSOR_UNAVAILABLE);

    sample = Sample(30, 0, 180);
    assert(Control_SetManualAngle(&control, 170) == CONTROL_OK);
    assert(Control_StartHold(&control, &sample, 180) == CONTROL_OK);
    sample.timestamp_ms = 200;
    (void)Control_Update(&control, &sample, 200);
    assert(control.state.output_angle_deg == 170.0f); /* bumpless captured output */
    Control_StopHold(&control);

    Control_DefaultConfig(&config);
    config.axis = CONTROL_AXIS_PITCH;
    config.direction = -1;
    config.ki = 0;
    Control_Init(&control, &config, 0xfffffff0u);
    sample = Sample(10, 0, 0xfffffff0u);
    assert(Control_StartHold(&control, &sample, 0xfffffff0u) == CONTROL_OK);
    assert(control.state.target_deg == 0);
    assert(Control_SetTarget(&control, 10) == CONTROL_OK);
    sample.timestamp_ms = 4;
    (void)Control_Update(&control, &sample, 4);
    assert(control.state.output_angle_deg < 90); /* direction and time wrap */
    sample.timestamp_ms = 400;
    (void)Control_Update(&control, &sample, 400);
    assert(control.state.fault == CONTROL_FAULT_TIMING);

    /* Simple first-order plant checks meaningful bounded recovery after disturbance.
       This model is a host test only, not real-mechanism PID validation. */
    Control_DefaultConfig(&config);
    Control_Init(&control, &config, 0);
    sample = Sample(0, 0, 0);
    assert(Control_StartHold(&control, &sample, 0) == CONTROL_OK);
    assert(Control_SetTarget(&control, 15) == CONTROL_OK);
    previous = 90;
    for (now = 20; now <= 30000u; now += 20) {
        if (now == 16000u) { plant += 8.0f; }
        plant += 0.08f * ((control.state.output_angle_deg - 90.0f) - plant);
        sample = Sample(plant, 0, now);
        (void)Control_Update(&control, &sample, now);
        assert(control.state.pulse_us >= 500 && control.state.pulse_us <= 2500);
        assert(Abs(control.state.output_angle_deg - previous) <= 1.801f);
        assert(Abs(control.state.i_term) <= config.integral_limit_deg);
        assert(control.state.fault == CONTROL_FAULT_NONE);
        previous = control.state.output_angle_deg;
    }
    assert(Abs(control.state.error_deg) < 1.0f);
    Control_DefaultConfig(&config); config.mode = CONTROL_MODE_FOLLOW; config.deadband_deg = 1.0f;
    Control_Init(&control, &config, 0); sample = Sample(0, 0, 0);
    assert(Control_StartHold(&control, &sample, 0) == CONTROL_OK);
    for (now = 20; now <= 60000u; now += 20) {
        sample = Sample((now / 20u) % 2u ? 0.4f : -0.4f, 0, now);
        assert(Control_Update(&control, &sample, now) == 1500u);
        assert(control.state.i_term == 0 && control.state.d_term == 0);
    } /* Stationary noise must not accumulate motion over time. */
    for (; now <= 63000u; now += 20) {
        sample = Sample(20, 0, now); (void)Control_Update(&control, &sample, now);
    }
    assert(Abs(control.state.output_angle_deg - 110.0f) <= 1.0f);
    for (; now <= 66000u; now += 20) {
        sample = Sample(0, 0, now); (void)Control_Update(&control, &sample, now);
    }
    assert(Abs(control.state.output_angle_deg - 90.0f) <= 1.0f);
    for (; now <= 70000u; now += 20) {
        sample = Sample(130, 0, now); (void)Control_Update(&control, &sample, now);
    }
    assert(control.state.output_angle_deg == 180 && control.state.saturated);
    Control_StopHold(&control); assert(control.state.output_angle_deg == 180);

    config.follow_filter_seconds = 0; config.slew_deg_per_second = 1000;
    Control_Init(&control, &config, 0); sample = Sample(170, 0, 0);
    assert(Control_StartHold(&control, &sample, 0) == CONTROL_OK);
    sample = Sample(179, 0, 20); (void)Control_Update(&control, &sample, 20);
    sample = Sample(-179, 0, 40); (void)Control_Update(&control, &sample, 40);
    assert(control.state.output_angle_deg == 101); /* Crossing 180 continues same direction. */
    assert(Control_StartHold(&control, &sample, 40) == CONTROL_OK);
    assert(control.bias_angle_deg == 90 && control.follow_origin_deg == 170);
    (void)Control_Update(&control, &sample, 200);
    assert(control.state.fault == CONTROL_FAULT_IMU_STALE && control.state.output_angle_deg == 101);

    config.axis = CONTROL_AXIS_PITCH; config.direction = -1;
    Control_Init(&control, &config, 0); sample = Sample(100, 0, 0);
    assert(Control_StartHold(&control, &sample, 0) == CONTROL_OK);
    sample = Sample(120, 10, 20); (void)Control_Update(&control, &sample, 20);
    assert(control.state.output_angle_deg == 80); /* Selected axis and reverse mounting. */
    {
        const float targets[] = {0, 30, 90, 120, 180};
        unsigned int index;
        Control_DefaultConfig(&config); config.axis = CONTROL_AXIS_PITCH;
        config.direction = APP_HOLD_DIRECTION;
        config.target_min_deg = 0; config.target_limit_deg = 180;
        config.integral_limit_deg = 180; config.kp = APP_HOLD_KP; config.ki = APP_HOLD_KI;
        config.kd = APP_HOLD_KD; config.deadband_deg = APP_HOLD_DEADBAND_DEG;
        config.hold_output_span_deg = APP_HOLD_OUTPUT_SPAN_DEG;
        config.slew_deg_per_second = APP_HOLD_SLEW_DEG_PER_SECOND;
        for (index = 0; index < sizeof(targets)/sizeof(targets[0]); ++index) {
            float base = 0;
            plant = 90; previous = 90;
            Control_Init(&control, &config, 0); sample = Sample(25, plant, 0);
            assert(Control_StartHold(&control, &sample, 0) == CONTROL_OK);
            assert(Control_SetTarget(&control, -1) == CONTROL_BAD_ARGUMENT);
            assert(Control_SetTarget(&control, 181) == CONTROL_BAD_ARGUMENT);
            assert(Control_SetTarget(&control, targets[index]) == CONTROL_OK);
            for (now = 20; now <= 90000; now += 20) {
                if (now == 45000 && index > 0 && index < 4) base = 8; /* Tilting the servo base. */
                plant += 0.08f * (base + 180.0f - control.state.output_angle_deg - plant);
                sample = Sample(25, plant, now); (void)Control_Update(&control, &sample, now);
                assert(control.state.output_angle_deg >= 0 && control.state.output_angle_deg <= 180);
                assert(Abs(control.state.output_angle_deg - previous) <=
                    config.slew_deg_per_second * 0.02f + 0.001f);
                assert(control.state.fault == CONTROL_FAULT_NONE); previous = control.state.output_angle_deg;
            }
            assert(Abs(targets[index] - plant) < 1.0f);
        }
    }
    {
        uint32_t baseline_ms = RecoveryMs(APP_HOLD_KP, 0.25f, APP_HOLD_SLEW_DEG_PER_SECOND);
        uint32_t normal_ms = RecoveryMs(APP_HOLD_KP, APP_HOLD_KI, APP_HOLD_SLEW_DEG_PER_SECOND);
        assert(normal_ms < baseline_ms && normal_ms < 20000u);
        printf("Simulated 8deg base-step recovery (includes 1s dwell): Ki0.25=%lu ms, trialKi=%lu ms\n",
               (unsigned long)baseline_ms, (unsigned long)normal_ms);
    }
    Control_DefaultConfig(&config); config.axis = CONTROL_AXIS_PITCH;
    config.target_min_deg = 0; config.target_limit_deg = 180;
    config.kp = 0.5f; config.ki = 0; config.kd = 0;
    config.hold_output_span_deg = 15;
    Control_Init(&control, &config, 0); sample = Sample(0, 90, 0);
    assert(Control_StartHold(&control, &sample, 0) == CONTROL_OK);
    for (now = 20; now <= 60000; now += 20) {
        /* Wrong/no mechanical feedback must not drive commissioning to a rail. */
        sample = Sample(0, now < 30000 ? 270.0f : -90.0f, now);
        (void)Control_Update(&control, &sample, now);
        assert(control.state.output_angle_deg >= 75 && control.state.output_angle_deg <= 105);
        assert(control.state.i_term == 0 && control.state.d_term == 0);
        assert(control.state.saturated && control.state.fault == CONTROL_FAULT_NONE);
    }
    puts("PASS: PID0..180 target limits, simulated base disturbance recovery, filtered D and legacy core modes");
    return 0;
}
