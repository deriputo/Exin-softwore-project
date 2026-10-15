#include "i2c_bus.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_i2c.h"
#include "stm32f10x_rcc.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
I2C_TypeDef fake_i2c1;
static uint32_t primask;
static unsigned int starts, stops, receives, sends, last_direction, last_address, ack, pos;
uint32_t __get_PRIMASK(void) { return primask; }
void __disable_irq(void) { primask = 1; }
void __set_PRIMASK(uint32_t value) { primask = value; }
void GPIO_PinRemapConfig(uint32_t remap, FunctionalState v) { (void)remap; (void)v; }
void GPIO_StructInit(GPIO_InitTypeDef *gpio) { memset(gpio, 0, sizeof(*gpio)); }
void GPIO_Init(void *port, GPIO_InitTypeDef *gpio) { (void)port; (void)gpio; }
void RCC_APB2PeriphClockCmd(uint32_t p, FunctionalState v) { (void)p; (void)v; }
void RCC_APB1PeriphClockCmd(uint32_t p, FunctionalState v) { (void)p; (void)v; }
void I2C_DeInit(I2C_TypeDef *i) { i->SR1 = i->SR2 = 0; }
void I2C_StructInit(I2C_InitTypeDef *init) { memset(init, 0, sizeof(*init)); }
void I2C_Init(I2C_TypeDef *i, I2C_InitTypeDef *init) { (void)i; assert(init->I2C_ClockSpeed == 100000u); }
void I2C_Cmd(I2C_TypeDef *i, FunctionalState v) { (void)i; (void)v; }
void I2C_AcknowledgeConfig(I2C_TypeDef *i, FunctionalState v) { (void)i; ack = v; }
void I2C_NACKPositionConfig(I2C_TypeDef *i, uint16_t v) { (void)i; pos = v; }
void I2C_GenerateSTART(I2C_TypeDef *i, FunctionalState v) { (void)i; (void)v; ++starts; }
void I2C_GenerateSTOP(I2C_TypeDef *i, FunctionalState v) { (void)i; (void)v; ++stops; }
void I2C_Send7bitAddress(I2C_TypeDef *i, uint8_t a, uint8_t d) { (void)i; last_address = a; last_direction = d; }
void I2C_SendData(I2C_TypeDef *i, uint8_t b) { (void)i; (void)b; ++sends; }
uint8_t I2C_ReceiveData(I2C_TypeDef *i) { (void)i; return (uint8_t)(++receives); }
static void Step(uint16_t flags, uint16_t sr2, uint32_t now)
{
    fake_i2c1.SR1 = flags; fake_i2c1.SR2 = sr2; I2CBus_Process(now);
}
static void Read(uint16_t length, bool prefix)
{
    uint8_t rx[18], tx = 0x3b;
    I2CBusRequest req = {0x68, &tx, prefix ? 1u : 0u, rx + 1, length, I2C_BUS_IDLE};
    unsigned int j;
    memset(rx, 0xa5, sizeof(rx)); starts = stops = receives = sends = 0;
    I2CBus_Init();
    assert(I2CBus_Submit(&req, 100)); assert(!I2CBus_Suspend());
    assert(!I2CBus_Submit(&req, 100));
    Step(0, 0, 100); Step(I2C_SR1_SB, I2C_SR2_BUSY, 100);
    assert(last_address == 0xd0 && last_direction == (prefix ? 0u : 1u));
    if (prefix) {
        Step(I2C_SR1_ADDR, I2C_SR2_BUSY, 100); Step(I2C_SR1_TXE, I2C_SR2_BUSY, 100);
        Step(I2C_SR1_BTF, I2C_SR2_BUSY, 100); Step(I2C_SR1_SB, I2C_SR2_BUSY, 100);
        assert(last_direction == 1u && sends == 1u);
    }
    Step(I2C_SR1_ADDR, I2C_SR2_BUSY, 100);
    if (length == 1) {
        assert(!ack && stops == 1u);
        Step(I2C_SR1_RXNE, I2C_SR2_BUSY, 100);
    } else if (length == 2) {
        assert(!ack && pos == 1u); Step(I2C_SR1_BTF, I2C_SR2_BUSY, 100);
    } else {
        assert(ack && pos == 0u);
        for (j = 0; j < length - 3u; ++j) Step(I2C_SR1_RXNE, I2C_SR2_BUSY, 100);
        Step(0, I2C_SR2_BUSY, 100); /* Enter last three state. */
        Step(I2C_SR1_BTF, I2C_SR2_BUSY, 100); assert(!ack);
        Step(I2C_SR1_BTF, I2C_SR2_BUSY, 100);
    }
    assert(stops == 1u && receives == length);
    Step(0, 0, 100);
    assert(req.result == I2C_BUS_OK && !I2CBus_IsBusy() && ack && !pos);
    assert(rx[0] == 0xa5 && rx[length + 1] == 0xa5);
    for (j = 0; j < length; ++j) assert(rx[j + 1] == j + 1);
    assert(starts == (prefix ? 2u : 1u));
}
int main(void)
{
    uint8_t tx[] = {0x6b, 1};
    I2CBusRequest req = {0x68, tx, 2, 0, 0, I2C_BUS_IDLE};
    unsigned int n;
    for (n = 1; n <= 14; ++n) { Read((uint16_t)n, false); Read((uint16_t)n, true); }
    primask = 1; Read(2, true); assert(primask == 1); primask = 0;
    I2CBus_Init(); assert(I2CBus_Submit(&req, 0));
    Step(0, 0, 0); Step(I2C_SR1_SB, I2C_SR2_BUSY, 0); Step(I2C_SR1_ADDR, I2C_SR2_BUSY, 0);
    Step(I2C_SR1_TXE, I2C_SR2_BUSY, 0); Step(I2C_SR1_TXE, I2C_SR2_BUSY, 0);
    Step(I2C_SR1_BTF, I2C_SR2_BUSY, 0); Step(0, 0, 0);
    assert(req.result == I2C_BUS_OK);
    assert(I2CBus_Submit(&req, 0)); Step(I2C_SR1_AF, 0, 0);
    assert(req.result == I2C_BUS_NACK && !I2CBus_IsBusy());
    assert(I2CBus_Submit(&req, 0xfffffff0u)); Step(0, I2C_SR2_BUSY, 0x0eu);
    assert(req.result == I2C_BUS_TIMEOUT);
    assert(I2CBus_Submit(&req, 50)); Step(I2C_SR1_BERR, 0, 50);
    assert(req.result == I2C_BUS_ERROR);
    assert(I2CBus_Suspend()); assert(!I2CBus_Submit(&req, 70)); I2CBus_Resume();
    req.address = 0x80; assert(!I2CBus_Submit(&req, 70) && req.result == I2C_BUS_ERROR);
    puts("I2C read 1/2/3/14-byte endings, restart/write, timeout/wrap, NACK, suspend and IRQ restore tests passed");
    return 0;
}
