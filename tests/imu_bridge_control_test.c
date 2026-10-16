#include "imu_core.h"
#include "control.h"
#include "app_config.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void Word(uint8_t *p, int16_t n)
{
    uint16_t u = (uint16_t)n; p[0] = (uint8_t)(u >> 8); p[1] = (uint8_t)u;
}
static ControlSample Input(const IMUSample *imu)
{
    ControlSample s = {0, 0, 0, 0, 0, 0};
    s.roll_deg = imu->roll_deg; s.pitch_deg = APP_ATTITUDE_REFERENCE_DEG + imu->pitch_deg;
    s.timestamp_ms = imu->timestamp_ms; s.valid = imu->valid && imu->calibrated;
#if IMU_MOUNT_ROTATE_X_90
    s.angular_rate_dps = imu->gyro_z_dps;
#else
    s.angular_rate_dps = imu->gyro_y_dps;
#endif
    s.angular_rate_valid = 1;
    return s;
}
static void Gravity(uint8_t raw[14])
{
    memset(raw, 0, 14);
#if IMU_MOUNT_ROTATE_X_90
    Word(raw + 2, -16384);
#else
    Word(raw + 4, 16384);
#endif
}
int main(void)
{
    IMUCore core; IMUSample imu; Control c; ControlConfig cfg;
    ControlSample s; uint8_t raw[14]; uint32_t now = 0; unsigned int i; uint16_t held;
    IMUCore_Init(&core); Gravity(raw);
    for (i = 0; i < IMU_CALIBRATION_SAMPLES; ++i)
        IMUCore_Update(&core, raw, now += 10, &imu);
    assert(imu.valid && imu.calibrated);
    Control_DefaultConfig(&cfg); cfg.mode = CONTROL_MODE_PID; cfg.axis = APP_HOLD_AXIS;
    cfg.direction = APP_HOLD_DIRECTION; cfg.target_min_deg = 0; cfg.target_limit_deg = 180;
    cfg.kp = APP_POSITION_PID_KP; cfg.ki = APP_POSITION_PID_KI; cfg.kd = APP_POSITION_PID_KD;
    cfg.deadband_deg = APP_HOLD_DEADBAND_DEG; cfg.integral_limit_deg = 180;
    cfg.deadband_transition_deg = APP_HOLD_DEADBAND_TRANSITION_DEG;
    cfg.slew_deg_per_second = APP_HOLD_SLEW_DEG_PER_SECOND;
#if APP_RATE_PID_TEST
    cfg.ki = APP_RATE_PID_KI; cfg.kd = APP_RATE_PID_KD; cfg.use_rate_sample = 1;
    cfg.rate_filter_seconds = APP_RATE_FILTER_SECONDS;
    cfg.rate_term_limit_deg = APP_RATE_TERM_LIMIT_DEG; cfg.rate_quiet_dps = APP_RATE_QUIET_DPS;
#endif
    Control_Init(&c, &cfg, now); s = Input(&imu);
    assert(Control_StartHold(&c, &s, now) == CONTROL_OK);
    Word(raw, -1360);
#if IMU_MOUNT_ROTATE_X_90
    Word(raw + 2, -2834); Word(raw + 4, 82); Word(raw + 12, 3280);
#else
    Word(raw + 2, 82); Word(raw + 4, 2834); Word(raw + 10, 3280);
#endif
    for (i = 0; i < IMU_GYRO_BRIDGE_MS / 10u; ++i) {
        IMUCore_Update(&core, raw, now += 10, &imu); s = Input(&imu);
        (void)Control_Update(&c, &s, now);
        assert(imu.valid && imu.gyro_only && c.state.fault == CONTROL_FAULT_NONE);
    }
    assert(c.state.output_angle_deg > 90); /* Selected gyro axis actually drives correction. */
    IMUCore_Update(&core, raw, now += 10, &imu); s = Input(&imu);
    held = Control_Update(&c, &s, now);
    assert(!imu.valid && c.state.fault == CONTROL_FAULT_IMU_INVALID);
    Gravity(raw); IMUCore_Update(&core, raw, now += 10, &imu); s = Input(&imu);
    assert(imu.valid && imu.accel_trusted && !imu.gyro_only);
    assert(Control_Update(&c, &s, now) == held && c.state.fault == CONTROL_FAULT_IMU_INVALID);
    assert(Control_StartHold(&c, &s, now) == CONTROL_FAULT_LATCHED);
    Control_StopHold(&c); assert(Control_StartHold(&c, &s, now) == CONTROL_OK);
    /* Recovery at110ms must continue HOLD without resetting/rearming it. */
#if IMU_MOUNT_ROTATE_X_90
    Word(raw + 2, -21299);
#else
    Word(raw + 4, 21299);
#endif
    for (i = 0; i < 11; ++i) {
        IMUCore_Update(&core, raw, now += 10, &imu); s = Input(&imu);
        (void)Control_Update(&c, &s, now);
        assert(imu.valid && imu.gyro_only && c.state.fault == CONTROL_FAULT_NONE);
    }
    Gravity(raw); IMUCore_Update(&core, raw, now += 10, &imu); s = Input(&imu);
    (void)Control_Update(&c, &s, now);
    assert(imu.valid && imu.accel_trusted && c.state.hold_enabled && c.state.fault == CONTROL_FAULT_NONE);
    held = c.state.pulse_us;
    memset(raw, 0, 14); IMUCore_Update(&core, raw, now += 10, &imu); s = Input(&imu);
    assert(Control_Update(&c, &s, now) == held && c.state.fault == CONTROL_FAULT_IMU_INVALID);
    printf("PASS: real IMU fusion -> HOLD continuity <=%ums, timeout latch, no auto rearm, hard zero rejection\n", (unsigned int)IMU_GYRO_BRIDGE_MS);
    return 0;
}
