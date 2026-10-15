#ifndef POWER_CORE_H
#define POWER_CORE_H
#include <stdint.h>

typedef struct { uint32_t last_activity_ms, timeout_ms; } PowerPolicy;
void PowerPolicy_Init(PowerPolicy *policy, uint32_t now_ms, uint32_t timeout_ms);
void PowerPolicy_Touch(PowerPolicy *policy, uint32_t now_ms);
uint8_t PowerPolicy_ShouldIdle(const PowerPolicy *policy, uint32_t now_ms, uint32_t busy_flags);
#endif
