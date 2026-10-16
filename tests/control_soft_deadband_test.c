#include "control.h"
#include "app_config.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static ControlConfig Config(float width)
{
    ControlConfig c; Control_DefaultConfig(&c);
    c.axis = APP_HOLD_AXIS; c.direction = APP_HOLD_DIRECTION;
    c.target_min_deg = 0; c.target_limit_deg = 180; c.integral_limit_deg = 180;
    c.kp = APP_POSITION_PID_KP; c.ki = APP_RATE_PID_KI; c.kd = APP_RATE_PID_KD;
    c.deadband_deg = APP_HOLD_DEADBAND_DEG; c.deadband_transition_deg = width;
    c.use_rate_sample = 1; c.rate_filter_seconds = APP_RATE_FILTER_SECONDS;
    c.rate_term_limit_deg = APP_RATE_TERM_LIMIT_DEG; c.rate_quiet_dps = APP_RATE_QUIET_DPS;
    c.slew_deg_per_second = APP_HOLD_SLEW_DEG_PER_SECOND; return c;
}
static void Start(Control *c, const ControlConfig *cfg)
{
    ControlSample s = {0, 90, 0, 1, 0, 1};
    Control_Init(c, cfg, 0); assert(c->config.deadband_transition_deg == cfg->deadband_transition_deg);
    assert(Control_StartHold(c, &s, 0) == CONTROL_OK);
}
int main(void)
{
    ControlConfig soft = Config(APP_HOLD_DEADBAND_TRANSITION_DEG), hard = Config(0);
    Control a, b; ControlSample s = {0, 90, 0, 1, 0, 1};
    unsigned int i, old_changes = 0, new_changes = 0;
    uint16_t old_pulse = 1500, new_pulse = 1500, held;
    /* Prescribed measurement perturbation at the deadband boundary. This is
       a unit input, not a physical closed-loop performance experiment. */
    Start(&a, &soft); Start(&b, &hard);
    for (i = 1; i <= 50; ++i) {
        s.pitch_deg = i % 2 ? 90.51f : 90.49f; s.timestamp_ms = i * 20;
        (void)Control_Update(&a, &s, s.timestamp_ms); (void)Control_Update(&b, &s, s.timestamp_ms);
        if (a.state.pulse_us != new_pulse) ++new_changes;
        if (b.state.pulse_us != old_pulse) ++old_changes;
        new_pulse = a.state.pulse_us; old_pulse = b.state.pulse_us;
        assert(fabsf(a.state.output_angle_deg - 90) < .02f);
    }
    assert(new_changes == 0 && old_changes >= 40);
    printf("Prescribed 0.49/0.51deg boundary perturbation, 1sec: PWM changes hard=%u soft=%u\n", old_changes, new_changes);
    /* Both signs, zero effect inside and full legacy strength beyond the band. */
    for (i = 0; i < 2; ++i) {
        float sign = i ? -1.0f : 1.0f;
        Start(&a, &soft); Start(&b, &hard);
        s.pitch_deg = 90 + sign * .5f; s.timestamp_ms = 20;
        (void)Control_Update(&a, &s, 20); assert(a.state.p_term == 0 && a.state.i_term == 0);
        s.pitch_deg = 90 + sign * .5001f; s.timestamp_ms = 40;
        (void)Control_Update(&a, &s, 40); assert(fabsf(a.state.p_term) < .00001f);
        s.pitch_deg = 90 + sign * .65f; s.timestamp_ms = 60;
        (void)Control_Update(&a, &s, 60);
        assert(fabsf(a.state.p_term + sign * .2275f) < .0001f);
        Start(&a, &soft); Start(&b, &hard);
        s.pitch_deg = 90 + sign * 2;
        for (s.timestamp_ms = 20; s.timestamp_ms <= 400; s.timestamp_ms += 20) {
            (void)Control_Update(&a, &s, s.timestamp_ms); (void)Control_Update(&b, &s, s.timestamp_ms);
            assert(a.state.pulse_us == b.state.pulse_us && a.state.i_term == b.state.i_term);
        }
    }
    /* Do not smooth away gyro braking: only P/I error is shaped. */
    Start(&a, &soft); s.pitch_deg = 90; s.timestamp_ms = 20; s.angular_rate_dps = 100;
    (void)Control_Update(&a, &s, 20); assert(a.state.d_term == -.5f);
    held = a.state.pulse_us; s.valid = 0; s.timestamp_ms = 40;
    assert(Control_Update(&a, &s, 40) == held && a.state.fault == CONTROL_FAULT_IMU_INVALID);
    s.valid = 1; s.timestamp_ms = 60;
    assert(Control_Update(&a, &s, 60) == held && a.state.fault == CONTROL_FAULT_IMU_INVALID);
    soft.deadband_transition_deg = NAN; Control_Init(&a, &soft, 0);
    assert(a.config.deadband_transition_deg == 0);
    soft.deadband_transition_deg = -1; Control_Init(&a, &soft, 0);
    assert(a.config.deadband_transition_deg == 0);
    soft.deadband_transition_deg = 6; Control_Init(&a, &soft, 0);
    assert(a.config.deadband_transition_deg == 0);
    puts("PASS: soft PI boundary, signed symmetry, full large-error response, unchanged braking and fault latch");
    return 0;
}
