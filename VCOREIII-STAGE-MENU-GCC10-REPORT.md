# VCore-III GCC 10 stage-menu and embedded-recovery report

Date: 2026-06-19
Version: 0.7.0 experimental candidate

## Reference input

The GCC 4.7.3 branch implementation was used as a design and build reference,
not as a hardware-proven menu/recovery baseline. The already confirmed hardware
facts retained from the earlier branch are the Jaguar1 early-init path, DDR and
cache setup, boot-mode exit, copying permanent stage 1 to the uncached top-of-
RAM alias, and successful entry into that stage.

## Implemented GCC 10 behavior

After the early Jaguar1/Luton26 initialization sequence, permanent stage 1 runs
at `0xa7f00000` and owns the remaining boot process.

A quiet boot prints `PMOSMENU PROBE`, waits three seconds, then validates and
loads the flash kernel. Any received byte interrupts that probe but is discarded.
Stage 1 drains all additional bytes already buffered behind it, prints a menu,
and accepts only a newly received `1` or `2` during the following five seconds:

- `1`: enter the PMOSRAM2 framed executable loader;
- `2`: select the recovery binary matching the detected SoC, copy it to
  `0x81000000`, perform cache maintenance, and enter it.

A menu timeout during an otherwise valid boot resumes flash-kernel processing.
A fatal flash-image check instead enters the same menu persistently. The old
later-region 4 MiB chain fallback is no longer used. The active and fallback
copies inside the 256 KiB first-stage wrapper remain byte-identical.

## Embedded recovery layout

The Luton26 and Jaguar1 recovery executables are separately linked and
validated at `0x81000000`. They are embedded inside stage 1 in an allocated but
non-executable `.embedded_recovery` section. The stage validator:

- disassembles only executable `.text`;
- rejects final relocations, unresolved symbols, BSS, GOT, PLT, and dynamic
  sections;
- confirms `.embedded_recovery` is not executable;
- compares both embedded byte ranges against their original binaries;
- records their exact size and SHA-256;
- enforces the complete stage range below `0xa8000000`.

## Legacy kernel compatibility

The development profile remains `CRC_POLICY=warn` and
`SIZE_POLICY=legacy-warn`. CRC mismatch and crossing the legacy threshold warn
and continue, while zero length, the hard flash-slot boundary, invalid KSEG0
addresses, arithmetic overflow, and RAM overlap remain fatal.

Permanent stage 1 no longer rejects a kernel merely because the declared size
is not divisible by 32. It copies the exact declared byte count. This safely
supports older no-size loader images while preserving the hard boundary. The
host packer continues to pad newly generated payloads to 32 bytes for backwards
compatibility with older assembly copy loops.

## Validation performed

Thirty Python source, menu, framing, recovery, and payload tests pass.
Code-generation validator positive and negative fixtures pass.

Clang/LLD structural builds passed for strict, development, and permissive
profiles with UART enabled. Their largest observed loader was 66,592 bytes,
leaving ample room for two copies inside the 256 KiB wrapper.

The exact supplied GCC 4.7.3/binutils 2.23.2 reference compiler built every
UART-enabled profile:

| Profile | Stage 1 | Loader copy | Boot region |
|---|---:|---:|---:|
| strict | 34,736 | 47,616 | 262,144 |
| development | 34,800 | 47,872 | 262,144 |
| permissive | 34,608 | 44,352 | 262,144 |

The development stage contains:

- executable stage code: `0x13d0` bytes;
- non-executable embedded recovery section: `0x6ec0` bytes;
- complete allocated image: `0x87f0` / 34,800 bytes;
- Luton26 recovery: `0x36f0` / 14,064 bytes;
- Jaguar1 recovery: `0x37d0` / 14,288 bytes.

Pre-DDR stage objects remain data-free, call-free, stackless, GOT-free, and
without unresolved relocations. All wrapper images validate as exactly 256 KiB
with identical active and fallback loader bodies.

## Remaining acceptance work

The exact Ubuntu GCC 10.3/binutils 2.38 Distrobox build must be run by the user.
The stage menu and embedded recovery selection are not yet hardware tested.
Firmware recovery should first be entered without sending a package, followed
by its documented dry-run path. Destructive erase/program testing should only
follow with a verified full SPI backup and external programmer connected.

## Expected hardware sequences

Quiet boot:

```text
PMOSMENU PROBE SOC=jaguar1 PROBE_MS=00000bb8
PMOSBOOT FLASH HEADER=...
PMOSBOOT FLASH-BOOT LOAD=... SIZE=... ENTRY=...
```

Interrupt with no explicit selection:

```text
PMOSMENU SELECT 1=RAM-LOADER 2=FW-RECOVERY TIMEOUT_MS=00001388
PMOSMENU TIMEOUT
PMOSBOOT FLASH HEADER=...
```

Explicit option 2 on Jaguar1:

```text
PMOSREC CHAINLOAD SOC=jaguar1 SIZE=... ENTRY=81000000
PMOSRECOVERY2;SOC=jaguar1;...
```

Fatal flash validation:

```text
PMOSBOOT FLASH-FAIL <reason>
PMOSMENU SELECT 1=RAM-LOADER 2=FW-RECOVERY TIMEOUT_MS=00001388
```
