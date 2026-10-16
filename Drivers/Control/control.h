#ifndef CONTROL_H
#define CONTROL_H

#include <stdint.h>

typedef enum { CONTROL_AXIS_ROLL = 0, CONTROL_AXIS_PITCH = 1 } ControlAxis;
typedef enum { CONTROL_MODE_PID = 0, CONTROL_MODE_FOLLOW = 1,
               CONTROL_MODE_DIRECT = 2 } ControlMode;
typedef enum {
    CONTROL_FAULT_NONE = 0,
    CONTROL_FAULT_IMU_INVALID,
    CONTROL_FAULT_IMU_STALE,
    CONTROL_FAULT_TIMING
} ControlFault;
typedef enum {
    CONTROL_OK = 0,
    CONTROL_BAD_ARGUMENT,
    CONTROL_HOLD_ACTIVE,
    CONTROL_SENSOR_UNAVAILABLE,
    CONTROL_FAULT_LATCHED
} ControlResult;

typedef struct {
    float roll_deg;
    float pitch_deg;
    uint32_t timestamp_ms;
    uint8_t valid;
    float angular_rate_dps; /* Same selected axis/sign and timestamp as attitude. */
    uint8_t angular_rate_valid;
} ControlSample;

typedef struct {
    float kp;
    float ki;
    float kd;
    float integral_error_scale_deg; /* 0: constant Ki; >0: bounded gain scheduling. */
    float integral_min_ki; /* Gain near zero error, 0 <= min <= ki (upper gain). */
    float deadband_deg;
    float target_limit_deg;
    float target_min_deg;
    float integral_limit_deg;
    float slew_deg_per_second;
    float hold_output_span_deg; /* Max movement from output captured at HOLD entry. */
    uint32_t period_ms;
    uint32_t sample_timeout_ms;
    uint32_t timing_timeout_ms;
    ControlAxis axis;
    int8_t direction; /* +1 or -1: must be checked on the real mechanism. */
    ControlMode mode;
    float follow_gain;
    float follow_filter_seconds;
    float direct_position_gain; /* Actuator degrees / attitude error degree. */
    float actuator_time_constant_seconds; /* Estimate, not position feedback. */
    float direct_rate_gain_seconds; /* Bounded feedback from filtered attitude rate. */
    float direct_rate_limit_deg;
    uint8_t use_rate_sample; /* PID only; 0 preserves angle-difference derivative. */
    float rate_filter_seconds;
    float rate_term_limit_deg; /* 0: legacy unlimited D before final output limits. */
    float rate_quiet_dps;
    uint8_t rate_away_only; /* Assist motion away from target; no return braking. */
} ControlConfig;

typedef struct {
    float target_deg;
    float measured_deg;
    float error_deg;
    float p_term;
    float i_term;
    float d_term;
    float output_angle_deg;
    uint16_t pulse_us;
    uint8_t hold_enabled;
    uint8_t saturated;
    ControlFault fault;
} ControlSnapshot;

typedef struct {
    ControlConfig config;
    ControlSnapshot state;
    uint32_t last_update_ms;
    uint32_t last_sample_ms;
    float previous_measured;
    float bias_angle_deg;
    float filtered_rate_dps;
    float follow_origin_deg;
    float follow_displacement_deg;
    float follow_filtered_deg;
    float follow_accepted_deg;
    float estimated_actuator_deg;
    uint8_t have_previous;
} Control;

void Control_DefaultConfig(ControlConfig *config);
void Control_Init(Control *control, const ControlConfig *config, uint32_t now_ms);
ControlResult Control_SetManualAngle(Control *control, int16_t angle_deg);
/* Captures sensor and servo reference. PID/DIRECT obey target limits;
   FOLLOW accepts the full valid sensor range, unwraps motion and maps 1:1. */
ControlResult Control_StartHold(Control *control, const ControlSample *sample, uint32_t now_ms);
/* Clears fault and retains the last pulse as manual output. */
void Control_StopHold(Control *control);
ControlResult Control_SetTarget(Control *control, float target_deg);
/* Main loop; updates PID/FOLLOW/DIRECT on new sample/period only. */
uint16_t Control_Update(Control *control, const ControlSample *sample, uint32_t now_ms);
const ControlSnapshot *Control_GetSnapshot(const Control *control);
const char *Control_FaultName(ControlFault fault);

#endif
