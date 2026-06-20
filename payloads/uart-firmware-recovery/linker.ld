OUTPUT_ARCH(mips)
ENTRY(_start)
SECTIONS
{
  . = 0x81000000;
  __recovery_load_start = .;

  .text : ALIGN(4)
  {
    KEEP(*(.text.start))
    *(.text*)
    *(.rodata*)
  }

  .data : ALIGN(16)
  {
    _gp = . + 0x7ff0;
    *(.got*)
    *(.sdata*)
    *(.data*)
  }

  .bss (NOLOAD) : ALIGN(16)
  {
    __bss_start = .;
    *(.sbss*)
    *(.bss*)
    *(COMMON)
    __bss_end = ALIGN(4);
  }

  __recovery_load_end = .;

  /DISCARD/ :
  {
    *(.MIPS.abiflags)
    *(.reginfo)
    *(.pdr)
    *(.comment)
    *(.note*)
    *(.gnu.attributes)
  }

  ASSERT(_start == 0x81000000, "recovery flat-binary entry is not byte zero")
  ASSERT(__recovery_load_end < 0x81400000, "recovery payload overlaps firmware staging")
}
