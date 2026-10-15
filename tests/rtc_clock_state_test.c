#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "rtc_clock.h"
#include "rtc_clock_config.h"

RTC_TypeDef fake_rtc;
RCC_TypeDef fake_rcc;
GPIO_TypeDef fake_gpiob;
static uint8_t lse_ready;
static uint8_t nvic_enabled;
static unsigned int backup_resets;
static uint32_t prescaler;
static uint32_t alarm_counter;
static uint16_t backup_words[RTC_BACKUP_WORD_COUNT];
static uint8_t standby_flag;
static unsigned int counter_writes;

static uint8_t AlarmLEDOn(void)
{
    uint8_t high = (uint8_t)((GPIOB->ODR & RTC_ALARM_LED_PIN) != 0);
    return RTC_ALARM_LED_ACTIVE_LOW ? (uint8_t)!high : high;
}

static void SetHardwareCounter(uint32_t value)
{
    RTC->CNTH = value >> 16;
    RTC->CNTL = value & 0xffffu;
}

static void StartWrite(void)
{
    assert(RTC->CRL & RTC_FLAG_RTOFF); /* Driver must serialize asynchronous writes. */
    RTC->CRL &= ~RTC_FLAG_RTOFF;
}

static void CompleteWrite(void)
{
    RTC->CRL |= RTC_FLAG_RTOFF;
}

void NVIC_DisableIRQ(uint8_t irq) { (void)irq; nvic_enabled = 0; }
void NVIC_ClearPendingIRQ(uint8_t irq) { (void)irq; }
void NVIC_Init(NVIC_InitTypeDef *config) { nvic_enabled = (uint8_t)config->NVIC_IRQChannelCmd; }
void RCC_APB2PeriphClockCmd(uint32_t clocks, FunctionalState enable) { (void)clocks; (void)enable; }
void RCC_APB1PeriphClockCmd(uint32_t clocks, FunctionalState enable) { (void)clocks; (void)enable; }
void PWR_BackupAccessCmd(FunctionalState enable) { (void)enable; }
FlagStatus PWR_GetFlagStatus(uint32_t flag) { (void)flag; return standby_flag ? SET : RESET; }
void PWR_ClearFlag(uint32_t flag) { if (flag == PWR_FLAG_SB) { standby_flag = 0; } }
void BKP_DeInit(void) { ++backup_resets; RCC->BDCR = 0; memset(backup_words, 0, sizeof(backup_words)); }
uint16_t BKP_ReadBackupRegister(uint16_t reg) { assert(reg >= 4u && reg <= 40u); return backup_words[reg / 4u - 1u]; }
void BKP_WriteBackupRegister(uint16_t reg, uint16_t value) { assert(reg >= 4u && reg <= 40u); backup_words[reg / 4u - 1u] = value; }
void RCC_LSEConfig(uint8_t config) { (void)config; }
FlagStatus RCC_GetFlagStatus(uint8_t flag) { (void)flag; return lse_ready ? SET : RESET; }
void RCC_RTCCLKConfig(uint32_t source) { RCC->BDCR = source; }
void RCC_RTCCLKCmd(FunctionalState enable) { if (enable) { RCC->BDCR |= RCC_BDCR_RTCEN; } }
void GPIO_StructInit(GPIO_InitTypeDef *config) { memset(config, 0, sizeof(*config)); }
void GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *config) { (void)port; (void)config; }
void GPIO_WriteBit(GPIO_TypeDef *port, uint16_t pin, BitAction bit)
{
    if (bit == Bit_SET) { port->ODR |= pin; }
    else { port->ODR &= (uint16_t)~pin; }
}
FlagStatus RTC_GetFlagStatus(uint16_t flag) { return (RTC->CRL & flag) ? SET : RESET; }
void RTC_ClearFlag(uint16_t flags) { RTC->CRL &= (uint32_t)~flags; }
void RTC_ITConfig(uint16_t interrupts, FunctionalState enable)
{
    StartWrite();
    if (enable) { RTC->CRH |= interrupts; }
    else { RTC->CRH &= (uint32_t)~interrupts; }
}
void RTC_SetPrescaler(uint32_t value) { StartWrite(); prescaler = value; }
void RTC_SetCounter(uint32_t value) { StartWrite(); ++counter_writes; SetHardwareCounter(value); }
void RTC_SetAlarm(uint32_t value) { StartWrite(); alarm_counter = value; }
void RTC_ClearITPendingBit(uint16_t interrupts) { RTC->CRL &= (uint32_t)~interrupts; }
FlagStatus RTC_GetITStatus(uint16_t interrupt)
{
    return (RTC->CRL & RTC->CRH & interrupt) ? SET : RESET;
}

