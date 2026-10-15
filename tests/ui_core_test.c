#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include "ui_core.h"
int main(void)
{
    UIState ui;
    int i;
    UI_Init(&ui);
    for (i = 0; i < 5; ++i) {
        assert(ui.page == (UIPage)i);
        assert(UI_Key(&ui, KEY_ID_1, KEY_EVENT_SHORT, 0) == UI_ACTION_PAGE);
    }
    assert(ui.page == UI_MAIN);
    assert(UI_Key(&ui, KEY_ID_1, KEY_EVENT_LONG, 0) == UI_ACTION_NONE);
    UI_Rotate(&ui, 3, 0);
    assert(UI_Key(&ui, KEY_ID_1, KEY_EVENT_SHORT, 1) == UI_ACTION_PAGE && ui.page == UI_SERVO);
    UI_Rotate(&ui, 200, 0);
    assert(ui.servo_edit_deg == 180);
    UI_Rotate(&ui, -500, 0);
    assert(ui.servo_edit_deg == 0);
    assert(UI_Key(&ui, KEY_ID_2, KEY_EVENT_SHORT, 1) == UI_ACTION_SERVO_CONFIRM);
    assert(UI_Key(&ui, KEY_ID_2, KEY_EVENT_LONG, 1) == UI_ACTION_HOLD_TOGGLE);
    UI_Rotate(&ui, 100, 1);
    assert(ui.attitude_edit_tenths == 1000);
    UI_Rotate(&ui, -200, 1);
    assert(ui.attitude_edit_tenths == 0);
    UI_Rotate(&ui, INT32_MAX, 1); assert(ui.attitude_edit_tenths == 1800);
    ui.page = UI_TIME;
    ui.parameter = 0;
    UI_Rotate(&ui, 1, 0);
    assert(ui.time_edit.hour == 0);
    UI_Rotate(&ui, -1, 0);
    assert(ui.time_edit.hour == 23);
    UI_Key(&ui, KEY_ID_2, KEY_EVENT_SHORT, 0);
    UI_Rotate(&ui, 1, 0);
    assert(ui.time_edit.minute == 0);
    UI_Key(&ui, KEY_ID_2, KEY_EVENT_SHORT, 0);
    UI_Rotate(&ui, 5, 0);
    assert(ui.time_edit.second == 0);
    assert(UI_Key(&ui, KEY_ID_2, KEY_EVENT_LONG, 0) == UI_ACTION_TIME_SAVE);
    ui.page = UI_ALARM;
    assert(UI_Key(&ui, KEY_ID_1, KEY_EVENT_LONG, 0) == UI_ACTION_ALARM_SLOT && ui.alarm_number == 2);
    ui.parameter = 3;
    UI_Rotate(&ui, 1, 0); assert(ui.alarm_enabled);
    UI_Rotate(&ui, -1, 0); assert(!ui.alarm_enabled);
    UI_Key(&ui, KEY_ID_2, KEY_EVENT_SHORT, 0); assert(ui.parameter == 0);
    assert(UI_Key(&ui, KEY_ID_2, KEY_EVENT_LONG, 0) == UI_ACTION_ALARM_SAVE);
    assert(UI_Wrap(0, INT32_MIN, 60) >= 0 && UI_Wrap(0, INT32_MIN, 60) < 60);
    puts("PASS: five pages, menu selection, edit/save, time wrap, alarm slots, servo/attitude limits");
    return 0;
}
