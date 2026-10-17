#include "app.h"
#include "app_config.h"
#include "ui_core.h"
#include "key.h"
#include "key_config.h"
#include "encoder.h"
#include "pwm.h"
#include "rtc_clock.h"
#include "power.h"
#include "i2c_bus.h"
#include "oled.h"
#include "imu.h"
#include "imu_config.h"
#include "control.h"
#include "serial.h"
#include "bluetooth.h"
#include "protocol.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

static UIState ui;
static Control control;
static Protocol protocol, bluetooth_protocol;
static const uint8_t wired_channel = 0, bluetooth_channel = 1;
static uint8_t protocol_turn;
static uint32_t last_bluetooth_trace;
static uint32_t now, last_display, last_trace, boot_start, init_time, notice_until;
static uint16_t last_encoder;
static uint8_t boot_started, boot_done, trace_on, ui_time_pending;
static uint8_t consumed_keys, alarm_previous, sleep_previous, restore_checked;
static uint8_t standby_bus_suspended;
static char notice[22];

typedef struct {
    IMUSample imu;
    uint32_t at_ms, age_ms;
    uint16_t pulse_us, calibration_count;
    ControlFault fault;
    char source[24];
    uint8_t captured;
    uint8_t gyro_config, gyro_config_verified;
} HoldFaultRecord;
static HoldFaultRecord hold_fault;

static void Notice(const char *text)
{
    (void)snprintf(notice, sizeof(notice), "%s", text);
    notice_until = now + 1500u;
}

static ControlSample Sample(void)
{
    IMUSample imu_sample;
    ControlSample sample;
    memset(&sample, 0, sizeof(sample));
    if (IMU_GetSample(&imu_sample)) {
        sample.roll_deg = imu_sample.roll_deg;
        sample.pitch_deg = APP_ATTITUDE_REFERENCE_DEG + imu_sample.pitch_deg;
        sample.timestamp_ms = imu_sample.timestamp_ms;
        sample.valid = (uint8_t)(imu_sample.valid && imu_sample.calibrated);
        if (APP_HOLD_AXIS == CONTROL_AXIS_PITCH) {
            /* Matches IMUCore tilt_pitch += mapped gyro Y * dt. */
#if IMU_MOUNT_ROTATE_X_90
            sample.angular_rate_dps = imu_sample.gyro_z_dps;
#else
            sample.angular_rate_dps = imu_sample.gyro_y_dps;
#endif
            sample.angular_rate_valid = 1;
        }
    }
    return sample;
}

static void RecordHoldFault(const ControlSnapshot *state)
{
    const char *source;
    float norm_squared, projection;
    if (state->fault == CONTROL_FAULT_NONE) {
        /* Preserve evidence after OFF; a successful new HOLD starts a new run. */
        if (state->hold_enabled) { hold_fault.captured = 0; }
        return;
    }
    if (hold_fault.captured) { return; }
    memset(&hold_fault, 0, sizeof(hold_fault));
    (void)IMU_GetSample(&hold_fault.imu);
    hold_fault.at_ms = now;
    hold_fault.age_ms = now - hold_fault.imu.timestamp_ms;
    hold_fault.pulse_us = state->pulse_us;
    hold_fault.calibration_count = IMU_GetCalibrationCount();
    hold_fault.fault = state->fault;
    hold_fault.gyro_config_verified = (uint8_t)IMU_GetGyroConfig(&hold_fault.gyro_config);
    norm_squared = hold_fault.imu.accel_x_g * hold_fault.imu.accel_x_g +
                   hold_fault.imu.accel_y_g * hold_fault.imu.accel_y_g +
                   hold_fault.imu.accel_z_g * hold_fault.imu.accel_z_g;
#if IMU_MOUNT_ROTATE_X_90
    projection = hold_fault.imu.accel_x_g * hold_fault.imu.accel_x_g +
                 hold_fault.imu.accel_y_g * hold_fault.imu.accel_y_g;
#else
    projection = hold_fault.imu.accel_x_g * hold_fault.imu.accel_x_g +
                 hold_fault.imu.accel_z_g * hold_fault.imu.accel_z_g;
#endif
    source = IMU_GetFaultReason();
    if (state->fault == CONTROL_FAULT_TIMING) { source = "CONTROL_TIMING"; }
    else if (source[0]) { /* Keep the original communication/calibration stage. */ }
    else if (hold_fault.age_ms > IMU_STALE_MS) { source = "SAMPLE_STALE"; }
    else if (!hold_fault.imu.calibrated) { source = "UNCALIBRATED"; }
    else if (hold_fault.imu.gyro_saturated) { source = "GYRO_RANGE"; }
    else if (!(norm_squared > 0.0025f && norm_squared < 16.0f)) { source = "ACCEL_NORM"; }
    else if (!hold_fault.imu.accel_trusted && hold_fault.imu.gravity_age_ms != UINT32_MAX &&
             hold_fault.imu.gravity_age_ms > IMU_GYRO_BRIDGE_MS) { source = "GRAVITY_TIMEOUT"; }
    else if (!(projection >= 0.04f)) { source = "AXIS_PROJECTION"; }
    else { source = "CONTROL_INPUT"; }
    (void)snprintf(hold_fault.source, sizeof(hold_fault.source), "%s", source);
    hold_fault.captured = 1;
}

static void SyncServoEdit(void)
{
    const ControlSnapshot *s = Control_GetSnapshot(&control);
    ui.servo_edit_deg = (int16_t)(s->output_angle_deg + 0.5f);
    ui.attitude_edit_tenths = (int16_t)Protocol_FloatToTenths(s->target_deg);
    ui.servo_dirty = 0;
}