static void Fixture(void)
{
    memset(&fake_rtc, 0, sizeof(fake_rtc));
    memset(&fake_rcc, 0, sizeof(fake_rcc));
    memset(&fake_gpiob, 0, sizeof(fake_gpiob));
    RTC->CRL = RTC_FLAG_RTOFF;
    RCC->BDCR = RCC_RTCCLKSource_LSE;
    lse_ready = 0;
    nvic_enabled = 0;
    backup_resets = 0;
    prescaler = 0;
    alarm_counter = 0;
    counter_writes = 0;
    standby_flag = 0;
    memset(backup_words, 0, sizeof(backup_words));
}

static void FinishRestart(uint32_t now_ms)
{
    unsigned int i;
    lse_ready = 1;
    RTCClock_Process(now_ms);
    RTC->CRL |= RTC_FLAG_RSF;
    for (i = 1; i <= 12; ++i) {
        CompleteWrite();
        RTCClock_Process(now_ms + i * 5u);
    }
    assert(RTCClock_GetStatus() == RTC_CLOCK_READY);
}

static void Boot(void)
{
    unsigned int i;
    Fixture();
    RTCClock_Init(0);
    lse_ready = 1;
    RTCClock_Process(0);
    RTC->CRL |= RTC_FLAG_RSF;
    for (i = 1; i <= 10; ++i) {
        CompleteWrite();
        RTCClock_Process(i * 5u);
    }
    assert(RTCClock_GetStatus() == RTC_CLOCK_READY);
    assert(prescaler == 32767u);
    assert(nvic_enabled);
    assert(backup_resets == 0);
}

