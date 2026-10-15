#include <assert.h>
#include <stdio.h>
#include "power_core.h"

int main(void)
{
    PowerPolicy policy;
    PowerPolicy_Init(&policy, 100u, 30000u);
    assert(!PowerPolicy_ShouldIdle(&policy, 30099u, 0u));
    assert(PowerPolicy_ShouldIdle(&policy, 30100u, 0u));
    assert(!PowerPolicy_ShouldIdle(&policy, 30100u, 1u));
    PowerPolicy_Touch(&policy, 30100u);
    assert(!PowerPolicy_ShouldIdle(&policy, 30101u, 0u));
    PowerPolicy_Init(&policy, 0xfffffff0u, 30u);
    assert(!PowerPolicy_ShouldIdle(&policy, 13u, 0u));
    assert(PowerPolicy_ShouldIdle(&policy, 14u, 0u));
    policy.timeout_ms = 0;
    assert(!PowerPolicy_ShouldIdle(&policy, 0xFFFF0000u, 0u));
    puts("PASS: power timeout, activity reset, busy inhibition, tick wrap, automatic idle disabled");
    return 0;
}
