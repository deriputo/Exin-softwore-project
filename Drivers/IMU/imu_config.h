#ifndef IMU_CONFIG_H
#define IMU_CONFIG_H
#define IMU_ADDRESS 0x68u /* AD0 low. Set 0x69 if AD0 high. */
#define IMU_AUTO_ADDRESS 1u /* Probe both MPU6050 addresses before any writes. */
#define IMU_SAMPLE_MS 10u
#define IMU_STALE_MS 100u
#define IMU_CALIBRATION_SAMPLES 200u
#define IMU_CALIBRATION_TIMEOUT_MS 10000u
#define IMU_FILTER_TAU_SECONDS 0.5f
/* Actual rod mount: mechanism (X,Y,Z) = sensor (X,Z,-Y). */
#define IMU_MOUNT_ROTATE_X_90 1u
#endif
