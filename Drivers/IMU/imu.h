#ifndef IMU_H
#define IMU_H
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    float gyro_x_dps, gyro_y_dps, gyro_z_dps;
    float accel_x_g, accel_y_g, accel_z_g;
    float roll_deg, pitch_deg, yaw_deg;
    float temperature_c;
    uint32_t timestamp_ms;
    bool valid;
    bool calibrated;
    bool accel_trusted;
    bool gyro_only;
    bool gyro_saturated;
    uint32_t gravity_age_ms; /* UINT32_MAX: no accepted gravity reference yet. */
    uint32_t gyro_bridge_count; /* Accepted gyro-only frames since initialization/calibration. */
} IMUSample;
typedef enum { IMU_STARTING, IMU_CALIBRATING, IMU_READY, IMU_FAULT } IMUStatus;
void IMU_Init(uint32_t now_ms);
void IMU_Process(uint32_t now_ms);
bool IMU_GetSample(IMUSample *sample);
bool IMU_IsReady(void);
IMUStatus IMU_GetStatus(void);
uint8_t IMU_GetAddress(void);
const char *IMU_GetFaultReason(void); /* Last failure retained while retrying. */
uint16_t IMU_GetCalibrationCount(void);
bool IMU_RequestCalibration(void);
void IMU_SetPaused(bool paused); /* Stop new requests before low power entry. */
#endif
