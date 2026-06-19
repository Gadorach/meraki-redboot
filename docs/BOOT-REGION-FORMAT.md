# VCore-III 256 KiB boot-region format

## Raw loader versus flashable region

The GPL target `arch/mips/vcoreiii/loader/loader.bin` is only the relocatable
LinuxLoader body. It begins with `__start`, but it does not contain the VCore-III
reset/exception vector table found in the supplied 256 KiB images. Flashing the
raw loader at offset zero therefore does not reproduce the supplied boot
region.

The supplied images reveal this outer layout:

```text
0x00000  reset vector
0x00080  TLB exception vector
0x00100  cache-error vector
0x00180  general-exception vector
0x00200  interrupt vector
0x00400  common dispatcher / region-base calculation
0x0041c  fallback-loader descriptor
0x00434  active-selector branch sequence
0x00444  active-loader descriptor
...      0xff padding
...      active loader body
...      fallback loader body
0x40000  end of loader region / start of payload header
```

Each descriptor contains:

```text
+0x00  LUI  k1, high16(loader offset)
+0x04  ORI  k1, k1, low16(loader offset)
+0x08  ADD  k1, ra, k1
+0x0c  JR   k1
+0x10  NOP
+0x14  loader length in bytes
```

The dispatcher masks the return address with `0xfffffc00`, producing the base of
the aligned boot region regardless of the reset alias, then follows the active
selector. The standalone wrapper preserves those instruction words and builds
both descriptor values from the current loader size.

## Why two source-built copies

The historical images contain an older fallback body and a newer active body of
different lengths. The source for that older binary is not separately identified
in the GPL archive. To avoid retaining any opaque executable component, this
project embeds two independently placed copies of the same current source build.
The active selector points to the active copy. The fallback descriptor remains
available for a future atomic loader-update implementation, but ordinary reset
execution follows the active selector exactly as in the supplied images.

## Payload boundary and policy geometry

After initialization, `head.S` rounds its current flash address to the next
256 KiB boundary and expects the 32-byte payload header there. For the first
region this is flash offset `0x40000`.

The default geometry is:

```text
LOADER_SIZE          = 0x00040000
PAYLOAD_HEADER_SIZE  = 0x00000020
PAYLOAD_SLOT_END     = 0x00400000
FALLBACK_REGION_SIZE = 0x00400000
LEGACY_PAYLOAD_LIMIT = 0x003bffe0
HARD_PAYLOAD_LIMIT   = 0x003bffe0
```

`FALLBACK_REGION_SIZE` is the distance used by `loader_fail` to find the next
fallback loader. `PAYLOAD_SLOT_END` identifies the first byte owned by the next
partition and can be earlier than that fallback stride. `HARD_PAYLOAD_LIMIT` is
always enforced and may not exceed
`PAYLOAD_SLOT_END - LOADER_SIZE - PAYLOAD_HEADER_SIZE`. The legacy threshold is
independently configured as reject, warn, or ignored.

The header fields are little-endian 32-bit words:

```text
0x00  magic       0x4d495053
0x04  RAM load address (must begin with 0x8)
0x08  padded payload byte count, non-zero and 32-byte aligned
0x0c  entry point
0x10  expected CRC-32
0x14  reserved
0x18  reserved
0x1c  reserved
0x20  payload bytes
```

CRC covers the complete header with word `0x10` replaced by zero, followed by
the padded payload. The included packer generates and verifies this format.

A larger payload cannot be made safe merely by disabling a branch. It requires
a larger boot region, matching fallback placement, sufficient NOR space, and a
valid RAM destination. The configuration checker prevents the hard limit from
crossing the configured next-region boundary.

## Recovered versus confirmed

Confirmed directly from GPL source:

- loader initialization and payload algorithm;
- 256 KiB loader boundary and 4 MiB region calculation;
- payload magic, fields, CRC loop, load-address check, and branch behavior.

Recovered from the two supplied images:

- vector/dispatcher instruction sequence;
- descriptor locations and encoding;
- active-selector branch sequence;
- packing loader bodies at the end of the 256 KiB region.

The reconstructed wrapper is byte-structure validated against those references,
but still requires first-boot hardware acceptance.
