/* Actual application/UI/protocol/PID, with deterministic hardware doubles. */
#include "app.h"
#include "app_config.h"
#include "control.h"
#include "ui_core.h"
#include "key.h"
#include "key_config.h"
#include "encoder.h"
#include "pwm.h"
#include "rtc_clock.h"
#include "power.h"
#include "power_core.h"
#include "i2c_bus.h"
#include "oled.h"
#include "oled_gfx.h"
#include "imu.h"
#include "serial.h"
#include "bluetooth.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

GPIO_TypeDef app_gpioa;
USART_TypeDef app_usart1;
USART_TypeDef app_usart3;
static uint32_t at, rtc_base_ms, rtc_base, commit_ms, backup_data, pwm_calls;
static uint32_t display_ready_at = 100u;
static RTCClockTime requested;
static RTCClockStatus rtc_status;
static RTCClockModel rtc_model;
static PowerPolicy policy;
static PowerMode mode;
static uint8_t idle, pressed, blocked_tx;
static uint16_t encoder, pulse;
static int32_t delta;
static float pose;
static bool bus_busy, display_power, imu_failed;
static bool imu_uncalibrated;
static bool imu_invalid_data;
static float imu_ax, imu_ay, imu_az;
static uint32_t imu_age;
static uint8_t frame[1024];
static char screen[1024], rx[4096], tx[131072];
static size_t rx_read, rx_write, tx_length;
static char bt_rx[4096], bt_tx[131072];
static size_t bt_rx_read, bt_rx_write, bt_tx_length;
static uint8_t bt_blocked_tx;
static KeyEvent events[64];
static unsigned int event_read, event_write;

uint8_t GPIO_ReadInputDataBit(GPIO_TypeDef *port, uint16_t pin) { return (uint8_t)((port->IDR & pin) != 0); }
uint8_t Key_GetEvent(KeyEvent *event)
{
    if (event_read == event_write) return 0;
    *event = events[event_read++ % 64u]; return 1;
}
uint8_t Key_GetPressedMask(void) { return pressed; }
uint16_t Encoder_GetCounter(void) { return encoder; }
int32_t Encoder_ReadDeltaSteps(void) { int32_t result = delta; delta = 0; return result; }
void ServoPWM_SetPulseUs(uint16_t value) { pulse = value; ++pwm_calls; }
void LEDPWM_BreathStart(uint32_t ms) { (void)ms; }
void PWM_Process(uint32_t ms) { (void)ms; }

void RTCClock_Init(uint32_t ms)
{
    rtc_base_ms = ms; rtc_base = 86395u; rtc_status = RTC_CLOCK_READY;
    RTCCore_Init(&rtc_model, rtc_base);
}
static uint32_t Counter(void) { return rtc_base + (at - rtc_base_ms) / 1000u; }
void RTCClock_Process(uint32_t ms)
{
    if (rtc_status == RTC_CLOCK_UPDATING_TIME && (int32_t)(ms - commit_ms) >= 0) {
        rtc_base = RTCCore_TimeToSeconds(&requested); rtc_base_ms = ms;
        RTCCore_SetClock(&rtc_model, rtc_base); rtc_status = RTC_CLOCK_READY;
    }
    (void)RTCCore_Update(&rtc_model, Counter());
}
RTCClockStatus RTCClock_GetStatus(void) { return rtc_status; }
RTCClockResult RTCClock_GetTime(RTCClockTime *time)
{
    if (rtc_status != RTC_CLOCK_READY) return RTC_CLOCK_NOT_READY;
    RTCCore_SecondsToTime(Counter(), time); return RTC_CLOCK_OK;
}
RTCClockResult RTCClock_SetTime(const RTCClockTime *time)
{
    if (rtc_status != RTC_CLOCK_READY) return RTC_CLOCK_BUSY;
    requested = *time; commit_ms = at + 30u; rtc_status = RTC_CLOCK_UPDATING_TIME;
    return RTC_CLOCK_OK;
}
RTCClockResult RTCClock_GetAlarm(uint8_t number, RTCClockAlarm *alarm)
{
    if (number < 1u || number > 3u) return RTC_CLOCK_BAD_ARGUMENT;
    *alarm = rtc_model.alarms[number - 1u]; return RTC_CLOCK_OK;
}
RTCClockResult RTCClock_SetAlarm(uint8_t number, const RTCClockTime *time, uint8_t enabled)
{
    return RTCCore_SetAlarm(&rtc_model, number, time, enabled, Counter()) ? RTC_CLOCK_OK : RTC_CLOCK_BAD_ARGUMENT;
}
RTCClockResult RTCClock_EnableAlarm(uint8_t number, uint8_t enabled)
{
    return RTCCore_EnableAlarm(&rtc_model, number, enabled, Counter()) ? RTC_CLOCK_OK : RTC_CLOCK_BAD_ARGUMENT;
}
RTCClockResult RTCClock_DeleteAlarm(uint8_t number)
{
    return RTCCore_DeleteAlarm(&rtc_model, number) ? RTC_CLOCK_OK : RTC_CLOCK_BAD_ARGUMENT;
}
uint8_t RTCClock_GetActiveAlarmMask(void) { return rtc_model.active_mask; }
void RTCClock_AcknowledgeAlarm(void) { RTCCore_Acknowledge(&rtc_model); }
uint8_t RTCClock_WasStandbyResume(void) { return 0; }
uint32_t RTCClock_GetResumeData(void) { return 0; }

