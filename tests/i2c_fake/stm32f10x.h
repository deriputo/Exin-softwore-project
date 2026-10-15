#ifndef FAKE_STM32_I2C_H
#define FAKE_STM32_I2C_H
#include <stdint.h>
typedef enum { DISABLE = 0, ENABLE = 1 } FunctionalState;
typedef struct { volatile uint16_t SR1, SR2; } I2C_TypeDef;
extern I2C_TypeDef fake_i2c1;
#define I2C1 (&fake_i2c1)
#define I2C_SR1_SB 0x0001u
#define I2C_SR1_ADDR 0x0002u
#define I2C_SR1_BTF 0x0004u
#define I2C_SR1_RXNE 0x0040u
#define I2C_SR1_TXE 0x0080u
#define I2C_SR1_BERR 0x0100u
#define I2C_SR1_ARLO 0x0200u
#define I2C_SR1_AF 0x0400u
#define I2C_SR1_OVR 0x0800u
#define I2C_SR2_BUSY 0x0002u
uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __set_PRIMASK(uint32_t state);
#endif