int main(void)
{
    RTCClockTime time;
    RTCClockTime target = {0, 0, 0};
    RTCClockTime new_time = {14, 30, 5};
    RTCClockAlarm alarm;

    /* Main initializes the LED before any other peripheral can fail/stall. */
    Fixture();
    GPIOB->ODR = RTC_ALARM_LED_ACTIVE_LOW ? 0u : GPIO_Pin_0;
    RTCClock_InitAlarmLED();
    assert(!AlarmLEDOn());
    assert(RTC->CRL == RTC_FLAG_RTOFF && !counter_writes);

    Fixture();
    RTCClock_Init(0);
    assert(RTCClock_GetStatus() == RTC_CLOCK_STARTING);
    assert(RTCClock_GetTime(&time) == RTC_CLOCK_NOT_READY);
    RTCClock_Process(4999);
    assert(RTCClock_GetStatus() == RTC_CLOCK_STARTING);
    RTCClock_Process(5000);
    assert(RTCClock_GetStatus() == RTC_CLOCK_FAILED);
    assert(RTCClock_GetError() == RTC_CLOCK_ERROR_LSE_TIMEOUT);
    assert(!nvic_enabled && !AlarmLEDOn());

    Fixture();
    RTCClock_Init(0);
    lse_ready = 1;
    RTCClock_Process(0);
    RTCClock_Process(1000); /* No RSF synchronization. */
    assert(RTCClock_GetError() == RTC_CLOCK_ERROR_SYNC_TIMEOUT);

    Fixture();
    RTCClock_Init(0);
    lse_ready = 1;
    RTCClock_Process(0);
    RTC->CRL |= RTC_FLAG_RSF;
    RTCClock_Process(5); /* First write starts, then never completes. */
    RTCClock_Process(1005);
    assert(RTCClock_GetError() == RTC_CLOCK_ERROR_WRITE_TIMEOUT);

    Fixture();
    RCC->BDCR = 0x200u; /* Old LSI selection requires backup-domain reset. */
    RTCClock_Init(0);
    assert(backup_resets == 1);

    Boot();
    assert(RTCClock_GetTime(&time) == RTC_CLOCK_OK);
    assert(time.hour == 23 && time.minute == 59 && time.second == 55);
    assert(RTCClock_SetAlarm(1, &target, 1) == RTC_CLOCK_OK);
    RTCClock_Process(100);
    assert(alarm_counter == 86400u);
    CompleteWrite();
    RTCClock_Process(105);
    SetHardwareCounter(86400u);
    RTC->CRL |= RTC_FLAG_ALR;
    RTCClock_IRQHandler();
    RTCClock_Process(110);
    assert(RTCClock_GetActiveAlarmMask() == 1);
    assert(AlarmLEDOn());
    assert(alarm_counter == 172800u);
    CompleteWrite();
    RTCClock_Process(610);
    assert(!AlarmLEDOn());
    RTCClock_Process(1110);
    assert(AlarmLEDOn());
    RTCClock_AcknowledgeAlarm();
    assert(RTCClock_GetActiveAlarmMask() == 0);
    assert(!AlarmLEDOn());
    assert(RTCClock_GetAlarm(1, &alarm) == RTC_CLOCK_OK && alarm.enabled);
    RTCClock_Process(1115);
    assert(RTCClock_GetActiveAlarmMask() == 0);

    assert(RTCClock_SetTime(&new_time) == RTC_CLOCK_OK);
    assert(RTCClock_GetStatus() == RTC_CLOCK_UPDATING_TIME);
    assert(RTCClock_GetTime(&time) == RTC_CLOCK_BUSY);
    assert(RTCClock_SetTime(&new_time) == RTC_CLOCK_BUSY);
    RTCClock_Process(1120);
    RTCClock_Process(1125); /* No busy loop and no premature completion. */
    assert(RTCClock_GetStatus() == RTC_CLOCK_UPDATING_TIME);
    CompleteWrite();
    RTCClock_Process(1130);
    assert(RTCClock_GetStatus() == RTC_CLOCK_READY);
    assert(RTCClock_GetTime(&time) == RTC_CLOCK_OK);
    assert(time.hour == 14 && time.minute == 30 && time.second == 5);

    /* Delay past target without delivering an IRQ: RTC interval catch-up still works. */
    Boot();
    target.hour = 0; target.minute = 0; target.second = 0;
    assert(RTCClock_SetAlarm(2, &target, 1) == RTC_CLOCK_OK);
    SetHardwareCounter(86402u);
    RTCClock_Process(200);
    assert(RTCClock_GetActiveAlarmMask() == 2);
    assert(AlarmLEDOn());
    assert(RTCClock_EnableAlarm(2, 0) == RTC_CLOCK_OK);
    assert(RTCClock_GetActiveAlarmMask() == 0 && !AlarmLEDOn());

    /* True Standby retains CNT, all three alarm slots and opaque app data. */
    Boot();
    target.hour = 0; target.minute = 0; target.second = 0;
    assert(RTCClock_SetAlarm(1, &target, 1) == RTC_CLOCK_OK);
    target.second = 1;
    assert(RTCClock_SetAlarm(2, &target, 1) == RTC_CLOCK_OK);
    target.hour = 12; target.minute = 34; target.second = 56;
    assert(RTCClock_SetAlarm(3, &target, 0) == RTC_CLOCK_OK);
    assert(!RTCClock_CanStandby()); /* New alarm schedule not yet in hardware. */
    RTCClock_Process(100);
    assert(!RTCClock_CanStandby()); /* Asynchronous write outstanding. */
    CompleteWrite(); RTCClock_Process(105);
    assert(RTCClock_CanStandby());
    assert(RTCClock_PrepareStandby(0x12345678u));
    SetHardwareCounter(86401u);
    RTC->CRL |= RTC_FLAG_ALR;
    standby_flag = 1;
    RTCClock_Init(200);
    assert(RTCClock_WasStandbyResume());
    assert(RTCClock_GetResumeData() == 0x12345678u);
    FinishRestart(200);
    assert(counter_writes == 1u); /* Only original cold initialization wrote CNT. */
    assert(RTCClock_GetTime(&time) == RTC_CLOCK_OK && time.second == 1u && time.hour == 0u);
    assert(RTCClock_GetActiveAlarmMask() == 3u); /* Both targets crossed while asleep. */
    assert(RTCClock_GetAlarm(3, &alarm) == RTC_CLOCK_OK && !alarm.enabled && alarm.time.hour == 12u);
    assert(!RTCClock_CanStandby()); /* Reminder inhibits a second sleep. */
    RTCClock_AcknowledgeAlarm();
    assert(RTCClock_CanStandby());
    assert(RTCClock_PrepareStandby(99u));
    standby_flag = 0; /* Ordinary NRST ignores even an otherwise valid snapshot. */
    RTCClock_Init(300); FinishRestart(300);
    assert(!RTCClock_WasStandbyResume());
    assert(counter_writes == 2u);
    assert(RTCClock_GetAlarm(1, &alarm) == RTC_CLOCK_OK && !alarm.enabled);
    assert(RTCClock_GetTime(&time) == RTC_CLOCK_OK && time.hour == 23u && time.second == 55u);

    assert(RTCClock_PrepareStandby(100u));
    backup_words[5] ^= 1u; standby_flag = 1;
    RTCClock_Init(400); FinishRestart(400);
    assert(!RTCClock_WasStandbyResume()); /* Corrupt CRC falls back to documented cold boot. */
    assert(counter_writes == 3u);
    assert(RTCClock_CanStandby());
    RTC->CRL |= RTC_FLAG_ALR;
    assert(!RTCClock_CanStandby() && !RTCClock_PrepareStandby(0u));

    puts("PASS: RTC states, async writes, timeouts, schedule, LED, late alarm, Standby retention/catch-up, cold reset, CRC, entry race guard");
    return 0;
}