static void EnterPage(void)
{
    RTCClockAlarm alarm;
    if (ui.page == UI_TIME) { (void)RTCClock_GetTime(&ui.time_edit); }
    if (ui.page == UI_ALARM && RTCClock_GetAlarm(ui.alarm_number, &alarm) == RTC_CLOCK_OK) {
        ui.alarm_edit = alarm.time;
        ui.alarm_enabled = alarm.enabled;
    }
    if (ui.page == UI_SERVO) { SyncServoEdit(); }
}

static const char *RTCResultName(RTCClockResult result)
{
    switch (result) {
    case RTC_CLOCK_OK: return "OK";
    case RTC_CLOCK_BUSY: return "BUSY";
    case RTC_CLOCK_NOT_READY: return "RTC NOT READY";
    default: return "BAD ARGUMENT";
    }
}

static const char *IMUHoldBlockReason(void)
{
    IMUSample sample;
    (void)IMU_GetSample(&sample);
    if (!sample.valid) {
        if (IMU_GetFaultReason()[0]) { return IMU_GetFaultReason(); }
        if (IMU_GetStatus() == IMU_STARTING) { return "IMU STARTING"; }
        if ((uint32_t)(now - sample.timestamp_ms) > IMU_STALE_MS) { return "IMU SAMPLE STALE"; }
        return "IMU DATA INVALID";
    }
    if (!sample.calibrated) { return "CALIBRATE IMU FIRST"; }
    return "IMU NOT READY";
}

static const char *ControlResultName(ControlResult result)
{
    switch (result) {
    case CONTROL_OK: return "OK";
    case CONTROL_HOLD_ACTIVE: return "HOLD ACTIVE";
    case CONTROL_SENSOR_UNAVAILABLE: return IMUHoldBlockReason();
    case CONTROL_FAULT_LATCHED: return "FAULT LATCHED";
    default: return "BAD ARGUMENT";
    }
}

static void HandleAction(UIAction action)
{
    ControlResult result;
    ControlSample sample;
    RTCClockResult rtc_result;
    const ControlSnapshot *s = Control_GetSnapshot(&control);
    switch (action) {
    case UI_ACTION_PAGE: case UI_ACTION_ALARM_SLOT: EnterPage(); break;
    case UI_ACTION_TIME_SAVE:
        if (ui_time_pending || Protocol_IsWaiting(&protocol) ||
            Protocol_IsWaiting(&bluetooth_protocol)) { Notice("BUSY"); break; }
        rtc_result = RTCClock_SetTime(&ui.time_edit);
        if (rtc_result == RTC_CLOCK_OK) { ui_time_pending = 1; Notice("SAVING..."); }
        else { Notice(RTCResultName(rtc_result)); }
        break;
    case UI_ACTION_ALARM_SAVE:
        rtc_result = RTCClock_SetAlarm(ui.alarm_number, &ui.alarm_edit, ui.alarm_enabled);
        Notice(rtc_result == RTC_CLOCK_OK ? "ALARM SAVED" : RTCResultName(rtc_result));
        break;
    case UI_ACTION_SERVO_CONFIRM:
        result = s->hold_enabled ? Control_SetTarget(&control, ui.attitude_edit_tenths / 10.0f)
                                 : Control_SetManualAngle(&control, ui.servo_edit_deg);
        Notice(ControlResultName(result));
        if (result == CONTROL_OK) { SyncServoEdit(); }
        break;
    case UI_ACTION_HOLD_TOGGLE:
        if (s->hold_enabled) { Control_StopHold(&control); Notice("HOLD OFF"); }
        else {
            sample = Sample();
            result = Control_StartHold(&control, &sample, now);
            Notice(result == CONTROL_OK ? "HOLD ON" : ControlResultName(result));
        }
        SyncServoEdit();
        break;
    case UI_ACTION_IMU_CALIBRATE:
        if (s->hold_enabled) { Notice("TURN HOLD OFF"); }
        else if (s->output_angle_deg < 89.9f || s->output_angle_deg > 90.1f) {
            Notice("SET SERVO 90 FIRST");
        }
        else { Notice(IMU_RequestCalibration() ? "KEEP IMU STILL" : "IMU NOT READY"); }
        break;
    default: break;
    }
}

static void Keys(void)
{
    KeyEvent event;
    uint8_t mask;
    /* Use the debounced level, not matching PRESS timestamps: one key may be
       held before the other, and both may already be down when the alarm fires. */
    if (RTCClock_GetActiveAlarmMask() &&
        (Key_GetPressedMask() & (KEY1_MASK | KEY2_MASK)) == (KEY1_MASK | KEY2_MASK)) {
        RTCClock_AcknowledgeAlarm();
        consumed_keys |= (KEY1_MASK | KEY2_MASK);
        Notice("ALARM STOPPED");
    }
    while (Key_GetEvent(&event)) {
        mask = (uint8_t)(1u << (unsigned int)event.key);
        /* Capture idle before Touch clears it. Consume the entire wake gesture,
           including LONG/SHORT and RELEASE, using the acknowledgement mask. */
        if (Power_IsIdle() || sleep_previous) {
            consumed_keys |= (uint8_t)(Key_GetPressedMask() | mask);
        }
        Power_Touch(now);
        if (RTCClock_GetActiveAlarmMask()) {
            /* Single-key gestures while ringing neither dismiss nor edit UI. */
            consumed_keys |= (uint8_t)(Key_GetPressedMask() | mask);
        }
        /* A second key may finish debouncing one scan after the first. Consume
           that overlapping press too, so a two-key acknowledgement cannot
           confirm an edited value when the keys are released separately. */
        if (consumed_keys && event.type == KEY_EVENT_PRESS) { consumed_keys |= mask; }
        if (consumed_keys & mask) {
            if (event.type == KEY_EVENT_RELEASE) { consumed_keys &= (uint8_t)~mask; }
            continue;
        }
        if (boot_done) { HandleAction(UI_Key(&ui, event.key, event.type, APP_MENU_DIRECT_SELECT)); }
    }
}

