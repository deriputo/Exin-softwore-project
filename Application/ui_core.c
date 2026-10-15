#include "ui_core.h"
#include <string.h>

int32_t UI_Wrap(int32_t value, int32_t delta, int32_t range)
{
    int32_t result = (value + delta % range) % range;
    return result < 0 ? result + range : result;
}

void UI_Init(UIState *ui)
{
    memset(ui, 0, sizeof(*ui));
    ui->page = UI_MAIN;
    ui->alarm_number = 1;
    ui->servo_edit_deg = 90;
    ui->time_edit.hour = 23;
    ui->time_edit.minute = 59;
    ui->time_edit.second = 55;
}

UIAction UI_Key(UIState *ui, KeyId key, KeyEventType type, uint8_t direct_menu)
{
    if (type == KEY_EVENT_SHORT && key == KEY_ID_1) {
        if (ui->page == UI_MAIN && direct_menu) {
            ui->page = (UIPage)(ui->menu_selection + 1u);
        } else {
            ui->page = (UIPage)(((unsigned int)ui->page + 1u) % 5u);
        }
        ui->parameter = 0;
        ui->servo_dirty = 0;
        return UI_ACTION_PAGE;
    }
    if (key == KEY_ID_1 && type == KEY_EVENT_LONG && ui->page == UI_ALARM) {
        ui->alarm_number = (uint8_t)(ui->alarm_number % 3u + 1u);
        ui->parameter = 0;
        return UI_ACTION_ALARM_SLOT;
    }
    if (key != KEY_ID_2) { return UI_ACTION_NONE; }
    if (type == KEY_EVENT_SHORT) {
        if (ui->page == UI_TIME) { ui->parameter = (uint8_t)((ui->parameter + 1u) % 3u); }
        if (ui->page == UI_ALARM) { ui->parameter = (uint8_t)((ui->parameter + 1u) % 4u); }
        if (ui->page == UI_SERVO) { return UI_ACTION_SERVO_CONFIRM; }
    } else if (type == KEY_EVENT_LONG) {
        if (ui->page == UI_TIME) { return UI_ACTION_TIME_SAVE; }
        if (ui->page == UI_ALARM) { return UI_ACTION_ALARM_SAVE; }
        if (ui->page == UI_SERVO) { return UI_ACTION_HOLD_TOGGLE; }
        if (ui->page == UI_IMU) { return UI_ACTION_IMU_CALIBRATE; }
    }
    return UI_ACTION_NONE;
}

static void EditTime(RTCClockTime *time, uint8_t parameter, int32_t steps)
{
    if (parameter == 0) { time->hour = (uint8_t)UI_Wrap(time->hour, steps, 24); }
    else if (parameter == 1) { time->minute = (uint8_t)UI_Wrap(time->minute, steps, 60); }
    else { time->second = (uint8_t)UI_Wrap(time->second, steps, 60); }
}

void UI_Rotate(UIState *ui, int32_t steps, uint8_t hold_enabled)
{
    int64_t value;
    if (steps == 0) { return; }
    if (ui->page == UI_MAIN) { ui->menu_selection = (uint8_t)UI_Wrap(ui->menu_selection, steps, 4); }
    else if (ui->page == UI_TIME) { EditTime(&ui->time_edit, ui->parameter, steps); }
    else if (ui->page == UI_ALARM) {
        if (ui->parameter == 3) { ui->alarm_enabled = (uint8_t)(steps > 0); }
        else { EditTime(&ui->alarm_edit, ui->parameter, steps); }
    } else if (ui->page == UI_SERVO) {
        if (hold_enabled) {
            value = (int64_t)ui->attitude_edit_tenths + (int64_t)steps * 10;
            ui->attitude_edit_tenths = (int16_t)(value < 0 ? 0 : value > 1800 ? 1800 : value);
        } else {
            value = (int64_t)ui->servo_edit_deg + steps;
            ui->servo_edit_deg = (int16_t)(value < 0 ? 0 : value > 180 ? 180 : value);
        }
        ui->servo_dirty = 1;
    }
}
