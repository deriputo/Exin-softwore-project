/* GNU ld script for STM32F103C8T6: documented 64 KiB Flash / 20 KiB SRAM.
   Matches ST's original GCC medium-density startup symbols. */
ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 64K
    RAM (xrw)  : ORIGIN = 0x20000000, LENGTH = 20K
}

_Min_Stack_Size = 0x800; /* 2 KiB reserved; not available to .data/.bss/heap. */
_estack = ORIGIN(RAM) + LENGTH(RAM);

/* Separate executable Flash and writable SRAM ELF load segments. */
PHDRS
{
    flash PT_LOAD FLAGS(5); /* read + execute */
    ram   PT_LOAD FLAGS(6); /* read + write, not executable */
}

SECTIONS
{
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } > FLASH :flash

    .text :
    {
        . = ALIGN(4);
        *(.text .text.*)
        *(.glue_7 .glue_7t)
        *(.eh_frame)
        KEEP(*(.init))
        KEEP(*(.fini))
        . = ALIGN(4);
        _etext = .;
    } > FLASH :flash

    .rodata :
    {
        . = ALIGN(4);
        *(.rodata .rodata.*)
        . = ALIGN(4);
    } > FLASH :flash

    .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } > FLASH :flash
    .ARM.exidx :
    {
        __exidx_start = .;
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
        __exidx_end = .;
    } > FLASH :flash

    .preinit_array :
    {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array*))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } > FLASH :flash
    .init_array :
    {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array*))
        PROVIDE_HIDDEN(__init_array_end = .);
    } > FLASH :flash
    .fini_array :
    {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array*))
        PROVIDE_HIDDEN(__fini_array_end = .);
    } > FLASH :flash

    _sidata = LOADADDR(.data);
    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data .data.*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT > FLASH :ram

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(8);
        _ebss = .;
        _end = .;
        PROVIDE(end = .);
        __HeapBase = .;
    } > RAM :ram

    .stack ORIGIN(RAM) + LENGTH(RAM) - _Min_Stack_Size (NOLOAD) :
    {
        . = ALIGN(8);
        __StackLimit = .;
        __HeapLimit = .;
        . += _Min_Stack_Size;
        __StackTop = .;
    } > RAM :ram

    .ARM.attributes 0 : { *(.ARM.attributes) } :NONE

    ASSERT(_end <= __StackLimit, "SRAM overflow: .data/.bss collide with reserved 2 KiB stack")
    ASSERT(__StackTop == _estack, "Stack must terminate at the top of SRAM")
}