static uint8_t WriteProtocol(const char *bytes, uint16_t length, void *context)
{
    return *(const uint8_t *)context ? Bluetooth_Write(bytes, length) : Serial_Write(bytes, length);
}

static void Decimal(char *out, size_t size, float value, uint8_t sign)
{
    (void)Protocol_FormatTenths(out, size, Protocol_FloatToTenths(value), sign);
}

static ProtocolHandlerResult Command(const ProtocolCommand *cmd, uint8_t first,
    uint32_t at, char *reply, size_t cap, void *context)
{
    RTCClockTime time;
    RTCClockAlarm alarm;
    RTCClockResult rtc_result;
    ControlResult result = CONTROL_OK;
    ControlSample sample;
    IMUSample imu_sample;
    const ControlSnapshot *s = Control_GetSnapshot(&control);
    char x[18], y[18], z[18];
    size_t used;
    unsigned int i;
    uint8_t channel_mask = (uint8_t)(1u << *(const uint8_t *)context);
    uint8_t gyro_register;
    bool gyro_verified;
    switch (cmd->type) {
    case PROTOCOL_CMD_HELP:
        (void)snprintf(reply, cap,
            "HELP/help\r\nTIME(hh:mm:ss)\r\nALARMn(hh:mm:ss) n=1..3\r\n"
            "ALARMn OFF|DEL\r\nALARM ?\r\nIMU ?\r\nSERVO ?\r\n"
            "SERVO SET <0..180>\r\nSERVO HOLD ON|OFF|?\r\nSERVO TRACE ON|OFF|?\r\nIMU RAW ?\r\nIMU FAULT ?\r\n");
        break;
    case PROTOCOL_CMD_TIME_SET:
        if (first) {
            if (ui_time_pending) { (void)snprintf(reply, cap, "ERR BUSY\r\n"); break; }
            time.hour = cmd->hour; time.minute = cmd->minute; time.second = cmd->second;
            rtc_result = RTCClock_SetTime(&time);
            if (rtc_result != RTC_CLOCK_OK) {
                (void)snprintf(reply, cap, "ERR %s\r\n", RTCResultName(rtc_result)); break;
            }
            return PROTOCOL_WAIT;
        }
        if (RTCClock_GetStatus() == RTC_CLOCK_UPDATING_TIME) { return PROTOCOL_WAIT; }
        if (RTCClock_GetStatus() != RTC_CLOCK_READY) {
            (void)snprintf(reply, cap, "ERR RTC WRITE\r\n"); break;
        }
        (void)snprintf(reply, cap, "OK TIME=%02u:%02u:%02u\r\n",
                       cmd->hour, cmd->minute, cmd->second);
        if (ui.page == UI_TIME) { EnterPage(); }
        break;
    case PROTOCOL_CMD_ALARM_SET: case PROTOCOL_CMD_ALARM_OFF: case PROTOCOL_CMD_ALARM_DELETE:
        time.hour = cmd->hour; time.minute = cmd->minute; time.second = cmd->second;
        if (cmd->type == PROTOCOL_CMD_ALARM_SET) {
            rtc_result = RTCClock_SetAlarm(cmd->alarm_number, &time, 1);
        } else if (cmd->type == PROTOCOL_CMD_ALARM_OFF) {
            rtc_result = RTCClock_EnableAlarm(cmd->alarm_number, 0);
        } else { rtc_result = RTCClock_DeleteAlarm(cmd->alarm_number); }
        if (rtc_result == RTC_CLOCK_OK) {
            (void)RTCClock_GetAlarm(cmd->alarm_number, &alarm);
            (void)snprintf(reply, cap, "OK ALARM%u=%02u:%02u:%02u %s\r\n", cmd->alarm_number,
                alarm.time.hour, alarm.time.minute, alarm.time.second, alarm.enabled ? "ON" : "OFF");
        } else { (void)snprintf(reply, cap, "ERR %s\r\n", RTCResultName(rtc_result)); }
        if (rtc_result == RTC_CLOCK_OK && ui.page == UI_ALARM) { EnterPage(); }
        break;
    case PROTOCOL_CMD_ALARM_QUERY:
        if (RTCClock_GetStatus() != RTC_CLOCK_READY) {
            (void)snprintf(reply, cap, "ERR RTC NOT READY\r\n"); break;
        }
        used = 0;
        for (i = 1; i <= RTC_LOGICAL_ALARM_COUNT; ++i) {
            (void)RTCClock_GetAlarm((uint8_t)i, &alarm);
            used += (size_t)snprintf(reply + used, cap - used, "ALARM%u=%02u:%02u:%02u %s\r\n",
                i, alarm.time.hour, alarm.time.minute, alarm.time.second, alarm.enabled ? "ON" : "OFF");
        }
        break;
    case PROTOCOL_CMD_IMU_QUERY:
        if (!IMU_GetSample(&imu_sample) || !imu_sample.valid || !imu_sample.calibrated ||
            (uint32_t)(at - imu_sample.timestamp_ms) > 100u) {
            (void)snprintf(reply, cap, "ERR IMU NOT READY\r\n"); break;
        }
        Decimal(x, sizeof(x), imu_sample.roll_deg, 1);
        Decimal(y, sizeof(y), imu_sample.pitch_deg, 1);
        Decimal(z, sizeof(z), imu_sample.yaw_deg, 1);
        (void)snprintf(reply, cap, "X=%s,Y=%s,Z=%s\r\n", x, y, z);
        break;
    case PROTOCOL_CMD_IMU_RAW_QUERY:
        (void)IMU_GetSample(&imu_sample); /* Diagnostic retains raw data even when invalid/uncalibrated. */
        gyro_verified = IMU_GetGyroConfig(&gyro_register);
        Decimal(x, sizeof(x), imu_sample.gyro_x_dps, 1);
        Decimal(y, sizeof(y), imu_sample.gyro_y_dps, 1);
        Decimal(z, sizeof(z), imu_sample.gyro_z_dps, 1);
        (void)snprintf(reply, cap,
            "RAW AX=%+ld AY=%+ld AZ=%+ld mg GX=%s GY=%s GZ=%s VALID=%u CAL=%u COUNT=%u AGE=%lu REASON=%s MODE=%s REFAGE=%lu BRIDGE=%lu SETTLE=%u BUDGET=%u RANGE=%u CFG=%02X CFGOK=%u\r\n",
            (long)Protocol_FloatToTenths(imu_sample.accel_x_g * 100.0f),
            (long)Protocol_FloatToTenths(imu_sample.accel_y_g * 100.0f),
            (long)Protocol_FloatToTenths(imu_sample.accel_z_g * 100.0f), x, y, z,
            (unsigned int)imu_sample.valid, (unsigned int)imu_sample.calibrated,
            (unsigned int)IMU_GetCalibrationCount(), (unsigned long)(at - imu_sample.timestamp_ms),
            IMU_GetFaultReason()[0] ? IMU_GetFaultReason() : "NONE",
            !imu_sample.valid ? "INVALID" : (imu_sample.gyro_only ? "GYRO" : "ACC"),
            (unsigned long)imu_sample.gravity_age_ms, (unsigned long)imu_sample.gyro_bridge_count,
            (unsigned int)(imu_sample.valid && imu_sample.settling_fast),
            (unsigned int)IMU_GYRO_BRIDGE_MS, (unsigned int)IMU_GYRO_RANGE_DPS,
            (unsigned int)gyro_register, (unsigned int)gyro_verified);
        break;
    case PROTOCOL_CMD_IMU_FAULT_QUERY:
        if (!hold_fault.captured) {
            (void)snprintf(reply, cap, "LAST_FAULT=NONE\r\n"); break;
        }
        Decimal(x, sizeof(x), hold_fault.imu.gyro_x_dps, 1);
        Decimal(y, sizeof(y), hold_fault.imu.gyro_y_dps, 1);
        Decimal(z, sizeof(z), hold_fault.imu.gyro_z_dps, 1);
        (void)snprintf(reply, cap,
            "LAST_FAULT=%s AT=%lu SRC=\"%s\" AGE=%lu VALID=%u CAL=%u COUNT=%u "
            "AX=%+ld AY=%+ld AZ=%+ld mg OUT=%uus REFAGE=%lu GYROCLIP=%u "
            "GX=%s GY=%s GZ=%s dps GRX=%d GRY=%d GRZ=%d RANGE=%u CFG=%02X CFGOK=%u\r\n",
            Control_FaultName(hold_fault.fault), (unsigned long)hold_fault.at_ms,
            hold_fault.source, (unsigned long)hold_fault.age_ms,
            (unsigned int)hold_fault.imu.valid, (unsigned int)hold_fault.imu.calibrated,
            (unsigned int)hold_fault.calibration_count,
            (long)Protocol_FloatToTenths(hold_fault.imu.accel_x_g * 100.0f),
            (long)Protocol_FloatToTenths(hold_fault.imu.accel_y_g * 100.0f),
            (long)Protocol_FloatToTenths(hold_fault.imu.accel_z_g * 100.0f), hold_fault.pulse_us,
            (unsigned long)hold_fault.imu.gravity_age_ms, (unsigned int)hold_fault.imu.gyro_saturated,
            x, y, z, (int)hold_fault.imu.gyro_raw[0], (int)hold_fault.imu.gyro_raw[1],
            (int)hold_fault.imu.gyro_raw[2], (unsigned int)IMU_GYRO_RANGE_DPS,
            (unsigned int)hold_fault.gyro_config, (unsigned int)hold_fault.gyro_config_verified);
        break;
    case PROTOCOL_CMD_SERVO_SET:
        result = Control_SetManualAngle(&control, cmd->angle_deg);
        if (result == CONTROL_OK) {
            SyncServoEdit();
            (void)snprintf(reply, cap, "OK SERVO=%d HOLD=OFF\r\n", cmd->angle_deg);
        } else { (void)snprintf(reply, cap, "ERR %s\r\n", ControlResultName(result)); }
        break;
    case PROTOCOL_CMD_SERVO_QUERY:
        Decimal(x, sizeof(x), s->output_angle_deg, 0);
        (void)snprintf(reply, cap, "SERVO=%s HOLD=%s PULSE=%uus\r\n",
            x, s->hold_enabled ? "ON" : "OFF", s->pulse_us);
        break;
    case PROTOCOL_CMD_HOLD_ON:
        sample = Sample();
        result = Control_StartHold(&control, &sample, at);
        if (result == CONTROL_OK) { SyncServoEdit(); }
        if (result == CONTROL_OK) { (void)snprintf(reply, cap, "OK SERVO HOLD=ON\r\n"); }
        else { (void)snprintf(reply, cap, "ERR %s\r\n", ControlResultName(result)); }
        break;
    case PROTOCOL_CMD_HOLD_OFF:
        Control_StopHold(&control); SyncServoEdit();
        (void)snprintf(reply, cap, "OK SERVO HOLD=OFF\r\n"); break;
    case PROTOCOL_CMD_HOLD_QUERY:
        Decimal(x, sizeof(x), s->target_deg, 1); Decimal(y, sizeof(y), s->measured_deg, 1);
        (void)snprintf(reply, cap, "SERVO HOLD=%s FAULT=%s target=%s measured=%s\r\n",
            s->hold_enabled ? "ON" : "OFF", s->fault == CONTROL_FAULT_NONE ? "OFF" : Control_FaultName(s->fault), x, y); break;
    case PROTOCOL_CMD_TRACE_ON:
        trace_on |= channel_mask;
        if (channel_mask == 1u) { last_trace = at; } else { last_bluetooth_trace = at; }
        (void)snprintf(reply, cap, "H,ms,target,measured,error,P,I,D,servo,saturated,fault\r\nOK SERVO TRACE=ON\r\n"); break;
    case PROTOCOL_CMD_TRACE_OFF:
        trace_on &= (uint8_t)~channel_mask; (void)snprintf(reply, cap, "OK SERVO TRACE=OFF\r\n"); break;
    case PROTOCOL_CMD_TRACE_QUERY:
        (void)snprintf(reply, cap, "SERVO TRACE=%s\r\n", (trace_on & channel_mask) ? "ON" : "OFF"); break;
    default: (void)snprintf(reply, cap, "ERR COMMAND\r\n"); break;
    }
    return PROTOCOL_DONE;
}

