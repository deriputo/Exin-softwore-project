#ifndef BLUETOOTH_H
#define BLUETOOTH_H
#include "serial.h"
/* HC-04 transparent UART: USART3 PB10 TX / PB11 RX, default 9600 8N1. */
ErrorStatus Bluetooth_Init(void);
uint8_t Bluetooth_ReadByte(uint8_t *byte);
uint8_t Bluetooth_Write(const char *bytes, uint16_t length);
uint8_t Bluetooth_WriteString(const char *text);
uint8_t Bluetooth_TxIdle(void);
uint8_t Bluetooth_IsIdle(void);
void Bluetooth_GetStats(SerialStats *stats);
void Bluetooth_IRQHandler(void);
#endif
