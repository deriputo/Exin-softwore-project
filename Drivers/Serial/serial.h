#ifndef SERIAL_H
#define SERIAL_H

#include "stm32f10x.h"

typedef struct {
    uint32_t received;
    uint32_t rx_dropped;
    uint32_t rx_errors;
    uint32_t tx_busy;
} SerialStats;

/* USART1 PA9 TX / PA10 RX, 115200 8N1; do not remap these pins. */
ErrorStatus Serial_Init(void);
uint8_t Serial_ReadByte(uint8_t *byte);
/* Main-loop only. Returns 1 only if the entire message was queued. Never waits. */
uint8_t Serial_Write(const char *bytes, uint16_t length);
uint8_t Serial_WriteString(const char *text);
uint8_t Serial_TxIdle(void);
/* RX queue empty and no untransmitted bytes; used before Stop/Standby. */
uint8_t Serial_IsIdle(void);
void Serial_GetStats(SerialStats *stats);
void Serial_IRQHandler(void);

#endif
