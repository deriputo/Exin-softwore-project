#include "control.h"
#include "app_config.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static float Abs(float x) { return x < 0 ? -x : x; }
static ControlSample Sample(float y, uint32_t now)
{
    ControlSample s = {0, 0, 0, 1, 0, 0}; s.pitch_deg = y; s.timestamp_ms = now; return s;
}
static ControlConfig Config(int direct)
{
    ControlConfig c; Control_DefaultConfig(&c);
    c.mode = direct ? CONTROL_MODE_DIRECT : CONTROL_MODE_PID;
    c.axis = APP_HOLD_AXIS; c.direction = APP_HOLD_DIRECTION;
    c.target_min_deg = 0; c.target_limit_deg = 180; c.integral_limit_deg = 180;
    c.kp = APP_POSITION_PID_KP; c.ki = APP_POSITION_PID_KI; c.kd = APP_POSITION_PID_KD;
    c.integral_min_ki = c.integral_error_scale_deg = 0;
    c.deadband_deg = APP_HOLD_DEADBAND_DEG;
    c.slew_deg_per_second = APP_HOLD_SLEW_DEG_PER_SECOND;
    c.direct_position_gain = APP_DIRECT_POSITION_GAIN;
    c.actuator_time_constant_seconds = APP_ACTUATOR_TIME_CONSTANT_SECONDS;
    c.direct_rate_gain_seconds = APP_DIRECT_RATE_GAIN_SECONDS;
    c.direct_rate_limit_deg = APP_DIRECT_RATE_LIMIT_DEG;
    return c;
}
typedef struct { float overshoot, tail, rms; uint32_t entry; } Result;
static Result Run(int direct, float step, float tau, float gain, unsigned int cd,
                  unsigned int sd, int continuous)
{
    Control c; ControlConfig cfg = Config(direct); ControlSample s;
    float commands[64], values[64], shaft = 90, y = 90, previous = 90, sum = 0;
    uint32_t now, last_out = 1000; unsigned int i, count = 0;
    Result r = {0, 0, 0, 0};
    for (i = 0; i < 64; ++i) commands[i] = values[i] = 90;
    Control_Init(&c, &cfg, 0); s = Sample(90, 0);
    assert(c.config.mode == cfg.mode && c.config.ki == cfg.ki);
    assert(Control_StartHold(&c, &s, 0) == CONTROL_OK);
    for (now = 20; now <= 20000; now += 20) {
        float base = now < 1000 ? 0 : (continuous ?
            10 * sinf(6.2831853f * (now - 1000) / 4000) : step);
        float crossing;
        i = (now / 20) % 64; commands[i] = c.state.output_angle_deg;
        shaft += .02f / (tau + .02f) * (commands[(i + 64 - cd) % 64] - shaft);
        y = 90 + base + APP_HOLD_DIRECTION * gain * (shaft - 90);
        values[i] = y; s = Sample(values[(i + 64 - sd) % 64], now);
        (void)Control_Update(&c, &s, now);
        assert(c.state.fault == CONTROL_FAULT_NONE);
        assert(c.state.output_angle_deg >= 0 && c.state.output_angle_deg <= 180);
        assert(Abs(c.state.output_angle_deg - previous) <= 3.601f);
        previous = c.state.output_angle_deg;
        if (now >= 1000) {
            crossing = step > 0 ? 90 - y : y - 90;
            if (crossing > r.overshoot) r.overshoot = crossing;
            if (Abs(y - 90) > 1) last_out = now;
        }
        if (now >= 4000) { sum += (y - 90) * (y - 90); ++count; }
    }
    r.tail = y - 90; r.rms = sqrtf(sum / count);
    r.entry = last_out <= 19000 ? last_out + 20 - 1000 : 20000;
    return r;
}
int main(void)
{
    const float taus[] = {.01f, .02f, .04f, .08f};
    const float gains[] = {1, 1.1f, 1.2f}, steps[] = {-30, -5, 5, 30};
    unsigned int a, b, cd, sd, f, count = 0;
    float worst = 0; uint32_t slow = 0, now; uint16_t held;
    Control c; ControlConfig cfg = Config(0); ControlSample s;
    for (a=0;a<4;++a) for (b=0;b<3;++b) for (cd=0;cd<=1;++cd)
    for (sd=0;sd<=2;++sd) for (f=0;f<4;++f) {
        Result r = Run(0, steps[f], taus[a], gains[b], cd, sd, 0);
        assert(r.overshoot <= 2 && r.entry < 10000 && Abs(r.tail) <= 1);
        if (r.overshoot > worst) worst = r.overshoot;
        if (r.entry > slow) slow = r.entry;
        ++count;
    }
    printf("PIDR fast hypothesis %u cases: worst overshoot=%.3fdeg entry=%lu ms\n",
        count, (double)worst, (unsigned long)slow);
    /* Reproduce the CSV plateau: e=47.7, I=-25.7. Both physical directions
       must use the available slew budget, reach a rail without windup, and
       leave that rail after error reversal. */
    for (a = 0; a < 2; ++a) {
        float previous, rail_i;
        cfg = Config(0); cfg.direction = a ? 1 : -1;
        Control_Init(&c, &cfg, 0); s = Sample(90, 0);
        assert(Control_StartHold(&c, &s, 0) == CONTROL_OK);
        c.state.i_term = -25.7f;
        c.state.output_angle_deg = 90 + cfg.direction * (.7f * 47.7f - 25.7f);
        c.previous_measured = 42.3f;
        previous = c.state.output_angle_deg;
        s = Sample(42.3f, 20); (void)Control_Update(&c, &s, 20);
        assert(Abs(c.state.i_term - (-25.7f + 3.6f)) < .001f);
        assert(Abs(c.state.output_angle_deg - (previous + cfg.direction * 3.6f)) < .001f);
        assert(c.state.saturated);
        for (now = 40; now <= 2000; now += 20) {
            previous = c.state.output_angle_deg;
            s = Sample(42.3f, now); (void)Control_Update(&c, &s, now);
            assert(Abs(c.state.output_angle_deg - previous) <= 3.601f);
        }
        assert(c.state.output_angle_deg == (a ? 180 : 0));
        rail_i = c.state.i_term;
        for (; now <= 3000; now += 20) {
            s = Sample(42.3f, now); (void)Control_Update(&c, &s, now);
            assert(c.state.i_term == rail_i && c.state.saturated);
        }
        s = Sample(137.7f, now); (void)Control_Update(&c, &s, now);
        assert(a ? c.state.output_angle_deg < 180 : c.state.output_angle_deg > 0);
    }
    cfg = Config(0);
    for (a = 0; a < 2; ++a) {
        Result large = Run(0, a ? 50 : -50, .04f, 1.1f, 0, 0, 0);
        assert(large.entry < 3000 && Abs(large.tail) <= 1 && large.overshoot <= 2);
        printf("Large %.0fdeg reference: entry=%lu ms overshoot=%.3fdeg\n",
            a ? 50.0 : -50.0, (unsigned long)large.entry, (double)large.overshoot);
    }
    {
        Result old = Run(1, 15, .24f, 1, 0, 0, 0);
        Result next = Run(0, 15, .24f, 1, 0, 0, 0);
        Result old_track = Run(1, 0, .12f, 1, 0, 0, 1);
        Result next_track = Run(0, 0, .12f, 1, 0, 0, 1);
        assert(next.entry < old.entry && next_track.rms < old_track.rms);
        printf("Reference entry old=%lu new=%lu ms; tracking RMS old=%.3f new=%.3fdeg\n",
            (unsigned long)old.entry, (unsigned long)next.entry,
            (double)old_track.rms, (double)next_track.rms);
    }
    Control_Init(&c, &cfg, 0); s = Sample(90, 0);
    assert(Control_StartHold(&c, &s, 0) == CONTROL_OK);
    for (now = 20; now <= 60000; now += 20) {
        s = Sample(90 + ((now / 20) % 2 ? .4f : -.4f), now);
        (void)Control_Update(&c, &s, now);
        assert(c.state.i_term == 0 && c.state.p_term == 0);
        assert(c.state.pulse_us >= 1499 && c.state.pulse_us <= 1501);
    }
    held = c.state.pulse_us; s.valid = 0;
    assert(Control_Update(&c, &s, now) == held);
    assert(c.state.fault == CONTROL_FAULT_IMU_INVALID);
    s = Sample(80, now + 20);
    assert(Control_Update(&c, &s, now + 20) == held);
    assert(c.state.fault == CONTROL_FAULT_IMU_INVALID);
    puts("PASS: PIDR fast-envelope hypothesis, comparison, static noise and fault retention; hardware unvalidated");
    return 0;
}
