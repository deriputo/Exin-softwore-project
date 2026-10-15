#include <assert.h>
#include <stdio.h>
#include "serial_ring.h"

int main(void)
{
    SerialRing ring;
    uint8_t byte;
    uint8_t data[32];
    uint32_t cycle;
    uint16_t i;
    SerialRing_Init(&ring);
    assert(SerialRing_Count(&ring) == 0);
    assert(SerialRing_Space(&ring) == SERIAL_RING_CAPACITY - 1u);
    assert(!SerialRing_Pop(&ring, &byte));
    for (i = 0; i < SERIAL_RING_CAPACITY - 1u; ++i) {
        assert(SerialRing_Push(&ring, (uint8_t)i));
    }
    assert(!SerialRing_Push(&ring, 42));
    assert(SerialRing_Space(&ring) == 0);
    for (i = 0; i < SERIAL_RING_CAPACITY - 1u; ++i) {
        assert(SerialRing_Pop(&ring, &byte));
        assert(byte == (uint8_t)i);
    }
    assert(!SerialRing_Pop(&ring, &byte));
    assert(!SerialRing_Pop(&ring, 0));
    for (i = 0; i < 32u; ++i) { data[i] = (uint8_t)i; }
    for (cycle = 0; cycle < 10000u; ++cycle) {
        assert(SerialRing_Write(&ring, data, 32));
        for (i = 0; i < 32u; ++i) {
            assert(SerialRing_Pop(&ring, &byte));
            assert(byte == (uint8_t)i);
        }
    }
    for (i = 0; i < SERIAL_RING_CAPACITY - 20u; ++i) { assert(SerialRing_Push(&ring, 1)); }
    i = SerialRing_Count(&ring);
    assert(!SerialRing_Write(&ring, data, 32));
    assert(SerialRing_Count(&ring) == i); /* no partial write */
    assert(!SerialRing_Write(&ring, 0, 1));
    assert(SerialRing_Write(&ring, 0, 0));
    puts("PASS: RX/TX ring full, wrap, FIFO order, all-or-none enqueue");
    return 0;
}
