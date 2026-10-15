#include <assert.h>
#include <stdio.h>
#include "key_core.h"

static void Tick(uint8_t mask, unsigned int count)
{
    while (count-- != 0u) {
        KeyCore_ScanTick(mask);
    }
}

static KeyEvent Expect(KeyId key, KeyEventType type)
{
    KeyEvent event;
    assert(KeyCore_PopEvent(&event));
    assert(event.key == key);
    assert(event.type == type);
    return event;
}

static void Empty(void)
{
    KeyEvent event;
    assert(!KeyCore_PopEvent(&event));
}

int main(void)
{
    KeyEvent event;
    unsigned int i;

    /* Idle and bounce shorter than the debounce window produce no events. */
    KeyCore_Init();
    Tick(0, 100);
    Tick(KEY1_MASK, 4);
    Tick(0, 5);
    Empty();
    assert(KeyCore_GetPressedMask() == 0);

    /* First observed edge + 20 ms of consecutive samples confirms the press. */
    Tick(KEY1_MASK, 4);
    Empty();
    Tick(KEY1_MASK, 1);
    Expect(KEY_ID_1, KEY_EVENT_PRESS);
    assert(KeyCore_GetPressedMask() == KEY1_MASK);
    Tick(KEY1_MASK, 10);
    Tick(0, 5);
    Expect(KEY_ID_1, KEY_EVENT_SHORT);
    Expect(KEY_ID_1, KEY_EVENT_RELEASE);
    Empty();

    /* Exactly 1000 ms is not long; 1005 ms is. Holding never repeats LONG. */
    KeyCore_Init();
    Tick(KEY2_MASK, 5);
    event = Expect(KEY_ID_2, KEY_EVENT_PRESS);
    assert(event.timestamp_ms == 25);
    Tick(KEY2_MASK, 200);
    Empty();
    Tick(KEY2_MASK, 1);
    event = Expect(KEY_ID_2, KEY_EVENT_LONG);
    assert(event.timestamp_ms == 1030);
    Tick(KEY2_MASK, 10000);
    Empty();
    Tick(0, 5);
    Expect(KEY_ID_2, KEY_EVENT_RELEASE);
    Empty();

    /* Release bounce must not split one gesture into multiple gestures. */
    KeyCore_Init();
    Tick(KEY1_MASK, 5);
    Expect(KEY_ID_1, KEY_EVENT_PRESS);
    Tick(0, 3);
    Tick(KEY1_MASK, 3);
    Empty();
    assert(KeyCore_GetPressedMask() == KEY1_MASK);
    Tick(0, 5);
    Expect(KEY_ID_1, KEY_EVENT_SHORT);
    Expect(KEY_ID_1, KEY_EVENT_RELEASE);
    Empty();

    /* Both keys retain independent state and both appear in the state mask. */
    KeyCore_Init();
    Tick(KEY1_MASK | KEY2_MASK, 5);
    Expect(KEY_ID_1, KEY_EVENT_PRESS);
    Expect(KEY_ID_2, KEY_EVENT_PRESS);
    assert(KeyCore_GetPressedMask() == (KEY1_MASK | KEY2_MASK));
    Tick(KEY2_MASK, 5);
    Expect(KEY_ID_1, KEY_EVENT_SHORT);
    Expect(KEY_ID_1, KEY_EVENT_RELEASE);
    assert(KeyCore_GetPressedMask() == KEY2_MASK);
    Tick(KEY2_MASK, 196);
    Expect(KEY_ID_2, KEY_EVENT_LONG);
    Tick(0, 5);
    Expect(KEY_ID_2, KEY_EVENT_RELEASE);
    Empty();

    /* Multiple gestures survive a delayed main-loop consumer in FIFO order. */
    KeyCore_Init();
    for (i = 0; i < 8; ++i) {
        Tick(KEY1_MASK, 5);
        Tick(0, 5);
    }
    for (i = 0; i < 8; ++i) {
        Expect(KEY_ID_1, KEY_EVENT_PRESS);
        Expect(KEY_ID_1, KEY_EVENT_SHORT);
        Expect(KEY_ID_1, KEY_EVENT_RELEASE);
    }
    Empty();
    assert(KeyCore_GetDroppedCount() == 0);

    /* Full queue drops new events and records the loss, without corruption. */
    KeyCore_Init();
    for (i = 0; i < 12; ++i) {
        Tick(KEY1_MASK, 5);
        Tick(0, 5);
    }
    assert(KeyCore_GetDroppedCount() == 4);
    for (i = 0; i < KEY_EVENT_QUEUE_SIZE; ++i) {
        assert(KeyCore_PopEvent(&event));
        assert(event.key == KEY_ID_1);
        assert(event.type == (i % 3 == 0 ? KEY_EVENT_PRESS :
                              i % 3 == 1 ? KEY_EVENT_SHORT : KEY_EVENT_RELEASE));
    }
    Empty();
    Tick(KEY2_MASK, 5);
    Expect(KEY_ID_2, KEY_EVENT_PRESS);
    assert(!KeyCore_PopEvent(0));
    Empty();

    puts("PASS: debounce, short/long, threshold, no-repeat, two-key state, FIFO, overflow");
    return 0;
}
