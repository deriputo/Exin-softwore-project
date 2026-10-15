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
    Update(&core, raw, now += 10u, &sample); assert(!sample.valid);
    puts("PASS: IMU calibration/fusion and signed Y inclination beyond90 without fold, invalid projection");
    return 0;
}
