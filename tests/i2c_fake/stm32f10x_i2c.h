#ifndef FAKE_I2C_SPL_H
#define FAKE_I2C_SPL_H
#include "stm32f10x.h"
typedef struct {
    uint32_t I2C_ClockSpeed;
    uint16_t I2C_Mode, I2C_DutyCycle, I2C_OwnAddress1, I2C_Ack, I2C_AcknowledgedAddress;
} I2C_InitTypeDef;
#define I2C_Mode_I2C 0u
#define I2C_DutyCycle_2 0u
#define I2C_Ack_Enable 1u
#define I2C_AcknowledgedAddress_7bit 0u
#define I2C_Direction_Transmitter 0u
#define I2C_Direction_Receiver 1u
#define I2C_NACKPosition_Current 0u
#define I2C_NACKPosition_Next 1u
void I2C_DeInit(I2C_TypeDef *i);
void I2C_StructInit(I2C_InitTypeDef *init);
void I2C_Init(I2C_TypeDef *i, I2C_InitTypeDef *init);
void I2C_Cmd(I2C_TypeDef *i, FunctionalState value);
void I2C_AcknowledgeConfig(I2C_TypeDef *i, FunctionalState value);
void I2C_NACKPositionConfig(I2C_TypeDef *i, uint16_t value);
void I2C_GenerateSTART(I2C_TypeDef *i, FunctionalState value);
void I2C_GenerateSTOP(I2C_TypeDef *i, FunctionalState value);
void I2C_Send7bitAddress(I2C_TypeDef *i, uint8_t address, uint8_t direction);
void I2C_SendData(I2C_TypeDef *i, uint8_t byte);
uint8_t I2C_ReceiveData(I2C_TypeDef *i);
#endif