static void Trace(void)
{
    const ControlSnapshot *s = Control_GetSnapshot(&control);
    char record[192], values[7][18];
    float numeric[7];
    unsigned int i;
    int length;
    /* Cached command replies get TX capacity first; a partial input line does
       not stop the trace, and complete records are queued atomically. */
    uint8_t wired_due = (uint8_t)((trace_on & 1u) && !protocol.reply_ready &&
        !protocol.queue_count && (uint32_t)(now - last_trace) >= APP_TRACE_PERIOD_MS);
    uint8_t bluetooth_due = (uint8_t)((trace_on & 2u) && !bluetooth_protocol.reply_ready &&
        !bluetooth_protocol.queue_count &&
        (uint32_t)(now - last_bluetooth_trace) >= APP_BLUETOOTH_TRACE_PERIOD_MS);
    if (!wired_due && !bluetooth_due) { return; }
    numeric[0] = s->target_deg; numeric[1] = s->measured_deg; numeric[2] = s->error_deg;
    numeric[3] = s->p_term; numeric[4] = s->i_term; numeric[5] = s->d_term;
    numeric[6] = s->output_angle_deg;
    for (i = 0; i < 7; ++i) { Decimal(values[i], sizeof(values[i]), numeric[i], 0); }
    length = snprintf(record, sizeof(record), "H,%lu,%s,%s,%s,%s,%s,%s,%s,%u,%s\r\n",
        (unsigned long)now, values[0], values[1], values[2], values[3], values[4], values[5],
        values[6], s->saturated, Control_FaultName(s->fault));
    if (length > 0 && (size_t)length < sizeof(record)) {
        if (wired_due) { last_trace = now; (void)Serial_Write(record, (uint16_t)length); }
        if (bluetooth_due) { last_bluetooth_trace = now; (void)Bluetooth_Write(record, (uint16_t)length); }
    }
}

