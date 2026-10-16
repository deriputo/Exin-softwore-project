#include "control.h"
#include "app_config.h"
#include <assert.h>
#include <stdio.h>
#include <math.h>

static float Abs(float x) { return x < 0 ? -x : x; }
static float StepCorrection(void)
{
    float rate = APP_DIRECT_RATE_GAIN_SECONDS * 5.0f / 0.07f;
    float correction;
    if (rate > APP_DIRECT_RATE_LIMIT_DEG) rate = APP_DIRECT_RATE_LIMIT_DEG;
    correction = APP_DIRECT_POSITION_GAIN * 5.0f + rate;
    if (correction > 3.6f) correction = 3.6f;
    return correction;
}
static ControlSample Sample(float angle, uint32_t now)
{
    ControlSample s = {0, 0, 0, 1};
    s.pitch_deg = angle; s.timestamp_ms = now; return s;
}
static ControlConfig Config(ControlMode mode)
{
    ControlConfig c;
    Control_DefaultConfig(&c); c.mode = mode;
    c.axis = CONTROL_AXIS_PITCH; c.direction = APP_HOLD_DIRECTION;
    c.target_min_deg = 0; c.target_limit_deg = 180;
    c.kp = APP_HOLD_KP; c.ki = APP_HOLD_KI; c.kd = APP_HOLD_KD;
    c.integral_min_ki = APP_HOLD_INTEGRAL_MIN_KI;
    c.integral_error_scale_deg = APP_HOLD_INTEGRAL_ERROR_SCALE_DEG;
    c.integral_limit_deg = 180;
    c.deadband_deg = APP_HOLD_DEADBAND_DEG;
    c.slew_deg_per_second = APP_HOLD_SLEW_DEG_PER_SECOND;
    c.direct_position_gain = APP_DIRECT_POSITION_GAIN;
    c.actuator_time_constant_seconds = APP_ACTUATOR_TIME_CONSTANT_SECONDS;
    c.direct_rate_gain_seconds = APP_DIRECT_RATE_GAIN_SECONDS;
    c.direct_rate_limit_deg = APP_DIRECT_RATE_LIMIT_DEG;
    return c;
}

typedef struct { uint32_t entry_ms; float overshoot, tail_error; } Result;

/* Continuous +/-10deg base motion, including independently delayed sensing.
   True attitude, not delayed telemetry, is used to score tracking. */
static float Tracking(ControlConfig cfg, float tau, unsigned int delay, float period)
{
    Control c; ControlSample sample;
    float shaft = 90, measurements[64], sum = 0, previous = 90;
    unsigned int n, count = 0; uint32_t now;
    for (n = 0; n < 64; ++n) measurements[n] = 90;
    Control_Init(&c, &cfg, 0); sample = Sample(90, 0);
    assert(Control_StartHold(&c, &sample, 0) == CONTROL_OK);
    for (now = 20; now <= 20000; now += 20) {
        float base = now < 1000 ? 0 : 10 * sinf(6.2831853f * (now - 1000) / 1000 / period);
        float y; unsigned int index = (now / 20) % 64;
        shaft += .02f / (tau + .02f) * (c.state.output_angle_deg - shaft);
        y = 90 + base + APP_HOLD_DIRECTION * (shaft - 90);
        measurements[index] = y;
        sample = Sample(measurements[(index + 64 - delay) % 64], now);
        (void)Control_Update(&c, &sample, now);
        assert(c.state.fault == CONTROL_FAULT_NONE);
        assert(c.state.output_angle_deg >= 0 && c.state.output_angle_deg <= 180);
        assert(Abs(c.state.output_angle_deg - previous) <= 3.601f);
        previous = c.state.output_angle_deg;
        if (now >= 4000) { sum += (y - 90) * (y - 90); ++count; }
    }
    return sqrtf(sum / count);
}

/* Independent physical actuator and delayed sensor; estimates never read this
   model's shaft position. Includes mismatch in lag, delay and mechanical gain. */
