#include "control.h"
#include <string.h>

static float Abs(float value) { return value < 0.0f ? -value : value; }
static float Wrap(float value)
{
    if (value > 180.0f) { value -= 360.0f; }
    if (value < -180.0f) { value += 360.0f; }
    return value;
}
static float Clamp(float value, float low, float high)
{
    return value < low ? low : (value > high ? high : value);
}
static uint8_t NumberValid(float value)
{
    /* Reject NaN/infinity without depending on libc math functions. */
    return (uint8_t)(value == value && value >= -100000.0f && value <= 100000.0f);
}
static uint16_t AngleToPulse(float angle)
{
    return (uint16_t)(500.0f + Clamp(angle, 0.0f, 180.0f) * (2000.0f / 180.0f) + 0.5f);
}
static float Selected(const Control *control, const ControlSample *sample)
{
    return control->config.axis == CONTROL_AXIS_PITCH ? sample->pitch_deg : sample->roll_deg;
}
static uint8_t SampleValid(const Control *control, const ControlSample *sample)
{
    float measured;
    if (!sample || !sample->valid) {
        return 0;
    }
    measured = Selected(control, sample);
    return (uint8_t)(NumberValid(measured) && Abs(measured) <= 360.0f);
}

void Control_DefaultConfig(ControlConfig *config)
{
    if (!config) { return; }
    config->kp = 2.5f;
    config->ki = 0.4f;
    config->kd = 0.06f;
    config->integral_error_scale_deg = 0.0f;
    config->integral_min_ki = 0.0f;
    config->deadband_deg = 0.3f;
    config->target_limit_deg = 60.0f;
    config->target_min_deg = -60.0f;
    config->integral_limit_deg = 35.0f;
    config->slew_deg_per_second = 90.0f;
    config->hold_output_span_deg = 180.0f;
    config->period_ms = 20u;
    config->sample_timeout_ms = 100u;
    config->timing_timeout_ms = 200u;
    config->axis = CONTROL_AXIS_ROLL;
    config->direction = 1;
    config->mode = CONTROL_MODE_PID;
    config->follow_gain = 1.0f;
    config->follow_filter_seconds = 0.15f;
    config->direct_position_gain = 0.5f;
    config->actuator_time_constant_seconds = 0.30f;
}

static uint8_t ConfigValid(const ControlConfig *config)
{
    return (uint8_t)(config && NumberValid(config->kp) && config->kp >= 0.0f &&
        NumberValid(config->ki) && config->ki >= 0.0f &&
        NumberValid(config->kd) && config->kd >= 0.0f &&
        NumberValid(config->integral_error_scale_deg) && config->integral_error_scale_deg >= 0.0f &&
        config->integral_error_scale_deg <= 180.0f &&
        NumberValid(config->integral_min_ki) && config->integral_min_ki >= 0.0f &&
        config->integral_min_ki <= config->ki &&
        NumberValid(config->deadband_deg) && config->deadband_deg >= 0.0f &&
        NumberValid(config->target_limit_deg) && config->target_limit_deg > 0.0f &&
        config->target_limit_deg <= 180.0f &&
        NumberValid(config->target_min_deg) && config->target_min_deg >= -180.0f &&
        config->target_min_deg < config->target_limit_deg &&
        NumberValid(config->integral_limit_deg) && config->integral_limit_deg >= 0.0f &&
        NumberValid(config->slew_deg_per_second) && config->slew_deg_per_second > 0.0f &&
        NumberValid(config->hold_output_span_deg) && config->hold_output_span_deg > 0.0f &&
        config->hold_output_span_deg <= 180.0f &&
        config->period_ms >= 5u && config->period_ms <= 100u &&
        config->sample_timeout_ms >= config->period_ms && config->sample_timeout_ms <= 1000u &&
        config->timing_timeout_ms >= config->period_ms && config->timing_timeout_ms <= 1000u &&
        (config->axis == CONTROL_AXIS_ROLL || config->axis == CONTROL_AXIS_PITCH) &&
        (config->direction == 1 || config->direction == -1) &&
        (config->mode == CONTROL_MODE_PID || config->mode == CONTROL_MODE_FOLLOW ||
         config->mode == CONTROL_MODE_DIRECT) &&
        NumberValid(config->follow_gain) && config->follow_gain > 0.0f && config->follow_gain <= 10.0f &&
        NumberValid(config->follow_filter_seconds) && config->follow_filter_seconds >= 0.0f &&
        config->follow_filter_seconds <= 5.0f &&
        NumberValid(config->direct_position_gain) && config->direct_position_gain > 0.0f &&
        config->direct_position_gain <= 2.0f &&
        NumberValid(config->actuator_time_constant_seconds) &&
        config->actuator_time_constant_seconds >= 0.02f &&
        config->actuator_time_constant_seconds <= 2.0f);
}