static void Text(int16_t x, int16_t y, const char *format, ...)
{
    char buffer[40];
    va_list args;
    va_start(args, format);
    (void)vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    OLED_DrawText(x, y, buffer);
}

static void Degree(int16_t x, int16_t y)
{
    OLED_DrawRect(x, y, 3, 3);
}

static void RenderIMU(void)
{
    IMUSample sample;
    char a[18], b[18], c[18];
    if (!IMU_GetSample(&sample) || !sample.valid || (uint32_t)(now - sample.timestamp_ms) > 100u) {
        Text(0, 15, "GX --"); Text(0, 25, "GY --"); Text(0, 35, "GZ --");
        Degree(98, 15); Text(103, 15, "/s");
        Degree(98, 25); Text(103, 25, "/s");
        Degree(98, 35); Text(103, 35, "/s");
        Text(0, 47, "%s A=%02X", IMU_GetStatus() == IMU_FAULT ? "FAULT" : "STARTING",
             (unsigned int)IMU_GetAddress());
        Text(0, 56, "%s", IMU_GetFaultReason()[0] ? IMU_GetFaultReason() : "WAITING FOR SAMPLE");
        return;
    }
    Decimal(a, sizeof(a), sample.gyro_x_dps, 1);
    Decimal(b, sizeof(b), sample.gyro_y_dps, 1);
    Decimal(c, sizeof(c), sample.gyro_z_dps, 1);
    Text(0, 15, "GX %s", a); Degree(98, 15); Text(103, 15, "/s");
    Text(0, 25, "GY %s", b); Degree(98, 25); Text(103, 25, "/s");
    Text(0, 35, "GZ %s", c); Degree(98, 35); Text(103, 35, "/s");
    if (!sample.calibrated) {
        Text(0, 47, "CAL %u/%u", (unsigned int)IMU_GetCalibrationCount(),
             (unsigned int)IMU_CALIBRATION_SAMPLES);
        Text(0, 56, "%s", IMU_GetFaultReason()[0] ? IMU_GetFaultReason() : "KEEP IMU STILL");
    }
    else {
        Decimal(a, sizeof(a), sample.roll_deg, 1); Decimal(b, sizeof(b), sample.pitch_deg, 1);
        Decimal(c, sizeof(c), sample.yaw_deg, 1);
        Text(0, 47, "X%s Y%s", a, b); Text(0, 56, "Z%s", c);
    }
}

