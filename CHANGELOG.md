# Release record

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

The current source tree implements boot-region format version 4 and UART
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
