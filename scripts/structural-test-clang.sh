#!/usr/bin/env bash
# Host-side structural test for environments without the pinned GCC 4.7.3.
# The flash stage uses Clang only as a source/link/layout check. The UART engine
# is independently fixed-linked at its reserved uncached RAM address.
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)
for tool in clang ld.lld llvm-objcopy llvm-objdump; do
  command -v "$tool" >/dev/null 2>&1 || { echo "SKIP: $tool is not installed"; exit 0; }
done
if command -v llvm-readelf >/dev/null 2>&1; then readelf_tool=llvm-readelf; else readelf_tool=readelf; fi
if command -v llvm-nm >/dev/null 2>&1; then nm_tool=llvm-nm; else nm_tool=nm; fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

common=(--target=mipsel-linux-gnu -march=mips32r2 -mabi=32 -msoft-float -mno-abicalls \
  -ffreestanding -fno-builtin -fno-common -fno-stack-protector -fomit-frame-pointer \
  -Os -nostdinc -I"$root/include" -I"$root/src")
stage1_addr=0xa7f00000
stage1_max=0x00100000
printf 'PMOSRECOVERY2;SOC=luton26;STRUCTURAL\n' > "$tmp/recovery-luton26.bin"
printf 'PMOSRECOVERY2;SOC=jaguar1;STRUCTURAL\n' > "$tmp/recovery-jaguar1.bin"

