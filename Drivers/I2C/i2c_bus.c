#include "i2c_bus.h"
#include "i2c_bus_config.h"
#include "stm32f10x.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_i2c.h"
#include "stm32f10x_rcc.h"

typedef enum {
    BUS_FREE, WAIT_FREE, START_WRITE, ADDR_WRITE, WRITE_BYTES, WRITE_DONE,
    START_READ, ADDR_READ, READ_BYTES, READ_LAST_THREE, READ_LAST_TWO,
    READ_ONE, STOP_DONE
} BusState;
static I2CBusRequest *active;
static BusState state;
static uint16_t index;
static uint32_t started_ms;
static bool suspended;

static void Configure(void)
{
    I2C_InitTypeDef init;
    I2C_DeInit(I2C1);
    I2C_StructInit(&init);
    init.I2C_ClockSpeed = I2C_BUS_CLOCK_HZ;
    init.I2C_Mode = I2C_Mode_I2C;
    init.I2C_DutyCycle = I2C_DutyCycle_2;
    init.I2C_OwnAddress1 = 0;
    init.I2C_Ack = I2C_Ack_Enable;
    init.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    I2C_Init(I2C1, &init);
    I2C_Cmd(I2C1, ENABLE);
}

void I2CBus_Init(void)
{
    GPIO_InitTypeDef gpio;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE);
    GPIO_PinRemapConfig(GPIO_Remap_I2C1, ENABLE);
    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin = GPIO_Pin_8 | GPIO_Pin_9;
    gpio.GPIO_Mode = GPIO_Mode_AF_OD;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &gpio);
    active = 0;
    state = BUS_FREE;
    suspended = false;
    Configure();
}

bool I2CBus_IsBusy(void) { return active != 0; }
bool I2CBus_Suspend(void)
{
    if (active != 0) return false;
    I2C_Cmd(I2C1, DISABLE);
    suspended = true;
    return true;
}
void I2CBus_Resume(void)
{
    Configure();
    suspended = false;
}

bool I2CBus_Submit(I2CBusRequest *request, uint32_t now_ms)
{
    if (active != 0 || suspended || request == 0) return false;
    if (request->address > 0x7fu ||
        (request->tx_length && request->tx == 0) ||
        (request->rx_length && request->rx == 0) ||
        (!request->tx_length && !request->rx_length)) {
        request->result = I2C_BUS_ERROR;
        return false;
    }
    active = request;
    request->result = I2C_BUS_PENDING;
    started_ms = now_ms;
    index = 0;
    state = WAIT_FREE;
    I2C_AcknowledgeConfig(I2C1, ENABLE);
    I2C_NACKPositionConfig(I2C1, I2C_NACKPosition_Current);
    return true;
}

static void Complete(I2CBusResult result)
{
    active->result = result;
    active = 0;
    state = BUS_FREE;
}
static void Abort(I2CBusResult result)
{
    I2C_GenerateSTOP(I2C1, ENABLE);
    Configure(); /* Bounded peripheral reset; does not wait for stuck SDA. */
    Complete(result);
}
static void ClearAddress(void)
{
    volatile uint16_t sr;
    sr = I2C1->SR1;
    sr = I2C1->SR2;
    (void)sr;
}

void I2CBus_Process(uint32_t now_ms)
{
    uint16_t flags;
    uint16_t remaining;
    uint32_t irq;
    if (active == 0 || suspended) return;
    flags = I2C1->SR1;
    if (flags & I2C_SR1_AF) { Abort(I2C_BUS_NACK); return; }
    if (flags & (I2C_SR1_BERR | I2C_SR1_ARLO | I2C_SR1_OVR)) {
        Abort(I2C_BUS_ERROR); return;
    }
    if ((uint32_t)(now_ms - started_ms) >= I2C_BUS_TIMEOUT_MS) {
        Abort(I2C_BUS_TIMEOUT); return;
    }
    switch (state) {
    case WAIT_FREE:
        if (!(I2C1->SR2 & I2C_SR2_BUSY)) {
            I2C_GenerateSTART(I2C1, ENABLE);
            state = active->tx_length ? START_WRITE : START_READ;
        }
        break;
    case START_WRITE:
        if (flags & I2C_SR1_SB) {
            I2C_Send7bitAddress(I2C1, (uint8_t)(active->address << 1), I2C_Direction_Transmitter);
            state = ADDR_WRITE;
        }
        break;
    case ADDR_WRITE:
        if (flags & I2C_SR1_ADDR) { ClearAddress(); state = WRITE_BYTES; }
        break;
    case WRITE_BYTES:
        if (flags & I2C_SR1_TXE) {
            I2C_SendData(I2C1, active->tx[index++]);
            if (index == active->tx_length) state = WRITE_DONE;
        }
        break;
    case WRITE_DONE:
        if (flags & I2C_SR1_BTF) {
            if (active->rx_length) {
                I2C_GenerateSTART(I2C1, ENABLE);
                state = START_READ;
                index = 0;
            } else { I2C_GenerateSTOP(I2C1, ENABLE); state = STOP_DONE; }
        }
        break;
    case START_READ:
        if (flags & I2C_SR1_SB) {
            I2C_Send7bitAddress(I2C1, (uint8_t)(active->address << 1), I2C_Direction_Receiver);
            state = ADDR_READ;
        }
        break;
    case ADDR_READ:
        if (flags & I2C_SR1_ADDR) {
            irq = __get_PRIMASK();
            __disable_irq();
            if (active->rx_length == 1u) {
                I2C_AcknowledgeConfig(I2C1, DISABLE);
                ClearAddress();
                I2C_GenerateSTOP(I2C1, ENABLE);
                state = READ_ONE;
            } else if (active->rx_length == 2u) {
                I2C_NACKPositionConfig(I2C1, I2C_NACKPosition_Next);
                I2C_AcknowledgeConfig(I2C1, DISABLE);
                ClearAddress();
                state = READ_LAST_TWO;
            } else { ClearAddress(); state = READ_BYTES; }
            __set_PRIMASK(irq);
        }
        break;
    case READ_ONE:
        if (flags & I2C_SR1_RXNE) {
            active->rx[index++] = I2C_ReceiveData(I2C1);
            state = STOP_DONE;
        }
        break;
    case READ_BYTES:
        remaining = (uint16_t)(active->rx_length - index);
        if (remaining == 3u) state = READ_LAST_THREE;
        else if (flags & I2C_SR1_RXNE) active->rx[index++] = I2C_ReceiveData(I2C1);
        break;
    case READ_LAST_THREE:
        if (flags & I2C_SR1_BTF) {
            irq = __get_PRIMASK();
            __disable_irq();
            I2C_AcknowledgeConfig(I2C1, DISABLE);
            active->rx[index++] = I2C_ReceiveData(I2C1);
            __set_PRIMASK(irq);
            state = READ_LAST_TWO;
        }
        break;
    case READ_LAST_TWO:
        if (flags & I2C_SR1_BTF) {
            irq = __get_PRIMASK();
            __disable_irq();
            I2C_GenerateSTOP(I2C1, ENABLE);
            active->rx[index++] = I2C_ReceiveData(I2C1);
            active->rx[index++] = I2C_ReceiveData(I2C1);
            __set_PRIMASK(irq);
            state = STOP_DONE;
        }
        break;
    case STOP_DONE:
        if (!(I2C1->SR2 & I2C_SR2_BUSY)) {
            I2C_AcknowledgeConfig(I2C1, ENABLE);
            I2C_NACKPositionConfig(I2C1, I2C_NACKPosition_Current);
            Complete(I2C_BUS_OK);
        }
        break;
    default: break;
    }
}
