# Release record

The current source tree implements boot-region format version 3 and UART
recovery protocol version 2. Its active capabilities are documented in the
README and `docs/`.

Development records and prior release notes are maintained under:

- `docs/history/changelog-pre-v0.3.md`
- `docs/history/source-policy-development.md`
- `docs/history/boot-region-reconstruction.md`
- `docs/history/uart-recovery-development.md`
- `docs/history/validation-notes.md`

## 0.7.0 — explicit stage menu and embedded platform recovery

- Added a two-step post-DDR UART menu: any byte interrupts the three-second
  probe, then a separate explicit `1` or `2` must arrive within five seconds.
- Drains bytes already buffered behind the interrupt character before accepting
  a menu choice, preventing a burst of line noise from selecting an option.
- Added option 1 for the framed PMOSRAM2 executable loader and option 2 for the
  recovery payload selected from the detected Jaguar1 or Luton26 family.
- Embedded both separately validated recovery images in a non-executable ELF
  section; instruction validation is limited to stage code while size, range,
  byte identity, and SHA-256 checks include both recovery images.
- Replaced the later 4 MiB flash-chain fallback with a persistent recovery menu
  after every fatal flash-kernel validation failure. The two identical loader
  copies inside the 256 KiB wrapper remain.
- Permanent stage 1 now copies the exact declared kernel byte count, allowing
  known legacy no-size images with non-32-byte lengths to boot safely. Newly
  packaged images remain 32-byte padded for compatibility with older loaders.
- Added menu-selection tooling, manifest metadata, source contracts, and
  hardware acceptance steps for RAM loading and platform recovery.

## 0.5.0

- Removed the full UART protocol engine from the relocatable flash text after
  hardware analysis proved its C calls and literals retained link-time flash
  addresses when the active loader copy was relocated.
- Added a separately linked fixed-RAM UART stage 1 at `0x80f00000`.
- Embedded the validated stage-1 binary as inert bytes in both source-built
  loader copies.
- Added a source-relative assembly copy loop, D-cache writeback/invalidation,
  I-cache invalidation, and fixed-address `jalr` entry shim after DDR setup.
- Reserved stage 1 below the upload window beginning at `0x81000000`.
- Added fixed-image extent, relocation, unresolved-symbol, BSS, and overlap
  validation plus byte-for-byte verification of the embedded stage-1 blob.
- Added standalone stage-1 ELF, binary, map, symbols, disassembly, artifact,
  checksum, and manifest metadata outputs.

## 0.4.2

- Replaced the CP0 TLB hazard and indexed-write compatibility helpers with
  statement macros so GCC 10 emits `ehb` and `tlbwi` directly at every
  pre-DDR call site.
- Preserved the exact TLB instruction sequence and memory clobbers while
  eliminating GCC's size-based `-Werror=inline` rejection.
- Added a regression contract that prohibits reintroducing callable TLB helper
  functions into the pre-DDR graph.

## 0.4.1

- Declared `loader-codegen-report.txt` as an output derived from `loader.elf`,
  fixing parallel manifest builds.
- Made `make` and `make all` automatically route through Distrobox when the
  host does not provide the GNU MIPS cross-toolchain.
- Prevented the Distrobox inner build from recursively invoking Distrobox.
- Made native tool checks stop cleanly after reporting missing executables.
