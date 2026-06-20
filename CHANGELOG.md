# Release record

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

The current source tree implements build-manifest format version 5 and UART
recovery protocol version 2. Active capabilities are documented in the README
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
