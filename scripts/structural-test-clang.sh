#!/usr/bin/env bash
# Host-side structural test for environments without GNU mipsel-linux-gnu-gcc.
# This does NOT replace the release GNU cross build: Clang rejects the original
# GCC combination of -fPIC with -mno-abicalls, so C objects are compiled
# non-PIC here solely to validate source completeness, linking, wrapper layout,
# policy selection, and source-generated payload packaging.
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

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

common=(--target=mipsel-linux-gnu -march=mips32r2 -mabi=32 -msoft-float -mno-abicalls \
  -ffreestanding -fno-builtin -fno-common -fno-stack-protector -fomit-frame-pointer \
  -Os -nostdinc -I"$root/include" -I"$root/src")

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
         -DCONFIG_UART_RAMLOADER_COUNT_HZ=208000000)

  dir="$tmp/$variant"
  mkdir -p "$dir"
  clang "${common[@]}" -D__ASSEMBLY__ -x assembler-with-cpp "${defs[@]}" \
    -c "$root/src/head.S" -o "$dir/head.o"
  clang "${common[@]}" -std=gnu89 -fno-pic -G0 -c "$root/src/init_luton26.c" -o "$dir/init_luton26.o"
  clang "${common[@]}" -std=gnu89 -fno-pic -G0 -c "$root/src/init_jaguar.c" -o "$dir/init_jaguar.o"
  clang "${common[@]}" -std=gnu89 -fno-pic -G0 "${defs[@]}" \
    -c "$root/src/uart_ramloader.c" -o "$dir/uart_ramloader.o"
  ld.lld -m elf32ltsmip -static -nostdlib -T "$root/src/loader.lds" \
    -o "$dir/loader.elf" "$dir/head.o" "$dir/init_luton26.o" "$dir/init_jaguar.o" "$dir/uart_ramloader.o"
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
    --loader "$dir/loader.bin" --image "$dir/image.bin" --symbols "$dir/loader.sym"
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
