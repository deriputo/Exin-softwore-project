#include "encoder_core.h"

static int64_t AddPosition(EncoderCore *encoder, int64_t position, int32_t delta)
{
    if (delta > 0 && position > INT64_MAX - delta) {
        encoder->faults |= ENCODER_FAULT_POSITION_LIMIT;
        return INT64_MAX;
    }
    if (delta < 0 && position < INT64_MIN - delta) {
        encoder->faults |= ENCODER_FAULT_POSITION_LIMIT;
        return INT64_MIN;
    }
    return position + delta;
}

uint8_t EncoderCore_Init(EncoderCore *encoder, uint16_t counter,
                         uint16_t counts_per_step, int8_t direction)
{
    if (encoder == 0 || counts_per_step == 0 || counts_per_step > 32767u ||
        (direction != 1 && direction != -1)) {
        return 0;
    }
    encoder->counts_per_step = counts_per_step;
    encoder->direction = direction;
    EncoderCore_ResetPosition(encoder, counter);
    return 1;
}

void EncoderCore_ResetPosition(EncoderCore *encoder, uint16_t counter)
{
    encoder->previous_counter = counter;
    encoder->remainder = 0;
    encoder->position_counts = 0;
    encoder->position_steps = 0;
    encoder->faults = 0;
}

int32_t EncoderCore_Update(EncoderCore *encoder, uint16_t counter)
{
    uint32_t wrapped_delta;
    int32_t delta;
    int32_t steps;

    wrapped_delta = ((uint32_t)counter + 65536u - encoder->previous_counter) & 0xffffu;
    encoder->previous_counter = counter;

    if (wrapped_delta == 32768u) {
        /* +32768 and -32768 cannot be distinguished without a shorter sample interval. */
        encoder->faults |= ENCODER_FAULT_AMBIGUOUS_DELTA;
        encoder->remainder = 0;
        return 0;
    }
    delta = wrapped_delta > 32768u ? (int32_t)wrapped_delta - 65536 : (int32_t)wrapped_delta;
    delta *= encoder->direction;
    encoder->position_counts = AddPosition(encoder, encoder->position_counts, delta);

    encoder->remainder += delta;
    steps = encoder->remainder / (int32_t)encoder->counts_per_step;
    encoder->remainder -= steps * (int32_t)encoder->counts_per_step;
    encoder->position_steps = AddPosition(encoder, encoder->position_steps, steps);
    return steps;
}
