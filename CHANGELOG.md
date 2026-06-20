# Release record

## 0.7.0 post-release UART receive-window correction

- Keep compact ACKs and deterministic target-to-host test streams byte-transparent.
- Add single-frame boundary recovery for malformed headers and payload timeouts,
  with the target frame timeout bounded below the host ACK deadline.
- Drain failed feature-test bursts before returning to the command parser,
  preventing queued binary frame data from producing `UNKNOWN-COMMAND` floods.
- Report structured frame error code, expected sequence, UART status and drained
  byte count, and expose UART status in compact ACK records.
- Retain protocol support for windows up to 16; production host policy uses
  window 1 while paced larger-window testing remains diagnostic-only.


## 0.7.0 post-release recovery manifest parser correction

- Scoped minimal JSON key lookup to direct members of the object being validated.
- Prevented nested `artifact.kernel_payload.sha256` and region digests from shadowing the top-level `artifact.sha256` binding.
- Added `manifest_lookup_contract: direct-object-members-v1` to recovery payload and loader manifests.
- Added regression coverage for sorted manifests where a nested SHA-256 key appears before the full-image digest.

## 0.7.0 — explicit stage-2 menu and embedded platform recovery

- Replaced the automatic UART-header probe with a two-step boot menu. Any byte
  received during the three-second probe opens the menu, but the trigger byte is
  discarded and an explicit `1` or `2` must be entered within five seconds.
- Menu option `1` enters the persistent PMOSRAM executable uploader. Menu option
  `2` copies and executes the firmware-recovery program matching the detected
  Luton26 or Jaguar1 SoC.
- Invalid input or menu timeout continues normal flash-kernel boot, preventing
  incidental UART noise from indefinitely stopping startup.
- Embedded both recovery binaries inside the fixed-RAM stage. Fatal flash-kernel
  validation failures now launch the matching embedded recovery directly rather
  than jumping to the historical next-flash-region fallback.
- Retained the dual loader copies in the 256 KiB wrapper; both complete copies,
  including both recovery payloads, still fit with substantial free space.
- Kept structured PASS/WARN/FAIL/SKIP diagnostics for flash-kernel and UART
  image checks, including expected/observed or declared/effective values.
- Updated the host RAM uploader to trigger the menu and select option `1`
  automatically; added `tools/uart_boot_menu.py` for manual option selection.
- Split fixed-stage code, data, and embedded binaries into separate ELF sections
  so call validation never interprets payload bytes as MIPS instructions.
- Exact GCC 4.7.3/binutils 2.23.2 development build passes with a 39,952-byte
  fixed-RAM stage, 54,816-byte loader copy, and 262,144-byte boot region.

## 0.6.1 — historical size rounding and structured image diagnostics

- Changed unaligned SPIM kernel sizes from an unconditional failure into the
  historical 32-byte rounded-copy behavior for `development`, `permissive`, and
  non-strict custom profiles. The declared size is rounded up only for transfer,
  CRC, boundary, overlap, and cache-maintenance calculations.
- Kept zero size, rounding overflow, hard-slot overflow, invalid load/entry
  geometry, stack overlap, stage overlap, and strict-profile unaligned size as
  fail-closed checks.
- Added structured `PASS`, `WARN`, `FAIL`, and `SKIP` diagnostics with actual
  compared values for every flash-kernel header, size, address, overlap, copy,
  CRC, cache, and execution check.
- Added the same detailed diagnostics to UART executable header validation and
  final object CRC-32/SHA-256 verification. Per-frame transfer success remains
  represented by compact ACK lines; retransmission failures now include the
  expected and observed sequence, CRC, or remaining length.
- Preserved the protocol-v2 `PMOSRAM VERIFIED` and `PMOSRAM EXEC` markers so
  existing host senders remain compatible.
- Fixed the fixed-stage validation recipe so a validator failure piped through
  `tee` propagates its real exit status instead of allowing the build to continue.
