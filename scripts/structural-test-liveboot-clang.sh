#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)
for tool in clang ld.lld llvm-objcopy readelf nm python3; do
  command -v "$tool" >/dev/null 2>&1 || { echo "SKIP: $tool is not installed"; exit 0; }
done

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/live" "$tmp/recovery" "$tmp/stage1"

common=(--target=mipsel-linux-gnu -EL -march=mips32r2 -mabi=32 -msoft-float -G 0
  -mno-abicalls -fno-pic -ffreestanding -fno-builtin -fno-common
  -fno-stack-protector -fomit-frame-pointer -ffunction-sections -fdata-sections
  -Os -nostdinc -I"$root/include" -I"$root/src" -std=gnu89 -Wall -Wextra
  -Wno-unused-function -Wno-unused-variable -Wno-unused-parameter -Wno-sign-compare)

clang "${common[@]}" -c "$root/payloads/uart-liveboot/entry.S" -o "$tmp/live/entry.o"
for family_spec in 'luton26:1' 'jaguar1:2'; do
  family=${family_spec%%:*}
  family_id=${family_spec##*:}
  clang "${common[@]}" -DRECOVERY_SOC_FAMILY=$family_id -c \
    "$root/payloads/uart-liveboot/liveboot.c" -o "$tmp/live/liveboot-$family.o"
  ld.lld -m elf32ltsmip -static -nostdlib --gc-sections \
    -T "$root/payloads/uart-liveboot/linker.ld" \
    "$tmp/live/entry.o" "$tmp/live/liveboot-$family.o" -o "$tmp/live/pmoslive-$family.elf"
  readelf -h "$tmp/live/pmoslive-$family.elf" | \
    grep -Eq 'Entry point address:[[:space:]]+0x86c00000$'
  test -z "$(nm -u "$tmp/live/pmoslive-$family.elf")"
  if nm "$tmp/live/pmoslive-$family.elf" | grep -Eiq 'spi|erase|program|flash'; then
    echo "PMOSLIVE $family retained a flash-related linked symbol" >&2
    exit 1
  fi
  llvm-objcopy -O binary "$tmp/live/pmoslive-$family.elf" "$tmp/live/pmoslive-$family.bin"
  python3 "$root/payloads/uart-liveboot/write_descriptor.py" \
    --family "$family" --binary "$tmp/live/pmoslive-$family.bin" \
    --output "$tmp/live/pmoslive-$family.descriptor.json"
  ! grep -aEq 'ERASEFLASH|FLASH-PREFLIGHT|PROGRESS ERASE|PROGRESS PROGRAM' \
    "$tmp/live/pmoslive-$family.bin"
  grep -aFq "PMOSLIVE3;SOC=$family" "$tmp/live/pmoslive-$family.bin"
done

clang "${common[@]}" -c "$root/payloads/uart-firmware-recovery/entry.S" \
  -o "$tmp/recovery/entry.o"
for family in 1 2; do
  clang "${common[@]}" -DRECOVERY_SOC_FAMILY=$family -c \
    "$root/payloads/uart-firmware-recovery/recovery.c" \
    -o "$tmp/recovery/recovery-$family.o"
  ld.lld -m elf32ltsmip -static -nostdlib --gc-sections \
    -T "$root/payloads/uart-firmware-recovery/linker.ld" \
    "$tmp/recovery/entry.o" "$tmp/recovery/recovery-$family.o" \
    -o "$tmp/recovery/recovery-$family.elf"
  readelf -h "$tmp/recovery/recovery-$family.elf" | \
    grep -Eq 'Entry point address:[[:space:]]+0x86c00000$'
  test -z "$(nm -u "$tmp/recovery/recovery-$family.elf")"
  llvm-objcopy -O binary "$tmp/recovery/recovery-$family.elf" \
    "$tmp/recovery/recovery-$family.bin"
done

policy=(-DCONFIG_CRC_POLICY_WARN=1 -DCONFIG_SIZE_POLICY_LEGACY_WARN=1
  -DCONFIG_FALLBACK_REGION_SIZE=0x00400000 -DCONFIG_PAYLOAD_SLOT_END=0x00400000
  -DCONFIG_LEGACY_PAYLOAD_LIMIT=0x003bffe0 -DCONFIG_HARD_PAYLOAD_LIMIT=0x003bffe0
  -DCONFIG_UART_RAMLOADER=1 -DCONFIG_UART_RAMLOADER_MAX_SIZE=0x00400000
  -DCONFIG_UART_RAMLOADER_RAM_START=0x81000000 -DCONFIG_UART_RAMLOADER_RAM_END=0x87f00000
  -DCONFIG_UART_RAMLOADER_PROBE_TIMEOUT_MS=3000
  -DCONFIG_UART_RAMLOADER_INTERBYTE_TIMEOUT_MS=3000 -DCONFIG_UART_MENU_TIMEOUT_MS=5000
  -DCONFIG_UART_RAMLOADER_COUNT_HZ=208000000
  -DCONFIG_UART_RAMLOADER_STAGE1_MAX_SIZE=0x00100000)

clang "${common[@]}" -x assembler-with-cpp -D__ASSEMBLY__ -c \
  "$root/src/uart_stage1_entry.S" -o "$tmp/stage1/entry.o"
clang "${common[@]}" "${policy[@]}" -c "$root/src/uart_ramloader.c" \
  -o "$tmp/stage1/uart_ramloader.o"
clang "${common[@]}" -x assembler-with-cpp -D__ASSEMBLY__ \
  -DRECOVERY_LUTON26_FILE=\"$tmp/recovery/recovery-1.bin\" \
  -DRECOVERY_JAGUAR1_FILE=\"$tmp/recovery/recovery-2.bin\" \
  -DLIVEBOOT_LUTON26_FILE=\"$tmp/live/pmoslive-luton26.bin\" \
  -DLIVEBOOT_JAGUAR1_FILE=\"$tmp/live/pmoslive-jaguar1.bin\" \
  -c "$root/src/uart_stage1_recovery_blobs.S" -o "$tmp/stage1/recovery_blobs.o"
ld.lld -m elf32ltsmip -static -nostdlib -T "$root/src/uart_stage1.lds" \
  --defsym=UART_STAGE1_LOAD_ADDR=0xa7f00000 \
  --defsym=UART_STAGE1_MAX_SIZE=0x00100000 \
  "$tmp/stage1/entry.o" "$tmp/stage1/uart_ramloader.o" \
  "$tmp/stage1/recovery_blobs.o" -o "$tmp/stage1/uart-stage1.elf"
readelf -h "$tmp/stage1/uart-stage1.elf" | \
  grep -Eq 'Entry point address:[[:space:]]+0xa7f00000$'
test -z "$(nm -u "$tmp/stage1/uart-stage1.elf")"
llvm-objcopy -O binary "$tmp/stage1/uart-stage1.elf" "$tmp/stage1/uart-stage1.bin"
test "$(stat -c %s "$tmp/stage1/uart-stage1.bin")" -le $((0x00100000))
for marker in \
  'PMOSBOOT MENU 1=UART-RAMLOADER 2=FW-RECOVERY 3=LIVEBOOT' \
  'PMOSLIVE3;SOC=luton26' \
  'PMOSLIVE3;SOC=jaguar1' \
  'PMOSRECOVERY3;SOC=luton26' \
  'PMOSRECOVERY3;SOC=jaguar1'; do
  grep -aFq "$marker" "$tmp/stage1/uart-stage1.bin"
done

printf 'Clang PMOSLIVE/stage1 structural build passed: luton26=%s bytes, jaguar1=%s bytes, stage1=%s bytes.\n' \
  "$(stat -c %s "$tmp/live/pmoslive-luton26.bin")" \
  "$(stat -c %s "$tmp/live/pmoslive-jaguar1.bin")" \
  "$(stat -c %s "$tmp/stage1/uart-stage1.bin")"
