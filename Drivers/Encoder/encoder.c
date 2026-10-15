#include "encoder.h"
#include "encoder_config.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_tim.h"

static EncoderCore encoder;

ErrorStatus Encoder_Init(void)
{
    GPIO_InitTypeDef gpio;
    TIM_TimeBaseInitTypeDef timer;
    TIM_ICInitTypeDef input;
    uint16_t initial_counter;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM4, ENABLE);
    TIM_DeInit(TIM4);
    GPIO_PinRemapConfig(GPIO_Remap_TIM4, DISABLE);

    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin = GPIO_Pin_6 | GPIO_Pin_7;
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(GPIOB, &gpio);

    TIM_TimeBaseStructInit(&timer);
    timer.TIM_Prescaler = 0;
    timer.TIM_Period = 0xffffu;
    timer.TIM_CounterMode = TIM_CounterMode_Up;
    /* CKD controls the filter sampling clock, not the encoder count division. */
    timer.TIM_ClockDivision = TIM_CKD_DIV4;
    TIM_TimeBaseInit(TIM4, &timer);

    /* TI12 counts both edges on both channels (x4). Rising polarity means
       non-inverted inputs here, not counting only rising edges. */
    TIM_EncoderInterfaceConfig(TIM4, TIM_EncoderMode_TI12,
                               TIM_ICPolarity_Rising, TIM_ICPolarity_Rising);
    TIM_ICStructInit(&input);
    input.TIM_ICPolarity = TIM_ICPolarity_Rising;
    input.TIM_ICSelection = TIM_ICSelection_DirectTI;
    input.TIM_ICPrescaler = TIM_ICPSC_DIV1;
    input.TIM_ICFilter = ENCODER_INPUT_FILTER;
    input.TIM_Channel = TIM_Channel_1;
    TIM_ICInit(TIM4, &input);
    input.TIM_Channel = TIM_Channel_2;
    TIM_ICInit(TIM4, &input);

    TIM_SetCounter(TIM4, 0);
    TIM_ClearFlag(TIM4, TIM_FLAG_Update | TIM_FLAG_CC1 | TIM_FLAG_CC2);
    /* No TIM4 interrupt or DMA: quadrature decoding is entirely hardware-based. */
    initial_counter = (uint16_t)TIM_GetCounter(TIM4);
    if (!EncoderCore_Init(&encoder, initial_counter,
                         ENCODER_COUNTS_PER_STEP, ENCODER_DIRECTION)) {
        return ERROR;
    }
    TIM_Cmd(TIM4, ENABLE);
    return SUCCESS;
}

int32_t Encoder_ReadDeltaSteps(void)
{
    return EncoderCore_Update(&encoder, (uint16_t)TIM_GetCounter(TIM4));
}

uint16_t Encoder_GetCounter(void)
{
    return (uint16_t)TIM_GetCounter(TIM4);
}

int64_t Encoder_GetPositionCounts(void)
{
    return encoder.position_counts;
}

int64_t Encoder_GetPositionSteps(void)
{
    return encoder.position_steps;
}

uint8_t Encoder_GetFaults(void)
{
    return encoder.faults;
}

void Encoder_ResetPosition(void)
{
    EncoderCore_ResetPosition(&encoder, (uint16_t)TIM_GetCounter(TIM4));
}
