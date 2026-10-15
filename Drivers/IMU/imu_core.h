#ifndef IMU_CORE_H
#define IMU_CORE_H
#include "imu.h"
#include "imu_config.h"
typedef struct {
    float bias[3];
    float gyro_sum[3];
    float roll_sum, pitch_sum;
    float zero_roll, zero_pitch;
    float roll, pitch, yaw;
    float tilt_pitch, tilt_pitch_sum, zero_tilt_pitch;
    uint16_t calibration_count;
    uint32_t previous_ms;
    bool calibrated;
    bool first;
} IMUCore;
void IMUCore_Init(IMUCore *core);
void IMUCore_StartCalibration(IMUCore *core);
void IMUCore_Update(IMUCore *core, const uint8_t raw[14], uint32_t now_ms, IMUSample *sample);
#endif
