#ifndef ENCODER_CONFIG_H
#define ENCODER_CONFIG_H

/* TIM4_CH1=PB6 (A), TIM4_CH2=PB7 (B), default mapping, common contact to GND.
   TIM4 remap pins PD12/PD13 are not exposed on STM32F103C8T6 LQFP48.
   Future I2C1 uses remapped PB8/PB9 instead of PB6/PB7. */
#define ENCODER_COUNTS_PER_STEP 4u
#define ENCODER_DIRECTION       1 /* Set -1 if clockwise decreases. */
#define ENCODER_INPUT_FILTER    15u /* 0..15; short-spike filtering, not ms debounce. */

#if ENCODER_COUNTS_PER_STEP < 1 || ENCODER_COUNTS_PER_STEP > 32767
#error ENCODER_COUNTS_PER_STEP must be in 1..32767
#endif
#if ENCODER_DIRECTION != 1 && ENCODER_DIRECTION != -1
#error ENCODER_DIRECTION must be 1 or -1
#endif
#if ENCODER_INPUT_FILTER > 15
#error ENCODER_INPUT_FILTER must be in 0..15
#endif

#endif
