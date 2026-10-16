#ifndef IMU_CONFIG_H
#define IMU_CONFIG_H
#define IMU_ADDRESS 0x68u /* AD0 low. Set 0x69 if AD0 high. */
#define IMU_AUTO_ADDRESS 1u /* Probe both MPU6050 addresses before any writes. */
#define IMU_SAMPLE_MS 10u
#define IMU_STALE_MS 100u
#define IMU_CALIBRATION_SAMPLES 200u
#define IMU_CALIBRATION_TIMEOUT_MS 10000u
#define IMU_FILTER_TAU_SECONDS 0.5f
/* GYRO_CONFIG FS_SEL=2: these three values must be changed together. */
#define IMU_GYRO_CONFIG_VALUE 0x10u
#define IMU_GYRO_RANGE_DPS 1000u
#define IMU_GYRO_LSB_PER_DPS 32.8f
/* Trial: faster selected-axis residual correction only after quiet gravity.
   This does not change calibration, validity or gyro-only bridge rules. */
#define IMU_SETTLE_ENABLE 1u
#define IMU_SETTLE_QUIET_MS 80u
#define IMU_SETTLE_MAX_GAP_MS 30u
#define IMU_SETTLE_GYRO_DPS 2.0f
#define IMU_SETTLE_NORM_TOLERANCE 0.03f
#define IMU_SETTLE_ACCEL_SPAN_DEG 0.2f
#define IMU_SETTLE_INNOVATION_DEG 2.0f
#define IMU_SETTLE_TAU_SECONDS 0.10f
#define IMU_SETTLE_MAX_CORRECTION_DPS 6.0f
/* Independent trial: tolerate up to 200ms without trusted gravity, but keep
   the separate 100ms maximum integration gap and all hard-invalid checks. */
#define IMU_GYRO_BRIDGE_MS 200u
#define IMU_GYRO_SATURATION_RAW 32700
/* Actual rod mount: mechanism (X,Y,Z) = sensor (X,Z,-Y). */
#define IMU_MOUNT_ROTATE_X_90 1u
#endif
