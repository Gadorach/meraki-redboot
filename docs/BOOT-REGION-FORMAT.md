# VCore-III 256 KiB boot-region format

The flashable loader artifact is an exact 256 KiB region. It contains reset and
exception vectors, a common dispatcher, two loader descriptors, and two
source-built copies of the same LinuxLoader body.

```text
0x00000  reset vector
0x00080  TLB exception vector
0x00100  cache-error vector
0x00180  general-exception vector
0x00200  interrupt vector
0x00400  common dispatcher and region-base calculation
0x0041c  fallback-loader descriptor
0x00434  active-selector branch sequence
0x00444  active-loader descriptor
...      padding
...      active source-built loader body
...      fallback source-built loader body
0x40000  end of loader region and start of SPIM payload header
```

Each descriptor contains a position-independent jump sequence and the loader
length. The wrapper computes both body offsets from the current linked loader
size. The active selector enters the active copy; the fallback descriptor is
available to the loader's region-stride fallback mechanism. No opaque executable
body is embedded.

## Payload geometry

The loader expects a 32-byte little-endian SPIM payload header at flash offset
`0x40000`:

```text
0x00  magic                 0x4d495053
0x04  RAM load address      KSEG0 address
0x08  padded payload bytes  non-zero and 32-byte aligned
0x0c  entry point
0x10  expected IEEE CRC-32
0x14  reserved
0x18  reserved
0x1c  reserved
0x20  payload bytes
```

CRC covers the header with the CRC word set to zero, followed by the complete
padded payload. `tools/mkvcoreiii_payload.py` creates and verifies this format.

Default policy geometry:

```text
LOADER_SIZE          = 0x00040000
PAYLOAD_HEADER_SIZE  = 0x00000020
PAYLOAD_SLOT_END     = 0x00400000
FALLBACK_REGION_SIZE = 0x00400000
LEGACY_PAYLOAD_LIMIT = 0x003bffe0
HARD_PAYLOAD_LIMIT   = 0x003bffe0
```

`HARD_PAYLOAD_LIMIT` is mandatory and cannot exceed
`PAYLOAD_SLOT_END - LOADER_SIZE - PAYLOAD_HEADER_SIZE`. The independent legacy
threshold may reject, warn, or be omitted according to the selected profile.
For the postmerkOS layout, where SquashFS begins at `0x00300000`, set
`PAYLOAD_SLOT_END=0x00300000`; the configuration checker derives the safe hard
limit.

The wrapper recipe uses decimal shell operands and is exercised by
`make test-wrapper-fit` as well as the complete structural wrapper test.

Design provenance and format reconstruction notes are kept in
[`history/boot-region-reconstruction.md`](history/boot-region-reconstruction.md).
