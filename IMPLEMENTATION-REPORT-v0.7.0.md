# VCore-III Stage-2 Menu and Embedded Recovery — v0.7.0

## Purpose

This release turns the fixed-RAM stage into a deliberate boot/recovery selector while retaining normal unattended boot behavior.

## Boot menu

- Stage 2 listens for any UART activity for 3 seconds.
- No activity continues normal flash-kernel validation and boot.
- Any activity opens a menu; the trigger byte is discarded.
- The user then has 5 seconds to enter an explicit option:
  - `1`: persistent UART executable loader.
  - `2`: embedded firmware-recovery program selected by detected SoC.
- Invalid input is reported and ignored while the selection window remains open.
- Selection timeout continues normal boot, preventing incidental UART noise from permanently halting startup.

## Recovery behavior

The fixed-RAM stage embeds both recovery programs:

- Luton26: MS22/MS220 family.
- Jaguar1: MS42/MS320 family.

Fatal flash-image validation failures automatically copy and execute the matching recovery program at `0x81000000`. The obsolete next-4-MiB-region chain fallback is no longer used by the RAM stage.

The two complete LinuxLoader copies in the 256 KiB wrapper are retained. Both copies include the menu and both recovery binaries and still fit comfortably.

## Image diagnostics

Kernel and UART image checks emit structured records:

- `PASS-*`: check completed successfully.
- `WARN-*`: compatibility policy accepted a discrepancy.
- `FAIL-*`: fatal validation failure and recovery transition.
- `SKIP-*`: check intentionally disabled by policy.

Messages include compared values, such as:

```text
PMOSBOOT PASS-CRC EXPECTED: 0x4332F726 | GOT: 0x4332F726
PMOSBOOT WARN-SIZE-ALIGN DECLARED: 0x00234567 | EFFECTIVE: 0x00234580
PMOSBOOT FAIL-CRC EXPECTED: 0x4332F726 | GOT: 0x55457D65
```

Development mode uses historical 32-byte rounded transfer length for unaligned kernel payloads and reports `WARN-SIZE-ALIGN`. Strict mode rejects them.

## Exact GCC 4.7.3 build

- Toolchain: GCC 4.7.3, GNU binutils 2.23.2.
- Fixed-RAM stage: 39,952 bytes (`0x9c10`).
- Complete loader copy: 54,816 bytes.
- Boot region: 262,144 bytes.
- Boot-region SHA-256: `7596a3578a9e4ca2704b6cd3d2c1a8270cca02b8c46fcbe1fc4605136a6f54f4`.
- Stage SHA-256: `26889dba7132926f42ed47461d4ef2846661b9f102f98af8c57c5837157993c1`.

## Validation

- 39 Python contract and protocol tests passed.
- Strict, development, permissive, and strict-with-UART structural loader profiles passed.
- Luton26 and Jaguar1 recovery structural builds passed.
- Exact GCC 4.7.3 development build passed stage, loader, wrapper, relocation, and image validation.
