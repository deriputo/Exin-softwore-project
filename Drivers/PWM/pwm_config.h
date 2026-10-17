#ifndef PWM_CONFIG_H
#define PWM_CONFIG_H

/* Fixed default routes: TIM1_CH1=PA8, TIM3_CH1=PA6, no remapping.
   Changing routes requires changing both GPIO and timer channel setup in pwm.c. */
/* Boot LED: 3.3V -> resistor -> LED anode; cathode -> PA6. */
#define LED_PWM_ACTIVE_LOW 1 /* Low = on, high = off; breathing ends off. */

#endif
