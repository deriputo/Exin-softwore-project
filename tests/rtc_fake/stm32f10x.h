/* Minimal behavior-test doubles, not vendor headers or a firmware SDK. */
#ifndef TEST_RTC_STM32F10X_H
#define TEST_RTC_STM32F10X_H
#include <stdint.h>
typedef enum { ERROR = 0, SUCCESS = 1 } ErrorStatus;
typedef enum { DISABLE = 0, ENABLE = 1 } FunctionalState;
typedef enum { RESET = 0, SET = 1 } FlagStatus;
typedef enum { Bit_RESET = 0, Bit_SET = 1 } BitAction;
typedef struct { volatile uint32_t CNTH, CNTL, CRL, CRH; } RTC_TypeDef;
typedef struct { volatile uint32_t BDCR; } RCC_TypeDef;
typedef struct { uint16_t ODR; } GPIO_TypeDef;
typedef struct { uint16_t GPIO_Pin; uint8_t GPIO_Mode, GPIO_Speed; } GPIO_InitTypeDef;
typedef struct {
    uint8_t NVIC_IRQChannel, NVIC_IRQChannelPreemptionPriority, NVIC_IRQChannelSubPriority;
    FunctionalState NVIC_IRQChannelCmd;
} NVIC_InitTypeDef;
extern RTC_TypeDef fake_rtc;
extern RCC_TypeDef fake_rcc;
extern GPIO_TypeDef fake_gpiob;
#define RTC (&fake_rtc)
#define RCC (&fake_rcc)
#define GPIOB (&fake_gpiob)
#define GPIO_Pin_0 1u
#define GPIO_Mode_Out_PP 1u
#define GPIO_Speed_2MHz 2u
#define RTC_IRQn 3u
#define RCC_APB2Periph_GPIOB 1u
#define RCC_APB1Periph_PWR 2u
#define RCC_APB1Periph_BKP 4u
#define RCC_BDCR_RTCSEL 0x300u
#define RCC_BDCR_RTCEN 0x8000u
#define RCC_RTCCLKSource_LSE 0x100u
#define RCC_LSE_ON 1u
#define RCC_FLAG_LSERDY 1u
#define RTC_FLAG_SEC 0x01u
#define RTC_FLAG_ALR 0x02u
#define RTC_FLAG_OW 0x04u
#define RTC_FLAG_RSF 0x08u
#define RTC_FLAG_RTOFF 0x20u
#define RTC_IT_SEC RTC_FLAG_SEC
#define RTC_IT_ALR RTC_FLAG_ALR
#define RTC_IT_OW RTC_FLAG_OW
#define BKP_DR1 4u
#define PWR_FLAG_SB 2u
#define PWR_FLAG_WU 1u
void NVIC_DisableIRQ(uint8_t irq);
void NVIC_ClearPendingIRQ(uint8_t irq);
void NVIC_Init(NVIC_InitTypeDef *config);
void RCC_APB2PeriphClockCmd(uint32_t clocks, FunctionalState enable);
void RCC_APB1PeriphClockCmd(uint32_t clocks, FunctionalState enable);
void PWR_BackupAccessCmd(FunctionalState enable);
FlagStatus PWR_GetFlagStatus(uint32_t flag);
void PWR_ClearFlag(uint32_t flag);
void BKP_DeInit(void);
uint16_t BKP_ReadBackupRegister(uint16_t reg);
void BKP_WriteBackupRegister(uint16_t reg, uint16_t value);
void RCC_LSEConfig(uint8_t config);
FlagStatus RCC_GetFlagStatus(uint8_t flag);
void RCC_RTCCLKConfig(uint32_t source);
void RCC_RTCCLKCmd(FunctionalState enable);
void GPIO_StructInit(GPIO_InitTypeDef *config);
void GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *config);
void GPIO_WriteBit(GPIO_TypeDef *port, uint16_t pin, BitAction bit);
FlagStatus RTC_GetFlagStatus(uint16_t flag);
void RTC_ClearFlag(uint16_t flags);
void RTC_ITConfig(uint16_t interrupts, FunctionalState enable);
void RTC_SetPrescaler(uint32_t value);
void RTC_SetCounter(uint32_t value);
void RTC_SetAlarm(uint32_t value);
void RTC_ClearITPendingBit(uint16_t interrupts);
FlagStatus RTC_GetITStatus(uint16_t interrupt);
#endif