static void Render(void)
{
    RTCClockTime time;
    const ControlSnapshot *s = Control_GetSnapshot(&control);
    char a[18], b[18];
    static const char *items[] = { "TIME", "ALARM", "IMU", "SERVO" };
    unsigned int i;
    if (!OLED_IsReady() || !OLED_IsPowered()) { return; }
    OLED_Clear();
    if (!boot_done) {
        uint32_t progress = (uint32_t)(now - boot_start);
        if (progress > APP_BOOT_ANIMATION_MS) { progress = APP_BOOT_ANIMATION_MS; }
        Text(16, 12, "e-XIN CLOCK"); Text(22, 28, "STARTING...");
        OLED_DrawRect(8, 46, 112, 10);
        OLED_FillRect(10, 48, (int16_t)(108u * progress / APP_BOOT_ANIMATION_MS), 6, true);
        OLED_Present(); return;
    }
    if (RTCClock_GetTime(&time) == RTC_CLOCK_OK) {
        Text(0, 0, "%02u:%02u:%02u", time.hour, time.minute, time.second);
    } else { Text(0, 0, "--:--:--"); }
    Text(60, 0, "%s", ui.page == UI_MAIN ? "MENU" : items[(unsigned int)ui.page - 1u]);
    OLED_FillRect(0, 10, 128, 1, true);
    switch (ui.page) {
    case UI_MAIN:
        for (i = 0; i < 4; ++i) {
            Text(16, (int16_t)(15u + i * 12u), "%s", items[i]);
            if (ui.menu_selection == i) { OLED_DrawRect(0, (int16_t)(13u + i * 12u), 128, 11); Text(3, (int16_t)(15u + i * 12u), ">"); }
        }
        break;
    case UI_TIME:
        Text(0, 18, "SET %02u:%02u:%02u", ui.time_edit.hour, ui.time_edit.minute, ui.time_edit.second);
        OLED_FillRect((int16_t)(24u + ui.parameter * 18u), 28, 12, 1, true);
        Text(0, 36, "K2: NEXT H/M/S"); Text(0, 49, "HOLD K2: SAVE"); break;
    case UI_ALARM:
        Text(0, 16, "A%u %02u:%02u:%02u %s", ui.alarm_number, ui.alarm_edit.hour,
             ui.alarm_edit.minute, ui.alarm_edit.second, ui.alarm_enabled ? "ON" : "OFF");
        OLED_FillRect(ui.parameter == 3 ? 72 : (int16_t)(18u + ui.parameter * 18u), 26,
                      ui.parameter == 3 ? 18 : 12, 1, true);
        Text(0, 35, "K2: SELECT / SAVE"); Text(0, 48, "HOLD K1: A1/A2/A3"); break;
    case UI_IMU: RenderIMU(); break;
    case UI_SERVO:
        Text(0, 14, "%s %s", s->hold_enabled ?
            (APP_RATE_PID_TEST ? (APP_HOLD_DEADBAND_TRANSITION_DEG > 0 ? (IMU_GYRO_BRIDGE_MS == 200u ? (IMU_GYRO_RANGE_DPS == 1000u ? "HOLD Y G1K" : "HOLD Y S200") : "HOLD Y SOFT") : (IMU_SETTLE_ENABLE ? "HOLD Y SETTLE" : "HOLD Y RATE")) : (APP_POSITION_PID_TEST ? "HOLD Y PIDR3" : (APP_CONTROL_MODE == CONTROL_MODE_DIRECT ? "HOLD Y DIRECT+" : (APP_HOLD_DIAGNOSTIC ? "HOLD Y TEST" : "HOLD Y")))) : "MANUAL", ui.servo_dirty ? "EDIT" : "");
        if (s->hold_enabled) {
            (void)Protocol_FormatTenths(a, sizeof(a), ui.attitude_edit_tenths, 1);
            Decimal(b, sizeof(b), s->measured_deg, 1);
            Text(0, 25, "T%s M%s", a, b);
        } else { Text(0, 25, "SET %d deg", ui.servo_edit_deg); }
        Decimal(a, sizeof(a), s->output_angle_deg, 0);
        Text(0, 36, "OUT %s %u us", a, s->pulse_us);
        Text(0, 47, "K2: OK LONG: HOLD");
        if (s->fault) { Text(0, 56, "%s", Control_FaultName(s->fault)); }
        break;
    default: break;
    }
    if (RTCClock_GetActiveAlarmMask()) {
        OLED_FillRect(0, 45, 128, 19, true);
        OLED_FillRect(1, 46, 126, 17, false);
        Text(4, 47, "ALARM! K1 + K2"); Text(4, 56, "BOTH TO STOP");
    } else if ((int32_t)(notice_until - now) > 0) {
        OLED_FillRect(0, 55, 128, 9, false); Text(0, 56, "%s", notice);
    }
    OLED_Present();
}