static Result Scenario(ControlMode mode, float base_step, float tau, float gain,
                       unsigned int command_delay, unsigned int sensor_delay)
{
    Control c; ControlConfig cfg = Config(mode); ControlSample s;
    float shaft = 90, output_before = 90, commands[64], measurements[64];
    float y = 90, overshoot = 0;
    uint32_t now, last_out = 1000;
    unsigned int index;
    Result result;
    for (index = 0; index < 64; ++index) commands[index] = measurements[index] = 90;
    Control_Init(&c, &cfg, 0); s = Sample(90, 0);
    assert(Control_StartHold(&c, &s, 0) == CONTROL_OK);
    for (now = 20; now <= 20000; now += 20) {
        float base = now >= 1000 ? base_step : 0;
        float cross;
        index = (now / 20) % 64;
        commands[index] = c.state.output_angle_deg;
        shaft += 0.02f / (tau + 0.02f) *
            (commands[(index + 64 - command_delay) % 64] - shaft);
        y = 90 + base + APP_HOLD_DIRECTION * gain * (shaft - 90);
        measurements[index] = y;
        s = Sample(measurements[(index + 64 - sensor_delay) % 64], now);
        (void)Control_Update(&c, &s, now);
        assert(c.state.fault == CONTROL_FAULT_NONE);
        assert(c.state.output_angle_deg >= 0 && c.state.output_angle_deg <= 180);
        assert(Abs(c.state.output_angle_deg - output_before) <= 3.601f);
        assert(c.estimated_actuator_deg >= 0 && c.estimated_actuator_deg <= 180);
        if (mode == CONTROL_MODE_DIRECT) {
            assert(c.state.i_term == 0);
            assert(Abs(c.state.d_term) <= cfg.direct_rate_limit_deg);
            if (Abs(c.state.error_deg) <= cfg.deadband_deg) assert(c.state.d_term == 0);
        }
        output_before = c.state.output_angle_deg;
        if (now >= 1000) {
            cross = base_step > 0 ? 90 - y : y - 90;
            if (cross > overshoot) overshoot = cross;
            if (Abs(y - 90) > 1) last_out = now;
        }
    }
    result.entry_ms = last_out <= 19000 ? last_out + 20 - 1000 : 20000;
    result.overshoot = overshoot; result.tail_error = y - 90;
    return result;
}

