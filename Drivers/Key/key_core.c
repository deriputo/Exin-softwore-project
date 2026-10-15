#include "key_core.h"

#define DEBOUNCE_TICKS ((KEY_DEBOUNCE_MS + KEY_SCAN_PERIOD_MS - 1u) / KEY_SCAN_PERIOD_MS)

typedef struct {
    uint8_t candidate;
    uint8_t stable;
    uint16_t stable_ticks;
    uint32_t held_ms;
    uint8_t long_sent;
} KeyState;

static KeyState keys[KEY_COUNT];
static volatile KeyEvent queue[KEY_EVENT_QUEUE_SIZE];
static volatile uint8_t read_index;
static volatile uint8_t write_index;
static volatile uint8_t queue_count;
static volatile uint8_t pressed_state;
static volatile uint32_t dropped_count;
static uint32_t scan_time_ms;

static void PushEvent(uint8_t key, KeyEventType type)
{
    if (queue_count == KEY_EVENT_QUEUE_SIZE) {
        /* Keep older events; report overflow instead of silently overwriting. */
        ++dropped_count;
        return;
    }
    queue[write_index].key = (KeyId)key;
    queue[write_index].type = type;
    queue[write_index].timestamp_ms = scan_time_ms;
    write_index = (uint8_t)((write_index + 1u) % KEY_EVENT_QUEUE_SIZE);
    ++queue_count;
}

void KeyCore_Init(void)
{
    uint8_t i;
    for (i = 0; i < KEY_COUNT; ++i) {
        keys[i].candidate = 0;
        keys[i].stable = 0;
        keys[i].stable_ticks = 0;
        keys[i].held_ms = 0;
        keys[i].long_sent = 0;
    }
    read_index = 0;
    write_index = 0;
    queue_count = 0;
    pressed_state = 0;
    dropped_count = 0;
    scan_time_ms = 0;
}

void KeyCore_ScanTick(uint8_t pressed_mask)
{
    uint8_t i;
    scan_time_ms += KEY_SCAN_PERIOD_MS;

    for (i = 0; i < KEY_COUNT; ++i) {
        KeyState *key = &keys[i];
        uint8_t raw = (uint8_t)((pressed_mask >> i) & 1u);

        if (raw != key->candidate) {
            key->candidate = raw;
            key->stable_ticks = 0; /* Start the 20 ms window at the first observation. */
        } else if (key->stable_ticks < DEBOUNCE_TICKS) {
            ++key->stable_ticks;
        }

        if (key->stable != key->candidate && key->stable_ticks >= DEBOUNCE_TICKS) {
            key->stable = key->candidate;
            if (key->stable) {
                pressed_state |= (uint8_t)(1u << i);
                key->held_ms = 0;
                key->long_sent = 0;
                PushEvent(i, KEY_EVENT_PRESS);
            } else {
                pressed_state &= (uint8_t)~(1u << i);
                if (!key->long_sent) {
                    PushEvent(i, KEY_EVENT_SHORT);
                }
                PushEvent(i, KEY_EVENT_RELEASE);
                key->held_ms = 0;
            }
            continue;
        }

        /* Duration is measured on the debounced state. No repeated long events. */
        if (key->stable && !key->long_sent) {
            key->held_ms += KEY_SCAN_PERIOD_MS;
            if (key->held_ms > KEY_LONG_PRESS_MS) {
                key->long_sent = 1;
                PushEvent(i, KEY_EVENT_LONG);
            }
        }
    }
}

uint8_t KeyCore_PopEvent(KeyEvent *event)
{
    if (event == 0 || queue_count == 0) {
        return 0;
    }
    event->key = queue[read_index].key;
    event->type = queue[read_index].type;
    event->timestamp_ms = queue[read_index].timestamp_ms;
    read_index = (uint8_t)((read_index + 1u) % KEY_EVENT_QUEUE_SIZE);
    --queue_count;
    return 1;
}

uint8_t KeyCore_GetPressedMask(void)
{
    return pressed_state;
}

uint32_t KeyCore_GetDroppedCount(void)
{
    return dropped_count;
}
