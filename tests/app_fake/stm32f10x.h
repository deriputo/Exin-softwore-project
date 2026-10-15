#ifndef APP_FAKE_STM32_H
#define APP_FAKE_STM32_H
#include <stdint.h>
typedef enum { ERROR = 0, SUCCESS = 1 } ErrorStatus;
typedef struct { uint32_t IDR; } GPIO_TypeDef;
typedef struct { uint32_t SR; } USART_TypeDef;
extern GPIO_TypeDef app_gpioa;
extern USART_TypeDef app_usart1;
extern USART_TypeDef app_usart3;
#define GPIOA (&app_gpioa)
#define USART1 (&app_usart1)
#define USART3 (&app_usart3)
#define GPIO_Pin_0 1u
#define GPIO_Pin_1 2u
#define GPIO_Pin_2 4u
#define Bit_RESET 0u
#define Bit_SET 1u
#define USART_SR_RXNE 0x20u
#define USART_SR_ORE 0x08u
#define USART_SR_FE 0x02u
#define USART_SR_NE 0x04u
uint8_t GPIO_ReadInputDataBit(GPIO_TypeDef *port, uint16_t pin);
#endif
