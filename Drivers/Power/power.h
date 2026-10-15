#ifndef POWER_H
#define POWER_H

#include <stdint.h>

typedef enum { POWER_MODE_SLEEP = 0, POWER_MODE_STANDBY = 1 } PowerMode;
typedef enum {
    POWER_BLOCK_NONE = 0,
    POWER_BLOCK_APPLICATION = 1,
    POWER_BLOCK_RTC = 2,
    POWER_BLOCK_WAKE_PIN_HIGH = 3,
    POWER_BLOCK_INTERRUPTS_MASKED = 4
} PowerBlockReason;

/* Application-owned busy flags. Any nonzero value inhibits automatic idle. */
#define POWER_BUSY_STARTUP       (1u << 0)
#define POWER_BUSY_ALARM         (1u << 1)
#define POWER_BUSY_HOLD          (1u << 2)
#define POWER_BUSY_TRACE         (1u << 3)
#define POWER_BUSY_SERIAL        (1u << 4)
#define POWER_BUSY_DISPLAY       (1u << 5)
#define POWER_BUSY_INPUT         (1u << 6)
#define POWER_BUSY_FAULT         (1u << 7)

void Power_Init(uint32_t now_ms);
void Power_Touch(uint32_t now_ms);
void Power_SetMode(PowerMode mode);
PowerMode Power_GetMode(void);
void Power_SetIdleTimeout(uint32_t milliseconds);
/* 32 app-owned bits retained only across a valid, prepared Standby wake. */
void Power_SetBackupData(uint32_t data);
/* Optional nonblocking prepare hook: return 0 while peripherals drain, 1 when ready.
   Cancel hook restores peripherals if activity aborts preparation. */
void Power_SetStandbyHooks(uint8_t (*prepare)(void), void (*cancel)(void));
/* Optional bounded, read-only recheck while IRQ handlers are masked.
   Return 1 only if user input and peripheral state still allow Standby. */
void Power_SetStandbyGuard(uint8_t (*guard)(void));
/* Call at the end of the main loop; no call from an IRQ. */
void Power_Process(uint32_t now_ms, uint32_t busy_flags);
/* CPU-only Sleep with TIM/USART/RTC running. May be used whenever the main-loop
   iteration has drained ready work, even before the OLED idle timeout. */
void Power_SleepUntilInterrupt(void);
uint8_t Power_IsIdle(void);
PowerBlockReason Power_GetBlockReason(void);
uint8_t Power_WasStandbyWake(void);

#endif
