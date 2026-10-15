#include "pwm.h"
#include "pwm_config.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_tim.h"

static uint16_t servo_pulse_us;
static uint8_t breath_active;
static uint32_t breath_started_ms;
static uint32_t breath_last_update_ms;

static uint8_t GetDivider(uint32_t clock_hz, uint16_t *prescaler)
{
    uint32_t divider;
    if (clock_hz < PWM_TIMER_TICK_HZ || clock_hz % PWM_TIMER_TICK_HZ != 0u) {
        return 0;
    }
    divider = clock_hz / PWM_TIMER_TICK_HZ;
    if (divider > 65536u) {
        return 0;
    }
    *prescaler = (uint16_t)(divider - 1u);
    return 1;
}

ErrorStatus PWM_Init(void)
{
    RCC_ClocksTypeDef clocks;
    GPIO_InitTypeDef gpio;
    TIM_TimeBaseInitTypeDef timer;
    TIM_OCInitTypeDef output;
    uint32_t tim1_clock;
    uint32_t tim3_clock;
    uint16_t tim1_prescaler;
    uint16_t tim3_prescaler;

    RCC_GetClocksFreq(&clocks);
    tim1_clock = clocks.PCLK2_Frequency;
    tim3_clock = clocks.PCLK1_Frequency;
    if ((RCC->CFGR & RCC_CFGR_PPRE2) != 0u) {
        tim1_clock *= 2u;
    }
    if ((RCC->CFGR & RCC_CFGR_PPRE1) != 0u) {
        tim3_clock *= 2u;
    }
    if (!GetDivider(tim1_clock, &tim1_prescaler) ||
        !GetDivider(tim3_clock, &tim3_prescaler)) {
        return ERROR;
    }

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_AFIO |
                          RCC_APB2Periph_TIM1, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);
    /* This module owns both timers and uses their default pin routes. */
    TIM_DeInit(TIM1);
    TIM_DeInit(TIM3);
    GPIO_PinRemapConfig(GPIO_FullRemap_TIM1, DISABLE);
    GPIO_PinRemapConfig(GPIO_FullRemap_TIM3, DISABLE);

    GPIO_StructInit(&gpio);
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    gpio.GPIO_Pin = GPIO_Pin_8 | GPIO_Pin_6;
    GPIO_Init(GPIOA, &gpio);

    TIM_InternalClockConfig(TIM1);
    TIM_InternalClockConfig(TIM3);
    TIM_TimeBaseStructInit(&timer);
    timer.TIM_CounterMode = TIM_CounterMode_Up;
    timer.TIM_ClockDivision = TIM_CKD_DIV1;
    timer.TIM_RepetitionCounter = 0;
    timer.TIM_Prescaler = tim1_prescaler;
    timer.TIM_Period = (uint16_t)(SERVO_PERIOD_TICKS - 1u);
    TIM_TimeBaseInit(TIM1, &timer);
    timer.TIM_Prescaler = tim3_prescaler;
    timer.TIM_Period = (uint16_t)(LED_PERIOD_TICKS - 1u);
    TIM_TimeBaseInit(TIM3, &timer);

    TIM_OCStructInit(&output);
    output.TIM_OCMode = TIM_OCMode_PWM1;
    output.TIM_OutputState = TIM_OutputState_Enable;
    output.TIM_OCPolarity = TIM_OCPolarity_High;
    output.TIM_Pulse = PWMCore_AngleToPulseUs(90);
    TIM_OC1Init(TIM1, &output);
    TIM_OC1PreloadConfig(TIM1, TIM_OCPreload_Enable);
    TIM_ARRPreloadConfig(TIM1, ENABLE);

    output.TIM_Pulse = 0;
#if LED_PWM_ACTIVE_LOW
    output.TIM_OCPolarity = TIM_OCPolarity_Low;
#endif
    TIM_OC1Init(TIM3, &output);
    TIM_OC1PreloadConfig(TIM3, TIM_OCPreload_Enable);
    TIM_ARRPreloadConfig(TIM3, ENABLE);

    /* Load PSC/ARR/CCR before starting to avoid an incorrect first PWM period. */
    TIM_GenerateEvent(TIM1, TIM_EventSource_Update);
    TIM_GenerateEvent(TIM3, TIM_EventSource_Update);
    TIM_ClearFlag(TIM1, TIM_FLAG_Update);
    TIM_ClearFlag(TIM3, TIM_FLAG_Update);
    TIM_SetCounter(TIM1, 0);
    TIM_SetCounter(TIM3, 0);
    TIM_CtrlPWMOutputs(TIM1, ENABLE); /* Advanced timer requires MOE. */
    TIM_Cmd(TIM1, ENABLE);
    TIM_Cmd(TIM3, ENABLE);

    servo_pulse_us = PWMCore_AngleToPulseUs(90);
    breath_active = 0;
    breath_started_ms = 0;
    breath_last_update_ms = 0;
    return SUCCESS;
}

void ServoPWM_SetAngle(int16_t angle_deg)
{
    ServoPWM_SetPulseUs(PWMCore_AngleToPulseUs(angle_deg));
}

void ServoPWM_SetPulseUs(uint16_t pulse_us)
{
    servo_pulse_us = PWMCore_ClampPulseUs(pulse_us);
    TIM_SetCompare1(TIM1, servo_pulse_us);
}

uint16_t ServoPWM_GetPulseUs(void)
{
    return servo_pulse_us;
}

void LEDPWM_SetBrightness(uint16_t brightness_permille)
{
    breath_active = 0; /* Explicit manual control cancels the animation. */
    TIM_SetCompare1(TIM3, PWMCore_BrightnessToTicks(brightness_permille));
}

void LEDPWM_BreathStart(uint32_t now_ms)
{
    breath_started_ms = now_ms;
    breath_last_update_ms = now_ms;
    breath_active = 1;
    TIM_SetCompare1(TIM3, 0);
}

void LEDPWM_BreathStop(void)
{
    LEDPWM_SetBrightness(0);
}

uint8_t LEDPWM_IsBreathing(void)
{
    return breath_active;
}

void PWM_Process(uint32_t now_ms)
{
    uint32_t elapsed;
    if (!breath_active) {
        return;
    }
    elapsed = now_ms - breath_started_ms; /* Unsigned subtraction handles wrap. */
    if (elapsed >= LED_BREATH_DURATION_MS) {
        LEDPWM_BreathStop();
        return;
    }
    if ((uint32_t)(now_ms - breath_last_update_ms) < 5u) {
        return;
    }
    breath_last_update_ms = now_ms;
    TIM_SetCompare1(TIM3, PWMCore_BrightnessToTicks(PWMCore_BreathBrightness(elapsed)));
}