void Power_Init(uint32_t ms) { PowerPolicy_Init(&policy, ms, 60000u); idle = 0; mode = POWER_MODE_SLEEP; }
void Power_Touch(uint32_t ms) { PowerPolicy_Touch(&policy, ms); idle = 0; }
void Power_SetIdleTimeout(uint32_t ms) { policy.timeout_ms = ms; }
void Power_SetStandbyHooks(uint8_t (*prepare)(void), void (*cancel)(void)) { (void)prepare; (void)cancel; }
void Power_SetStandbyGuard(uint8_t (*guard)(void)) { (void)guard; }
PowerMode Power_GetMode(void) { return mode; }
uint8_t Power_IsIdle(void) { return idle; }
void Power_SetBackupData(uint32_t data) { backup_data = data; }
void Power_Process(uint32_t ms, uint32_t busy) { idle = PowerPolicy_ShouldIdle(&policy, ms, busy); }

void I2CBus_Init(void) { bus_busy = false; }
void I2CBus_Process(uint32_t ms) { (void)ms; }
bool I2CBus_IsBusy(void) { return bus_busy; }
bool I2CBus_Suspend(void) { return !bus_busy; }
void I2CBus_Resume(void) { }
void IMU_Init(uint32_t ms) { (void)ms; }
void IMU_Process(uint32_t ms) { (void)ms; }
bool IMU_GetSample(IMUSample *sample)
{
    memset(sample, 0, sizeof(*sample)); sample->valid = !imu_failed && !imu_invalid_data && imu_age <= 100u;
    sample->calibrated = !imu_failed && !imu_uncalibrated;
    sample->timestamp_ms = at - imu_age;
    sample->roll_deg = 25.0f; sample->pitch_deg = pose; /* Different axes catch a wrong selection. */
    sample->gyro_x_dps = 1.2f; sample->gyro_y_dps = -3.4f; sample->gyro_z_dps = 0.5f;
    sample->accel_x_g = imu_ax; sample->accel_y_g = imu_ay; sample->accel_z_g = imu_az;
    return sample->valid;
}
IMUStatus IMU_GetStatus(void) { return imu_failed ? IMU_FAULT : IMU_READY; }
uint8_t IMU_GetAddress(void) { return 0x68; }
const char *IMU_GetFaultReason(void) { return imu_failed ? "WHO NACK" : ""; }
uint16_t IMU_GetCalibrationCount(void) { return imu_uncalibrated ? 42u : 200u; }
bool IMU_RequestCalibration(void) { return true; }
void IMU_SetPaused(bool paused) { (void)paused; }
void OLED_Init(uint32_t ms) { (void)ms; display_power = true; }
void OLED_Process(uint32_t ms) { (void)ms; }
bool OLED_IsReady(void) { return at >= display_ready_at; }
bool OLED_IsBusy(void) { return false; }
bool OLED_IsPowered(void) { return display_power; }
void OLED_SetPower(bool on) { display_power = on; }
void OLED_Clear(void) { memset(frame, 0, sizeof(frame)); screen[0] = '\0'; }
void OLED_DrawText(int16_t x, int16_t y, const char *text)
{
    OLEDGfx_Text(frame, x, y, text);
    assert(strlen(screen) + strlen(text) + 2u < sizeof(screen));
    strcat(screen, text); strcat(screen, "\n");
}
void OLED_DrawPixel(int16_t x, int16_t y, bool on) { OLEDGfx_Pixel(frame, x, y, on); }
void OLED_DrawRect(int16_t x, int16_t y, int16_t w, int16_t h) { OLEDGfx_Rect(frame, x, y, w, h, false, true); }
void OLED_FillRect(int16_t x, int16_t y, int16_t w, int16_t h, bool on) { OLEDGfx_Rect(frame, x, y, w, h, true, on); }
void OLED_Present(void) { }

