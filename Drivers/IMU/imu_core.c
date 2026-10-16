#include "imu_core.h"
#include <math.h>
#include <string.h>
#define RAD_TO_DEG 57.2957795131f
#define DEG_TO_RAD 0.01745329252f

static int16_t SignedWord(const uint8_t *bytes)
{
    uint16_t word = (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
    int32_t value = word;
    if (word & 0x8000u) value -= 65536;
    return (int16_t)value;
}
static float Wrap(float angle)
{
    /* Input is bounded by finite sensor data and capped dt, max two turns. */
    if (angle > 180.0f) angle -= 360.0f;
    if (angle < -180.0f) angle += 360.0f;
    return angle;
}
void IMUCore_Init(IMUCore *core)
{
    memset(core, 0, sizeof(*core));
    core->first = true;
}
void IMUCore_StartCalibration(IMUCore *core) { IMUCore_Init(core); }

void IMUCore_Update(IMUCore *core, const uint8_t raw[14], uint32_t now_ms, IMUSample *sample)
{
    float g[3], norm, accel_roll, accel_pitch, accel_tilt_pitch, projection, dt, alpha;
    float roll_rad, pitch_rad, cosine, sine, cos_pitch;
    float ax, ay, az;
    bool frame_usable, gravity_trusted;
    unsigned int i;
    sample->accel_x_g = SignedWord(raw) / 16384.0f;
    sample->accel_y_g = SignedWord(raw + 2) / 16384.0f;
    sample->accel_z_g = SignedWord(raw + 4) / 16384.0f;
    sample->temperature_c = SignedWord(raw + 6) / 340.0f + 36.53f;
    sample->gyro_saturated = false;
    for (i = 0; i < 3; ++i) {
        int16_t word = SignedWord(raw + 8 + i * 2);
        if (word >= IMU_GYRO_SATURATION_RAW || word <= -IMU_GYRO_SATURATION_RAW)
            sample->gyro_saturated = true;
        g[i] = word / 65.5f;
    }
    norm = sqrtf(sample->accel_x_g * sample->accel_x_g +
                 sample->accel_y_g * sample->accel_y_g +
                 sample->accel_z_g * sample->accel_z_g);
    ax = sample->accel_x_g;
#if IMU_MOUNT_ROTATE_X_90
    ay = sample->accel_z_g; az = -sample->accel_y_g;
#else
    ay = sample->accel_y_g; az = sample->accel_z_g;
#endif
    accel_roll = atan2f(ay, az) * RAD_TO_DEG;
    accel_pitch = atan2f(-ax, sqrtf(ay * ay + az * az)) * RAD_TO_DEG;
    /* Signed single-axis Y inclination: preserve which side of vertical the
       mechanism occupies. Conventional Euler Pitch folds at +/-90 degrees. */
    projection = ax * ax + az * az;
    gravity_trusted = norm >= 0.8f && norm <= 1.2f && projection >= 0.04f;
    frame_usable = norm > 0.05f && norm < 4.0f && !sample->gyro_saturated;
    sample->accel_trusted = gravity_trusted;
    accel_tilt_pitch = atan2f(-ax, az) * RAD_TO_DEG;
    if (!core->calibrated) {
        bool still = norm >= 0.85f && norm <= 1.15f && projection >= 0.04f &&
                     fabsf(g[0]) <= 5.0f && fabsf(g[1]) <= 5.0f && fabsf(g[2]) <= 5.0f;
        /* Consecutive stable samples: never average motion into gyro bias. */
        if (core->calibration_count &&
            (fabsf(Wrap(accel_roll - core->roll_sum / core->calibration_count)) > 2.0f ||
             fabsf(accel_pitch - core->pitch_sum / core->calibration_count) > 2.0f)) still = false;
        if (!still) {
            core->calibration_count = 0;
            memset(core->gyro_sum, 0, sizeof(core->gyro_sum));
            core->roll_sum = core->pitch_sum = core->tilt_pitch_sum = 0;
        } else {
            for (i = 0; i < 3; ++i) core->gyro_sum[i] += g[i];
            /* Keep roll samples on one branch around +/-180deg. */
            if (core->calibration_count)
                core->roll_sum += core->roll_sum / core->calibration_count +
                                  Wrap(accel_roll - core->roll_sum / core->calibration_count);
            else core->roll_sum += accel_roll;
            core->pitch_sum += accel_pitch;
            if (core->calibration_count)
                core->tilt_pitch_sum += core->tilt_pitch_sum / core->calibration_count +
                    Wrap(accel_tilt_pitch - core->tilt_pitch_sum / core->calibration_count);
            else core->tilt_pitch_sum += accel_tilt_pitch;
            ++core->calibration_count;
            if (core->calibration_count == IMU_CALIBRATION_SAMPLES) {
                for (i = 0; i < 3; ++i) core->bias[i] = core->gyro_sum[i] / IMU_CALIBRATION_SAMPLES;
                core->zero_roll = core->roll_sum / IMU_CALIBRATION_SAMPLES;
                core->zero_pitch = core->pitch_sum / IMU_CALIBRATION_SAMPLES;
                core->zero_tilt_pitch = core->tilt_pitch_sum / IMU_CALIBRATION_SAMPLES;
                core->calibrated = true;
                core->first = true;
            }
        }
    }
    for (i = 0; i < 3; ++i) g[i] -= core->bias[i];
    sample->gyro_x_dps = g[0];
    sample->gyro_y_dps = g[1];
    sample->gyro_z_dps = g[2];
#if IMU_MOUNT_ROTATE_X_90
    /* Bias calibration and displayed gyros remain in original sensor axes. */
    { float mapped_y = g[2]; g[2] = -g[1]; g[1] = mapped_y; }
#endif
    dt = (uint32_t)(now_ms - core->previous_ms) * 0.001f;
    if (!frame_usable) {
        /* Zero/corrupt-range frames and clipped gyros cannot seed prediction. */
        core->first = true;
    } else if (core->first || dt > 0.1f) {
        /* Never initialize/reacquire from acceleration rejected as gravity. */
        if (gravity_trusted) {
            core->roll = accel_roll;
            core->pitch = accel_pitch;
            core->tilt_pitch = accel_tilt_pitch;
            core->first = false;
            core->last_gravity_ms = now_ms;
            core->have_gravity_reference = true;
        } else core->first = true;
    } else if (gravity_trusted || (core->calibrated && core->have_gravity_reference &&
               (uint32_t)(now_ms - core->last_gravity_ms) <= IMU_GYRO_BRIDGE_MS)) {
        roll_rad = core->roll * DEG_TO_RAD;
        pitch_rad = core->pitch * DEG_TO_RAD;
        sine = sinf(roll_rad);
        cosine = cosf(roll_rad);
        cos_pitch = cosf(pitch_rad);
        /* Euler singularity at +/-90deg: intended servo tilt range is +/-60deg. */
        if (fabsf(cos_pitch) < 0.15f) cos_pitch = cos_pitch < 0 ? -0.15f : 0.15f;
        core->roll += (g[0] + (g[1] * sine + g[2] * cosine) * sinf(pitch_rad) / cos_pitch) * dt;
        core->pitch += (g[1] * cosine - g[2] * sine) * dt;
        core->yaw = Wrap(core->yaw + (g[1] * sine + g[2] * cosine) / cos_pitch * dt);
        core->roll = Wrap(core->roll);
        core->pitch = Wrap(core->pitch);
        core->tilt_pitch = Wrap(core->tilt_pitch + g[1] * dt);
        if (gravity_trusted) {
            alpha = IMU_FILTER_TAU_SECONDS / (IMU_FILTER_TAU_SECONDS + dt);
            core->roll = Wrap(core->roll + (1.0f - alpha) * Wrap(accel_roll - core->roll));
            core->pitch += (1.0f - alpha) * Wrap(accel_pitch - core->pitch);
            if (projection >= 0.04f) {
                core->tilt_pitch = Wrap(core->tilt_pitch +
                    (1.0f - alpha) * Wrap(accel_tilt_pitch - core->tilt_pitch));
            }
            core->last_gravity_ms = now_ms;
            core->have_gravity_reference = true;
        }
    } else core->first = true; /* Expired reference: wait for trusted gravity. */
    core->previous_ms = now_ms;
    sample->roll_deg = Wrap(core->roll - core->zero_roll);
    sample->pitch_deg = Wrap(core->tilt_pitch - core->zero_tilt_pitch);
    sample->yaw_deg = core->yaw;
    sample->timestamp_ms = now_ms;
    sample->gravity_age_ms = core->have_gravity_reference ?
        (uint32_t)(now_ms - core->last_gravity_ms) : UINT32_MAX;
    sample->valid = frame_usable && !core->first && core->have_gravity_reference &&
        (gravity_trusted || (core->calibrated && sample->gravity_age_ms <= IMU_GYRO_BRIDGE_MS));
    sample->gyro_only = sample->valid && !gravity_trusted;
    if (sample->gyro_only && core->gyro_bridge_count != UINT32_MAX) ++core->gyro_bridge_count;
    sample->gyro_bridge_count = core->gyro_bridge_count;
    sample->calibrated = core->calibrated;
}
