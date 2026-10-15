#include "rtc_clock.h"
#include "rtc_clock_config.h"
#include "stm32f10x_bkp.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_pwr.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_rtc.h"
#include "misc.h"

typedef enum {
    ST_LSE = 0, ST_SYNC, ST_BOOT_IRQ_OFF, ST_BOOT_PRESCALER,
    ST_BOOT_COUNTER, ST_BOOT_ALARM, ST_BOOT_IRQ_ON,
    ST_READY, ST_AVAILABLE, ST_TIME_WRITE, ST_ALARM_WRITE, ST_FAILED
} ClockState;

static ClockState state;
static RTCClockError error;
static RTCClockModel model;
static uint8_t initialized;
static uint32_t stage_started_ms;
static uint8_t time_requested;
static uint32_t requested_seconds;
static uint8_t hardware_alarm_known;
static uint8_t hardware_alarm_enabled;
static uint32_t hardware_alarm_counter;
static volatile uint8_t alarm_irq_pending;
static uint8_t led_was_active;
static uint32_t led_started_ms;
static uint8_t standby_resume;
static uint32_t resume_data;

static uint16_t BackupRegister(uint8_t index)
{
    return (uint16_t)(BKP_DR1 + (uint16_t)index * 4u);
}

static void LEDWrite(uint8_t on)
{
#if RTC_ALARM_LED_ACTIVE_LOW
    on = (uint8_t)!on;
#endif
    GPIO_WriteBit(RTC_ALARM_LED_PORT, RTC_ALARM_LED_PIN, on ? Bit_SET : Bit_RESET);
}

static void UpdateLED(uint32_t now_ms)
{
    if (model.active_mask == 0) {
        led_was_active = 0;
        LEDWrite(0);
        return;
    }
    if (!led_was_active) {
        led_was_active = 1;
        led_started_ms = now_ms;
    }
    LEDWrite((uint8_t)(((uint32_t)(now_ms - led_started_ms) / 500u) % 2u == 0u));
}

static uint32_t ReadCounter(void)
{
    uint32_t high = RTC->CNTH & 0xffffu;
    uint32_t low = RTC->CNTL & 0xffffu;
    uint32_t high_after = RTC->CNTH & 0xffffu;
    /* Avoid a torn 32-bit read when the low half rolls between APB reads. */
    if (high != high_after) {
        high = high_after;
        low = RTC->CNTL & 0xffffu;
    }
    return (high << 16) | low;
}

static void BeginStage(ClockState next, uint32_t now_ms)
{
    state = next;
    stage_started_ms = now_ms;
}

static void Fail(RTCClockError reason)
{
    error = reason;
    state = ST_FAILED;
    NVIC_DisableIRQ(RTC_IRQn);
    LEDWrite(0);
}

static uint8_t WriteFinished(uint32_t now_ms)
{
    if (RTC_GetFlagStatus(RTC_FLAG_RTOFF) != RESET) {
        return 1;
    }
    if ((uint32_t)(now_ms - stage_started_ms) >= RTC_CLOCK_IO_TIMEOUT_MS) {
        Fail(RTC_CLOCK_ERROR_WRITE_TIMEOUT);
    }
    return 0;
}

static RTCClockResult CheckReady(void)
{
    if (!initialized || state == ST_FAILED) {
        return RTC_CLOCK_NOT_READY;
    }
    if (time_requested) {
        return RTC_CLOCK_BUSY;
    }
    return RTC_CLOCK_OK;
}

void RTCClock_InitAlarmLED(void)
{
    GPIO_InitTypeDef gpio;
    RCC_APB2PeriphClockCmd(RTC_ALARM_LED_CLOCK, ENABLE);
    LEDWrite(0); /* Set inactive latch BEFORE enabling the output driver. */
    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin = RTC_ALARM_LED_PIN;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(RTC_ALARM_LED_PORT, &gpio);
}