uint8_t Serial_ReadByte(uint8_t *byte)
{
    if (rx_read == rx_write) return 0;
    *byte = (uint8_t)rx[rx_read++]; return 1;
}
uint8_t Serial_Write(const char *bytes, uint16_t length)
{
    if (blocked_tx) return 0;
    assert(tx_length + length < sizeof(tx)); memcpy(tx + tx_length, bytes, length);
    tx_length += length; tx[tx_length] = '\0'; return 1;
}
uint8_t Serial_WriteString(const char *text) { return Serial_Write(text, (uint16_t)strlen(text)); }
uint8_t Serial_IsIdle(void) { return (uint8_t)(rx_read == rx_write && !blocked_tx); }

uint8_t Bluetooth_ReadByte(uint8_t *byte)
{
    if (bt_rx_read == bt_rx_write) return 0;
    *byte = (uint8_t)bt_rx[bt_rx_read++]; return 1;
}
uint8_t Bluetooth_Write(const char *bytes, uint16_t length)
{
    if (bt_blocked_tx) return 0;
    assert(bt_tx_length + length < sizeof(bt_tx)); memcpy(bt_tx + bt_tx_length, bytes, length);
    bt_tx_length += length; bt_tx[bt_tx_length] = '\0'; return 1;
}
uint8_t Bluetooth_IsIdle(void) { return (uint8_t)(bt_rx_read == bt_rx_write && !bt_blocked_tx); }

static void Step(void) { at += 5u; App_Process(at); }
static void Until(uint32_t end) { while (at < end) Step(); }
static void Feed(const char *text)
{
    size_t len = strlen(text);
    if (rx_read == rx_write) rx_read = rx_write = 0;
    assert(rx_write + len < sizeof(rx)); memcpy(rx + rx_write, text, len); rx_write += len;
}
static void BluetoothFeed(const char *text)
{
    size_t len = strlen(text);
    if (bt_rx_read == bt_rx_write) bt_rx_read = bt_rx_write = 0;
    assert(bt_rx_write + len < sizeof(bt_rx)); memcpy(bt_rx + bt_rx_write, text, len); bt_rx_write += len;
}
static void Key(KeyId key, uint8_t long_press)
{
    const KeyEventType types[] = { KEY_EVENT_PRESS, long_press ? KEY_EVENT_LONG : KEY_EVENT_SHORT, KEY_EVENT_RELEASE };
    unsigned int i;
    for (i = 0; i < 3u; ++i) {
        KeyEvent *event = &events[event_write++ % 64u];
        event->key = key; event->type = types[i]; event->timestamp_ms = at;
    }
    Step();
}
static void Edge(KeyId key, KeyEventType type, uint8_t down)
{
    KeyEvent *event = &events[event_write++ % 64u];
    event->key = key; event->type = type; event->timestamp_ms = at;
    pressed = down; Step();
}
static void Turn(int32_t steps) { encoder = (uint16_t)(encoder + steps * 4); delta = steps; Step(); }
static UIPage Page(void) { return (UIPage)((backup_data >> 8) & 255u); }
static void Reset(void)
{
    at = 0; display_ready_at = 100u; encoder = 0; delta = 0; pose = 0; pressed = blocked_tx = 0;
    imu_failed = false;
    imu_uncalibrated = false; imu_age = 0;
    imu_invalid_data = false; imu_ax = 0; imu_ay = -1; imu_az = 0;
    rx_read = rx_write = tx_length = event_read = event_write = 0; tx[0] = '\0';
    bt_rx_read = bt_rx_write = bt_tx_length = bt_blocked_tx = 0; bt_tx[0] = '\0'; app_usart3.SR = 0;
    app_gpioa.IDR = KEY1_GPIO_PIN | KEY2_GPIO_PIN; app_usart1.SR = 0; pwm_calls = 0;
    App_Init(0); Until(1400);
}
static void Picture(const char *name)
{
    char path[128]; FILE *file; unsigned int x, y;
    Until(at + 100u); (void)snprintf(path, sizeof(path), "build/tests/ui_%s.pbm", name);
    file = fopen(path, "wb"); assert(file != NULL);
    fprintf(file, "P1\n128 64\n");
    for (y = 0; y < 64u; ++y) {
        for (x = 0; x < 128u; ++x) fprintf(file, "%u ", (frame[(y / 8u) * 128u + x] >> (y % 8u)) & 1u);
        fputc('\n', file);
    }
    fclose(file);
}
static unsigned int TraceCount(void)
{
    const char *p = tx; unsigned int count = 0;
    while ((p = strstr(p, "\r\nH,")) != NULL) { ++count; p += 4; }
    return count;
}

