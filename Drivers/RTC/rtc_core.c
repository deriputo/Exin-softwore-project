#include "rtc_core.h"

uint8_t RTCCore_TimeValid(const RTCClockTime *time)
{
    return (uint8_t)(time != 0 && time->hour < 24u && time->minute < 60u && time->second < 60u);
}

uint32_t RTCCore_TimeToSeconds(const RTCClockTime *time)
{
    return (uint32_t)time->hour * 3600u + (uint32_t)time->minute * 60u + time->second;
}

void RTCCore_SecondsToTime(uint32_t seconds, RTCClockTime *time)
{
    seconds %= RTC_SECONDS_PER_DAY;
    time->hour = (uint8_t)(seconds / 3600u);
    time->minute = (uint8_t)((seconds % 3600u) / 60u);
    time->second = (uint8_t)(seconds % 60u);
}

void RTCCore_Init(RTCClockModel *model, uint32_t counter)
{
    uint8_t i;
    model->last_counter = counter;
    model->active_mask = 0;
    for (i = 0; i < RTC_LOGICAL_ALARM_COUNT; ++i) {
        model->alarms[i].time.hour = 0;
        model->alarms[i].time.minute = 0;
        model->alarms[i].time.second = 0;
        model->alarms[i].enabled = 0;
    }
}

void RTCCore_SetClock(RTCClockModel *model, uint32_t counter)
{
    uint8_t i;
    model->last_counter = counter;
    model->active_mask = 0;
    for (i = 0; i < RTC_LOGICAL_ALARM_COUNT; ++i) {
        if (model->alarms[i].enabled &&
            RTCCore_TimeToSeconds(&model->alarms[i].time) == counter % RTC_SECONDS_PER_DAY) {
            model->active_mask |= (uint8_t)(1u << i);
        }
    }
}

uint8_t RTCCore_Update(RTCClockModel *model, uint32_t counter)
{
    uint8_t i;
    uint8_t reached = 0;
    uint32_t elapsed = counter - model->last_counter;
    uint32_t last_time = model->last_counter % RTC_SECONDS_PER_DAY;

    if (elapsed == 0) {
        return 0;
    }
    for (i = 0; i < RTC_LOGICAL_ALARM_COUNT; ++i) {
        uint32_t distance;
        if (!model->alarms[i].enabled) {
            continue;
        }
        distance = (RTCCore_TimeToSeconds(&model->alarms[i].time) +
                    RTC_SECONDS_PER_DAY - last_time) % RTC_SECONDS_PER_DAY;
        if (distance == 0) {
            distance = RTC_SECONDS_PER_DAY;
        }
        if (elapsed >= distance) {
            reached |= (uint8_t)(1u << i);
        }
    }
    model->last_counter = counter;
    model->active_mask |= reached;
    return reached;
}

uint8_t RTCCore_SetAlarm(RTCClockModel *model, uint8_t number,
                        const RTCClockTime *time, uint8_t enabled, uint32_t counter)
{
    uint8_t index;
    if (number < 1u || number > RTC_LOGICAL_ALARM_COUNT || !RTCCore_TimeValid(time)) {
        return 0;
    }
    RTCCore_Update(model, counter);
    index = (uint8_t)(number - 1u);
    model->alarms[index].time = *time;
    model->alarms[index].enabled = (uint8_t)(enabled != 0);
    model->active_mask &= (uint8_t)~(1u << index);
    if (enabled && RTCCore_TimeToSeconds(time) == counter % RTC_SECONDS_PER_DAY) {
        model->active_mask |= (uint8_t)(1u << index);
    }
    return 1;
}

uint8_t RTCCore_EnableAlarm(RTCClockModel *model, uint8_t number,
                           uint8_t enabled, uint32_t counter)
{
    RTCClockTime time;
    if (number < 1u || number > RTC_LOGICAL_ALARM_COUNT) {
        return 0;
    }
    time = model->alarms[number - 1u].time;
    return RTCCore_SetAlarm(model, number, &time, enabled, counter);
}

uint8_t RTCCore_DeleteAlarm(RTCClockModel *model, uint8_t number)
{
    uint8_t index;
    if (number < 1u || number > RTC_LOGICAL_ALARM_COUNT) {
        return 0;
    }
    index = (uint8_t)(number - 1u);
    model->alarms[index].time.hour = 0;
    model->alarms[index].time.minute = 0;
    model->alarms[index].time.second = 0;
    model->alarms[index].enabled = 0;
    model->active_mask &= (uint8_t)~(1u << index);
    return 1;
}

