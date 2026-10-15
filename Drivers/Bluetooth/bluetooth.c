#include "bluetooth.h"
#include "bluetooth_config.h"
#include "serial_ring.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_usart.h"
#include "misc.h"
#include <string.h>

static SerialRing rx_ring;
static SerialRing tx_ring;
static volatile SerialStats stats;
static uint8_t rx_discard_until_newline;

ErrorStatus Bluetooth_Init(void)
{
    GPIO_InitTypeDef gpio;
    USART_InitTypeDef usart;
    NVIC_InitTypeDef nvic;

    SerialRing_Init(&rx_ring);
    SerialRing_Init(&tx_ring);
    memset((void *)&stats, 0, sizeof(stats));
    rx_discard_until_newline = 0;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);
    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin = GPIO_Pin_10;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &gpio);
    gpio.GPIO_Pin = GPIO_Pin_11;
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOB, &gpio);

    USART_StructInit(&usart);
    usart.USART_BaudRate = BLUETOOTH_BAUD_RATE;
    usart.USART_WordLength = USART_WordLength_8b;
    usart.USART_StopBits = USART_StopBits_1;
    usart.USART_Parity = USART_Parity_No;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usart.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART3, &usart);
    USART_ITConfig(USART3, USART_IT_RXNE, ENABLE);
    USART_ITConfig(USART3, USART_IT_TXE, DISABLE);
    nvic.NVIC_IRQChannel = USART3_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = BLUETOOTH_IRQ_PREEMPTION_PRIORITY;
    nvic.NVIC_IRQChannelSubPriority = BLUETOOTH_IRQ_SUBPRIORITY;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);
    USART_Cmd(USART3, ENABLE);
    return SUCCESS;
}

uint8_t Bluetooth_ReadByte(uint8_t *byte)
{
    uint32_t state = __get_PRIMASK();
    uint8_t result;
    __disable_irq();
    result = SerialRing_Pop(&rx_ring, byte);
    __set_PRIMASK(state);
    return result;
}

uint8_t Bluetooth_Write(const char *bytes, uint16_t length)
{
    uint32_t state = __get_PRIMASK();
    uint8_t result;
    __disable_irq();
    result = SerialRing_Write(&tx_ring, (const uint8_t *)bytes, length);
    if (result && length) {
        USART_ITConfig(USART3, USART_IT_TXE, ENABLE);
    } else if (!result) {
        ++stats.tx_busy;
    }
    __set_PRIMASK(state);
    return result;
}

uint8_t Bluetooth_WriteString(const char *text)
{
    size_t length;
    if (!text) {
        return 0;
    }
    length = strlen(text);
    if (length >= SERIAL_RING_CAPACITY) {
        return 0;
    }
    return Bluetooth_Write(text, (uint16_t)length);
}

uint8_t Bluetooth_TxIdle(void)
{
    uint32_t state = __get_PRIMASK();
    uint8_t idle;
    __disable_irq();
    idle = (uint8_t)(SerialRing_Count(&tx_ring) == 0u &&
                     USART_GetFlagStatus(USART3, USART_FLAG_TC) != RESET);
    __set_PRIMASK(state);
    return idle;
}

uint8_t Bluetooth_IsIdle(void)
{
    uint32_t state = __get_PRIMASK();
    uint8_t idle;
    __disable_irq();
    idle = (uint8_t)(SerialRing_Count(&rx_ring) == 0u &&
                     SerialRing_Count(&tx_ring) == 0u &&
                     USART_GetFlagStatus(USART3, USART_FLAG_TC) != RESET);
    __set_PRIMASK(state);
    return idle;
}

void Bluetooth_GetStats(SerialStats *out)
{
    uint32_t state;
    if (!out) {
        return;
    }
    state = __get_PRIMASK();
    __disable_irq();
    *out = stats;
    __set_PRIMASK(state);
}

static void ResetCorruptedRx(void)
{
    /* Flush unread bytes on transport loss, then inject a parser-invalid marker.
       Resetting both indices is protected because the main reader masks IRQs.
       The marker invalidates any partial line already delivered to the parser. */
    stats.rx_dropped += SerialRing_Count(&rx_ring);
    SerialRing_Init(&rx_ring);
    (void)SerialRing_Push(&rx_ring, 0);
    rx_discard_until_newline = 1;
}

void Bluetooth_IRQHandler(void)
{
    uint32_t status = USART3->SR;
    uint8_t byte;
    /* SR then DR clears RXNE/ORE/FE/NE/PE. A corrupted byte is discarded. */
    if (status & (USART_SR_RXNE | USART_SR_ORE | USART_SR_FE | USART_SR_NE | USART_SR_PE)) {
        byte = (uint8_t)USART3->DR;
        if (status & (USART_SR_ORE | USART_SR_FE | USART_SR_NE | USART_SR_PE)) {
            ++stats.rx_errors;
            ResetCorruptedRx();
        } else if (status & USART_SR_RXNE) {
            ++stats.received;
            if (rx_discard_until_newline) {
                ++stats.rx_dropped;
                if (byte == '\n' || byte == '\r') {
                    (void)SerialRing_Push(&rx_ring, '\n');
                    rx_discard_until_newline = 0;
                }
            } else if (!SerialRing_Push(&rx_ring, byte)) {
                ++stats.rx_dropped;
                ResetCorruptedRx();
            }
        }
    }
    if (USART_GetITStatus(USART3, USART_IT_TXE) != RESET) {
        if (SerialRing_Pop(&tx_ring, &byte)) {
            USART_SendData(USART3, byte);
        } else {
            USART_ITConfig(USART3, USART_IT_TXE, DISABLE);
        }
    }
}

void USART3_IRQHandler(void)
{
    Bluetooth_IRQHandler();
}
