#!/usr/bin/env bash
# Host-side structural test for environments without GNU mipsel-linux-gnu-gcc.
# This does NOT replace the authoritative GNU GCC 10 build. It exercises the
# same no-ABI, data-free source model, PC-relative local-jump normalization,
# fixed wrapper geometry, policy selection, and payload packaging. Clang does
# not implement GCC's -fcall-used-s0...s7 policy, so the GNU validator remains
# authoritative for proving pre-DDR stacklessness.
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)
command -v clang >/dev/null 2>&1 || { echo "SKIP: clang is not installed"; exit 0; }
command -v ld.lld >/dev/null 2>&1 || { echo "SKIP: ld.lld is not installed"; exit 0; }
command -v llvm-objcopy >/dev/null 2>&1 || { echo "SKIP: llvm-objcopy is not installed"; exit 0; }
if command -v llvm-nm >/dev/null 2>&1; then
  nm_tool=llvm-nm
elif command -v nm >/dev/null 2>&1; then
  nm_tool=nm
else
  echo "SKIP: no nm implementation is installed"
  exit 0
fi
if command -v llvm-objdump >/dev/null 2>&1; then
  objdump_tool=llvm-objdump
else
  objdump_tool=objdump
fi
if command -v llvm-readelf >/dev/null 2>&1; then
  readelf_tool=llvm-readelf
else
  readelf_tool=readelf
fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
# LLD synthesizes an unused 8-byte MIPS GOT even when the input objects have no
# GOT relocations. The authoritative GNU build rejects GOT input and emits none.
# For this structural-only path, discard LLD's synthetic section explicitly.
python3 - "$root/src/loader.lds" "$tmp/loader-clang.lds" <<'PY_LDS'
from pathlib import Path
import sys
source = Path(sys.argv[1]).read_text()
source = source.replace("        *(.got) *(.got.plt)\n", "")
source = source.replace("*(.note*) *(.eh_frame*) }", "*(.note*) *(.eh_frame*) *(.got) *(.got.plt) }")
Path(sys.argv[2]).write_text(source)
PY_LDS
common=(--target=mipsel-linux-gnu -march=mips32r2 -mabi=32 -msoft-float \
  -mno-abicalls -fno-pic -G0 -ffreestanding -fno-builtin -fno-common \
  -fno-stack-protector -fomit-frame-pointer -Os -nostdinc \
  -I"$root/include" -I"$root/src" -Wno-unknown-attributes)
stage1_common=(--target=mipsel-linux-gnu -march=mips32r2 -mabi=32 -msoft-float \
  -mno-abicalls -fno-pic -G0 -ffreestanding -fno-builtin -fno-common \
  -fno-stack-protector -fomit-frame-pointer -Os -nostdinc \
  -I"$root/include" -I"$root/src" -Wno-unknown-attributes)

# Build the two target-specific recovery images once.  Stage 1 embeds these
# exact bytes in a non-executable section for platform-selected chainloading.
recovery_common=(--target=mipsel-linux-gnu -march=mips32r2 -mabi=32 -msoft-float
  -mno-abicalls -fno-pic -G0 -ffreestanding -fno-builtin -fno-common
  -fno-stack-protector -fomit-frame-pointer -Os -nostdinc -I"$root/include"
  -std=gnu89 -Wall -Wextra -Wno-unknown-attributes)
for family in luton26 jaguar1; do
  if [[ $family == luton26 ]]; then family_id=1; else family_id=2; fi
  clang "${recovery_common[@]}" -DRECOVERY_SOC_FAMILY=$family_id     -c "$root/payloads/uart-firmware-recovery/recovery.c"     -o "$tmp/recovery-$family.o"
  ld.lld -m elf32ltsmip -static -nostdlib     -T "$root/payloads/uart-firmware-recovery/linker.ld"     "$tmp/recovery-$family.o" -o "$tmp/recovery-$family.elf"
  python3 "$root/scripts/validate_fixed_payload.py"     --elf "$tmp/recovery-$family.elf" --entry 0x81000000     --readelf "$readelf_tool" --nm "$nm_tool"
  llvm-objcopy -O binary "$tmp/recovery-$family.elf" "$tmp/recovery-$family.bin"
done
clang "${common[@]}" -D__ASSEMBLY__ -x assembler-with-cpp   -DUART_RECOVERY_LUTON26_FILE=\"$tmp/recovery-luton26.bin\"   -DUART_RECOVERY_JAGUAR1_FILE=\"$tmp/recovery-jaguar1.bin\"   -c "$root/src/uart_recovery_blobs.S" -o "$tmp/uart-recovery-blobs.o"

if [[ -n ${PROFILE_FILTER:-} ]]; then
  read -r -a profiles <<<"$PROFILE_FILTER"
else
  profiles=(strict development permissive)
