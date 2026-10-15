#ifndef I2C_BUS_H
#define I2C_BUS_H
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    I2C_BUS_IDLE = 0, I2C_BUS_PENDING, I2C_BUS_OK,
    I2C_BUS_TIMEOUT, I2C_BUS_NACK, I2C_BUS_ERROR
} I2CBusResult;
typedef struct {
    uint8_t address; /* 7-bit address, NOT shifted. */
    const uint8_t *tx;
    uint16_t tx_length;
    uint8_t *rx;
    uint16_t rx_length;
    I2CBusResult result;
} I2CBusRequest;

void I2CBus_Init(void);
void I2CBus_Process(uint32_t now_ms);
/* No queue: retry BUSY next iteration. Buffers must live until completion. */
bool I2CBus_Submit(I2CBusRequest *request, uint32_t now_ms);
bool I2CBus_IsBusy(void);
bool I2CBus_Suspend(void); /* Only succeeds when no request is active. */
void I2CBus_Resume(void);
#endif
