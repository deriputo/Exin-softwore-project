#include "serial_ring.h"

#if SERIAL_RING_CAPACITY < 2 || (SERIAL_RING_CAPACITY & (SERIAL_RING_CAPACITY - 1)) != 0 || SERIAL_RING_CAPACITY > 32768
#error SERIAL_RING_CAPACITY must be a power of two in 2..32768
#endif

#define MASK ((uint16_t)(SERIAL_RING_CAPACITY - 1u))

void SerialRing_Init(SerialRing *ring)
{
    ring->head = 0;
    ring->tail = 0;
}

uint16_t SerialRing_Count(const SerialRing *ring)
{
    return (uint16_t)((ring->head - ring->tail) & MASK);
}

uint16_t SerialRing_Space(const SerialRing *ring)
{
    return (uint16_t)(MASK - SerialRing_Count(ring));
}

uint8_t SerialRing_Push(SerialRing *ring, uint8_t byte)
{
    uint16_t next = (uint16_t)((ring->head + 1u) & MASK);
    if (next == ring->tail) {
        return 0;
    }
    ring->data[ring->head] = byte;
    ring->head = next;
    return 1;
}

uint8_t SerialRing_Pop(SerialRing *ring, uint8_t *byte)
{
    if (!byte || ring->head == ring->tail) {
        return 0;
    }
    *byte = ring->data[ring->tail];
    ring->tail = (uint16_t)((ring->tail + 1u) & MASK);
    return 1;
}

uint8_t SerialRing_Write(SerialRing *ring, const uint8_t *bytes, uint16_t length)
{
    uint16_t i;
    if ((!bytes && length) || length > SerialRing_Space(ring)) {
        return 0;
    }
    for (i = 0; i < length; ++i) {
        (void)SerialRing_Push(ring, bytes[i]);
    }
    return 1;
}
