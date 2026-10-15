#ifndef SERIAL_CONFIG_H
#define SERIAL_CONFIG_H

#define SERIAL_BAUD_RATE 115200u
#define SERIAL_IRQ_PREEMPTION_PRIORITY 2u
#define SERIAL_IRQ_SUBPRIORITY 0u
/* Power-of-two capacity, with one slot reserved to distinguish full/empty. */
#define SERIAL_RING_CAPACITY 512u

#endif
