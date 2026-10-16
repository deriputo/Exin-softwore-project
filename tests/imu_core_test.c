#include "imu_core.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static void Word(uint8_t *out, int16_t value)
{
    uint16_t bits = (uint16_t)value;
    out[0] = (uint8_t)(bits >> 8); out[1] = (uint8_t)bits;
}
static void NegateWord(uint8_t *out, const uint8_t *in)
{
    uint16_t negative = (uint16_t)(0u - (uint16_t)(((uint16_t)in[0] << 8) | in[1]));
    out[0] = (uint8_t)(negative >> 8); out[1] = (uint8_t)negative;
}
static void Update(IMUCore *core, const uint8_t raw[14], uint32_t now, IMUSample *sample)
{
    uint8_t wire[14]; memcpy(wire, raw, sizeof(wire));
#if IMU_MOUNT_ROTATE_X_90
    /* Test scenarios are specified in mechanism coordinates, encode the
       physical sensor mounting before feeding the driver. */
    NegateWord(wire+2, raw+4); memcpy(wire+4, raw+2, 2);
    NegateWord(wire+10, raw+12); memcpy(wire+12, raw+10, 2);
#endif
    IMUCore_Update(core, wire, now, sample);
}
static void Calibrate(IMUCore *core, uint8_t raw[14], uint32_t *now, IMUSample *s)
{
    unsigned int i;
    IMUCore_Init(core); memset(raw, 0, 14); Word(raw + 4, 16384);
    for (i = 0; i < IMU_CALIBRATION_SAMPLES; ++i) Update(core, raw, *now += 10u, s);
    assert(s->valid && s->calibrated && s->accel_trusted && !s->gyro_only);
}
static void BridgeTests(void)
{
    IMUCore core; IMUSample s; uint8_t raw[14]; uint32_t now = 0, start; unsigned int i;
    Calibrate(&core, raw, &now, &s); start = now;
    /* Same low gravity-plane projection as the user's fault record, encoded
       in mechanism axes; 100dps around the selected Y axis. */
    Word(raw, -1360); Word(raw + 2, 82); Word(raw + 4, 2834); Word(raw + 10, 6550);
    for (i = 1; i <= 10; ++i) {
        Update(&core, raw, now = start + i * 10u, &s);
        assert(s.valid && s.gyro_only && !s.accel_trusted);
        assert(s.gravity_age_ms == i * 10u && fabsf(s.pitch_deg - i) < .01f);
    }
    Update(&core, raw, now += 10u, &s);
    assert(!s.valid && !s.gyro_only && s.gravity_age_ms == 110);
    assert(s.gyro_bridge_count == 10);
    assert(fabsf(s.pitch_deg - 10) < .01f); /* Expired bridge does not keep integrating. */
    memset(raw, 0, 14); Word(raw + 4, 16384); Update(&core, raw, now += 10u, &s);
    assert(s.valid && s.accel_trusted && !s.gyro_only && s.gravity_age_ms == 0);
    assert(fabsf(s.pitch_deg) < .01f); /* Reacquire only from trusted gravity. */
    assert(s.gyro_bridge_count == 10);
    Word(raw + 4, 2834); Update(&core, raw, now += 10u, &s);
    assert(s.valid && s.gyro_only);
    Word(raw + 4, 16384); Update(&core, raw, now += 10u, &s);
    assert(s.valid && s.accel_trusted && s.gravity_age_ms == 0 && !s.gyro_only);

    /* Plausible projection alone is insufficient: sustained nongravity norm
       cannot refresh the budget. A short interval still has gyro continuity. */
    Word(raw + 4, 21299); start = now;
    Update(&core, raw, now += 10u, &s); assert(s.valid && s.gyro_only);
    Update(&core, raw, now = start + 110u, &s); assert(!s.valid);

    Calibrate(&core, raw, &now, &s);
    memset(raw, 0, 14); Update(&core, raw, now += 10u, &s);
    assert(!s.valid); /* All-zero frames are never bridged. */
    Word(raw + 4, 2834); Update(&core, raw, now += 10u, &s);
    assert(!s.valid); /* A hard invalid frame broke continuity. */

    Calibrate(&core, raw, &now, &s); Word(raw + 10, 32767);
    Update(&core, raw, now += 10u, &s); assert(!s.valid && s.gyro_saturated);
    Word(raw + 10, -32768); Update(&core, raw, now += 10u, &s);
    assert(!s.valid && s.gyro_saturated);

    Calibrate(&core, raw, &now, &s); Word(raw + 4, 2834);
    Update(&core, raw, now += 101u, &s); assert(!s.valid); /* Unknown gap cannot be integrated. */

    Calibrate(&core, raw, &now, &s); Update(&core, raw, now = 0xfffffff0u, &s);
    assert(s.valid); Word(raw + 4, 2834);
    Update(&core, raw, now + 20u, &s); assert(s.valid && s.gyro_only && s.gravity_age_ms == 20);
    Update(&core, raw, now + 101u, &s); assert(!s.valid && s.gravity_age_ms == 101);

    IMUCore_Init(&core); memset(raw, 0, 14); Word(raw + 4, 2834);
    Update(&core, raw, 10, &s); assert(!s.valid && !s.calibrated);
    assert(s.gravity_age_ms == UINT32_MAX);
    Word(raw + 4, 16384); Update(&core, raw, 20, &s);
    assert(!s.calibrated); Word(raw + 4, 2834); Update(&core, raw, 30, &s);
    assert(!s.valid); /* No gyro-only bridge before calibration. */
}
static void SettlingTests(void)
{
    IMUCore core; IMUSample s; uint8_t raw[14]; uint32_t now = 0;
    unsigned int i, fast_entry = 0, legacy_entry = 0;
    float legacy = .7f, alpha = IMU_FILTER_TAU_SECONDS / (IMU_FILTER_TAU_SECONDS + .01f);
    Calibrate(&core, raw, &now, &s);
    core.tilt_pitch = core.roll = .7f; /* Same small residual, gravity is already level. */
    for (i = 1; i <= 150; ++i) {
        legacy *= alpha;
        Update(&core, raw, now += 10, &s);
        assert(s.valid && s.calibrated && !s.gyro_only);
        if (i <= 8) assert(!s.settling_fast && fabsf(s.pitch_deg - legacy) < .00001f);
        if (i >= 9) assert(s.settling_fast);
        if (!fast_entry && fabsf(s.pitch_deg) <= .1f) fast_entry = i * 10;
        if (!legacy_entry && legacy <= .1f) legacy_entry = i * 10;
        assert(fabsf(s.roll_deg - legacy) < .00001f); /* Other attitude axes unchanged. */
    }
    assert(fast_entry <= 300 && legacy_entry >= 900);
    printf("Quiet 0.7deg synthetic residual to 0.1deg: old=%ums new=%ums\n", legacy_entry, fast_entry);

    /* Moving, excessive norm and >2deg innovation retain the old correction. */
    Calibrate(&core, raw, &now, &s); core.tilt_pitch = .7f; legacy = .7f;
    Word(raw + 10, 655); /* Selected gyro 10dps: motion cannot qualify as quiet. */
    for (i = 0; i < 30; ++i) {
        legacy = (legacy + (655.0f / 65.5f) * .01f) * alpha;
        Update(&core, raw, now += 10, &s);
        assert(!s.settling_fast && !core.quiet_tracking && fabsf(s.pitch_deg - legacy) < .0001f);
    }
    Calibrate(&core, raw, &now, &s); core.tilt_pitch = .7f; legacy = .7f;
    Word(raw + 4, 18022); /* ~1.1g, valid legacy gravity but not strict quiet gravity. */
    for (i = 0; i < 30; ++i) {
        legacy *= alpha; Update(&core, raw, now += 10, &s);
        assert(s.valid && !core.quiet_tracking && fabsf(s.pitch_deg - legacy) < .0001f);
    }
    Calibrate(&core, raw, &now, &s); core.tilt_pitch = 3; legacy = 3;
    for (i = 0; i < 10; ++i) {
        legacy *= alpha; Update(&core, raw, now += 10, &s);
        assert(fabsf(s.pitch_deg - legacy) < .0001f);
    }
    /* Qualify, then interrupt the window: gap, moving gravity, gyro-only. */
    Calibrate(&core, raw, &now, &s);
    for (i = 0; i < 10; ++i) Update(&core, raw, now += 10, &s);
    assert(core.quiet_tracking);
    Update(&core, raw, now += 40, &s); assert(!core.quiet_tracking && s.valid);
    for (i = 0; i < 10; ++i) Update(&core, raw, now += 10, &s);
    Word(raw, -114); /* ~0.4deg gravity angle jump restarts the 80ms window. */
    Update(&core, raw, now += 10, &s); assert(core.quiet_since_ms == now);
    Word(raw, 0); Word(raw + 4, 2834);
    Update(&core, raw, now += 10, &s); assert(!s.settling_fast && !core.quiet_tracking && s.gyro_only);
    Update(&core, raw, now += 101, &s); assert(!s.valid && !core.quiet_tracking);

    Calibrate(&core, raw, &now, &s); now = 0xfffffff0u;
    Update(&core, raw, now, &s); /* Gap reacquires gravity and clears quiet window. */
    for (i = 0; i < 10; ++i) Update(&core, raw, now += 10, &s);
    assert(core.quiet_tracking && (uint32_t)(now - core.quiet_since_ms) >= IMU_SETTLE_QUIET_MS);
    /* 60sec of small raw gravity noise must remain bounded at rest. */
    for (i = 0; i < 6000; ++i) {
        Word(raw, (int16_t)(i % 2 ? 14 : -14));
        Update(&core, raw, now += 10, &s);
        assert(s.valid && fabsf(s.pitch_deg) < .06f);
    }
    IMUCore_StartCalibration(&core); assert(!core.quiet_tracking);
    puts("PASS: guarded selected-axis settling, motion/norm/innovation rejection, interruptions, wrap and static noise");
}
int main(void)
{
    IMUCore core;
    IMUSample sample;
    uint8_t raw[14] = {0};
    uint32_t now = 0;
    unsigned int i;
    IMUCore_Init(&core);
    Word(raw + 4, 16384); /* Z=1g. */
    Word(raw + 8, 65); /* Static gyro bias, ~1dps. */
    for (i = 0; i < 50; ++i) Update(&core, raw, now += 10u, &sample);
    assert(!sample.calibrated && core.calibration_count == 50);
    Word(raw + 8, 1000); /* Motion resets entire calibration window. */
    Update(&core, raw, now += 10u, &sample);
    assert(core.calibration_count == 0);
    Word(raw + 8, 65);
    for (i = 0; i < IMU_CALIBRATION_SAMPLES; ++i) Update(&core, raw, now += 10u, &sample);
    assert(sample.calibrated && sample.valid);
    assert(fabsf(sample.gyro_x_dps) < 0.001f);
    assert(fabsf(sample.roll_deg) < 0.01f && fabsf(sample.pitch_deg) < 0.01f);
    Word(raw + 12, 655); /* Z yaw 10 dps, acceleration cannot correct heading. */
    for (i = 0; i < 100; ++i) Update(&core, raw, now += 10u, &sample);
    assert(fabsf(sample.yaw_deg - 10.0f) < 0.03f);
    Word(raw + 12, 0);
    Word(raw + 2, 8192); Word(raw + 4, 14189); /* Roll +30deg. */
    for (i = 0; i < 600; ++i) Update(&core, raw, now += 10u, &sample);
    assert(fabsf(sample.roll_deg - 30.0f) < 0.03f);
    Word(raw + 2, 0); Word(raw, -8192); /* Pitch +30deg; exercise signed decode. */
    Update(&core, raw, now += 200u, &sample); /* Gap reacquires gravity. */
    assert(fabsf(sample.pitch_deg - 30.0f) < 0.03f);
    assert(fabsf(sample.roll_deg) < 0.03f);
    memset(raw, 0, sizeof(raw));
    Update(&core, raw, now += 10u, &sample);
    assert(!sample.valid); /* Disconnected/all zero device never valid. */
    IMUCore_StartCalibration(&core);
    assert(!core.calibrated && core.calibration_count == 0);
    now = 0xfffffff0u;
    Word(raw + 4, 16384);
    Update(&core, raw, now, &sample);
    Update(&core, raw, now + 20u, &sample);
    assert(isfinite(sample.roll_deg) && isfinite(sample.yaw_deg));
    IMUCore_Init(&core); memset(raw, 0, sizeof(raw)); Word(raw + 4, 16384);
    for (i = 0; i < IMU_CALIBRATION_SAMPLES; ++i) Update(&core, raw, now += 10u, &sample);
    {
        const float angles[] = {-100, -90, -45, 0, 45, 90, 100, 179};
        for (i = 0; i < sizeof(angles)/sizeof(angles[0]); ++i) {
            float radians = angles[i] * 0.01745329252f;
            Word(raw, (int16_t)lroundf(-sinf(radians) * 16384.0f));
            Word(raw + 4, (int16_t)lroundf(cosf(radians) * 16384.0f));
            Update(&core, raw, now += 200u, &sample);
            assert(sample.valid && fabsf(sample.pitch_deg - angles[i]) < 0.03f);
        }
    }
    memset(raw, 0, sizeof(raw)); Word(raw + 2, 16384); /* Gravity parallel to Y: inclination unobservable. */
    Update(&core, raw, now += 10u, &sample); assert(sample.valid && sample.gyro_only);
    Update(&core, raw, now += 101u, &sample); assert(!sample.valid);
    BridgeTests();
    SettlingTests();
    puts("PASS: IMU calibration/fusion, bounded gyro bridge, timeout/recovery, zero/clipped/gapped frames and rollover");
    return 0;
}