void Control_Init(Control *control, const ControlConfig *config, uint32_t now_ms)
{
    if (!control) { return; }
    memset(control, 0, sizeof(*control));
    if (ConfigValid(config)) {
        control->config = *config;
    } else {
        Control_DefaultConfig(&control->config);
    }
    control->state.output_angle_deg = 90.0f;
    control->state.pulse_us = 1500u;
    control->bias_angle_deg = 90.0f;
    control->estimated_actuator_deg = 90.0f;
    control->last_update_ms = now_ms;
}

ControlResult Control_SetManualAngle(Control *control, int16_t angle_deg)
{
    if (!control || angle_deg < 0 || angle_deg > 180) {
        return CONTROL_BAD_ARGUMENT;
    }
    if (control->state.hold_enabled) { return CONTROL_HOLD_ACTIVE; }
    control->state.output_angle_deg = (float)angle_deg;
    control->state.pulse_us = AngleToPulse((float)angle_deg);
    return CONTROL_OK;
}

ControlResult Control_StartHold(Control *control, const ControlSample *sample, uint32_t now_ms)
{
    float measured;
    if (!control) { return CONTROL_BAD_ARGUMENT; }
    if (control->state.fault != CONTROL_FAULT_NONE) { return CONTROL_FAULT_LATCHED; }
    if (!SampleValid(control, sample) ||
        (uint32_t)(now_ms - sample->timestamp_ms) > control->config.sample_timeout_ms) {
        return CONTROL_SENSOR_UNAVAILABLE;
    }
    measured = Selected(control, sample);
    if (control->config.mode != CONTROL_MODE_FOLLOW &&
        (measured < control->config.target_min_deg || measured > control->config.target_limit_deg)) {
        return CONTROL_BAD_ARGUMENT;
    }
    if (control->state.hold_enabled) { return CONTROL_OK; } /* repeated ON is idempotent */
    control->state.hold_enabled = 1;
    control->state.target_deg = measured;
    control->state.measured_deg = measured;
    control->state.error_deg = 0.0f;
    control->state.p_term = 0.0f;
    /* Capture current actuator output as bias; zero error keeps exactly this
       pulse even near a rail, without requiring a large integrator preload. */
    control->bias_angle_deg = control->state.output_angle_deg;
    /* Entry assumes manual motion has settled. There is no servo encoder. */
    control->estimated_actuator_deg = control->state.output_angle_deg;
    control->follow_origin_deg = measured;
    control->follow_displacement_deg = 0.0f;
    control->follow_filtered_deg = 0.0f;
    control->follow_accepted_deg = 0.0f;
    control->state.i_term = 0.0f;
    control->state.d_term = 0.0f;
    control->filtered_rate_dps = 0.0f;
    control->state.saturated = 0;
    control->previous_measured = measured;
    control->have_previous = 1;
    control->last_update_ms = now_ms;
    control->last_sample_ms = sample->timestamp_ms;
    return CONTROL_OK;
}

void Control_StopHold(Control *control)
{
    if (!control) { return; }
    control->state.hold_enabled = 0;
    control->state.fault = CONTROL_FAULT_NONE;
    control->state.saturated = 0;
    control->state.p_term = 0.0f;
    control->state.i_term = 0.0f;
    control->state.d_term = 0.0f;
    control->have_previous = 0;
}

ControlResult Control_SetTarget(Control *control, float target_deg)
{
    if (!control || !NumberValid(target_deg) || target_deg < control->config.target_min_deg ||
        target_deg > control->config.target_limit_deg) {
        return CONTROL_BAD_ARGUMENT;
    }
    if (!control->state.hold_enabled) { return CONTROL_BAD_ARGUMENT; }
    if (control->state.fault != CONTROL_FAULT_NONE) { return CONTROL_FAULT_LATCHED; }
    control->state.target_deg = target_deg;
    return CONTROL_OK;
}

