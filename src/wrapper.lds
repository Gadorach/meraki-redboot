OUTPUT_ARCH(mips)
ENTRY(vector_reset)
SECTIONS
{
    . = 0;
    .boot : { KEEP(*(.boot)) }
    /DISCARD/ : { *(.reginfo) *(.MIPS.abiflags) *(.pdr) *(.comment) *(.note*) }
}
ASSERT(SIZEOF(.boot) == 0x40000, "VCore-III boot region must be exactly 256 KiB")
