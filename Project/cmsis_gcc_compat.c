/* Project-local compatibility for the unmodified CMSIS v1.30 core_cm3.h.
   Its GCC path declares these functions externally. Only compile this file
   for GNU Arm; do not compile the old vendor core_cm3.c at the same time.
   Original vendor/CMSIS files and their licenses are preserved. */
#include <stdint.h>

#if defined(__GNUC__) && !defined(__ARMCC_VERSION) && (defined(__arm__) || defined(__thumb__))
uint32_t __get_PRIMASK(void)
{
    uint32_t value;
    __asm volatile ("mrs %0, primask" : "=r" (value) : : "memory");
    return value;
}

void __set_PRIMASK(uint32_t value)
{
    __asm volatile ("msr primask, %0" : : "r" (value) : "memory");
}
#endif