uint16_t Control_Update(Control *control, const ControlSample *sample, uint32_t now_ms)
{
    float dt, measured, error, candidate_i, raw_output, output, limit, signed_error;
    float output_low, output_high, integral_gain;
    uint32_t elapsed;
    if (!control) { return 1500u; }
    /* Keep telemetry live even in manual mode or with a latched controller
       fault. Observation never clears faults or changes the retained pulse. */
    if (SampleValid(control, sample) &&
        (uint32_t)(now_ms - sample->timestamp_ms) <= control->config.sample_timeout_ms) {
        control->state.measured_deg = Selected(control, sample);
        control->state.error_deg = control->config.mode == CONTROL_MODE_FOLLOW && control->state.hold_enabled ?
            control->follow_accepted_deg + control->follow_origin_deg - control->state.target_deg :
            control->state.target_deg - control->state.measured_deg;
    }
    if (!control->state.hold_enabled || control->state.fault != CONTROL_FAULT_NONE) {
        return control->state.pulse_us;
    }
    if (!SampleValid(control, sample)) {
        control->state.fault = CONTROL_FAULT_IMU_INVALID;
        return control->state.pulse_us;
    }
    if ((uint32_t)(now_ms - sample->timestamp_ms) > control->config.sample_timeout_ms) {
        control->state.fault = CONTROL_FAULT_IMU_STALE;
        return control->state.pulse_us;
    }
    elapsed = now_ms - control->last_update_ms;
    if (elapsed > control->config.timing_timeout_ms) {
        control->state.fault = CONTROL_FAULT_TIMING;
        return control->state.pulse_us;
    }
    if (elapsed < control->config.period_ms || sample->timestamp_ms == control->last_sample_ms) {
        return control->state.pulse_us;
    }
    dt = (float)elapsed * 0.001f;
    output_low = Clamp(control->bias_angle_deg - control->config.hold_output_span_deg, 0.0f, 180.0f);
    output_high = Clamp(control->bias_angle_deg + control->config.hold_output_span_deg, 0.0f, 180.0f);
    measured = Selected(control, sample);
    if (control->config.mode == CONTROL_MODE_DIRECT) {
        /* Advance using the preceding LIMITED command, not the new request.
           The command is not treated as a measured shaft position. */
        control->estimated_actuator_deg += dt /
            (control->config.actuator_time_constant_seconds + dt) *
            (control->state.output_angle_deg - control->estimated_actuator_deg);
        control->estimated_actuator_deg = Clamp(control->estimated_actuator_deg, 0.0f, 180.0f);
        error = control->state.target_deg - measured;
        control->state.error_deg = error;
        control->state.i_term = control->state.d_term = 0.0f;
        if (Abs(error) <= control->config.deadband_deg) {
            /* Keep the achieved command: returning to the lagging estimate
               here would undo a successful correction. */
            control->state.p_term = 0.0f;
            raw_output = control->state.output_angle_deg;
        } else {
            control->state.p_term = control->config.direct_position_gain * error;
            raw_output = control->estimated_actuator_deg +
                (float)control->config.direction * control->state.p_term;
        }
        limit = control->config.slew_deg_per_second * dt;
        output = Clamp(Clamp(raw_output, output_low, output_high),
            control->state.output_angle_deg - limit, control->state.output_angle_deg + limit);
        control->state.saturated = (uint8_t)(Abs(output - raw_output) > 0.001f);
        control->state.output_angle_deg = output;
        control->state.pulse_us = AngleToPulse(output);
        control->last_update_ms = now_ms;
        control->last_sample_ms = sample->timestamp_ms;
        control->previous_measured = measured;
        control->have_previous = 1;
        return control->state.pulse_us;
    }
    if (control->config.mode == CONTROL_MODE_FOLLOW) {
        /* The sensor is on the hand-turned board, not the actuator. Map board
           displacement directly; never integrate an unattainable PID error.
           Unwrap successive samples so +/-180 crossing cannot reverse output. */
        control->follow_displacement_deg += Wrap(measured - control->previous_measured);
        control->follow_filtered_deg += dt / (control->config.follow_filter_seconds + dt) *
            (control->follow_displacement_deg - control->follow_filtered_deg);
        if (Abs(control->follow_filtered_deg - control->follow_accepted_deg) >= control->config.deadband_deg) {
            control->follow_accepted_deg = control->follow_filtered_deg;
        }
        control->state.measured_deg = measured;
        control->state.error_deg = control->follow_accepted_deg +
            control->follow_origin_deg - control->state.target_deg;
        control->state.p_term = control->config.follow_gain * control->state.error_deg;
        control->state.i_term = control->state.d_term = 0.0f;
        raw_output = control->bias_angle_deg + (float)control->config.direction * control->state.p_term;
        limit = control->config.slew_deg_per_second * dt;
        output = Clamp(Clamp(raw_output, output_low, output_high),
            control->state.output_angle_deg - limit, control->state.output_angle_deg + limit);
        control->state.saturated = (uint8_t)(Abs(output - raw_output) > 0.001f);
        control->state.output_angle_deg = output;
        control->state.pulse_us = AngleToPulse(output);
        control->last_update_ms = now_ms;
        control->last_sample_ms = sample->timestamp_ms;
        control->previous_measured = measured;
        return control->state.pulse_us;
    }
    error = control->state.target_deg - measured;
    control->state.measured_deg = measured;
    control->state.error_deg = error;
    if (Abs(error) <= control->config.deadband_deg) { error = 0.0f; }
    control->state.p_term = control->config.kp * error;
    /* Derivative on measurement avoids a kick when target changes. */
    control->filtered_rate_dps += dt / (0.08f + dt) *
        ((control->have_previous ? (measured - control->previous_measured) / dt : 0.0f) -
         control->filtered_rate_dps);
    control->state.d_term = -control->config.kd * control->filtered_rate_dps;
    integral_gain = control->config.ki;
    if (control->config.integral_error_scale_deg > 0.0f) {
        float ratio = error / control->config.integral_error_scale_deg;
        float weight = ratio * ratio / (1.0f + ratio * ratio);
        integral_gain = control->config.integral_min_ki +
            (control->config.ki - control->config.integral_min_ki) * weight;
    }
    candidate_i = Clamp(control->state.i_term + integral_gain * error * dt,
        -control->config.integral_limit_deg, control->config.integral_limit_deg);
    raw_output = control->bias_angle_deg + (float)control->config.direction *
        (control->state.p_term + candidate_i + control->state.d_term);
    signed_error = (float)control->config.direction * error;
    limit = control->config.slew_deg_per_second * dt;
    /* Freeze integration whenever it would drive further into rail/slew limiting. */
    if (!((signed_error > 0.0f && (raw_output > output_high ||
            raw_output > control->state.output_angle_deg + limit)) ||
          (signed_error < 0.0f && (raw_output < output_low ||
            raw_output < control->state.output_angle_deg - limit)))) {
        control->state.i_term = candidate_i;
    }
    raw_output = control->bias_angle_deg + (float)control->config.direction *
        (control->state.p_term + control->state.i_term + control->state.d_term);
    output = Clamp(raw_output, output_low, output_high);
    output = Clamp(output, control->state.output_angle_deg - limit,
        control->state.output_angle_deg + limit);
    control->state.saturated = (uint8_t)(Abs(output - raw_output) > 0.001f);
    control->state.output_angle_deg = output;
    control->state.pulse_us = AngleToPulse(output);
    control->last_update_ms = now_ms;
    control->last_sample_ms = sample->timestamp_ms;
    control->previous_measured = measured;
    control->have_previous = 1;
    return control->state.pulse_us;
}

const ControlSnapshot *Control_GetSnapshot(const Control *control)
{
    return control ? &control->state : 0;
}

const char *Control_FaultName(ControlFault fault)
{
    switch (fault) {
    case CONTROL_FAULT_NONE: return "NONE";
    case CONTROL_FAULT_IMU_INVALID: return "IMU_INVALID";
    case CONTROL_FAULT_IMU_STALE: return "IMU_STALE";
    case CONTROL_FAULT_TIMING: return "TIMING";
    default: return "UNKNOWN";
    }
}
