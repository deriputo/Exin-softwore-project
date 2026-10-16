#include "imu_core.h"
#include "control.h"
#include "app_config.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void Word(uint8_t *p, int16_t n)
{
    uint16_t u = (uint16_t)n; p[0] = (uint8_t)(u >> 8); p[1] = (uint8_t)u;
}
static void Frame(uint8_t raw[14], float angle, float rate, float noise)
{
    float radians = (angle + noise) * .01745329252f;
    memset(raw, 0, 14);
    Word(raw, (int16_t)lroundf(-sinf(radians) * 16384));
#if IMU_MOUNT_ROTATE_X_90
    Word(raw + 2, (int16_t)lroundf(-cosf(radians) * 16384));
    Word(raw + 12, (int16_t)lroundf(rate * 65.5f));
#else
    Word(raw + 4, (int16_t)lroundf(cosf(radians) * 16384));
    Word(raw + 10, (int16_t)lroundf(rate * 65.5f));
#endif
}
static ControlSample Input(const IMUSample *imu)
{
    ControlSample s = {0, 0, 0, 0, 0, 0};
    s.pitch_deg = APP_ATTITUDE_REFERENCE_DEG + imu->pitch_deg;
    s.timestamp_ms = imu->timestamp_ms; s.valid = imu->valid && imu->calibrated;
#if IMU_MOUNT_ROTATE_X_90
    s.angular_rate_dps = imu->gyro_z_dps;
#else
    s.angular_rate_dps = imu->gyro_y_dps;
#endif
    s.angular_rate_valid = 1; return s;
}
int main(void)
{
    const float taus[] = {.01f, .02f, .04f, .08f};
    unsigned int k, i; float worst = 0;
    for (k = 0; k < 4; ++k) {
        IMUCore imu; IMUSample measured; Control c; ControlConfig cfg;
        ControlSample s; uint8_t raw[14]; uint32_t now = 0;
        float shaft = 90, command = 90, previous_angle = 0, low = 180, high = 0;
        IMUCore_Init(&imu); Frame(raw, 0, 0, 0);
        for (i = 0; i < IMU_CALIBRATION_SAMPLES; ++i)
            IMUCore_Update(&imu, raw, now += 10, &measured);
        Control_DefaultConfig(&cfg); cfg.axis = APP_HOLD_AXIS; cfg.direction = APP_HOLD_DIRECTION;
        cfg.target_min_deg = 0; cfg.target_limit_deg = 180; cfg.integral_limit_deg = 180;
        cfg.kp = APP_POSITION_PID_KP; cfg.ki = APP_RATE_PID_KI; cfg.kd = APP_RATE_PID_KD;
        cfg.use_rate_sample = 1; cfg.rate_filter_seconds = APP_RATE_FILTER_SECONDS;
        cfg.rate_term_limit_deg = APP_RATE_TERM_LIMIT_DEG; cfg.rate_quiet_dps = APP_RATE_QUIET_DPS;
        cfg.deadband_deg = APP_HOLD_DEADBAND_DEG; cfg.slew_deg_per_second = APP_HOLD_SLEW_DEG_PER_SECOND;
        Control_Init(&c, &cfg, now); s = Input(&measured);
        assert(Control_StartHold(&c, &s, now) == CONTROL_OK);
        imu.tilt_pitch = .7f; /* Estimation residual, physical mechanism is level. */
        for (i = 1; i <= 6000; ++i) {
            float angle, rate, noise = i % 2 ? .03f : -.03f;
            if (i % 2 == 0) command = c.state.output_angle_deg; /* 50Hz PWM hold. */
            shaft += .01f / (taus[k] + .01f) * (command - shaft);
            angle = 90 - shaft; rate = (angle - previous_angle) / .01f;
            previous_angle = angle; Frame(raw, angle, rate, noise);
            IMUCore_Update(&imu, raw, now += 10, &measured); s = Input(&measured);
            (void)Control_Update(&c, &s, now);
            assert(measured.valid && c.state.fault == CONTROL_FAULT_NONE);
            assert(fabsf(angle) < 2);
            if (fabsf(angle) > worst) worst = fabsf(angle);
            if (i > 5500) {
                if (c.state.output_angle_deg < low) low = c.state.output_angle_deg;
                if (c.state.output_angle_deg > high) high = c.state.output_angle_deg;
            }
        }
        assert(fabsf(previous_angle) < .5f && high - low < .2f);
    }
    printf("PASS: real IMU -> active RATE controller, four synthetic actuator lags, 50Hz PWM hold, 60s noise; max induced motion %.3fdeg\n", (double)worst);
    return 0;
}
