#ifndef TEST_POWER_STM32F10X_H
#define TEST_POWER_STM32F10X_H
#include "../rtc_fake/stm32f10x.h"
typedef struct { volatile uint32_t CR, CSR; } PWR_TypeDef;
typedef struct { volatile uint32_t SCR; } SCB_TypeDef;
typedef struct {
    uint32_t EXTI_Line;
    uint8_t EXTI_Mode, EXTI_Trigger;
    FunctionalState EXTI_LineCmd;
} EXTI_InitTypeDef;
extern PWR_TypeDef fake_pwr;
extern SCB_TypeDef fake_scb;
extern GPIO_TypeDef fake_gpioa;
#define PWR (&fake_pwr)
#define SCB (&fake_scb)
#define GPIOA (&fake_gpioa)
#define RCC_APB2Periph_GPIOA 8u
#define EXTI_Line17 (1u << 17)
#define EXTI_Mode_Event 1u
#define EXTI_Trigger_Rising 1u
#define SCB_SCR_SLEEPDEEP_Msk 4u
#define PWR_CR_PDDS 2u
uint32_t __get_PRIMASK(void);
void __set_PRIMASK(uint32_t value);
void __disable_irq(void);
void __DSB(void);
void __WFI(void);
void EXTI_StructInit(EXTI_InitTypeDef *config);
void EXTI_Init(EXTI_InitTypeDef *config);
void EXTI_ClearFlag(uint32_t flags);
void PWR_WakeUpPinCmd(FunctionalState enable);
uint8_t GPIO_ReadInputDataBit(GPIO_TypeDef *port, uint16_t pin);
#endif
