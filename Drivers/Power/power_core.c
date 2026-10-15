#include "power_core.h"

void PowerPolicy_Init(PowerPolicy *policy, uint32_t now_ms, uint32_t timeout_ms)
{
    policy->last_activity_ms = now_ms;
    policy->timeout_ms = timeout_ms;
}

void PowerPolicy_Touch(PowerPolicy *policy, uint32_t now_ms)
{
    policy->last_activity_ms = now_ms;
}

uint8_t PowerPolicy_ShouldIdle(const PowerPolicy *policy, uint32_t now_ms, uint32_t busy_flags)
{
    return (uint8_t)(busy_flags == 0u && policy->timeout_ms != 0u &&
                    (uint32_t)(now_ms - policy->last_activity_ms) >= policy->timeout_ms);
}