void RTCClock_Init(uint32_t now_ms)
{
    uint32_t selected_source;
    uint16_t backup[RTC_BACKUP_WORD_COUNT];
    uint8_t i;

    NVIC_DisableIRQ(RTC_IRQn);
    initialized = 0;
    error = RTC_CLOCK_ERROR_NONE;
    time_requested = 0;
    hardware_alarm_known = 0;
    hardware_alarm_enabled = 0;
    alarm_irq_pending = 0;
    led_was_active = 0;
    standby_resume = 0;
    resume_data = 0;
    RTCCore_Init(&model, RTC_CLOCK_INITIAL_SECONDS);

    RTCClock_InitAlarmLED();

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR | RCC_APB1Periph_BKP, ENABLE);
    PWR_BackupAccessCmd(ENABLE);
    selected_source = RCC->BDCR & RCC_BDCR_RTCSEL;
    if (PWR_GetFlagStatus(PWR_FLAG_SB) != RESET &&
        selected_source == RCC_RTCCLKSource_LSE &&
        (RCC->BDCR & RCC_BDCR_RTCEN) != 0u) {
        for (i = 0; i < RTC_BACKUP_WORD_COUNT; ++i) {
            backup[i] = BKP_ReadBackupRegister(BackupRegister(i));
        }
        standby_resume = RTCCore_UnpackBackup(&model, &resume_data, backup);
    }
    if (selected_source != 0u && selected_source != RCC_RTCCLKSource_LSE) {
        /* Source selection can only change after backup-domain reset.
           This also clears existing backup registers: module owns the RTC domain. */
        BKP_DeInit();
    }
    /* One-shot retention. A later ordinary reset must not restore this snapshot. */
    BKP_WriteBackupRegister(BKP_DR1, 0);
    PWR_ClearFlag(PWR_FLAG_SB);
    RCC_LSEConfig(RCC_LSE_ON);
    BeginStage(ST_LSE, now_ms);
}

void RTCClock_Process(uint32_t now_ms)
{
    uint32_t counter;
    uint32_t next_counter;
    uint8_t next_enabled;
    NVIC_InitTypeDef nvic;

    if (state == ST_FAILED) {
        return;
    }
    if (initialized) {
        if (!time_requested) {
            counter = ReadCounter();
            if (counter != model.last_counter || alarm_irq_pending) {
                alarm_irq_pending = 0;
                RTCCore_Update(&model, counter);
            }
        }
        UpdateLED(now_ms);
    }

    switch (state) {
    case ST_LSE:
        if (RCC_GetFlagStatus(RCC_FLAG_LSERDY) != RESET) {
            RCC_RTCCLKConfig(RCC_RTCCLKSource_LSE);
            RCC_RTCCLKCmd(ENABLE);
            RTC_ClearFlag(RTC_FLAG_RSF);
            BeginStage(ST_SYNC, now_ms);
        } else if ((uint32_t)(now_ms - stage_started_ms) >= RTC_CLOCK_LSE_TIMEOUT_MS) {
            Fail(RTC_CLOCK_ERROR_LSE_TIMEOUT);
        }
        break;
    case ST_SYNC:
        if (RTC_GetFlagStatus(RTC_FLAG_RSF) != RESET &&
            RTC_GetFlagStatus(RTC_FLAG_RTOFF) != RESET) {
            RTC_ITConfig(RTC_IT_SEC | RTC_IT_ALR | RTC_IT_OW, DISABLE);
            BeginStage(ST_BOOT_IRQ_OFF, now_ms);
        } else if ((uint32_t)(now_ms - stage_started_ms) >= RTC_CLOCK_IO_TIMEOUT_MS) {
            Fail(RTC_CLOCK_ERROR_SYNC_TIMEOUT);
        }
        break;
    case ST_BOOT_IRQ_OFF:
        if (WriteFinished(now_ms)) {
            if (standby_resume) {
                /* Preserve CNT and divider across a real Standby reset.
                   Catch up before clearing ALRF so an alarm during sleep survives. */
                counter = ReadCounter();
                RTCCore_Update(&model, counter);
                next_counter = counter - 1u;
                hardware_alarm_enabled = RTCCore_NextAlarm(&model, counter, &next_counter);
                hardware_alarm_counter = next_counter;
                RTC_SetAlarm(next_counter);
                BeginStage(ST_BOOT_ALARM, now_ms);
            } else {
                RTC_SetPrescaler(32767u);
                BeginStage(ST_BOOT_PRESCALER, now_ms);
            }
        }
        break;
    case ST_BOOT_PRESCALER:
        if (WriteFinished(now_ms)) {
            RTC_SetCounter(RTC_CLOCK_INITIAL_SECONDS);
            BeginStage(ST_BOOT_COUNTER, now_ms);
        }
        break;
    case ST_BOOT_COUNTER:
        if (WriteFinished(now_ms)) {
            hardware_alarm_counter = ReadCounter() - 1u;
            RTC_SetAlarm(hardware_alarm_counter); /* No future alarm while all slots are OFF. */
            BeginStage(ST_BOOT_ALARM, now_ms);
        }
        break;
    case ST_BOOT_ALARM:
        if (WriteFinished(now_ms)) {
            RTC_ClearFlag(RTC_FLAG_SEC | RTC_FLAG_ALR | RTC_FLAG_OW);
            RTC_ITConfig(RTC_IT_ALR, ENABLE);
            BeginStage(ST_BOOT_IRQ_ON, now_ms);
        }
        break;
    case ST_BOOT_IRQ_ON:
        if (WriteFinished(now_ms)) {
            if (standby_resume) {
                RTCCore_Update(&model, ReadCounter());
            } else {
                RTCCore_Init(&model, ReadCounter());
                hardware_alarm_enabled = 0;
            }
            hardware_alarm_known = 1;
            alarm_irq_pending = 0;
            NVIC_ClearPendingIRQ(RTC_IRQn);
            nvic.NVIC_IRQChannel = RTC_IRQn;
            nvic.NVIC_IRQChannelPreemptionPriority = RTC_CLOCK_IRQ_PREEMPTION_PRIORITY;
            nvic.NVIC_IRQChannelSubPriority = RTC_CLOCK_IRQ_SUB_PRIORITY;
            nvic.NVIC_IRQChannelCmd = ENABLE;
            NVIC_Init(&nvic);
            initialized = 1;
            BeginStage(ST_READY, now_ms);
        }
        break;
    case ST_READY:
        if (time_requested) {
            if (RTC_GetFlagStatus(RTC_FLAG_RTOFF) == RESET) {
                BeginStage(ST_AVAILABLE, now_ms);
                break;
            }
            RTC_SetCounter(requested_seconds);
            BeginStage(ST_TIME_WRITE, now_ms);
            break;
        }
        counter = ReadCounter();
        RTCCore_Update(&model, counter);
        next_counter = counter - 1u;
        next_enabled = RTCCore_NextAlarm(&model, counter, &next_counter);
        if (hardware_alarm_known && next_enabled == hardware_alarm_enabled &&
            (!next_enabled || next_counter == hardware_alarm_counter)) {
            break;
        }
        if (RTC_GetFlagStatus(RTC_FLAG_RTOFF) == RESET) {
            BeginStage(ST_AVAILABLE, now_ms);
            break;
        }
        RTC_ClearITPendingBit(RTC_IT_ALR);
        RTC_SetAlarm(next_counter);
        hardware_alarm_counter = next_counter;
        hardware_alarm_enabled = next_enabled;
        hardware_alarm_known = 1;
        BeginStage(ST_ALARM_WRITE, now_ms);
        break;
    case ST_AVAILABLE:
    case ST_ALARM_WRITE:
        if (WriteFinished(now_ms)) {
            BeginStage(ST_READY, now_ms);
        }
        break;
    case ST_TIME_WRITE:
        if (WriteFinished(now_ms)) {
            RTCCore_SetClock(&model, ReadCounter());
            time_requested = 0;
            hardware_alarm_known = 0;
            alarm_irq_pending = 0;
            led_was_active = 0;
            UpdateLED(now_ms);
            BeginStage(ST_READY, now_ms);
        }
        break;
    default:
        break;
    }
}

