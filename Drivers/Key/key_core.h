#ifndef KEY_CORE_H
#define KEY_CORE_H

#include <stdint.h>

#define KEY_COUNT             2u
#define KEY_SCAN_PERIOD_MS    5u
#define KEY_DEBOUNCE_MS       20u
#define KEY_LONG_PRESS_MS     1000u
#define KEY_EVENT_QUEUE_SIZE  32u
#define KEY1_MASK             (1u << 0)
#define KEY2_MASK             (1u << 1)

typedef enum {
    KEY_ID_1 = 0,
    KEY_ID_2 = 1
} KeyId;

typedef enum {
    KEY_EVENT_PRESS = 0, /* Debounced down edge; useful for alarm acknowledgement. */
    KEY_EVENT_SHORT,     /* On release, only if LONG was not emitted. */
    KEY_EVENT_LONG,      /* Once per hold, strictly greater than 1000 ms. */
    KEY_EVENT_RELEASE
} KeyEventType;

typedef struct {
    KeyId key;
    KeyEventType type;
    uint32_t timestamp_ms;
} KeyEvent;

/* No hardware dependencies. Call Init before enabling the scan interrupt. */
void KeyCore_Init(void);
/* pressed_mask uses 1 for pressed, regardless of physical GPIO polarity. */
void KeyCore_ScanTick(uint8_t pressed_mask);
/* Consumer access must exclude ScanTick, e.g. with a short IRQ critical section. */
uint8_t KeyCore_PopEvent(KeyEvent *event);
uint8_t KeyCore_GetPressedMask(void);
uint32_t KeyCore_GetDroppedCount(void);

#endif
