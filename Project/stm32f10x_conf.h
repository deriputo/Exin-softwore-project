#ifndef STM32F10X_CONF_H
#define STM32F10X_CONF_H

/* STM32F103C8T6 project configuration for the ST Standard Peripheral Library.
   Build with STM32F10X_MD and USE_STDPERIPH_DRIVER defined. */
#include "stm32f10x_rcc.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_tim.h"
#include "stm32f10x_rtc.h"
#include "stm32f10x_bkp.h"
#include "stm32f10x_pwr.h"
#include "stm32f10x_exti.h"
#include "stm32f10x_i2c.h"
#include "stm32f10x_usart.h"
#include "stm32f10x_dma.h"
#include "misc.h"

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line);
#define assert_param(expression) ((expression) ? (void)0 : assert_failed((uint8_t *)__FILE__, __LINE__))
#else
#define assert_param(expression) ((void)0)
#endif

#endif
