#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "power.h"
#include "rtc_clock.h"

PWR_TypeDef fake_pwr;
SCB_TypeDef fake_scb;
GPIO_TypeDef fake_gpioa;
static uint32_t primask;
static uint8_t rtc_ready, snapshot, fail_final_check;
static unsigned int sleep_entries, standby_attempts, checks, hook_calls, hook_cancels;
static uint32_t saved_data;
static unsigned int guard_calls;
static uint8_t reject_guard_at;

uint32_t __get_PRIMASK(void) { return primask; }
void __set_PRIMASK(uint32_t value) { primask = value; }
void __disable_irq(void) { primask = 1; }
void __DSB(void) { }
void __WFI(void)
{
    assert(primask == 1u);
    if (SCB->SCR & SCB_SCR_SLEEPDEEP_Msk) {
        assert(snapshot && (PWR->CR & PWR_CR_PDDS)); ++standby_attempts;
    } else { ++sleep_entries; }
}
void RCC_APB1PeriphClockCmd(uint32_t clocks, FunctionalState enable) { (void)clocks; (void)enable; }
void RCC_APB2PeriphClockCmd(uint32_t clocks, FunctionalState enable) { (void)clocks; (void)enable; }
void PWR_BackupAccessCmd(FunctionalState enable) { (void)enable; }
FlagStatus PWR_GetFlagStatus(uint32_t flag) { return (PWR->CSR & flag) ? SET : RESET; }
void PWR_ClearFlag(uint32_t flag) { PWR->CSR &= ~flag; }
void PWR_WakeUpPinCmd(FunctionalState enable) { assert(enable == DISABLE); }
void EXTI_StructInit(EXTI_InitTypeDef *config) { memset(config, 0, sizeof(*config)); }
void EXTI_Init(EXTI_InitTypeDef *config)
{
    assert(config->EXTI_Line == EXTI_Line17 && config->EXTI_Mode == EXTI_Mode_Event);
    assert(config->EXTI_Trigger == EXTI_Trigger_Rising && config->EXTI_LineCmd == ENABLE);
}
void EXTI_ClearFlag(uint32_t flags) { assert(flags == EXTI_Line17); }
uint8_t GPIO_ReadInputDataBit(GPIO_TypeDef *port, uint16_t pin) { (void)port; (void)pin; return Bit_RESET; }
uint8_t RTCClock_CanStandby(void)
{
    ++checks;
    return (uint8_t)(rtc_ready && !(fail_final_check && checks >= 2u));
}
uint8_t RTCClock_PrepareStandby(uint32_t data) { snapshot = 1; saved_data = data; return 1; }
void RTCClock_CancelStandby(void) { snapshot = 0; }
uint8_t RTCClock_WasStandbyResume(void) { return 0; }
static uint8_t PrepareHook(void) { ++hook_calls; return (uint8_t)(hook_calls > 1u); }
static void CancelHook(void) { ++hook_cancels; }
static uint8_t Guard(void)
{
    assert(primask == 1u);
    ++guard_calls;
    return (uint8_t)(guard_calls != reject_guard_at);
}

int main(void)
{
    rtc_ready = 1;
    Power_Init(0);
    assert(Power_GetMode() == POWER_MODE_SLEEP);
    Power_Process(59999u, 0u); assert(!Power_IsIdle());
    Power_Process(60000u, 0u); assert(Power_IsIdle());
    Power_Init(0); Power_SetIdleTimeout(30000u); /* Remaining cases test configurable timeout. */
    Power_Process(29999u, 0u); assert(!Power_IsIdle());
    Power_Process(30000u, 0u); assert(Power_IsIdle());
    assert(sleep_entries == 0u); /* Process manages OLED idle; public API performs WFI. */
    Power_SleepUntilInterrupt(); assert(sleep_entries == 1u && primask == 0u);
    primask = 1;
    Power_SleepUntilInterrupt(); assert(sleep_entries == 1u && primask == 1u);
    assert(Power_GetBlockReason() == POWER_BLOCK_INTERRUPTS_MASKED);
    primask = 0;
    Power_Touch(30000u); assert(!Power_IsIdle());
    Power_Process(60000u, POWER_BUSY_ALARM); assert(!Power_IsIdle());
    assert(Power_GetBlockReason() == POWER_BLOCK_APPLICATION);

    Power_SetMode(POWER_MODE_STANDBY);
    Power_SetStandbyHooks(PrepareHook, CancelHook);
    Power_SetBackupData(0x1234ABCDu);
    Power_Process(60000u, 0u); assert(Power_IsIdle() && hook_calls == 1u);
    Power_Process(60001u, POWER_BUSY_DISPLAY);
    assert(Power_IsIdle() && hook_cancels == 0u); /* Async OLED off remains pending. */
    Power_Touch(60001u); assert(hook_cancels == 1u && !Power_IsIdle());
    Power_Process(90001u, 0u);
    assert(standby_attempts == 1u && saved_data == 0x1234ABCDu && !snapshot);
    assert(primask == 0u && !(SCB->SCR & SCB_SCR_SLEEPDEEP_Msk) && !(PWR->CR & PWR_CR_PDDS));

    checks = 0; fail_final_check = 1;
    Power_Process(90002u, 0u);
    assert(standby_attempts == 1u && !snapshot && primask == 0u);
    assert(Power_GetBlockReason() == POWER_BLOCK_RTC); /* ALR occurred after serialization. */
    fail_final_check = 0; rtc_ready = 0;
    Power_Process(90003u, 0u); assert(standby_attempts == 1u);
    rtc_ready = 1;
    Power_SetStandbyGuard(Guard);
    guard_calls = 0; reject_guard_at = 1;
    Power_Process(90004u, 0u);
    assert(standby_attempts == 1u && !snapshot && primask == 0u);
    assert(Power_GetBlockReason() == POWER_BLOCK_APPLICATION);
    guard_calls = 0; reject_guard_at = 2;
    Power_Process(90005u, 0u);
    assert(standby_attempts == 1u && !snapshot && primask == 0u);
    assert(Power_GetBlockReason() == POWER_BLOCK_APPLICATION);
    guard_calls = 0; reject_guard_at = 0;
    Power_Process(90006u, 0u);
    assert(standby_attempts == 2u && guard_calls == 2u && !snapshot && primask == 0u);
    Power_SetIdleTimeout(0u); Power_Process(0xFFFF0000u, 0u); assert(!Power_IsIdle());
    puts("PASS: PWR Sleep race guard, default/timeout, hooks drain/cancel, busy, Standby RTC/app input rechecks and aborted-entry cleanup");
    return 0;
}