int main(void)
{
    Control c; ControlConfig cfg = Config(CONTROL_MODE_DIRECT);
    ControlSample s;
    uint32_t now;
    uint16_t retained;
    float estimate;
    unsigned int i;
    Control_Init(&c, &cfg, 0);
    assert(c.config.mode == CONTROL_MODE_DIRECT);
    assert(Control_SetManualAngle(&c, 130) == CONTROL_OK);
    s = Sample(90, 0); assert(Control_StartHold(&c, &s, 0) == CONTROL_OK);
    retained = c.state.pulse_us;
    s = Sample(90, 20); assert(Control_Update(&c, &s, 20) == retained);
    s = Sample(85, 40); (void)Control_Update(&c, &s, 40);
    assert(Abs(c.state.output_angle_deg - (130 - StepCorrection())) < 0.001f);
    assert(c.state.i_term == 0 && c.state.d_term > 0);
    assert(Abs(c.state.d_term - APP_DIRECT_RATE_GAIN_SECONDS * 5 / .07f) < .001f);
    assert(Control_StartHold(&c, &s, 40) == CONTROL_OK);
    assert(c.state.target_deg == 90 && c.bias_angle_deg == 130);
    estimate = c.estimated_actuator_deg;
    (void)Control_Update(&c, &s, 60);
    assert(c.estimated_actuator_deg == estimate); /* repeated sample is not predicted twice */
    retained = c.state.pulse_us; s.valid = 0;
    assert(Control_Update(&c, &s, 61) == retained);
    assert(c.state.fault == CONTROL_FAULT_IMU_INVALID && c.estimated_actuator_deg == estimate);
    s = Sample(95, 80); assert(Control_Update(&c, &s, 80) == retained);
    assert(c.state.fault == CONTROL_FAULT_IMU_INVALID && c.state.measured_deg == 95);
    Control_StopHold(&c); assert(Control_StartHold(&c, &s, 80) == CONTROL_OK);
    assert(c.state.target_deg == 95 && c.estimated_actuator_deg == c.state.output_angle_deg);
    assert(Control_SetTarget(&c, -1) == CONTROL_BAD_ARGUMENT);
    assert(Control_SetTarget(&c, 181) == CONTROL_BAD_ARGUMENT);

    /* Reaching the deadband must retain the last command, not retreat toward
       the model position which still lags behind that command. */
    Control_Init(&c, &cfg, 0); s = Sample(90, 0);
    assert(Control_StartHold(&c, &s, 0) == CONTROL_OK);
    assert(Control_SetTarget(&c, 100) == CONTROL_OK);
    s = Sample(90, 20); (void)Control_Update(&c, &s, 20); retained = c.state.pulse_us;
    assert(c.state.d_term == 0); /* target change does not create a rate kick */
    assert(c.estimated_actuator_deg > c.state.output_angle_deg);
    for (now = 40; now <= 60000; now += 20) {
        s = Sample(100 + ((now / 20) % 2 ? 0.4f : -0.4f), now);
        assert(Control_Update(&c, &s, now) == retained);
        assert(c.state.p_term == 0 && c.state.i_term == 0 && c.state.d_term == 0);
    }
    s = Sample(100, now + 201); (void)Control_Update(&c, &s, now + 201);
    assert(c.state.fault == CONTROL_FAULT_TIMING);

    Control_Init(&c, &cfg, 0); s = Sample(90, 0);
    assert(Control_StartHold(&c, &s, 0) == CONTROL_OK);
    (void)Control_Update(&c, &s, 101);
    assert(c.state.fault == CONTROL_FAULT_IMU_STALE);
    Control_Init(&c, &cfg, 0); s = Sample(181, 0);
    assert(Control_StartHold(&c, &s, 0) == CONTROL_BAD_ARGUMENT);

    /* Sustained unattainable feedback stays bounded; reversal can leave a rail
       immediately, without winding up an integral contribution. */
    Control_Init(&c, &cfg, 0); s = Sample(90, 0);
    assert(Control_StartHold(&c, &s, 0) == CONTROL_OK);
    assert(Control_SetTarget(&c, 180) == CONTROL_OK);
    for (now = 20; now <= 10000; now += 20) {
        s = Sample(0, now); (void)Control_Update(&c, &s, now);
        assert(c.state.output_angle_deg >= 0 && c.state.output_angle_deg <= 180);
        assert(c.state.i_term == 0);
    }
    assert(c.state.output_angle_deg == 0 && c.state.saturated);
    assert(Control_SetTarget(&c, 0) == CONTROL_OK);
    s = Sample(180, now); (void)Control_Update(&c, &s, now);
    assert(c.state.output_angle_deg > 0 && c.state.output_angle_deg <= 3.601f);

    /* Rollover and selected-axis/direction behavior. */
    cfg.axis = CONTROL_AXIS_ROLL; cfg.direction = 1;
    Control_Init(&c, &cfg, 0xfffffff0u); s = Sample(160, 0xfffffff0u); s.roll_deg = 90;
    assert(Control_StartHold(&c, &s, 0xfffffff0u) == CONTROL_OK);
    s.roll_deg = 85; s.timestamp_ms = 4;
    (void)Control_Update(&c, &s, 4);
    assert(Abs(c.state.output_angle_deg - (90 + StepCorrection())) < 0.001f);
    s.roll_deg = 0; s.timestamp_ms = 24;
    (void)Control_Update(&c, &s, 24);
    assert(c.state.d_term == APP_DIRECT_RATE_LIMIT_DEG); /* Large rate is capped. */

    /* Compare identical reference plants against the preserved PI profile. */
    {
        const float targets[] = {0, 30, 90, 120, 180};
        for (i = 0; i < sizeof(targets)/sizeof(targets[0]); ++i) {
            float shaft = 90, y = 90, previous = 90;
            cfg = Config(CONTROL_MODE_DIRECT); Control_Init(&c, &cfg, 0);
            s = Sample(90, 0); assert(Control_StartHold(&c, &s, 0) == CONTROL_OK);
            assert(Control_SetTarget(&c, targets[i]) == CONTROL_OK);
            for (now = 20; now <= 20000; now += 20) {
                shaft += 0.02f / 0.26f * (c.state.output_angle_deg - shaft);
                y = 180 - shaft; s = Sample(y, now);
                (void)Control_Update(&c, &s, now);
                assert(c.state.fault == CONTROL_FAULT_NONE);
                assert(c.state.output_angle_deg >= 0 && c.state.output_angle_deg <= 180);
                assert(Abs(c.state.output_angle_deg - previous) <= 3.601f);
                previous = c.state.output_angle_deg;
            }
            assert(Abs(y - targets[i]) <= 1);
        }
    }
    {
        const float steps[] = {-30, -15, -5, 5, 15, 30};
        for (i = 0; i < sizeof(steps)/sizeof(steps[0]); ++i) {
            Result pi = Scenario(CONTROL_MODE_PID, steps[i], 0.24f, 1, 0, 0);
            Result direct = Scenario(CONTROL_MODE_DIRECT, steps[i], 0.24f, 1, 0, 0);
            assert(direct.entry_ms < pi.entry_ms && Abs(direct.tail_error) <= 1);
            printf("Reference %.0fdeg: PI entry=%lu ms; DIRECT entry=%lu ms; overshoot=%.2fdeg\n",
                (double)steps[i], (unsigned long)pi.entry_ms, (unsigned long)direct.entry_ms,
                (double)direct.overshoot);
        }
    }
    {
        const float taus[] = {0.06f, 0.12f, 0.24f, 0.4f};
        const float gains[] = {0.8f, 1, 1.2f};
        const float steps[] = {-30, -5, 5, 30};
        unsigned int a, b, d, e, f, count = 0;
        float worst_overshoot = 0; uint32_t slowest = 0;
        for (a = 0; a < 4; ++a) for (b = 0; b < 3; ++b)
        for (d = 0; d <= 3; d += 3) for (e = 0; e <= 6; e += 3)
        for (f = 0; f < 4; ++f) {
            Result r = Scenario(CONTROL_MODE_DIRECT, steps[f], taus[a], gains[b], d, e);
            if (r.overshoot > worst_overshoot) worst_overshoot = r.overshoot;
            if (r.entry_ms > slowest) slowest = r.entry_ms;
            assert(r.entry_ms < 10000 && Abs(r.tail_error) <= 1);
            ++count;
        }
        printf("DIRECT mismatch %u cases: worst entry=%lu ms; worst overshoot=%.2fdeg\n",
            count, (unsigned long)slowest, (double)worst_overshoot);
        assert(worst_overshoot <= 2.0f); /* User-approved transient overshoot ceiling. */
    }
    {
        const float taus[] = {.06f, .12f, .24f};
        const float periods[] = {2, 4};
        unsigned int a, b, delay;
        ControlConfig old = Config(CONTROL_MODE_DIRECT);
        old.direct_position_gain = .30f;
        old.actuator_time_constant_seconds = .12f;
        old.direct_rate_gain_seconds = .06f;
        old.direct_rate_limit_deg = 3;
        cfg = Config(CONTROL_MODE_DIRECT);
        for (a = 0; a < 3; ++a) for (b = 0; b < 2; ++b)
        for (delay = 0; delay <= 3; delay += 3) {
            float before = Tracking(old, taus[a], delay, periods[b]);
            float after = Tracking(cfg, taus[a], delay, periods[b]);
            printf("Continuous tau=%.2f delay=%u period=%.0f: RMS old=%.3f new=%.3f deg\n",
                (double)taus[a], delay * 20, (double)periods[b], (double)before, (double)after);
            assert(after < before);
        }
    }
    puts("PASS: DIRECT model, deadband retention, repeated samples, noise, rails, reversal, faults, axis and rollover");
    return 0;
}
