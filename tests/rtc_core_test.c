#include <assert.h>
#include <stdio.h>
#include "rtc_core.h"

static RTCClockTime Time(uint8_t h, uint8_t m, uint8_t s)
{
    RTCClockTime time;
    time.hour = h;
    time.minute = m;
    time.second = s;
    return time;
}

int main(void)
{
    RTCClockModel model;
    RTCClockTime time;
    RTCClockTime other;
    uint32_t second;
    uint32_t target;

    assert(!RTCCore_TimeValid(0));
    time = Time(24, 0, 0);
    assert(!RTCCore_TimeValid(&time));
    time = Time(0, 60, 0);
    assert(!RTCCore_TimeValid(&time));
    time = Time(0, 0, 60);
    assert(!RTCCore_TimeValid(&time));
    time = Time(23, 59, 55);
    assert(RTCCore_TimeToSeconds(&time) == 86395u);

    for (second = 0; second < RTC_SECONDS_PER_DAY; ++second) {
        RTCCore_SecondsToTime(second, &time);
        assert(RTCCore_TimeValid(&time));
        assert(RTCCore_TimeToSeconds(&time) == second);
    }
    RTCCore_SecondsToTime(86399, &time);
    assert(time.hour == 23 && time.minute == 59 && time.second == 59);
    RTCCore_SecondsToTime(86400, &time);
    assert(time.hour == 0 && time.minute == 0 && time.second == 0);
    RTCCore_SecondsToTime(86400u * 10u + 3661u, &time);
    assert(time.hour == 1 && time.minute == 1 && time.second == 1);

    RTCCore_Init(&model, 86395u);
    assert(!RTCCore_NextAlarm(&model, 86395u, &target));
    time = Time(0, 0, 0);
    assert(!RTCCore_SetAlarm(&model, 0, &time, 1, 86395u));
    assert(!RTCCore_SetAlarm(&model, 4, &time, 1, 86395u));
    assert(!RTCCore_SetAlarm(&model, 1, 0, 1, 86395u));
    assert(RTCCore_SetAlarm(&model, 1, &time, 1, 86395u));
    assert(RTCCore_NextAlarm(&model, 86395u, &target));
    assert(target == 86400u);
    assert(RTCCore_Update(&model, 86399u) == 0);
    assert(RTCCore_Update(&model, 86400u) == 1);
    assert(model.active_mask == 1);
    RTCCore_Acknowledge(&model);
    assert(model.active_mask == 0 && model.alarms[0].enabled);
    assert(RTCCore_Update(&model, 86400u) == 0);
    assert(RTCCore_NextAlarm(&model, 86400u, &target) && target == 172800u);
    assert(RTCCore_Update(&model, 172800u) == 1);

    /* Nearest selection, simultaneous slots, and late processing. */
    RTCCore_Init(&model, 100u);
    time = Time(0, 2, 0);
    other = Time(0, 1, 45);
    assert(RTCCore_SetAlarm(&model, 1, &time, 1, 100u));
    assert(RTCCore_SetAlarm(&model, 2, &other, 1, 100u));
    assert(RTCCore_SetAlarm(&model, 3, &other, 1, 100u));
    assert(RTCCore_NextAlarm(&model, 100u, &target) && target == 105u);
    assert(RTCCore_Update(&model, 110u) == 6);
    assert(model.active_mask == 6);
    assert(RTCCore_NextAlarm(&model, 110u, &target) && target == 120u);
    assert(RTCCore_Update(&model, 125u) == 1);
    assert(model.active_mask == 7);
    RTCCore_Acknowledge(&model);
    assert(RTCCore_Update(&model, 125u) == 0);
    assert(RTCCore_Update(&model, 125u + 2u * RTC_SECONDS_PER_DAY) == 7);

    /* OFF keeps time; DEL clears it; disabling/deleting removes that reminder. */
    assert(RTCCore_EnableAlarm(&model, 2, 0, model.last_counter));
    assert(!model.alarms[1].enabled);
    assert(RTCCore_TimeToSeconds(&model.alarms[1].time) == 105u);
    assert(model.active_mask == 5);
    assert(RTCCore_DeleteAlarm(&model, 3));
    assert(!model.alarms[2].enabled);
    assert(RTCCore_TimeToSeconds(&model.alarms[2].time) == 0);
    assert(model.active_mask == 1);
    assert(!RTCCore_EnableAlarm(&model, 4, 1, model.last_counter));
    assert(!RTCCore_DeleteAlarm(&model, 0));

    /* Enabling an alarm at current time rings once; next target is tomorrow. */
    RTCCore_Init(&model, 3661u);
    time = Time(1, 1, 1);
    assert(RTCCore_SetAlarm(&model, 1, &time, 1, 3661u));
    assert(model.active_mask == 1);
    assert(RTCCore_NextAlarm(&model, 3661u, &target));
    assert(target == 3661u + RTC_SECONDS_PER_DAY);
    RTCCore_Acknowledge(&model);
    assert(RTCCore_Update(&model, 3661u) == 0);

    /* Manual time jumps rebase instead of incorrectly treating backward jumps as days. */
    RTCCore_SetClock(&model, 100u);
    assert(model.active_mask == 0);
    assert(RTCCore_Update(&model, 101u) == 0);
    RTCCore_SetClock(&model, 3661u);
    assert(model.active_mask == 1);
    RTCCore_SetClock(&model, 4000u);
    assert(model.active_mask == 0);
    assert(RTCCore_Update(&model, 4001u) == 0);
    assert(RTCCore_NextAlarm(&model, 4001u, &target));
    assert(target == RTC_SECONDS_PER_DAY + 3661u);

    puts("PASS: all 86400 times, midnight, three alarms, nearest, late/simultaneous, daily, OFF/DEL, time jumps");
    return 0;
}
