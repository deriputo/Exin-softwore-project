#include "stm32f10x.h"
#include "misc.h"
#include "key.h"
#include "timebase.h"
#include "pwm.h"
#include "encoder.h"
#include "serial.h"
#include "bluetooth.h"
#include "power.h"
#include "rtc_clock.h"
#include "app.h"
#include "stm32f10x_rcc.h"

int main(void)
{
    App_CaptureResetFlags(RCC->CSR);
    RCC_ClearFlag(); /* Snapshot first; prevent old causes accumulating on next reset. */
    RTCClock_InitAlarmLED();
    /* Keep the debug clock available during ordinary Sleep/WFI. */
    DBGMCU->CR |= DBGMCU_CR_DBG_SLEEP;
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    if (Key_Init() != SUCCESS || PWM_Init() != SUCCESS ||
        Encoder_Init() != SUCCESS || Serial_Init() != SUCCESS ||
        Bluetooth_Init() != SUCCESS ||
        Timebase_Init(Key_ScanTick) != SUCCESS) {
        /* Configuration fault: do not run a partly initialized controller. */
        __disable_irq();
        for (;;) { __WFI(); }
    }
    App_Init(Timebase_GetMs());
    for (;;) {
        App_Process(Timebase_GetMs());
        if (App_CanSleep()) { Power_SleepUntilInterrupt(); }
    }
}
