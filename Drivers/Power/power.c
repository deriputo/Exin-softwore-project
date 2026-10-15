#include "power.h"
#include "power_config.h"
#include "power_core.h"
#include "rtc_clock.h"
#include "stm32f10x_exti.h"
#include "stm32f10x_pwr.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_rtc.h"
#include "stm32f10x_gpio.h"

static PowerPolicy policy;
static PowerMode mode;
static uint8_t idle, preparing, standby_wake;
static uint32_t backup_data;
static PowerBlockReason block_reason;
static uint8_t (*prepare_hook)(void);
static void (*cancel_hook)(void);
static uint8_t (*standby_guard)(void);

static void CancelPreparation(void)
{
    if (preparing) {
        RTCClock_CancelStandby();
        if (cancel_hook != 0) { cancel_hook(); }
        preparing = 0;
    }
}

void Power_Init(uint32_t now_ms)
{
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR | RCC_APB1Periph_BKP, ENABLE);
    PWR_BackupAccessCmd(ENABLE);
    standby_wake = (uint8_t)(PWR_GetFlagStatus(PWR_FLAG_SB) != RESET);
    /* Do not clear SBF here: RTCClock_Init must inspect it to restore LSE state. */
    PowerPolicy_Init(&policy, now_ms, POWER_IDLE_TIMEOUT_MS);
    mode = POWER_DEFAULT_MODE;
    idle = preparing = 0;
    backup_data = 0;
    block_reason = POWER_BLOCK_NONE;
    prepare_hook = 0;
    cancel_hook = 0;
    standby_guard = 0;
}

void Power_Touch(uint32_t now_ms)
{
    PowerPolicy_Touch(&policy, now_ms);
    idle = 0;
    block_reason = POWER_BLOCK_NONE;
    CancelPreparation();
}

void Power_SetMode(PowerMode next)
{
    if (next == POWER_MODE_SLEEP || next == POWER_MODE_STANDBY) {
        CancelPreparation();
        mode = next;
        idle = 0;
    }
}

PowerMode Power_GetMode(void) { return mode; }
void Power_SetIdleTimeout(uint32_t milliseconds) { policy.timeout_ms = milliseconds; }
void Power_SetBackupData(uint32_t data) { backup_data = data; }
void Power_SetStandbyHooks(uint8_t (*prepare)(void), void (*cancel)(void))
{
    CancelPreparation();
    prepare_hook = prepare;
    cancel_hook = cancel;
}
void Power_SetStandbyGuard(uint8_t (*guard)(void))
{
    CancelPreparation();
    standby_guard = guard;
}
uint8_t Power_IsIdle(void) { return idle; }
PowerBlockReason Power_GetBlockReason(void) { return block_reason; }
uint8_t Power_WasStandbyWake(void)
{
    return (uint8_t)(standby_wake || RTCClock_WasStandbyResume());
}

void Power_SleepUntilInterrupt(void)
{
    uint32_t primask = __get_PRIMASK();
    if (primask != 0u) {
        block_reason = POWER_BLOCK_INTERRUPTS_MASKED;
        return;
    }
    /* A pending interrupt wakes WFI even while PRIMASK masks its handler.
       This closes the check/sleep race; handler runs after PRIMASK restoration. */
    __disable_irq();
    SCB->SCR &= (uint32_t)~SCB_SCR_SLEEPDEEP_Msk;
    __DSB();
    __WFI();
    __set_PRIMASK(primask);
}

static void EnterStandby(void)
{
    uint32_t primask;
    EXTI_InitTypeDef exti;

    if (!RTCClock_CanStandby()) {
        block_reason = POWER_BLOCK_RTC;
        CancelPreparation();
        return;
    }
#if POWER_STANDBY_ENABLE_PA0_WKUP
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    if (GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_0) != Bit_RESET) {
        block_reason = POWER_BLOCK_WAKE_PIN_HIGH;
        CancelPreparation();
        return;
    }
#endif
    preparing = 1;
    if (prepare_hook != 0 && !prepare_hook()) { return; }
    primask = __get_PRIMASK();
    if (primask != 0u) {
        block_reason = POWER_BLOCK_INTERRUPTS_MASKED;
        CancelPreparation();
        return;
    }
    __disable_irq();
    if (standby_guard != 0 && !standby_guard()) {
        __set_PRIMASK(primask);
        block_reason = POWER_BLOCK_APPLICATION;
        CancelPreparation();
        return;
    }
    if (!RTCClock_PrepareStandby(backup_data)) {
        __set_PRIMASK(primask);
        block_reason = POWER_BLOCK_RTC;
        CancelPreparation();
        return;
    }
    EXTI_StructInit(&exti);
    exti.EXTI_Line = EXTI_Line17;
    exti.EXTI_Mode = EXTI_Mode_Event;
    exti.EXTI_Trigger = EXTI_Trigger_Rising;
    exti.EXTI_LineCmd = ENABLE;
    EXTI_ClearFlag(EXTI_Line17);
    EXTI_Init(&exti);
#if POWER_STANDBY_ENABLE_PA0_WKUP
    PWR_WakeUpPinCmd(ENABLE);
#else
    PWR_WakeUpPinCmd(DISABLE);
#endif
    /* Clear wake latch before the final RTC recheck, not afterwards.
       Calling SPL PWR_EnterSTANDBYMode would clear WUF again after that check. */
    PWR_ClearFlag(PWR_FLAG_WU);
    if (!RTCClock_CanStandby()) {
        __set_PRIMASK(primask);
        block_reason = POWER_BLOCK_RTC;
        CancelPreparation();
        return;
    }
    if (standby_guard != 0 && !standby_guard()) {
        __set_PRIMASK(primask);
        block_reason = POWER_BLOCK_APPLICATION;
        CancelPreparation();
        return;
    }
    PWR->CR |= PWR_CR_PDDS;
    SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;
    __DSB();
    __WFI();
    /* Real Standby wake resets the CPU. Reaching here means entry was aborted
       by a pending interrupt/debug condition, so restore a normal running CPU. */
    SCB->SCR &= (uint32_t)~SCB_SCR_SLEEPDEEP_Msk;
    PWR->CR &= (uint32_t)~PWR_CR_PDDS;
    __set_PRIMASK(primask);
    CancelPreparation();
}

void Power_Process(uint32_t now_ms, uint32_t busy_flags)
{
    block_reason = POWER_BLOCK_NONE;
    if (!PowerPolicy_ShouldIdle(&policy, now_ms, busy_flags)) {
        /* The prepare hook may have queued a final OLED-off transfer. Keep it
           prepared while that one application transfer drains. Touch cancels. */
        if (preparing && busy_flags != 0u &&
            (busy_flags & (uint32_t)~POWER_BUSY_DISPLAY) == 0u &&
            PowerPolicy_ShouldIdle(&policy, now_ms, 0u)) {
            idle = 1;
            block_reason = POWER_BLOCK_APPLICATION;
            return;
        }
        idle = 0;
        if (busy_flags != 0u) { block_reason = POWER_BLOCK_APPLICATION; }
        CancelPreparation();
        return;
    }
    idle = 1;
    if (mode == POWER_MODE_STANDBY) { EnterStandby(); }
}