if [[ -n ${PROFILE_FILTER:-} ]]; then read -r -a profiles <<<"$PROFILE_FILTER"; else profiles=(strict development permissive strict-uart); fi
for variant in "${profiles[@]}"; do
  case "$variant" in
    strict) crc_policy=strict; size_policy=legacy-strict; defs=(-DCONFIG_CRC_POLICY_STRICT=1 -DCONFIG_SIZE_POLICY_LEGACY_STRICT=1); uart=0 ;;
    development) crc_policy=warn; size_policy=legacy-warn; defs=(-DCONFIG_CRC_POLICY_WARN=1 -DCONFIG_SIZE_POLICY_LEGACY_WARN=1); uart=1 ;;
    permissive) crc_policy=off; size_policy=hard-only; defs=(-DCONFIG_CRC_POLICY_OFF=1 -DCONFIG_SIZE_POLICY_HARD_ONLY=1); uart=0 ;;
    strict-uart) crc_policy=strict; size_policy=legacy-strict; defs=(-DCONFIG_CRC_POLICY_STRICT=1 -DCONFIG_SIZE_POLICY_LEGACY_STRICT=1); uart=1 ;;
  esac
  defs+=(-DCONFIG_FALLBACK_REGION_SIZE=0x00400000 \
         -DCONFIG_PAYLOAD_SLOT_END=0x00400000 \
         -DCONFIG_LEGACY_PAYLOAD_LIMIT=0x003bffe0 \
         -DCONFIG_HARD_PAYLOAD_LIMIT=0x003bffe0 \
         -DCONFIG_UART_RAMLOADER_MAX_SIZE=0x00400000 \
         -DCONFIG_UART_RAMLOADER_RAM_START=0x81000000 \
         -DCONFIG_UART_RAMLOADER_RAM_END=0x87f00000 \
         -DCONFIG_UART_RAMLOADER_PROBE_TIMEOUT_MS=3000 \
         -DCONFIG_UART_RAMLOADER_INTERBYTE_TIMEOUT_MS=3000 \
         -DCONFIG_UART_MENU_TIMEOUT_MS=5000 \
         -DCONFIG_UART_RAMLOADER_COUNT_HZ=208000000)

  dir="$tmp/$variant"
  mkdir -p "$dir"
  stage1_defs=()
  if [[ $uart == 1 ]]; then
    defs+=(-DCONFIG_UART_RAMLOADER=1)
    clang "${common[@]}" -D__ASSEMBLY__ -x assembler-with-cpp \
      -c "$root/src/uart_stage1_entry.S" -o "$dir/stage1-entry.o"
    clang "${common[@]}" -std=gnu89 -fno-pic -G0 "${defs[@]}" \
      -c "$root/src/uart_ramloader.c" -o "$dir/stage1-c.o"
    clang "${common[@]}" -D__ASSEMBLY__ -x assembler-with-cpp \
      -DRECOVERY_LUTON26_FILE=\"$tmp/recovery-luton26.bin\" \
      -DRECOVERY_JAGUAR1_FILE=\"$tmp/recovery-jaguar1.bin\" \
      -c "$root/src/uart_stage1_recovery_blobs.S" -o "$dir/stage1-recovery.o"
    ld.lld -m elf32ltsmip -static -nostdlib -T "$root/src/uart_stage1.lds" \
      --defsym=UART_STAGE1_LOAD_ADDR=$stage1_addr --defsym=UART_STAGE1_MAX_SIZE=$stage1_max \
      "$dir/stage1-entry.o" "$dir/stage1-c.o" "$dir/stage1-recovery.o" -o "$dir/uart-stage1.elf"
    llvm-objcopy -O binary "$dir/uart-stage1.elf" "$dir/uart-stage1.bin"
    python3 "$root/scripts/validate_uart_stage1.py" --elf "$dir/uart-stage1.elf" \
      --objdump llvm-objdump --readelf "$readelf_tool" --nm "$nm_tool" \
      --load-address $stage1_addr --max-size $stage1_max
    stage1_size=$(stat -c %s "$dir/uart-stage1.bin")
    stage1_defs=(-DUART_STAGE1_FILE=\"$dir/uart-stage1.bin\" \
      -DUART_STAGE1_SIZE=$stage1_size -DUART_STAGE1_LOAD_ADDR=$stage1_addr)
  fi

  clang "${common[@]}" -D__ASSEMBLY__ -x assembler-with-cpp "${defs[@]}" "${stage1_defs[@]}" \
    -c "$root/src/head.S" -o "$dir/head.o"
  clang "${common[@]}" -std=gnu89 -fno-pic -G0 -c "$root/src/init_luton26.c" -o "$dir/init_luton26.o"
  clang "${common[@]}" -std=gnu89 -fno-pic -G0 -c "$root/src/init_jaguar.c" -o "$dir/init_jaguar.o"
  ld.lld -m elf32ltsmip -static -nostdlib -T "$root/src/loader.lds" \
    -o "$dir/loader.elf" "$dir/head.o" "$dir/init_luton26.o" "$dir/init_jaguar.o"
  llvm-objcopy -O binary "$dir/loader.elf" "$dir/loader.bin"
  "$nm_tool" -n "$dir/loader.elf" > "$dir/loader.sym"
  len=$(stat -c %s "$dir/loader.bin")
  fallback=$((0x40000 - len)); active=$((fallback - len))
  clang "${common[@]}" -D__ASSEMBLY__ -x assembler-with-cpp \
    -DACTIVE_OFFSET=$active -DFALLBACK_OFFSET=$fallback -DLOADER_LENGTH=$len \
    -DLOADER_FILE=\"$dir/loader.bin\" -c "$root/src/boot_wrapper.S" -o "$dir/wrapper.o"
  ld.lld -m elf32ltsmip -static -nostdlib -T "$root/src/wrapper.lds" -o "$dir/boot-region.elf" "$dir/wrapper.o"
  llvm-objcopy -O binary -j .boot "$dir/boot-region.elf" "$dir/image.bin"
  python3 "$root/scripts/validate_image.py" --variant "$variant" \
    --crc-policy "$crc_policy" --size-policy "$size_policy" \
    --loader "$dir/loader.bin" --image "$dir/image.bin" --symbols "$dir/loader.sym"
done

make -C "$root" --no-print-directory test-wrapper-fit
printf 'structural test kernel\n' > "$tmp/kernel.bin"
python3 "$root/tools/mkvcoreiii_payload.py" pack --input "$tmp/kernel.bin" \
  --output "$tmp/kernel.payload.bin" --load-address 0x81000000 --entry-point 0x81000000 \
  --metadata "$tmp/kernel.payload.json"
python3 "$root/tools/mkvcoreiii_payload.py" verify "$tmp/kernel.payload.bin"
echo "Clang structural source/wrapper test passed for: ${profiles[*]}."
