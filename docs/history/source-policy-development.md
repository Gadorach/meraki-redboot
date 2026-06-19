# Source-level validation policies

The adjacent GPL README describes two historical manual binary edits:

- replace the final CRC mismatch branch with `nop`;
- replace the payload-size rejection branch with `nop`.

Those edits proved that the hardware could continue past the checks, but they
left all unused validation work in place and removed the only payload-size
boundary. This project implements maintainable policies directly in `head.S`
instead.

## CRC policies

### `strict`

The loader calculates CRC over the 32-byte header with the CRC field treated as
zero, then over the payload while copying it to RAM. A mismatch enters the
normal fallback path.

### `warn`

The loader performs the same calculation, prints this message through the
initialized serial UART, and continues:

```text
WARNING: payload CRC mismatch; continuing because CRC policy is warn
```

### `off`

The loader compiles a separate copy-only loop. The following are omitted from
the binary:

- expected CRC header load;
- CRC accumulator initialization;
- header CRC operations;
- per-word payload CRC operations;
- final comparison;
- 256-entry CRC lookup table.

This is a true source-level removal, not a NOP at the final branch.

## Size policies

All profiles first enforce structural requirements:

- payload size must be non-zero;
- payload size must be a multiple of 32 bytes;
- payload size must not exceed `HARD_PAYLOAD_LIMIT`.

The hard boundary is never optional. It prevents the copy loop from entering the
next fallback flash region.

A second `LEGACY_PAYLOAD_LIMIT` has three policies:

### `legacy-strict`

Exceeding the legacy threshold enters the fallback path.

### `legacy-warn`

Exceeding the legacy threshold prints:

```text
WARNING: payload exceeds legacy size threshold; continuing within hard slot limit
```

The copy continues only because the mandatory hard limit has already passed.

### `hard-only`

The legacy threshold is not evaluated; only the hard boundary applies.

## Default geometry

```text
LOADER_SIZE          = 0x00040000
PAYLOAD_HEADER_SIZE  = 0x00000020
PAYLOAD_SLOT_END     = 0x00400000
FALLBACK_REGION_SIZE = 0x00400000
LEGACY_PAYLOAD_LIMIT = 0x003bffe0
HARD_PAYLOAD_LIMIT   = 0x003bffe0
```

With the historical 4 MiB payload slot, `0x003bffe0` is the actual safe payload
capacity: `0x400000 - 0x40000 - 0x20`. `PAYLOAD_SLOT_END` may be earlier than the
next fallback loader when another partition follows the kernel. The original
source compared the
payload length to `0x003c0000`, but that length excludes the 32-byte header, so
an exact-maximum payload could read 32 bytes into the next region. The corrected
hard boundary includes the header and removes that off-by-32 condition.

## Reviewable upstream patch

`patches/0001-vcoreiii-loader-validation-policies.patch` applies all policy logic
to an untouched GPL `head.S`. `scripts/refresh-from-gpl.sh` extracts the source,
applies that textual patch, and copies the result into `src/head.S`.

The supplied precompiled RedBoot images are never patched or consumed by a
normal build.
