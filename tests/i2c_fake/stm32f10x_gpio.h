#include "stm32f10x.h"
typedef struct { uint16_t GPIO_Pin, GPIO_Mode, GPIO_Speed; } GPIO_InitTypeDef;
#define GPIO_Pin_8 0x0100u
#define GPIO_Pin_9 0x0200u
#define GPIO_Mode_AF_OD 1
#define GPIO_Speed_50MHz 3
#define GPIO_Remap_I2C1 1
#define GPIOB ((void *)0)
void GPIO_PinRemapConfig(uint32_t remap, FunctionalState value);
void GPIO_StructInit(GPIO_InitTypeDef *gpio);
void GPIO_Init(void *port, GPIO_InitTypeDef *gpio);