RTCClockStatus RTCClock_GetStatus(void)
{
    if (state == ST_FAILED) {
        return RTC_CLOCK_FAILED;
    }
    if (!initialized) {
        return RTC_CLOCK_STARTING;
    }
    return time_requested ? RTC_CLOCK_UPDATING_TIME : RTC_CLOCK_READY;
}

RTCClockError RTCClock_GetError(void)
{
    return error;
}

RTCClockResult RTCClock_GetTime(RTCClockTime *time)
{
    RTCClockResult result;
    if (time == 0) {
        return RTC_CLOCK_BAD_ARGUMENT;
    }
    result = CheckReady();
    if (result != RTC_CLOCK_OK) {
        return result;
    }
    RTCCore_SecondsToTime(ReadCounter(), time);
    return RTC_CLOCK_OK;
}

RTCClockResult RTCClock_SetTime(const RTCClockTime *time)
{
    RTCClockResult result;
    if (!RTCCore_TimeValid(time)) {
        return RTC_CLOCK_BAD_ARGUMENT;
    }
    result = CheckReady();
    if (result != RTC_CLOCK_OK) {
        return result;
    }
    if (state != ST_READY) {
        return RTC_CLOCK_BUSY;
    }
    requested_seconds = RTCCore_TimeToSeconds(time);
    time_requested = 1;
    return RTC_CLOCK_OK;
}

RTCClockResult RTCClock_GetAlarm(uint8_t number, RTCClockAlarm *alarm)
{
    RTCClockResult result;
    if (number < 1u || number > RTC_LOGICAL_ALARM_COUNT || alarm == 0) {
        return RTC_CLOCK_BAD_ARGUMENT;
    }
    result = CheckReady();
    if (result != RTC_CLOCK_OK) {
        return result;
    }
    *alarm = model.alarms[number - 1u];
    return RTC_CLOCK_OK;
}

