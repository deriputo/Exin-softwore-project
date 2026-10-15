#include "stm32f10x.h"
#define RCC_APB2Periph_GPIOB 1u
#define RCC_APB2Periph_AFIO 2u
#define RCC_APB1Periph_I2C1 4u
void RCC_APB2PeriphClockCmd(uint32_t p, FunctionalState value);
void RCC_APB1PeriphClockCmd(uint32_t p, FunctionalState value);
