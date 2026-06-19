# Standalone VCore-III LinuxLoader builder

This project extracts the Meraki/Vitesse VCore-III ROM loader from the
`MS42-GPL-sources-3-18-122` Linux tree and makes it buildable without building
Linux, OpenWrt, Buildroot, or any pre-existing bootloader binary.

The GPL archive calls the supplied 256 KiB images **RedBoot**. The compilable
code is Meraki's compact **LinuxLoader** in:

```text
linux-3.18/arch/mips/vcoreiii/loader/
```

It initializes supported Vitesse VCore-III hardware, leaves boot mode, reads a
32-byte MIPS payload header at flash offset `0x00040000`, copies the payload into
RAM, and enters the specified kernel entry point. The source contains both the
Luton26 path (`VSC7425`/`VSC7427`) and the Jaguar1 path (`VSC7460`) used by the
MS22/MS220/MS320/MS42 family.

## What is source-built

Every byte in the generated 256 KiB boot-region image comes from this tree:

- `src/head.S` — reset-time loader, payload copy, CRC and size policies.
- `src/init_luton26.c` and `src/init_jaguar.c` — SoC/DDR/SPI/UART setup.
- `include/vtss/` — GPL Vitesse register definitions required by initialization.
- `include/linux/` and `include/asm/` — a small freestanding compatibility layer.
- `src/boot_wrapper.S` — reconstructed reset vectors, selector, and loader
  descriptors for the 256 KiB flash boot region.

The supplied `redboot-nocrc.bin` and `redboot-nocrc-sz.bin` are **not build
inputs**. They were used only to recover and verify the outer boot-region layout.
There is no post-link search-and-replace, hex editing, or binary branch patching.

## Recommended profiles

The source now treats CRC behavior and size behavior as independent policies.
The hard flash-slot boundary is always enforced.

| Profile | CRC policy | Legacy size policy | Hard slot boundary |
|---|---|---|---|
| `strict` | reject mismatch | reject above legacy threshold | reject |
| `development` | print warning and continue | print warning and continue | reject |
| `permissive` | omit CRC calculation/table entirely | no legacy check | reject |

`development` is the default because it preserves diagnostic visibility without
allowing a malformed size field to copy into the next fallback region.

Build examples:

```bash
make VARIANT=strict
make VARIANT=development
make VARIANT=permissive
make variants
```

Custom combinations are also supported:

```bash
make VARIANT=custom \
  CRC_POLICY=warn \
  SIZE_POLICY=hard-only
```

Accepted values:

```text
CRC_POLICY=strict|warn|off
SIZE_POLICY=legacy-strict|legacy-warn|hard-only
```

### What `CRC_POLICY=off` means

The CRC-disabled build does not merely replace the final rejection branch with a
NOP. It compiles a separate copy-only loop, omits the expected-CRC load, omits
header and payload CRC work, and omits the 1 KiB CRC lookup table.

### Size safety

The default geometry is:

```text
loader/payload-header offset: 0x00040000
payload header size:          0x00000020
payload slot end:             0x00400000
next fallback region:         0x00400000
maximum safe payload:         0x003bffe0
```

`PAYLOAD_SLOT_END` is the first flash byte not owned by the header and kernel
payload; it may be earlier than `FALLBACK_REGION_SIZE` when a root filesystem
follows the kernel. `HARD_PAYLOAD_LIMIT` is always fatal and cannot exceed
`PAYLOAD_SLOT_END - 0x40000 - 0x20`. `LEGACY_PAYLOAD_LIMIT` is a separate
compatibility threshold that may reject, warn, or be ignored.

With the default historical layout both limits are `0x003bffe0`; this is 32
bytes below the original source constant because the declared size excludes the
header itself. A meaningful warning range can be created by setting a lower
legacy threshold while retaining the real hard boundary.

For the postmerkOS layout where SquashFS begins at `0x00300000`, build with:

```bash
make VARIANT=development PAYLOAD_SLOT_END=0x00300000
```

This automatically derives `HARD_PAYLOAD_LIMIT=0x002bffe0` while leaving the
fallback-region stride at 4 MiB.

## Fastest build on CachyOS with Distrobox

Place the project somewhere visible inside Distrobox, normally under your home
directory, then run:

```bash
./build.sh
# or
make distrobox
```

The helper creates an Ubuntu 22.04 container named
`postmerkos-vcoreiii-loader`, installs the MIPS little-endian GNU cross compiler
and binutils, and builds the default `development` profile.

Other profiles:

```bash
./build.sh strict --distrobox
./build.sh permissive --distrobox
```

## Native Debian/Ubuntu build

```bash
./scripts/install-deps.sh
make -j"$(nproc)"
```

The default flashable output is:

```text
artifacts/vcoreiii-linuxloader-development.bin
```

It is exactly `0x40000` bytes and is intended for the first 256 KiB loader
region. The payload header remains at offset `0x40000`.

## Kernel payload packaging and CRC

The expected CRC is stored in each kernel payload header; it is not compiled into
LinuxLoader. Build the kernel first, then package it:

```bash
make payload \
  KERNEL=/path/to/vmlinuz.bin \
  LOAD_ADDRESS=0x81000000 \
  ENTRY_POINT=0x81000000
```

Or call the utility directly:

```bash
python3 tools/mkvcoreiii_payload.py pack \
  --input vmlinuz.bin \
  --output kernel.vcoreiii-payload.bin \
  --load-address 0x81000000 \
  --entry-point 0x81000000

python3 tools/mkvcoreiii_payload.py verify kernel.vcoreiii-payload.bin
```

The packer pads the kernel to a 32-byte boundary, writes the little-endian
32-byte header, calculates standard IEEE CRC-32 over the zeroed-CRC header plus
the padded payload, and stores that CRC in the header.

## Outputs

For each loader profile the build keeps:

- raw position-independent `loader.bin`;
- linked loader ELF, map, symbols, and disassembly;
- linked 256 KiB boot-region ELF and disassembly;
- flashable 256 KiB image;
- SHA-256 and JSON build manifests containing selected policies and limits.

Use `make inspect VARIANT=development` to print active/fallback locations.

## Refreshing from the GPL archive

The compatibility layer and wrapper stay local, while the loader C/assembly and
Vitesse register headers can be re-imported reproducibly:

```bash
make refresh-source \
  GPL_ARCHIVE=/path/to/MS42-GPL-sources-3-18-122-master.zip
```

The import script extracts the canonical GPL files, applies the reviewable
policy patch in `patches/`, rewrites only the Vitesse include paths for the
standalone namespace, and records source hashes in `SOURCE-PROVENANCE.txt`.

## Status

All three profiles have passed freestanding MIPS32r2 Clang/LLD structural builds,
wrapper validation, source-policy checks, and payload packer CRC tests. The
release path remains GNU `mipsel-linux-gnu-gcc`/binutils, matching the original
Kbuild model.

No newly generated loader has yet been boot-tested on physical MS220/MS42
hardware. Keep a verified full-flash backup, external SPI recovery, and serial
logging for first hardware tests.

See `docs/BUILDING.md`, `docs/BOOT-REGION-FORMAT.md`,
`docs/SOURCE-PATCHES.md`, and `docs/HARDWARE-ACCEPTANCE.md`.