static uint8_t PrepareStandby(void)
{
    IMU_SetPaused(true); OLED_SetPower(false);
    if (!OLED_IsReady() || OLED_IsPowered() || I2CBus_IsBusy()) { return 0; }
    standby_bus_suspended = (uint8_t)I2CBus_Suspend();
    return standby_bus_suspended;
}
static void CancelStandby(void)
{
    if (standby_bus_suspended) { I2CBus_Resume(); standby_bus_suspended = 0; }
    IMU_SetPaused(false); OLED_SetPower(true);
}
static uint8_t StandbyGuard(void)
{
    const ControlSnapshot *s = Control_GetSnapshot(&control);
    return (uint8_t)(Serial_IsIdle() && Bluetooth_IsIdle() && Protocol_IsIdle(&protocol) &&
        Protocol_IsIdle(&bluetooth_protocol) && !trace_on &&
        !s->hold_enabled && !Key_GetPressedMask() &&
        GPIO_ReadInputDataBit(KEY1_GPIO_PORT, KEY1_GPIO_PIN) == Bit_SET &&
        GPIO_ReadInputDataBit(KEY2_GPIO_PORT, KEY2_GPIO_PIN) == Bit_SET &&
        !(USART1->SR & (USART_SR_RXNE | USART_SR_ORE | USART_SR_FE | USART_SR_NE)) &&
        !(USART3->SR & (USART_SR_RXNE | USART_SR_ORE | USART_SR_FE | USART_SR_NE)) &&
        Encoder_GetCounter() == last_encoder && !I2CBus_IsBusy());
}

void App_Init(uint32_t at)
{
    ControlConfig config;
    now = at; init_time = at;
    last_display = at - APP_DISPLAY_PERIOD_MS; last_trace = boot_start = at;
    notice_until = at; notice[0] = '\0';
    boot_started = boot_done = trace_on = ui_time_pending = 0;
    consumed_keys = alarm_previous = sleep_previous = restore_checked = standby_bus_suspended = 0;
    memset(&hold_fault, 0, sizeof(hold_fault));
    UI_Init(&ui); Control_DefaultConfig(&config);
    config.mode = APP_CONTROL_MODE;
    config.direct_position_gain = APP_DIRECT_POSITION_GAIN;
    config.actuator_time_constant_seconds = APP_ACTUATOR_TIME_CONSTANT_SECONDS;
    config.direct_rate_gain_seconds = APP_DIRECT_RATE_GAIN_SECONDS;
    config.direct_rate_limit_deg = APP_DIRECT_RATE_LIMIT_DEG;
    config.axis = APP_HOLD_AXIS;
    config.direction = APP_HOLD_DIRECTION;
    config.target_min_deg = 0.0f;
    config.target_limit_deg = 180.0f;
    config.integral_limit_deg = 180.0f;
    config.kp = APP_HOLD_KP; config.ki = APP_HOLD_KI; config.kd = APP_HOLD_KD;
    config.integral_error_scale_deg = APP_HOLD_INTEGRAL_ERROR_SCALE_DEG;
    config.integral_min_ki = config.ki > 0.0f ? APP_HOLD_INTEGRAL_MIN_KI : 0.0f;
    config.deadband_deg = APP_HOLD_DEADBAND_DEG;
    config.deadband_transition_deg = APP_HOLD_DEADBAND_TRANSITION_DEG;
    config.hold_output_span_deg = APP_HOLD_OUTPUT_SPAN_DEG;
    config.slew_deg_per_second = APP_HOLD_SLEW_DEG_PER_SECOND;
#if APP_POSITION_PID_TEST
    config.kp = APP_POSITION_PID_KP;
    config.ki = APP_POSITION_PID_KI;
    config.kd = APP_POSITION_PID_KD;
    config.integral_min_ki = 0.0f;
    config.integral_error_scale_deg = 0.0f;
#endif
#if APP_RATE_PID_TEST
    config.ki = APP_RATE_PID_KI;
    config.kd = APP_RATE_PID_KD;
    config.use_rate_sample = (APP_HOLD_AXIS == CONTROL_AXIS_PITCH);
    config.rate_filter_seconds = APP_RATE_FILTER_SECONDS;
    config.rate_term_limit_deg = APP_RATE_TERM_LIMIT_DEG;
    config.rate_quiet_dps = APP_RATE_QUIET_DPS;
#endif
    Control_Init(&control, &config, at);
    Protocol_Init(&protocol, Command, WriteProtocol, (void *)&wired_channel);
    Protocol_Init(&bluetooth_protocol, Command, WriteProtocol, (void *)&bluetooth_channel);
    protocol_turn = 0; last_bluetooth_trace = at;
    Power_Init(at); Power_SetIdleTimeout(APP_IDLE_TIMEOUT_MS);
    Power_SetStandbyHooks(PrepareStandby, CancelStandby);
    Power_SetStandbyGuard(StandbyGuard);
    RTCClock_Init(at); I2CBus_Init(); IMU_Init(at); OLED_Init(at);
    LEDPWM_BreathStart(at); last_encoder = Encoder_GetCounter();
}

