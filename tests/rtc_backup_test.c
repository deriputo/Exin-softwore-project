#include <assert.h>
#include <stdio.h>
#include "rtc_core.h"

int main(void)
{
    RTCClockModel before, after;
    uint16_t words[RTC_BACKUP_WORD_COUNT];
    uint32_t data, seconds;
    uint8_t slot, word, bit;
    for (seconds = 0; seconds < RTC_SECONDS_PER_DAY; ++seconds) {
        RTCCore_Init(&before, 0xFEDCBA98u);
        for (slot = 0; slot < RTC_LOGICAL_ALARM_COUNT; ++slot) {
            RTCCore_SecondsToTime((seconds + slot * 12345u) % RTC_SECONDS_PER_DAY,
                                 &before.alarms[slot].time);
            before.alarms[slot].enabled = (uint8_t)((seconds >> slot) & 1u);
        }
        RTCCore_PackBackup(&before, seconds ^ 0xABCD0123u, words);
        assert(RTCCore_UnpackBackup(&after, &data, words));
        assert(after.last_counter == before.last_counter && after.active_mask == 0u);
        assert(data == (seconds ^ 0xABCD0123u));
        for (slot = 0; slot < RTC_LOGICAL_ALARM_COUNT; ++slot) {
            assert(RTCCore_TimeToSeconds(&after.alarms[slot].time) ==
                   RTCCore_TimeToSeconds(&before.alarms[slot].time));
            assert(after.alarms[slot].enabled == before.alarms[slot].enabled);
        }
    }
    for (word = 0; word < RTC_BACKUP_WORD_COUNT; ++word) {
        for (bit = 0; bit < 16u; ++bit) {
            words[word] ^= (uint16_t)(1u << bit);
            assert(!RTCCore_UnpackBackup(&after, &data, words));
            words[word] ^= (uint16_t)(1u << bit);
        }
    }
    RTCCore_Init(&before, 86398u);
    before.alarms[0].enabled = 1;
    RTCCore_PackBackup(&before, 0u, words);
    assert(RTCCore_UnpackBackup(&after, &data, words));
    assert(RTCCore_Update(&after, 86401u) == 1u);
    puts("PASS: backup roundtrip 86400 times, 160 single-bit corruptions, alarm wake catch-up");
    return 0;
}
