#ifndef UI_CORE_H
#define UI_CORE_H
#include <stdint.h>
#include "rtc_core.h"
#include "key_core.h"

typedef enum { UI_MAIN = 0, UI_TIME, UI_ALARM, UI_IMU, UI_SERVO } UIPage;
typedef enum {
    UI_ACTION_NONE = 0, UI_ACTION_PAGE, UI_ACTION_TIME_SAVE, UI_ACTION_ALARM_SAVE,
    UI_ACTION_SERVO_CONFIRM, UI_ACTION_HOLD_TOGGLE, UI_ACTION_ALARM_SLOT,
    UI_ACTION_IMU_CALIBRATE
} UIAction;
typedef struct {
    UIPage page;
    uint8_t menu_selection;
    uint8_t parameter;
    uint8_t alarm_number;
    RTCClockTime time_edit;
    RTCClockTime alarm_edit;
    uint8_t alarm_enabled;
    int16_t servo_edit_deg;
    int16_t attitude_edit_tenths;
    uint8_t servo_dirty;
} UIState;

void UI_Init(UIState *ui);
UIAction UI_Key(UIState *ui, KeyId key, KeyEventType type, uint8_t direct_menu);
void UI_Rotate(UIState *ui, int32_t steps, uint8_t hold_enabled);
int32_t UI_Wrap(int32_t value, int32_t delta, int32_t range);
#endif