- Exact GCC 4.7.3/binutils 2.23.2 development build and all structural profiles
  pass with the expanded diagnostics.

## 0.6.0 — fixed-RAM boot continuation

- Removed the normal return from fixed-RAM stage 1 to relocatable flash code.
  Hardware reached `PMOSRAM STAGE1 RETURN` but did not continue through the
  legacy kernel path.
- Stage 1 now permanently owns the post-UART boot sequence: SPIM header
  validation, mandatory and legacy size policies, CRC strict/warn/off policy,
  flash-to-RAM copy, cache maintenance, kernel entry, and fallback-region jump.
- The flash shim passes the detected SoC family, current payload-header address,
  next fallback entry, and loader runtime base before transferring control with
  a non-returning `jr`.
- Added explicit `PMOSBOOT` serial diagnostics for context, header location,
  kernel geometry, execution, and failure/fallback reasons.
- Added stack and fixed-stage overlap rejection before kernel copy.
- Added assembly helpers that clear kernel argument registers and preserve the
  original fallback reason convention in `k0`.
- Updated fixed-stage validation and regression coverage. Exact GCC 4.7.3
  builds pass strict, development, permissive, and strict-plus-UART profiles.

## 0.5.1 — GNU assembler relocatable-label fix

- Corrected the fixed-RAM stage shim so the two serial marker addresses are
  formed with explicit `R_MIPS_HI16`/`R_MIPS_LO16` relocations instead of
  `li register, symbol`. GNU assembler 2.23.2 requires the latter operand to be
  an absolute assembly-time expression and rejected the v0.5.0 source.
- Added regression coverage that requires `%hi/%lo` address construction for
  both `PMOSRAM STAGE1 COPY` and `PMOSRAM STAGE1 RETURN`.
- Compiled the complete development loader and both recovery payloads with the
  supplied GCC 4.7.3/binutils 2.23.2 toolchain. The exact legacy build now
  passes stage validation, flash-loader validation, wrapper generation, and the
  final 256 KiB image validator.
- Exact GCC 4.7.3 development output: stage 1 `0x1050` bytes, loader `19048`
  bytes, boot region `262144` bytes.

## 0.5.0 — fixed-RAM UART stage

- Corrected the first hardware-tested post-initialization failure, where the
  loader stopped immediately after `Low level initialization complete, exiting
  boot mode`.
- Removed ordinary UART C code from the relocatable flash-resident LinuxLoader.
  GCC 4.7.3 accepted the historical `-fPIC -mno-abicalls` combination but left
  direct MIPS `J/JAL` targets and absolute literal addresses that did not follow
  the active/fallback copy's runtime offset.
- Added a separately linked UART stage-1 executable at fixed uncached address
  `0xa7f00000`. The flash loader embeds it as data, copies it after DDR and the
  stack are available, and enters it through an assembly `jalr` shim.
- Reserved the physical top 1 MiB of the 128 MiB DRAM map for stage 1. UART
  upload destinations remain below `0x87f00000`; stage 1 uses the uncached KSEG1
  alias beginning at `0xa7f00000`.
- Added fixed-stage ELF validation for entry address, size, BSS, dynamic/GOT
  sections, unresolved symbols, final relocations, and direct MIPS jump/call
  targets.
- Strengthened flash-loader validation to reject absolute pre-DDR `J`
  instructions as well as stack use and call/link instructions.
- Added serial progress markers `PMOSRAM STAGE1 COPY` and `PMOSRAM STAGE1
  RETURN` to distinguish flash remap, stage copy/entry, UART timeout, and return
  failures during hardware testing.
- Corrected the assembly-to-C O32 call boundary: the shim now reserves the
  mandatory 16-byte caller argument area and stores its saved `gp`/SoC state
  above it, preventing GCC stage prologues from overwriting the return context.
- Added stage-1 ELF, binary, disassembly, validation log, hashes, and load address
  to the source-local `.work/` outputs and build manifest format v5.
