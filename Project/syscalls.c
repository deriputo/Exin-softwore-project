/* Minimal newlib support for a freestanding Cortex-M3 application.
   No implicit UART console: all device/file I/O reports an errno failure.
   snprintf formats in memory and does not use these transport stubs. */
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>
#include "stm32f10x.h"

extern unsigned char __HeapBase;
extern unsigned char __HeapLimit;

void *_sbrk(ptrdiff_t increment)
{
    static uintptr_t current;
    uintptr_t previous, amount;
    const uintptr_t base = (uintptr_t)&__HeapBase;
    const uintptr_t limit = (uintptr_t)&__HeapLimit;
    if (current == 0u) { current = base; }
    previous = current;
    if (increment >= 0) {
        amount = (uintptr_t)increment;
        if (current > limit || amount > limit - current) {
            errno = ENOMEM; return (void *)-1;
        }
        current += amount;
    } else {
        /* Handles PTRDIFF_MIN without signed negation overflow. */
        amount = (uintptr_t)(-(increment + 1)) + 1u;
        if (current < base || amount > current - base) {
            errno = ENOMEM; return (void *)-1;
        }
        current -= amount;
    }
    return (void *)previous;
}

int _write(int descriptor, const void *buffer, size_t length)
{
    (void)descriptor; (void)buffer; (void)length;
    errno = ENOSYS; return -1;
}

int _read(int descriptor, void *buffer, size_t length)
{
    (void)descriptor; (void)buffer; (void)length;
    errno = ENOSYS; return -1;
}

int _close(int descriptor)
{
    (void)descriptor; errno = EBADF; return -1;
}

int _lseek(int descriptor, int offset, int origin)
{
    (void)descriptor; (void)offset; (void)origin;
    errno = ESPIPE; return -1;
}

int _fstat(int descriptor, struct stat *status)
{
    (void)descriptor; (void)status;
    errno = EBADF; return -1;
}

int _isatty(int descriptor)
{
    (void)descriptor; errno = ENOTTY; return 0;
}

int _getpid(void) { return 1; }

int _kill(int process, int signal)
{
    (void)process; (void)signal; errno = EINVAL; return -1;
}

__attribute__((noreturn)) void _exit(int status)
{
    (void)status;
    __disable_irq();
    for (;;) { __WFI(); }
}
