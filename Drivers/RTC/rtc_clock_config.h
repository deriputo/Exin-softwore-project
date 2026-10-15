#ifndef RTC_CLOCK_CONFIG_H
#define RTC_CLOCK_CONFIG_H

/* Confirmed board: external 32768 Hz crystal on PC14/PC15. No LSI fallback. */
#define RTC_CLOCK_LSE_TIMEOUT_MS  5000u
#define RTC_CLOCK_IO_TIMEOUT_MS   1000u
#define RTC_CLOCK_INITIAL_SECONDS (23u * 3600u + 59u * 60u + 55u)

/* Confirmed wiring: 3.3V -> resistor/LED -> PB0; low turns it on. */
#define RTC_ALARM_LED_PORT   GPIOB
#define RTC_ALARM_LED_PIN    GPIO_Pin_0
#define RTC_ALARM_LED_CLOCK  RCC_APB2Periph_GPIOB
#define RTC_ALARM_LED_ACTIVE_LOW 1

#define RTC_CLOCK_IRQ_PREEMPTION_PRIORITY 3u
#define RTC_CLOCK_IRQ_SUB_PRIORITY       0u
#define RTC_CLOCK_DEFINE_IRQ_HANDLER     1

#endif
