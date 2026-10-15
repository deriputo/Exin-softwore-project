#include <assert.h>
#include <stdio.h>
#include "encoder_core.h"

int main(void)
{
    EncoderCore e;
    uint16_t counter;
    uint32_t random_state = 123456789u;
    int64_t expected_counts = 0;
    int64_t accumulated_steps = 0;
    unsigned int i;

    assert(!EncoderCore_Init(0, 0, 4, 1));
    assert(!EncoderCore_Init(&e, 0, 0, 1));
    assert(!EncoderCore_Init(&e, 0, 32768u, 1));
    assert(!EncoderCore_Init(&e, 0, 4, 0));
    assert(EncoderCore_Init(&e, 0, 4, 1));
    assert(EncoderCore_Update(&e, 0) == 0);
    assert(EncoderCore_Update(&e, 1) == 0);
    assert(EncoderCore_Update(&e, 2) == 0);
    assert(EncoderCore_Update(&e, 3) == 0);
    assert(EncoderCore_Update(&e, 4) == 1);
    assert(e.position_counts == 4 && e.position_steps == 1 && e.remainder == 0);
    assert(EncoderCore_Update(&e, 3) == 0);
    assert(EncoderCore_Update(&e, 2) == 0);
    assert(EncoderCore_Update(&e, 1) == 0);
    assert(EncoderCore_Update(&e, 0) == -1);
    assert(e.position_counts == 0 && e.position_steps == 0);

    /* Preserve magnitude and partial counts when multiple detents pass in one read. */
    assert(EncoderCore_Update(&e, 17) == 4);
    assert(e.remainder == 1);
    assert(EncoderCore_Update(&e, 20) == 1);
    assert(EncoderCore_Update(&e, 0) == -5);
    assert(e.position_steps == 0 && e.remainder == 0);

    /* Reversal during a partial step and small forward/backward jitter cancel. */
    assert(EncoderCore_Update(&e, 3) == 0);
    assert(EncoderCore_Update(&e, 2) == 0);
    assert(EncoderCore_Update(&e, 1) == 0);
    assert(EncoderCore_Update(&e, 0) == 0);
    for (i = 0; i < 100; ++i) {
        assert(EncoderCore_Update(&e, 1) == 0);
        assert(EncoderCore_Update(&e, 0) == 0);
    }

    /* Signed shortest displacement works in both directions across 16-bit wrap. */
    assert(EncoderCore_Init(&e, 65534u, 4, 1));
    assert(EncoderCore_Update(&e, 2) == 1);
    assert(e.position_counts == 4);
    assert(EncoderCore_Update(&e, 65534u) == -1);
    assert(e.position_counts == 0);
    assert(EncoderCore_Init(&e, 0, 4, 1));
    assert(EncoderCore_Update(&e, 65532u) == -1);
    assert(EncoderCore_Update(&e, 0) == 1);

    /* Configurable direction and counts per mechanical detent. */
    assert(EncoderCore_Init(&e, 0, 2, -1));
    assert(EncoderCore_Update(&e, 2) == -1);
    assert(e.position_counts == -2 && e.position_steps == -1);
    assert(EncoderCore_Update(&e, 0) == 1);
    assert(EncoderCore_Init(&e, 0, 1, 1));
    assert(EncoderCore_Update(&e, 32767u) == 32767);
    assert(EncoderCore_Update(&e, 0) == -32767);

    /* Exactly half a turn of the counter range has no unique direction. */
    assert(EncoderCore_Init(&e, 0, 4, 1));
    assert(EncoderCore_Update(&e, 32768u) == 0);
    assert(e.faults & ENCODER_FAULT_AMBIGUOUS_DELTA);
    assert(e.position_counts == 0 && e.remainder == 0);
    assert(EncoderCore_Update(&e, 32772u) == 1);
    EncoderCore_ResetPosition(&e, 1000);
    assert(e.position_counts == 0 && e.position_steps == 0 && e.faults == 0);
    assert(EncoderCore_Update(&e, 1004) == 1);

    /* Reset also discards half-completed detents and anchors to the new sample. */
    assert(EncoderCore_Update(&e, 1006) == 0);
    EncoderCore_ResetPosition(&e, 1006);
    assert(EncoderCore_Update(&e, 1008) == 0);
    assert(EncoderCore_Update(&e, 1010) == 1);

    /* Position storage must not invoke signed overflow even at extreme limits. */
    assert(EncoderCore_Init(&e, 0, 1, 1));
    e.position_counts = INT64_MAX - 1;
    e.position_steps = INT64_MAX - 1;
    assert(EncoderCore_Update(&e, 2) == 2);
    assert(e.position_counts == INT64_MAX && e.position_steps == INT64_MAX);
    assert(e.faults & ENCODER_FAULT_POSITION_LIMIT);
    EncoderCore_ResetPosition(&e, 0);
    e.position_counts = INT64_MIN + 1;
    e.position_steps = INT64_MIN + 1;
    assert(EncoderCore_Update(&e, 65534u) == -2);
    assert(e.position_counts == INT64_MIN && e.position_steps == INT64_MIN);

    /* Many forward/reverse samples: preserve every count and whole-step result. */
    assert(EncoderCore_Init(&e, 65000u, 4, 1));
    counter = 65000u;
    for (i = 0; i < 100000; ++i) {
        int32_t delta;
        int32_t steps;
        random_state = random_state * 1664525u + 1013904223u;
        delta = (int32_t)(random_state % 2001u) - 1000;
        counter = (uint16_t)(((int32_t)counter + delta + 65536) & 0xffff);
        expected_counts += delta;
        steps = EncoderCore_Update(&e, counter);
        accumulated_steps += steps;
        assert(e.position_counts == expected_counts);
        assert(e.position_steps == accumulated_steps);
        assert(e.position_counts == e.position_steps * 4 + e.remainder);
        assert(e.remainder > -4 && e.remainder < 4);
        assert(e.faults == 0);
    }
    puts("PASS: encoder steps, partial/reverse, magnitude, wrap, config, reset, limits, 100000 samples");
    return 0;
}
