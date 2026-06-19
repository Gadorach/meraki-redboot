#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)
command -v clang >/dev/null 2>&1 || { echo "SKIP: clang is not installed"; exit 0; }
command -v ld.lld >/dev/null 2>&1 || { echo "SKIP: ld.lld is not installed"; exit 0; }
command -v llvm-objcopy >/dev/null 2>&1 || { echo "SKIP: llvm-objcopy is not installed"; exit 0; }
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
common=(--target=mipsel-linux-gnu -march=mips32r2 -mabi=32 -msoft-float -mno-abicalls
  -ffreestanding -fno-builtin -fno-common -fno-stack-protector -fomit-frame-pointer
  -Os -nostdinc -I"$root/include" -std=gnu89 -Wall -Wextra)
for family in 1 2; do
  clang "${common[@]}" -DRECOVERY_SOC_FAMILY=$family -c \
    "$root/payloads/uart-firmware-recovery/recovery.c" -o "$tmp/recovery-$family.o"
  ld.lld -m elf32ltsmip -static -nostdlib \
    -T "$root/payloads/uart-firmware-recovery/linker.ld" \
    "$tmp/recovery-$family.o" -o "$tmp/recovery-$family.elf"
  llvm-objcopy -O binary "$tmp/recovery-$family.elf" "$tmp/recovery-$family.bin"
  test "$(stat -c %s "$tmp/recovery-$family.bin")" -gt 0
  test "$(stat -c %s "$tmp/recovery-$family.bin")" -le 4194304
done
grep -a -q 'PMOSRECOVERY2;SOC=luton26' "$tmp/recovery-1.bin"
grep -a -q 'PMOSRECOVERY2;SOC=jaguar1' "$tmp/recovery-2.bin"
echo 'Clang recovery payload structural builds passed for Luton26 and Jaguar1.'
