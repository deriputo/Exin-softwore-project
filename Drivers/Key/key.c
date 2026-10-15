#include "key.h"
#include "key_config.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"

ErrorStatus Key_Init(void)
{
    GPIO_InitTypeDef gpio;

    RCC_APB2PeriphClockCmd(KEY1_GPIO_CLOCK | KEY2_GPIO_CLOCK, ENABLE);
    GPIO_StructInit(&gpio);
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    gpio.GPIO_Pin = KEY1_GPIO_PIN;
    GPIO_Init(KEY1_GPIO_PORT, &gpio);
    gpio.GPIO_Pin = KEY2_GPIO_PIN;
    GPIO_Init(KEY2_GPIO_PORT, &gpio);

    KeyCore_Init();
    return SUCCESS;
}

void Key_ScanTick(void)
{
    uint8_t pressed = 0;
    if (GPIO_ReadInputDataBit(KEY1_GPIO_PORT, KEY1_GPIO_PIN) == Bit_RESET) {
        pressed |= KEY1_MASK;
    }
    if (GPIO_ReadInputDataBit(KEY2_GPIO_PORT, KEY2_GPIO_PIN) == Bit_RESET) {
        pressed |= KEY2_MASK;
    }
    KeyCore_ScanTick(pressed);
}

uint8_t Key_GetEvent(KeyEvent *event)
{
    uint32_t primask = __get_PRIMASK();
    uint8_t result;
    __disable_irq();
    result = KeyCore_PopEvent(event);
    __set_PRIMASK(primask);
    return result;
}

uint8_t Key_GetPressedMask(void)
{
    uint32_t primask = __get_PRIMASK();
    uint8_t result;
    __disable_irq();
    result = KeyCore_GetPressedMask();
    __set_PRIMASK(primask);
    return result;
}

uint32_t Key_GetDroppedCount(void)
{
    uint32_t primask = __get_PRIMASK();
    uint32_t result;
    __disable_irq();
    result = KeyCore_GetDroppedCount();
    __set_PRIMASK(primask);
    return result;
}
