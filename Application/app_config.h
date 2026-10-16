#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#define APP_IDLE_TIMEOUT_MS 60000u
#define APP_DISPLAY_PERIOD_MS 100u
#define APP_BOOT_ANIMATION_MS 1200u
/* Diagnostic: 50 Hz wired TRACE resolves the fast manual step more closely.
   Control period, servo PWM frequency and Bluetooth TRACE are unchanged. */
#define APP_TRACE_PERIOD_MS 20u
#define APP_BLUETOOTH_TRACE_PERIOD_MS 200u
#define APP_MENU_DIRECT_SELECT 1
#define APP_HOLD_AXIS CONTROL_AXIS_PITCH
/* Measured mount: output90->100 makes calibrated Pitch0->-10deg. */
#define APP_HOLD_DIRECTION -1
#define APP_CONTROL_MODE CONTROL_MODE_PID
/* Independent small-disturbance candidate; legacy PI/DIRECT settings retained. */
#define APP_POSITION_PID_TEST 1
#define APP_POSITION_PID_KP 0.7f
#define APP_POSITION_PID_KI 6.0f
#define APP_POSITION_PID_KD 0.005f
#define APP_RATE_PID_TEST 1
#define APP_RATE_PID_KI 8.0f
#define APP_RATE_PID_KD 0.005f
#define APP_RATE_FILTER_SECONDS 0.0f
#define APP_RATE_TERM_LIMIT_DEG 0.5f
#define APP_RATE_QUIET_DPS 3.0f
#define APP_DIRECT_POSITION_GAIN 0.3f
#define APP_ACTUATOR_TIME_CONSTANT_SECONDS 0.10f
#define APP_DIRECT_RATE_GAIN_SECONDS 0.06f
#define APP_DIRECT_RATE_LIMIT_DEG 6.0f
#define APP_ATTITUDE_REFERENCE_DEG 90.0f
/* Exam mode: full travel PID. Optional commissioning preset stays disabled. */
#define APP_HOLD_DIAGNOSTIC 0
#define APP_HOLD_NORMAL_KP 1.0f
/* Ki_eff = min + (max-min) * e^2/(e^2+scale^2). */
#define APP_HOLD_NORMAL_KI 1.0f
#define APP_HOLD_INTEGRAL_MIN_KI 0.8f
#define APP_HOLD_INTEGRAL_ERROR_SCALE_DEG 4.0f
/* Keep derivative disabled during integral reintroduction. */
#define APP_HOLD_NORMAL_KD 0.0f
#if APP_HOLD_DIAGNOSTIC
#define APP_HOLD_KP 0.5f
#define APP_HOLD_KI 0.0f
#define APP_HOLD_KD 0.0f
#define APP_HOLD_OUTPUT_SPAN_DEG 15.0f
#else
#define APP_HOLD_KP APP_HOLD_NORMAL_KP
#define APP_HOLD_KI APP_HOLD_NORMAL_KI
#define APP_HOLD_KD APP_HOLD_NORMAL_KD
#define APP_HOLD_OUTPUT_SPAN_DEG 180.0f
#endif
#define APP_HOLD_DEADBAND_DEG 0.5f
#define APP_HOLD_SLEW_DEG_PER_SECOND 180.0f
/* Cold boot: 23:59:55, servo 90 degrees; power policy uses Sleep by default. */
#endif