fi
for variant in "${profiles[@]}"; do
  case "$variant" in
    strict)
      crc_policy=strict
      size_policy=legacy-strict
      defs=(-DCONFIG_CRC_POLICY_STRICT=1 -DCONFIG_SIZE_POLICY_LEGACY_STRICT=1)
      ;;
    development)
      crc_policy=warn
      size_policy=legacy-warn
      defs=(-DCONFIG_CRC_POLICY_WARN=1 -DCONFIG_SIZE_POLICY_LEGACY_WARN=1)
      ;;
    permissive)
      crc_policy=off
      size_policy=hard-only
      defs=(-DCONFIG_CRC_POLICY_OFF=1 -DCONFIG_SIZE_POLICY_HARD_ONLY=1)
      ;;
  esac
  defs+=(-DCONFIG_FALLBACK_REGION_SIZE=0x00400000 \
         -DCONFIG_PAYLOAD_SLOT_END=0x00400000 \
         -DCONFIG_LEGACY_PAYLOAD_LIMIT=0x003bffe0 \
         -DCONFIG_HARD_PAYLOAD_LIMIT=0x003bffe0 \
         -DCONFIG_UART_RAMLOADER=1 \
         -DCONFIG_UART_RAMLOADER_MAX_SIZE=0x00400000 \
         -DCONFIG_UART_RAMLOADER_RAM_START=0x81000000 \
         -DCONFIG_UART_RAMLOADER_RAM_END=0x87f00000 \
         -DCONFIG_UART_RAMLOADER_PROBE_TIMEOUT_MS=3000 \
         -DCONFIG_UART_RAMLOADER_INTERBYTE_TIMEOUT_MS=3000 \
         -DCONFIG_UART_MENU_TIMEOUT_MS=5000 \
         -DCONFIG_UART_RAMLOADER_COUNT_HZ=208000000 \
         -DCONFIG_UART_STAGE1_ADDRESS=0xa7f00000 \
         -DCONFIG_UART_STAGE1_MAX_SIZE=0x00100000 \
         -DCONFIG_LOADER_REGION_SIZE=0x00040000)

  dir="$tmp/$variant"
  mkdir -p "$dir"
  clang "${stage1_common[@]}" -std=gnu89 "${defs[@]}" \
    -c "$root/src/uart_ramloader.c" -o "$dir/uart-stage1.o"
  ld.lld -m elf32ltsmip -static -nostdlib \
    --defsym=UART_STAGE1_ADDRESS=0xa7f00000 -T "$root/src/uart_stage1.lds" \
    -o "$dir/uart-stage1.elf" "$dir/uart-stage1.o" "$tmp/uart-recovery-blobs.o"
  python3 "$root/scripts/validate_uart_stage1.py" --elf "$dir/uart-stage1.elf" \
    --entry 0xa7f00000 --maximum-size 0x100000 --range-end 0xa8000000 \
    --objdump "$objdump_tool" --readelf "$readelf_tool" --nm "$nm_tool" \
    --recovery-luton26 "$tmp/recovery-luton26.bin" \
    --recovery-jaguar1 "$tmp/recovery-jaguar1.bin"
  llvm-objcopy -O binary "$dir/uart-stage1.elf" "$dir/uart-stage1.bin"
  python3 "$root/scripts/pad_binary.py" --alignment 4 "$dir/uart-stage1.bin"
  clang "${common[@]}" -D__ASSEMBLY__ -x assembler-with-cpp "${defs[@]}" \
    -DUART_STAGE1_FILE=\"$dir/uart-stage1.bin\" \
    -c "$root/src/head.S" -o "$dir/head.o"
  for platform in init_luton26 init_jaguar; do
    clang "${common[@]}" -std=gnu89 -DLOADER_EARLY_DATA_FREE=1 \
      -S "$root/src/$platform.c" -o "$dir/$platform.gcc.s"
    python3 "$root/scripts/normalize_mips_local_jumps.py" \
      --input "$dir/$platform.gcc.s" --output "$dir/$platform.s"
    clang "${common[@]}" -c "$dir/$platform.s" -o "$dir/$platform.o"
  done
  ld.lld -m elf32ltsmip -static -nostdlib -T "$tmp/loader-clang.lds" \
    -o "$dir/loader.elf" "$dir/head.o" "$dir/init_luton26.o" "$dir/init_jaguar.o"
  llvm-objcopy -O binary "$dir/loader.elf" "$dir/loader.bin"
  "$nm_tool" -n "$dir/loader.elf" > "$dir/loader.sym"
  len=$(stat -c %s "$dir/loader.bin")
  fallback=$((0x40000 - len))
  active=$((fallback - len))
  clang "${common[@]}" -D__ASSEMBLY__ -x assembler-with-cpp \
    -DACTIVE_OFFSET=$active -DFALLBACK_OFFSET=$fallback -DLOADER_LENGTH=$len \
    -DLOADER_FILE=\"$dir/loader.bin\" -c "$root/src/boot_wrapper.S" -o "$dir/wrapper.o"
  ld.lld -m elf32ltsmip -static -nostdlib -T "$root/src/wrapper.lds" \
    -o "$dir/boot-region.elf" "$dir/wrapper.o"
  llvm-objcopy -O binary -j .boot "$dir/boot-region.elf" "$dir/image.bin"
  python3 "$root/scripts/validate_image.py" --variant "$variant" \
    --crc-policy "$crc_policy" --size-policy "$size_policy" \
    --loader "$dir/loader.bin" --image "$dir/image.bin" --symbols "$dir/loader.sym" \
    --uart-stage1 "$dir/uart-stage1.bin"
done


# Exercise the Makefile arithmetic shared by the release wrapper recipe.
make -C "$root" --no-print-directory test-wrapper-fit

# Exercise the exact CRC/header implementation used by the build helper.
printf 'structural test kernel\n' > "$tmp/kernel.bin"
python3 "$root/tools/mkvcoreiii_payload.py" pack \
  --input "$tmp/kernel.bin" --output "$tmp/kernel.payload.bin" \
  --load-address 0x81000000 --entry-point 0x81000000 \
  --metadata "$tmp/kernel.payload.json"
python3 "$root/tools/mkvcoreiii_payload.py" verify "$tmp/kernel.payload.bin"

echo "Clang structural source/wrapper test passed for: ${profiles[*]}."