- Expanded regression coverage for the two-stage execution architecture.

## 0.4.3

- Forced the remaining CP0/TLB compatibility helpers in
  `include/asm/mipsregs.h` to inline under GCC 4.7.3, eliminating the calls
  that caused the observed pre-DDR `ra`/`s8` save frame.
- Moved the pinned toolchain, downloads, source-build logs, objects, ELFs,
  disassemblies, relocation tables, recovery outputs, and final artifacts into
  a source-local `.work/` tree.
- Added automatic import of a verified v0.4.0-v0.4.2 toolchain from the former
  XDG cache location.
- Added persistent native/Distrobox build logs and object-level `.cmd`, `.s`,
  `.dis`, and `.relocs` diagnostics before validation.
- Added `make work-layout` and `make support-bundle` for easy comparison and
  sharing of failed or successful builds.
- Enhanced the code-generation validator to report stack and call/link
  offenders together.

## 0.4.2

- Corrected GCC 4.7.3 reset-time code generation so the Luton26 and Jaguar1
  initialization entry points remain stackless before DDR is available.
- Forced the complete pre-DDR helper graph inline and expanded the common
  orchestration only after platform-specific helper bodies are visible.
- Marked `s0`-`s7` call-clobbered only for the two pre-DDR translation units,
  eliminating O32 callee-save stack traffic without changing post-DDR ABI.
- Strengthened validation to reject both stack references and nested MIPS
  call/link instructions in reset-time objects.
- Added regression coverage for the inline/register contract.

## 0.4.1

- Fixed the Ubuntu 22.04 source-toolchain bootstrap when Binutils 2.23.2
  recursively attempted to regenerate BFD Info documentation.
- Added `texinfo`, `bison`, `gawk`, and `m4` to every supported host
  dependency path.
- Passes `MAKEINFO=true` as a command-line make variable to all Binutils and
  GCC build/install stages so recursive makes inherit the override through
  `MAKEFLAGS`.
- Added a documented clean retry path for incomplete v0.4.0 toolchains.

## 0.4.0 — pinned GCC 4.7.3 build environment

- Restored LinuxLoader's historical `-mno-abicalls -fPIC -G 65535` C build
  model instead of attempting to translate it into GCC 10 code generation.
- Added a checksum-pinned, source-built GNU GCC 4.7.3 and binutils 2.23.2
  freestanding `mipsel-linux-gnu` toolchain.
- Added separate native and Ubuntu 22.04 Distrobox toolchain caches.
- Added exact compiler/linker identity checks and a historical PIC compile
  probe before release builds.
- Added a pre-DDR stack-use validator and final ELF relocation/dynamic-linkage
  checks.
- Made plain `make all` ask whether to compile through Distrobox.
- Added noninteractive `BUILD_MODE=native`, `distrobox`, and `auto` modes while
  preserving `make distrobox` as a compatibility alias.
- Made the normal build produce the boot region and both UART firmware-recovery
  payloads.
- Enabled UART RAM loading by default in the development profile.
- Corrected malformed `always_inline` declarations and declaration-order
  warnings inherited from the loader source without changing behavior.

The current source tree implements build-manifest format version 7, PMOSRAM
protocol version 2, and PMOSREC firmware-recovery protocol version 3. Active capabilities are documented in the README
and `docs/`.

Development records and prior release notes are maintained under:

- `docs/history/changelog-pre-v0.3.md`
- `docs/history/source-policy-development.md`
- `docs/history/boot-region-reconstruction.md`
- `docs/history/uart-recovery-development.md`
- `docs/history/validation-notes.md`
- `docs/history/gcc10-build-corrections.md`
- `docs/history/gcc10-loader-codegen.md`
- `docs/history/binutils-bootstrap-v0.4.1.md`
- `docs/history/uart-fixed-ram-stage-v0.5.0.md`

## Unreleased — recovery flat-binary entry correction

