#ifndef KEY_CONFIG_H
#define KEY_CONFIG_H

/* User wiring: KEY1 PA0, KEY2 PA2, active low. */
#define KEY1_GPIO_PORT  GPIOA
#define KEY1_GPIO_PIN   GPIO_Pin_0
#define KEY1_GPIO_CLOCK RCC_APB2Periph_GPIOA
#define KEY2_GPIO_PORT  GPIOA
#define KEY2_GPIO_PIN   GPIO_Pin_2
#define KEY2_GPIO_CLOCK RCC_APB2Periph_GPIOA

/* Both switches connect GPIO to GND when pressed; internal pull-ups enabled. */
#endif
