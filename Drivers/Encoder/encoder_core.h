#ifndef ENCODER_CORE_H
#define ENCODER_CORE_H

#include <stdint.h>

#define ENCODER_FAULT_AMBIGUOUS_DELTA  (1u << 0)
#define ENCODER_FAULT_POSITION_LIMIT   (1u << 1)

typedef struct {
    uint16_t previous_counter;
    uint16_t counts_per_step;
    int8_t direction;
    int32_t remainder;
    int64_t position_counts;
    int64_t position_steps;
    uint8_t faults;
} EncoderCore;

/* Returns 0 for an invalid pointer, step size, or direction. */
uint8_t EncoderCore_Init(EncoderCore *encoder, uint16_t counter,
                         uint16_t counts_per_step, int8_t direction);
/* Returns signed complete steps since the previous sample. Keeps fractional counts.
   Actual displacement between samples must be strictly within (-32768,32768). */
int32_t EncoderCore_Update(EncoderCore *encoder, uint16_t counter);
/* Rebaseline without writing hardware CNT; discards partial steps and faults. */
void EncoderCore_ResetPosition(EncoderCore *encoder, uint16_t counter);

#endif