- Added an assembly byte-zero entry veneer for both embedded recovery payloads.
- Fixed the recovery linker layout so ELF metadata cannot precede executable bytes.
- Initialises stack, global pointer, and BSS before entering C recovery code.
- Added build-time and structural assertions that `_start` is exactly `0x81000000`.
- Added a machine-readable `flat-binary-byte-zero-v1` payload entry contract.
## Unreleased — UART recovery package-header handoff

- Increased the initial PMOSPKG package-header receive grace period to 30 seconds.
- Kept per-frame interbyte timeout behavior unchanged.
- Documented that hosts must wait for the complete `PMOSREC DESCRIPTOR` line before sending binary data, preventing RX FIFO overrun while the polling recovery stage is still transmitting startup text.

## Unreleased — SPI NOR hardware preflight

- Preserve Jaguar1 `GENERAL_CTRL` and assert `IF_MASTER_SPI_ENA` before software SPI access.
- Detect all-high/all-low flash buses before any firmware package transfer.
- Add JEDEC, SFDP, status, protection, and error-latch preflight diagnostics.
- Add the `PMOSPFT1` destructive-but-restored 64 KiB scratch-sector test.
- Hard-protect the 256 KiB bootloader region and verify its before/after CRC-32 is unchanged.
- Publish `spi-nor-scratch-rw-restore-loader-crc-v3` and `preserve-general-ctrl-enable-spi-v1` capability contracts.

## Unreleased — MSCC software-SPI chip-select correction

- Corrected `SPI_MST:SW_MODE.SW_SPI_CS` handling to use the documented active-mask semantics.
- Assert CS0 with `BIT(0)` and deselect all devices by clearing the CS field, matching the working MSCC U-Boot bitbang driver.
- Ported the U-Boot mode-0 activation, transfer, and deactivation sequence.
- Added `PREFLIGHT=3` and `spi-nor-scratch-rw-restore-loader-crc-v3` so stale payloads with inverted CS handling are rejected.
- Added an early `SPI-CS-CONTRACT ACTIVE-MASK` diagnostic before JEDEC probing.


## Unreleased — PMOSREC binary UART transparency

- Split human-readable CRLF output from byte-transparent UART transmission.
- Send compact ACK records and deterministic target-to-host qualification data
  without rewriting binary `0x0a` bytes.
- Correct baud error diagnostics so ordinary divisor error no longer saturates
  to `4294967295` because of 32-bit multiplication overflow.
- Added contract tests that prevent binary protocol paths from using the text
  output helper.

## Unreleased — PMOSREC v3 adaptive UART recovery

- Kept the boot menu and PMOSRAM2 executable upload at the stable 115200 baud.
- Added target-divisor baud offers, bidirectional deterministic CRC qualification,
  autonomous rollback and two-percent rate refinement.
- Added 4096-byte frames, negotiated windows, CRC-protected compact cumulative
  ACK/NAK records and selective retransmission.
- Added manifest-first validation and raw, sparse, LZ4 and sparse-LZ4 transport
  qualification with full reconstructed-image SHA-256 verification.
- Added indefinite erase-confirmation retries, host-authorized automatic challenge
  response support, phase progress and a five-second target-side reset countdown.
- Added a family-specific ICPU watchdog fallback if the soft-chip reset request does not take effect.
- Published `PREFLIGHT=4`, protocol 3 and
  `pmosrec-v3-adaptive-uart-sparse-lz4-v1` capability boundaries.

## Unreleased — PMOSREC v3 stage-validator synchronization

- Updated the fixed-RAM stage validator to require embedded `PMOSRECOVERY3`
  payloads for both Luton26 and Jaguar1.
- Updated the Clang structural stage fixtures to embed v3 recovery markers.
- Added regression assertions preventing a future recovery-protocol upgrade from
  leaving the stage validator on an older descriptor generation.
- Release archives normalize source timestamps to avoid ZIP timezone clock-skew
  warnings on non-UTC build hosts.
