#ifndef RTC_CLOCK_H
#define RTC_CLOCK_H

#include "stm32f10x.h"
#include "rtc_core.h"

typedef enum {
    RTC_CLOCK_STARTING = 0,
    RTC_CLOCK_READY,
    RTC_CLOCK_UPDATING_TIME,
    RTC_CLOCK_FAILED
} RTCClockStatus;

typedef enum {
    RTC_CLOCK_OK = 0,
    RTC_CLOCK_BAD_ARGUMENT,
    RTC_CLOCK_NOT_READY,
    RTC_CLOCK_BUSY
} RTCClockResult;

typedef enum {
    RTC_CLOCK_ERROR_NONE = 0,
    RTC_CLOCK_ERROR_LSE_TIMEOUT,
    RTC_CLOCK_ERROR_SYNC_TIMEOUT,
    RTC_CLOCK_ERROR_WRITE_TIMEOUT
} RTCClockError;

/* Establish the inactive LED output immediately after entering main. */
void RTCClock_InitAlarmLED(void);
/* Starts nonblocking initialization. Timebase must be running before calling. */
void RTCClock_Init(uint32_t now_ms);
void RTCClock_Process(uint32_t now_ms);
RTCClockStatus RTCClock_GetStatus(void);
RTCClockError RTCClock_GetError(void);
RTCClockResult RTCClock_GetTime(RTCClockTime *time);
/* OK accepts a request, not its completion. Process finishes it asynchronously. */
RTCClockResult RTCClock_SetTime(const RTCClockTime *time);
RTCClockResult RTCClock_GetAlarm(uint8_t number, RTCClockAlarm *alarm);
RTCClockResult RTCClock_SetAlarm(uint8_t number, const RTCClockTime *time, uint8_t enabled);
RTCClockResult RTCClock_EnableAlarm(uint8_t number, uint8_t enabled);
RTCClockResult RTCClock_DeleteAlarm(uint8_t number);
uint8_t RTCClock_GetActiveAlarmMask(void);
/* Stop this reminder only; enabled alarms remain enabled for the following day. */
void RTCClock_AcknowledgeAlarm(void);
void RTCClock_IRQHandler(void);

/* True only for SBF wake with valid CRC backup and an already-running LSE RTC.
   Ordinary reset/cold boot still starts at 23:59:55. */
uint8_t RTCClock_WasStandbyResume(void);
uint32_t RTCClock_GetResumeData(void);
/* Standby is inhibited while startup/writes/reminders/schedule changes are pending. */
uint8_t RTCClock_CanStandby(void);
uint8_t RTCClock_PrepareStandby(uint32_t app_data);
void RTCClock_CancelStandby(void);

#endif