void App_Process(uint32_t at)
{
    uint8_t byte, alarm;
    uint16_t encoder;
    int32_t steps;
    uint32_t busy = 0;
    unsigned int limit;
    ControlSample sample;
    const ControlSnapshot *s;
    now = at;
    RTCClock_Process(at); PWM_Process(at);
    I2CBus_Process(at); IMU_Process(at); OLED_Process(at);
    if (!boot_started && OLED_IsReady()) { boot_start = at; boot_started = 1; }
    if (!boot_done && ((boot_started && (uint32_t)(at - boot_start) >= APP_BOOT_ANIMATION_MS) ||
                      (!boot_started && (uint32_t)(at - init_time) >= 6000u))) { boot_done = 1; Power_Touch(at); }
    if (!restore_checked && RTCClock_GetStatus() == RTC_CLOCK_READY) {
        restore_checked = 1;
        if (RTCClock_WasStandbyResume()) {
            uint32_t data = RTCClock_GetResumeData();
            (void)Control_SetManualAngle(&control, (int16_t)(data & 255u));
            if (((data >> 8) & 255u) <= UI_SERVO) { ui.page = (UIPage)((data >> 8) & 255u); }
            EnterPage();
        }
    }
    alarm = RTCClock_GetActiveAlarmMask();
    if (alarm && !alarm_previous) { Power_Touch(at); }
    alarm_previous = alarm;
    Keys();
    steps = Encoder_ReadDeltaSteps(); encoder = Encoder_GetCounter();
    if (encoder != last_encoder) { last_encoder = encoder; Power_Touch(at); }
    if (boot_done) { UI_Rotate(&ui, steps, Control_GetSnapshot(&control)->hold_enabled); }
    for (limit = 0; limit < 64u && Serial_ReadByte(&byte); ++limit) {
        Power_Touch(at); Protocol_FeedByte(&protocol, byte);
    }
    for (limit = 0; limit < 64u && Bluetooth_ReadByte(&byte); ++limit) {
        Power_Touch(at); Protocol_FeedByte(&bluetooth_protocol, byte);
    }
    /* One asynchronous RTC write owns the handler until it finishes. Alternate
       initial dispatch so continuous commands cannot starve the other port. */
    if (Protocol_IsWaiting(&protocol)) { Protocol_Process(&protocol, at); }
    else if (Protocol_IsWaiting(&bluetooth_protocol)) { Protocol_Process(&bluetooth_protocol, at); }
    else if (protocol_turn) {
        Protocol_Process(&bluetooth_protocol, at);
        if (!Protocol_IsWaiting(&bluetooth_protocol)) { Protocol_Process(&protocol, at); }
    } else {
        Protocol_Process(&protocol, at);
        if (!Protocol_IsWaiting(&protocol)) { Protocol_Process(&bluetooth_protocol, at); }
    }
    protocol_turn ^= 1u;
    if (ui_time_pending && RTCClock_GetStatus() != RTC_CLOCK_UPDATING_TIME) {
        ui_time_pending = 0;
        Notice(RTCClock_GetStatus() == RTC_CLOCK_READY ? "TIME SAVED" : "RTC WRITE ERROR");
    }
    sample = Sample(); ServoPWM_SetPulseUs(Control_Update(&control, &sample, at));
    s = Control_GetSnapshot(&control);
    RecordHoldFault(s);
    Trace();
    if (!boot_done) { busy |= POWER_BUSY_STARTUP; }
    if (RTCClock_GetActiveAlarmMask()) { busy |= POWER_BUSY_ALARM; }
    if (Key_GetPressedMask()) {
        /* The scan ISR can publish PRESS after Keys has drained its queue.
           Remember that key before Power_Process clears idle for busy input. */
        if (Power_IsIdle()) { consumed_keys |= Key_GetPressedMask(); }
        busy |= POWER_BUSY_INPUT;
    }
    if (ui_time_pending || RTCClock_GetStatus() == RTC_CLOCK_UPDATING_TIME) { busy |= POWER_BUSY_SERIAL; }
    if (Power_GetMode() == POWER_MODE_STANDBY) {
        if (s->hold_enabled) { busy |= POWER_BUSY_HOLD; }
        if (trace_on) { busy |= POWER_BUSY_TRACE; }
        if (!Serial_IsIdle() || !Bluetooth_IsIdle() || !Protocol_IsIdle(&protocol) ||
            !Protocol_IsIdle(&bluetooth_protocol)) { busy |= POWER_BUSY_SERIAL; }
        if (!OLED_IsReady()) { busy |= POWER_BUSY_DISPLAY; }
    }
    Power_SetBackupData((uint32_t)(s->output_angle_deg + 0.5f) | ((uint32_t)ui.page << 8));
    Power_Process(at, busy);
    if (Power_GetMode() == POWER_MODE_SLEEP && sleep_previous != Power_IsIdle()) {
        sleep_previous = Power_IsIdle(); OLED_SetPower(!sleep_previous);
        if (!sleep_previous) { last_display = at - APP_DISPLAY_PERIOD_MS; }
    }
    if (!Power_IsIdle() && (uint32_t)(at - last_display) >= APP_DISPLAY_PERIOD_MS) {
        last_display = at; Render();
    }
}

uint8_t App_CanSleep(void)
{
    /* The I2C state machine needs prompt main-loop polling between bus events. */
    return (uint8_t)(Power_IsIdle() && !I2CBus_IsBusy() && !OLED_IsBusy() &&
                     Serial_IsIdle() && Bluetooth_IsIdle() && !protocol.queue_count && !protocol.reply_ready &&
                     !bluetooth_protocol.queue_count && !bluetooth_protocol.reply_ready);
}