RTCClockResult RTCClock_SetAlarm(uint8_t number, const RTCClockTime *time, uint8_t enabled)
{
    RTCClockResult result;
    if (number < 1u || number > RTC_LOGICAL_ALARM_COUNT || !RTCCore_TimeValid(time)) {
        return RTC_CLOCK_BAD_ARGUMENT;
    }
    result = CheckReady();
    if (result != RTC_CLOCK_OK) {
        return result;
    }
    RTCCore_SetAlarm(&model, number, time, enabled, ReadCounter());
    if (model.active_mask == 0) {
        led_was_active = 0;
        LEDWrite(0);
    }
    return RTC_CLOCK_OK;
}

RTCClockResult RTCClock_EnableAlarm(uint8_t number, uint8_t enabled)
{
    RTCClockResult result;
    if (number < 1u || number > RTC_LOGICAL_ALARM_COUNT) {
        return RTC_CLOCK_BAD_ARGUMENT;
    }
    result = CheckReady();
    if (result != RTC_CLOCK_OK) {
        return result;
    }
    RTCCore_EnableAlarm(&model, number, enabled, ReadCounter());
    if (model.active_mask == 0) {
        led_was_active = 0;
        LEDWrite(0);
    }
    return RTC_CLOCK_OK;
}

RTCClockResult RTCClock_DeleteAlarm(uint8_t number)
{
    RTCClockResult result;
    if (number < 1u || number > RTC_LOGICAL_ALARM_COUNT) {
        return RTC_CLOCK_BAD_ARGUMENT;
    }
    result = CheckReady();
    if (result != RTC_CLOCK_OK) {
        return result;
    }
    RTCCore_Update(&model, ReadCounter());
    RTCCore_DeleteAlarm(&model, number);
    if (model.active_mask == 0) {
        led_was_active = 0;
        LEDWrite(0);
    }
    return RTC_CLOCK_OK;
}

uint8_t RTCClock_GetActiveAlarmMask(void)
{
    return initialized && state != ST_FAILED ? model.active_mask : 0;
}

void RTCClock_AcknowledgeAlarm(void)
{
    RTCCore_Acknowledge(&model);
    led_was_active = 0;
    LEDWrite(0);
}

uint8_t RTCClock_WasStandbyResume(void)
{
    return standby_resume;
}

uint32_t RTCClock_GetResumeData(void)
{
    return standby_resume ? resume_data : 0u;
}

uint8_t RTCClock_CanStandby(void)
{
    uint32_t counter, next_counter;
    uint8_t next_enabled;
    if (!initialized || state != ST_READY || time_requested ||
        RTC_GetFlagStatus(RTC_FLAG_RTOFF) == RESET ||
        RTC_GetFlagStatus(RTC_FLAG_ALR) != RESET || alarm_irq_pending) {
        return 0;
    }
    counter = ReadCounter();
    RTCCore_Update(&model, counter);
    if (model.active_mask != 0u) { return 0; }
    next_counter = counter - 1u;
    next_enabled = RTCCore_NextAlarm(&model, counter, &next_counter);
    return (uint8_t)(hardware_alarm_known &&
                     next_enabled == hardware_alarm_enabled &&
                     (!next_enabled || next_counter == hardware_alarm_counter));
}

uint8_t RTCClock_PrepareStandby(uint32_t app_data)
{
    uint16_t backup[RTC_BACKUP_WORD_COUNT];
    uint8_t i;
    if (!RTCClock_CanStandby()) { return 0; }
    RTCCore_PackBackup(&model, app_data, backup);
    /* Commit marker written last: reset during serialization leaves invalid data. */
    BKP_WriteBackupRegister(BKP_DR1, 0);
    for (i = 1; i < RTC_BACKUP_WORD_COUNT; ++i) {
        BKP_WriteBackupRegister(BackupRegister(i), backup[i]);
    }
    BKP_WriteBackupRegister(BKP_DR1, backup[0]);
    return 1;
}

void RTCClock_CancelStandby(void)
{
    BKP_WriteBackupRegister(BKP_DR1, 0);
}

void RTCClock_IRQHandler(void)
{
    if (RTC_GetITStatus(RTC_IT_ALR) != RESET) {
        RTC_ClearITPendingBit(RTC_IT_ALR);
        alarm_irq_pending = 1;
    }
}

#if RTC_CLOCK_DEFINE_IRQ_HANDLER
void RTC_IRQHandler(void)
{
    RTCClock_IRQHandler();
}
#endif
