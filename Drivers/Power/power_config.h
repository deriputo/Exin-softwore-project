#ifndef POWER_CONFIG_H
#define POWER_CONFIG_H

#define POWER_IDLE_TIMEOUT_MS 60000u
#define POWER_DEFAULT_MODE POWER_MODE_SLEEP
/* PA0 is currently KEY1, idle HIGH. Keep WKUP disabled with this wiring.
   Standby then wakes only through RTC alarm or NRST. Set to 1 ONLY after
   providing a dedicated active-HIGH wake button and relocating KEY1. */
#define POWER_STANDBY_ENABLE_PA0_WKUP 0

#endif