int main(void)
{
    uint16_t retained; uint32_t touch, calls; unsigned int traces;
    RTCClockTime time; const char *t, *s, *a;
    Reset(); at = 0; display_ready_at = 5900u; App_Init(0);
    Until(6000); assert(strstr(screen, "STARTING..."));
    Until(7095); assert(strstr(screen, "STARTING..."));
    Until(7200); assert(strstr(screen, "MENU")); /* Late OLED startup still gets a full animation. */
    Reset(); assert(Page() == UI_MAIN && strstr(screen, "MENU")); Picture("menu");
    Key(KEY_ID_1, 0); assert(Page() == UI_TIME); Picture("time");
    Key(KEY_ID_1, 0); assert(Page() == UI_ALARM); Picture("alarm");
    Key(KEY_ID_1, 0); assert(Page() == UI_IMU); Picture("imu");
    imu_failed = true; Picture("imu_fault");
    assert(strstr(screen, "GX --") && strstr(screen, "GY --") && strstr(screen, "GZ --"));
    assert(strstr(screen, "FAULT A=68") && strstr(screen, "WHO NACK"));
    assert(!strstr(screen, "CHECK PB8/PB9"));
    Feed("IMU RAW ?\n"); Until(at + 50u);
    assert(strstr(tx, "RAW AX=") && strstr(tx, "VALID=0") && strstr(tx, "REASON=WHO NACK"));
    imu_failed = false; Until(at + 100u);
    assert(strstr(screen, "GX +1.2") && strstr(screen, "GY -3.4") && strstr(screen, "GZ +0.5"));
    Key(KEY_ID_1, 0); assert(Page() == UI_SERVO); Picture("servo");
    Key(KEY_ID_1, 0); assert(Page() == UI_MAIN);
    Turn(3); Key(KEY_ID_1, 0); assert(Page() == UI_SERVO); /* Advanced menu selection. */

    Reset(); Turn(3); Key(KEY_ID_1, 0);
    imu_uncalibrated = true; Key(KEY_ID_2, 1); Until(at + 100u);
    assert(strstr(screen, "CALIBRATE IMU FIRST") && strstr(screen, "MANUAL"));
    retained = pulse; Until(at + 100u); assert(pulse == retained);
    imu_uncalibrated = false; imu_age = 101u;
    Key(KEY_ID_2, 1); Until(at + 100u);
    assert(strstr(screen, "IMU SAMPLE STALE") && strstr(screen, "MANUAL"));
    imu_age = 0; Key(KEY_ID_2, 1); Until(at + 100u);
    assert(strstr(screen, "HOLD ON") && !strstr(screen, "FOLLOW"));
    Key(KEY_ID_2, 1); /* Stop before rendering the calibration progress page. */
    Key(KEY_ID_1, 0); Turn(-1); Key(KEY_ID_1, 0); assert(Page() == UI_IMU);
    imu_uncalibrated = true; Until(at + 100u);
    assert(strstr(screen, "CAL 42/200") && strstr(screen, "KEEP IMU STILL"));

    Reset(); blocked_tx = 1;
    Feed("TIME(12:34:56)\r\nSERVO SET 120\r\nALARM1(12:34:57)\r\n");
    Until(1500); assert(pulse == 1500u && tx_length == 0 && !rtc_model.alarms[0].enabled);
    blocked_tx = 0; Until(1600);
    t = strstr(tx, "OK TIME=12:34:56"); s = strstr(tx, "OK SERVO=120 HOLD=OFF");
    a = strstr(tx, "OK ALARM1=12:34:57 ON"); assert(t && s && a && t < s && s < a);
    assert(pulse == 1833u); Until(2500); assert(RTCClock_GetActiveAlarmMask() == 1u);
    Key(KEY_ID_1, 0); assert(!RTCClock_GetActiveAlarmMask() && Page() == UI_MAIN && rtc_model.alarms[0].enabled);
    rtc_model.active_mask = 1u; Key(KEY_ID_2, 1); assert(!RTCClock_GetActiveAlarmMask() && Page() == UI_MAIN);
    Key(KEY_ID_1, 0); assert(Page() == UI_TIME);
    assert(RTCClock_GetTime(&time) == RTC_CLOCK_OK);
    Turn(1); Key(KEY_ID_2, 1); Until(at + 50u);
    assert(RTCClock_GetTime(&time) == RTC_CLOCK_OK && time.hour == 13u);

    Reset(); Feed("SERVO SET 120\n"); Until(1450);
    Key(KEY_ID_1, 0); Key(KEY_ID_1, 0); Key(KEY_ID_1, 0); Key(KEY_ID_1, 0);
    Picture("servo_uart"); assert(strstr(screen, "SET 120"));
    retained = pulse; Turn(1); assert(pulse == retained); Key(KEY_ID_2, 0); assert(pulse > retained);
    retained = pulse; Key(KEY_ID_1, 0); Until(at + 100u); assert(pulse == retained);

    Reset(); Key(KEY_ID_1, 0); Key(KEY_ID_1, 0); Key(KEY_ID_1, 0); Key(KEY_ID_1, 0);
    retained = pulse; Turn(10); rtc_model.active_mask = 1u;
    Edge(KEY_ID_1, KEY_EVENT_PRESS, KEY1_MASK);
    Edge(KEY_ID_2, KEY_EVENT_PRESS, KEY1_MASK | KEY2_MASK);
    Edge(KEY_ID_1, KEY_EVENT_SHORT, KEY2_MASK); Edge(KEY_ID_1, KEY_EVENT_RELEASE, KEY2_MASK);
    Edge(KEY_ID_2, KEY_EVENT_SHORT, 0); Edge(KEY_ID_2, KEY_EVENT_RELEASE, 0);
    assert(Page() == UI_SERVO && pulse == retained); /* Chord debounce skew must not confirm draft. */

    Reset(); Until(61295); assert(!Power_IsIdle()); Step(); assert(Power_IsIdle() && !display_power && App_CanSleep());
    ++encoder; Step(); touch = at; assert(!Power_IsIdle() && display_power); /* Partial encoder edge counts as activity. */
    Until(touch + 59995u); assert(!Power_IsIdle()); Step(); assert(Power_IsIdle());
    Feed("X"); Step(); touch = at; assert(!Power_IsIdle());
    Until(touch + 60000u); assert(Power_IsIdle() && App_CanSleep()); /* Partial command retains RAM during Sleep. */
    pressed = KEY1_MASK; Step(); assert(!Power_IsIdle());
    Edge(KEY_ID_1, KEY_EVENT_PRESS, KEY1_MASK);
    Edge(KEY_ID_1, KEY_EVENT_SHORT, 0); Edge(KEY_ID_1, KEY_EVENT_RELEASE, 0);
    assert(Page() == UI_MAIN); /* A scan published after queue drain still only wakes. */
    Key(KEY_ID_1, 0); assert(Page() == UI_TIME);

    Reset(); Until(62000); assert(Power_IsIdle());
    Key(KEY_ID_1, 0); assert(!Power_IsIdle() && display_power && Page() == UI_MAIN);
    Key(KEY_ID_1, 0); assert(Page() == UI_TIME); /* Next complete press works normally. */

    Reset(); Turn(3); Key(KEY_ID_1, 0); Turn(10); retained = pulse;
    Until(at + 61000u); assert(Power_IsIdle() && Page() == UI_SERVO);
    Edge(KEY_ID_2, KEY_EVENT_PRESS, KEY2_MASK); assert(!Power_IsIdle() && display_power);
    Until(at + 1100u); Edge(KEY_ID_2, KEY_EVENT_LONG, KEY2_MASK);
    Edge(KEY_ID_2, KEY_EVENT_RELEASE, 0); Until(at + 100u);
    assert(pulse == retained && strstr(screen, "MANUAL")); /* Wake hold cannot start HOLD. */
    Key(KEY_ID_2, 0); assert(pulse > retained); /* Only a fresh press confirms the draft. */

    Until(at + 61000u); assert(Power_IsIdle()); retained = pulse; Turn(5);
    Until(at + 61000u); assert(Power_IsIdle());
    Edge(KEY_ID_1, KEY_EVENT_PRESS, KEY1_MASK);
    Edge(KEY_ID_2, KEY_EVENT_PRESS, KEY1_MASK | KEY2_MASK);
    Edge(KEY_ID_1, KEY_EVENT_SHORT, KEY2_MASK); Edge(KEY_ID_1, KEY_EVENT_RELEASE, KEY2_MASK);
    Edge(KEY_ID_2, KEY_EVENT_SHORT, 0); Edge(KEY_ID_2, KEY_EVENT_RELEASE, 0);
    assert(Page() == UI_SERVO && pulse == retained); /* Overlapping wake keys are both consumed. */

    Reset(); pose = 10.0f; Feed("SERVO HOLD ON\nSERVO TRACE ON\n"); Until(1500);
    assert(strstr(tx, "OK SERVO HOLD=ON") && strstr(tx, "H,ms,target,measured,error,P,I,D,servo,saturated,fault"));
    Feed("SERVO HOLD ?\n"); Until(at + 50u);
    assert(strstr(tx, "target=+100.0 measured=+100.0")); /* Pitch=10 plus vertical90, not Roll=25. */
    Feed("X"); Step(); traces = TraceCount(); Until(at + 100u); assert(TraceCount() >= traces + 2u);
    Until(62000); assert(Power_IsIdle() && !display_power); traces = TraceCount(); calls = pwm_calls;
    pose = 7.0f; Until(62400); assert(TraceCount() >= traces + 2u && pwm_calls > calls && pulse < 1500u);
    bus_busy = true; assert(!App_CanSleep()); bus_busy = false; assert(App_CanSleep());
    Reset(); Turn(3); Key(KEY_ID_1, 0); Key(KEY_ID_2, 1); Until(at + 100u);
    assert(strstr(screen, "HOLD Y") && strstr(screen, "T+90.0 M+90.0"));
    Turn(10); Feed("SERVO HOLD ?\n"); Until(at + 50u);
    assert(strstr(tx, "target=+90.0")); /* Editing must not change live target. */
    Key(KEY_ID_2, 0); Feed("SERVO HOLD ?\n"); Until(at + 50u);
    assert(strstr(tx, "target=+100.0"));
    Turn(500); Key(KEY_ID_2, 0); Feed("SERVO HOLD ?\n"); Until(at + 50u);
    assert(strstr(tx, "target=+180.0"));
    Turn(-500); Key(KEY_ID_2, 0); Feed("SERVO HOLD ?\n"); Until(at + 50u);
    assert(strstr(tx, "target=+0.0"));
    /* A large measured disturbance must use the application 180deg/s limit:
       3.6deg / 20ms = 40us per update. The old90deg/s preset gives20us. */
    Reset(); Feed("SERVO HOLD ON\n"); Step(); pose = -90.0f;
    Until(at + 20u); assert(pulse == 1460u);
    Until(at + 20u); assert(pulse == 1420u);
    /* Exercise the actual application configuration, not only core defaults:
       confirmed targets must be able to move beyond the diagnostic +/-15deg.
       The fixed sensor double deliberately leaves a persistent pose error. */
    Reset(); Turn(3); Key(KEY_ID_1, 0); Key(KEY_ID_2, 1);
    Turn(500); Key(KEY_ID_2, 0); Until(at + 2000u);
    assert(pulse < 1333u && pulse >= 500u); /* Negative mount: larger target needs smaller output. */
    Feed("SERVO HOLD ?\n"); Until(at + 50u);
    assert(strstr(tx, "target=+180.0") && strstr(tx, "FAULT=OFF"));
    Turn(-500); Key(KEY_ID_2, 0); Until(at + 6000u);
    assert(pulse > 1667u && pulse <= 2500u); /* Negative mount: smaller target needs larger output. */
    Feed("SERVO HOLD ?\n"); Until(at + 50u);
    assert(strstr(tx, "target=+0.0") && strstr(tx, "FAULT=OFF"));

    /* Capture the first bad frame and preserve it after the driver recovers.
       A later live valid sample must not clear the controller's fault/pulse. */
    {
        char expected_at[32];
        Reset(); Feed("IMU FAULT ?\n"); Until(at + 50u);
        assert(strcmp(tx, "LAST_FAULT=NONE\r\n") == 0);
        Feed("SERVO HOLD ON\n"); Until(at + 50u);
        imu_failed = true; Step(); retained = pulse;
        (void)snprintf(expected_at, sizeof(expected_at), "AT=%lu ", (unsigned long)at);
        imu_failed = false; pose = 40.6f; Until(at + 100u);
        assert(pulse == retained);
        tx_length = 0; tx[0] = '\0';
        Feed("SERVO HOLD ?\nIMU RAW ?\nIMU FAULT ?\n"); Until(at + 100u);
        assert(strstr(tx, "FAULT=IMU_INVALID") && strstr(tx, "measured=+130.6"));
        assert(strstr(tx, "VALID=1 CAL=1") && strstr(tx, "REASON=NONE"));
        assert(strstr(tx, "LAST_FAULT=IMU_INVALID") && strstr(tx, "SRC=\"WHO NACK\""));
        assert(strstr(tx, expected_at) && pulse == retained);
        BluetoothFeed("IMU FAULT ?\n"); Until(at + 50u);
        assert(strstr(bt_tx, "SRC=\"WHO NACK\""));
        Feed("SERVO HOLD OFF\n"); Until(at + 50u);
        tx_length = 0; tx[0] = '\0'; Feed("IMU FAULT ?\n"); Until(at + 50u);
        assert(strstr(tx, expected_at) && strstr(tx, "SRC=\"WHO NACK\""));
        Feed("SERVO HOLD ON\n"); Until(at + 50u);
        tx_length = 0; tx[0] = '\0'; Feed("IMU FAULT ?\n"); Until(at + 50u);
        assert(strcmp(tx, "LAST_FAULT=NONE\r\n") == 0);
    }
    Reset(); Feed("SERVO HOLD ON\n"); Until(at + 50u);
    imu_age = 101u; Step(); imu_age = 0;
    Feed("IMU FAULT ?\n"); Until(at + 50u);
    assert(strstr(tx, "SRC=\"SAMPLE_STALE\"") && strstr(tx, "AGE=101"));

    Reset(); Feed("SERVO HOLD ON\n"); Until(at + 50u);
    imu_invalid_data = true; imu_ay = 0; imu_az = 1; Step();
    imu_invalid_data = false; imu_ay = -1; imu_az = 0;
    Feed("IMU FAULT ?\n"); Until(at + 50u);
    assert(strstr(tx, "SRC=\"AXIS_PROJECTION\"") && strstr(tx, "AX=+0 AY=+0 AZ=+1000"));

    Reset(); Feed("SERVO HOLD ON\n"); Until(at + 50u);
    imu_invalid_data = true; imu_ay = 0; Step();
    Feed("IMU FAULT ?\n"); Until(at + 50u);
    assert(strstr(tx, "SRC=\"ACCEL_NORM\""));

    Reset(); Feed("SERVO HOLD ON\n"); Until(at + 50u);
    imu_uncalibrated = true; Step(); imu_uncalibrated = false;
    Feed("IMU FAULT ?\n"); Until(at + 50u);
    assert(strstr(tx, "SRC=\"UNCALIBRATED\""));

    Reset(); Feed("TIME(14:"); BluetoothFeed("HELP\r\n"); Until(at + 100u);
    assert(strstr(bt_tx, "TIME(hh:mm:ss)") && !tx_length);
    Feed("30:05)\r\n"); Until(at + 100u);
    assert(strstr(tx, "OK TIME=14:30:05") && !strstr(bt_tx, "OK TIME="));
    BluetoothFeed("SERVO SET 120\r\nIMU ?\r\nALARM2(18:00:00)\r\n"); Until(at + 100u);
    assert(pulse == 1833u && strstr(bt_tx, "OK SERVO=120") && strstr(bt_tx, "X=") &&
           strstr(bt_tx, "OK ALARM2=18:00:00 ON"));
    assert(!strstr(tx, "OK SERVO=120") && rtc_model.alarms[1].enabled);

    Reset(); bt_blocked_tx = 1; BluetoothFeed("TIME(08:00:00)\r\nSERVO SET 120\r\n"); Step();
    Feed("TIME(09:00:00)\r\nSERVO SET 100\r\n"); Until(at + 100u);
    assert(strstr(tx, "OK TIME=09:00:00") && strstr(tx, "OK SERVO=100") && !bt_tx_length);
    bt_blocked_tx = 0; Until(at + 100u);
    assert(strstr(bt_tx, "OK TIME=08:00:00") && strstr(bt_tx, "OK SERVO=120"));

    Reset(); BluetoothFeed("SERVO TRACE ON\r\n"); Feed("SERVO TRACE ?\r\n"); Until(at + 100u);
    assert(strstr(tx, "SERVO TRACE=OFF") && strstr(bt_tx, "OK SERVO TRACE=ON"));
    Feed("SERVO TRACE ON\r\n"); Until(at + 1000u);
    assert(TraceCount() >= 15u && strstr(bt_tx, "\r\nH,"));
    BluetoothFeed("SERVO TRACE OFF\r\n"); Until(at + 100u); traces = (unsigned int)bt_tx_length;
    Until(at + 300u); assert(bt_tx_length == traces); /* Bluetooth OFF does not stop wired TRACE. */
    assert(TraceCount() >= 20u);

    Reset(); Until(at + 61000u); assert(Power_IsIdle() && !display_power);
    BluetoothFeed("SERVO ?\r\n"); Until(at + 100u);
    assert(!Power_IsIdle() && display_power && strstr(bt_tx, "SERVO="));
    if (!APP_POSITION_PID_TEST && APP_CONTROL_MODE == CONTROL_MODE_PID && APP_HOLD_KI == 0.0f) {
        uint16_t expected;
        Reset(); Feed("SERVO HOLD ON\n"); Until(at + 50u);
        if (APP_HOLD_KD == 0.0f) {
            const float jumps[] = {1.0f, -1.0f, 0.0f};
            unsigned int index;
            for (index = 0; index < sizeof(jumps)/sizeof(jumps[0]); ++index) {
                pose = jumps[index]; Until(at + 40u);
                expected = (uint16_t)(500.0f + (90.0f - APP_HOLD_DIRECTION * APP_HOLD_KP * pose) *
                    (2000.0f / 180.0f) + 0.5f);
                assert(pulse == expected); /* No derivative kick/tail on +/-1deg sample steps. */
            }
        }
        pose = 5.0f; Until(at + 2000u);
        expected = (uint16_t)(500.0f + (90.0f - APP_HOLD_DIRECTION * APP_HOLD_KP * 5.0f) *
            (2000.0f / 180.0f) + 0.5f);
        assert(pulse == expected);
        Until(at + 10000u);
        assert(pulse == expected); /* Application really has I disabled: no slow drift. */
    }
    if (!APP_POSITION_PID_TEST && APP_CONTROL_MODE == CONTROL_MODE_PID && APP_HOLD_KI > 0.0f && APP_HOLD_KD == 0.0f) {
        uint16_t first;
        Reset(); Feed("SERVO HOLD ON\n"); Until(at + 50u);
        pose = 5.0f; Until(at + 2000u); first = pulse;
        Until(at + 10000u);
        assert(pulse > first + 20u && pulse < 2500u); /* Restored I acts on persistent error. */
    }
    if (APP_CONTROL_MODE == CONTROL_MODE_DIRECT) {
        Reset(); Turn(3); Key(KEY_ID_1, 0);
        Feed("SERVO HOLD ON\n"); Until(at + 100u);
        assert(strstr(screen, "HOLD Y DIRECT+"));
        pose = 5.0f; Until(at + 40u);
        assert(pulse > 1500u); /* Corrected physical direction. */
        retained = pulse;
        pose = 0.0f; Until(at + 2000u);
        assert(pulse == retained); /* On-target command retained despite model lag. */
        Feed("SERVO TRACE ON\n"); Until(at + 100u);
        assert(strstr(tx, "H,ms,target,measured,error,P,I,D,servo,saturated,fault"));
    }
    if (APP_POSITION_PID_TEST) {
        uint16_t first;
        Reset(); Turn(3); Key(KEY_ID_1, 0);
        Feed("SERVO HOLD ON\n"); Until(at + 100u);
        assert(strstr(screen, "HOLD Y PIDR"));
        pose = 2; Until(at + 40u); first = pulse;
        assert(first > 1515u && first < 1535u);
        Until(at + 500u);
        assert(pulse > first + 50u); /* The application really applies Ki=6. */
        Feed("SERVO HOLD ?\n"); Until(at + 20u);
        assert(strstr(tx, "HOLD=ON FAULT=OFF"));
    }
    puts("PASS: application/UI, 60s wake gestures, dual UART and TRACE; active control profile integration behavior");
    return 0;
}
