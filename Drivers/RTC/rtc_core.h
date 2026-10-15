#ifndef RTC_CORE_H
#define RTC_CORE_H

#include <stdint.h>

#define RTC_SECONDS_PER_DAY 86400u
#define RTC_LOGICAL_ALARM_COUNT 3u

typedef struct {
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} RTCClockTime;

typedef struct {
    RTCClockTime time;
    uint8_t enabled;
} RTCClockAlarm;

typedef struct {
    RTCClockAlarm alarms[RTC_LOGICAL_ALARM_COUNT];
    uint32_t last_counter;
    uint8_t active_mask;
} RTCClockModel;

uint8_t RTCCore_TimeValid(const RTCClockTime *time);
uint32_t RTCCore_TimeToSeconds(const RTCClockTime *time);
void RTCCore_SecondsToTime(uint32_t seconds, RTCClockTime *time);
void RTCCore_Init(RTCClockModel *model, uint32_t counter);
/* SetClock rebases after an explicit time change; it does not treat jumps as elapsed time. */
void RTCCore_SetClock(RTCClockModel *model, uint32_t counter);
/* Returns the mask of alarms reached since the last sample. Active reminders accumulate. */
uint8_t RTCCore_Update(RTCClockModel *model, uint32_t counter);
/* Alarm numbers are 1..3. Enabling an alarm equal to current time rings immediately. */
uint8_t RTCCore_SetAlarm(RTCClockModel *model, uint8_t number,
                        const RTCClockTime *time, uint8_t enabled, uint32_t counter);
uint8_t RTCCore_EnableAlarm(RTCClockModel *model, uint8_t number,
                           uint8_t enabled, uint32_t counter);
uint8_t RTCCore_DeleteAlarm(RTCClockModel *model, uint8_t number);
void RTCCore_Acknowledge(RTCClockModel *model);
/* Selects the next strictly future RTC counter match, combining all enabled alarms. */
uint8_t RTCCore_NextAlarm(const RTCClockModel *model, uint32_t counter, uint32_t *target);

/* Ten 16-bit backup words fit STM32F103C8's medium-density BKP domain.
   CRC rejects incomplete/corrupt retention; app_data belongs to the application. */
#define RTC_BACKUP_WORD_COUNT 10u
void RTCCore_PackBackup(const RTCClockModel *model, uint32_t app_data,
                        uint16_t words[RTC_BACKUP_WORD_COUNT]);
uint8_t RTCCore_UnpackBackup(RTCClockModel *model, uint32_t *app_data,
                             const uint16_t words[RTC_BACKUP_WORD_COUNT]);

#endif
