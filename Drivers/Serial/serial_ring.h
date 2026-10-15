#ifndef SERIAL_RING_H
#define SERIAL_RING_H

#include <stdint.h>
#include "serial_config.h"

typedef struct {
    uint8_t data[SERIAL_RING_CAPACITY];
    volatile uint16_t head;
    volatile uint16_t tail;
} SerialRing;

void SerialRing_Init(SerialRing *ring);
uint16_t SerialRing_Count(const SerialRing *ring);
uint16_t SerialRing_Space(const SerialRing *ring);
uint8_t SerialRing_Push(SerialRing *ring, uint8_t byte);
uint8_t SerialRing_Pop(SerialRing *ring, uint8_t *byte);
/* All-or-none; caller owns concurrency protection. */
uint8_t SerialRing_Write(SerialRing *ring, const uint8_t *bytes, uint16_t length);

#endif
