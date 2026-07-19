# PMOSLIVE

PMOSLIVE is a high-RAM, flash-write-free sibling of PMOSREC. It reuses the
PMOSREC v3 UART framing, adaptive baud qualification, sparse/LZ4 transport,
CRC-32, and SHA-256 implementation.

It receives an unchanged 16 MiB postmerkOS retail image and its normal artifact
manifest. The SPIM kernel payload is verified and copied to `0x81000000`. The
actual SquashFS image size is checked against both the manifest and SquashFS
superblock, then copied to `0x87000000`. Linux receives the SquashFS as a legacy
external initrd with `mem=120M`. The initrd occupies physical 112-120 MiB and is
reserved by the kernel's legacy initrd handoff. Only physical 120-128 MiB is
excluded from Linux, retaining the fixed-RAM stage-1 reservation and headroom.

The legacy MIPS boot-parameter workspace is compacted into physical
`0x00000400-0x00000fff` and accessed through uncached KSEG1 at
`0xa0000400-0xa0000fff`. This leaves physical `0x00000000-0x000003ff` for
exception vectors and avoids the decompressed kernel beginning at physical
`0x00001000`.

The PMOSLIVE binary is linked at `0x86c00000`. A transport-only preprocessor
mode excludes PMOSREC's SPI, erase, program, preflight, confirmation, and flash
main-loop code before compilation. Link-time garbage collection and post-link
marker checks provide an additional verification layer.