void RTCCore_Acknowledge(RTCClockModel *model)
{
    model->active_mask = 0;
}

uint8_t RTCCore_NextAlarm(const RTCClockModel *model, uint32_t counter, uint32_t *target)
{
    uint8_t i;
    uint8_t found = 0;
    uint32_t best = RTC_SECONDS_PER_DAY;
    uint32_t current_time = counter % RTC_SECONDS_PER_DAY;
    for (i = 0; i < RTC_LOGICAL_ALARM_COUNT; ++i) {
        uint32_t distance;
        if (!model->alarms[i].enabled) {
            continue;
        }
        distance = (RTCCore_TimeToSeconds(&model->alarms[i].time) +
                    RTC_SECONDS_PER_DAY - current_time) % RTC_SECONDS_PER_DAY;
        if (distance == 0) {
            distance = RTC_SECONDS_PER_DAY;
        }
        if (!found || distance < best) {
            best = distance;
            found = 1;
        }
    }
    if (found && target != 0) {
        *target = counter + best;
    }
    return found;
}

#define RTC_BACKUP_MAGIC 0xA631u

static uint16_t BackupCRC(const uint16_t *words)
{
    uint16_t crc = 0xffffu;
    uint8_t i, byte, bit;
    for (i = 2u; i < RTC_BACKUP_WORD_COUNT; ++i) {
        for (byte = 0; byte < 2u; ++byte) {
            crc ^= (uint16_t)((words[i] >> (byte * 8u)) & 0xffu) << 8;
            for (bit = 0; bit < 8u; ++bit) {
                crc = (uint16_t)((crc & 0x8000u) ? ((uint32_t)crc << 1) ^ 0x1021u : (uint32_t)crc << 1);
            }
        }
    }
    return crc;
}

void RTCCore_PackBackup(const RTCClockModel *model, uint32_t app_data,
                        uint16_t words[RTC_BACKUP_WORD_COUNT])
{
    uint8_t i, bit;
    for (i = 0; i < RTC_BACKUP_WORD_COUNT; ++i) { words[i] = 0; }
    words[0] = RTC_BACKUP_MAGIC;
    words[2] = (uint16_t)model->last_counter;
    words[3] = (uint16_t)(model->last_counter >> 16);
    for (i = 0; i < RTC_LOGICAL_ALARM_COUNT; ++i) {
        uint32_t packed = RTCCore_TimeToSeconds(&model->alarms[i].time) |
                          (model->alarms[i].enabled ? (1u << 17) : 0u);
        for (bit = 0; bit < 18u; ++bit) {
            uint8_t offset = (uint8_t)(i * 18u + bit);
            if (packed & (1u << bit)) {
                words[4u + offset / 16u] |= (uint16_t)(1u << (offset % 16u));
            }
        }
    }
    words[8] = (uint16_t)app_data;
    words[9] = (uint16_t)(app_data >> 16);
    words[1] = BackupCRC(words);
}

uint8_t RTCCore_UnpackBackup(RTCClockModel *model, uint32_t *app_data,
                             const uint16_t words[RTC_BACKUP_WORD_COUNT])
{
    RTCClockModel restored;
    uint8_t i, bit;
    if (words[0] != RTC_BACKUP_MAGIC || words[1] != BackupCRC(words) ||
        (words[7] & 0xffc0u) != 0u) {
        return 0;
    }
    RTCCore_Init(&restored, (uint32_t)words[2] | ((uint32_t)words[3] << 16));
    for (i = 0; i < RTC_LOGICAL_ALARM_COUNT; ++i) {
        uint32_t packed = 0;
        for (bit = 0; bit < 18u; ++bit) {
            uint8_t offset = (uint8_t)(i * 18u + bit);
            if (words[4u + offset / 16u] & (uint16_t)(1u << (offset % 16u))) {
                packed |= 1u << bit;
            }
        }
        if ((packed & 0x1ffffu) >= RTC_SECONDS_PER_DAY) { return 0; }
        RTCCore_SecondsToTime(packed & 0x1ffffu, &restored.alarms[i].time);
        restored.alarms[i].enabled = (uint8_t)((packed >> 17) & 1u);
    }
    *model = restored;
    if (app_data != 0) {
        *app_data = (uint32_t)words[8] | ((uint32_t)words[9] << 16);
    }
    return 1;
}
