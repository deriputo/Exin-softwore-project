#ifndef APP_H
#define APP_H
#include <stdint.h>
void App_Init(uint32_t now_ms);
void App_Process(uint32_t now_ms);
uint8_t App_CanSleep(void);
#endif
