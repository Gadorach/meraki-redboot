#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)
for tool in clang ld.lld llvm-objdump readelf nm python3; do
  command -v "$tool" >/dev/null 2>&1 || { echo "SKIP: $tool is not installed"; exit 0; }
done

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

cat > "$tmp/good.c" <<'C'
typedef unsigned int u32;
__attribute__((noinline,used))
u32 stage_a(u32 x) { return x ^ 0x1357u; }
__attribute__((noinline,used))
u32 stage_b(u32 x) { __asm__ __volatile__("ehb" ::: "memory"); return x + 1u; }
C
cat > "$tmp/stack.S" <<'ASM'
.set noreorder
.set noat
.text
.globl stage_a
.type stage_a,@function
.ent stage_a
stage_a:
    addiu $29,$29,-8
    jr $31
     addiu $29,$29,8
.end stage_a
.size stage_a,.-stage_a
ASM
cat > "$tmp/absolute.S" <<'ASM'
.set noreorder
.set noat
.text
.globl stage_a
.type stage_a,@function
.ent stage_a
stage_a:
    lui $2,%hi(test_value)
    addiu $2,$2,%lo(test_value)
    jr $31
     nop
.end stage_a
.size stage_a,.-stage_a
.section .rodata,"a",@progbits
test_value: .word 1
ASM
cat > "$tmp/call.S" <<'ASM'
.set noreorder
.set noat
.text
.globl stage_a
.type stage_a,@function
.ent stage_a
stage_a:
    jal helper
     nop
    jr $31
     nop
.end stage_a
.size stage_a,.-stage_a
.type helper,@function
helper:
    jr $31
     nop
.size helper,.-helper
ASM
cat > "$tmp/local_jump.S" <<'ASM'
.set noreorder
.set noat
.text
.globl stage_a
.type stage_a,@function
.ent stage_a
stage_a:
    j 1f
     nop
1:
    jr $31
     nop
.end stage_a
.size stage_a,.-stage_a
ASM
cat > "$tmp/loader.lds" <<'LDS'
OUTPUT_ARCH(mips)
ENTRY(stage_a)
SECTIONS {
  . = 0;
  .loader : ALIGN(4) { *(.text*) }
  /DISCARD/ : {
    *(.rodata*) *(.data*) *(.sdata*) *(.lit4*) *(.lit8*)
    *(.got) *(.got.plt) *(.sbss*) *(.bss*) *(COMMON)
    *(.reginfo) *(.MIPS.abiflags) *(.pdr) *(.comment) *(.note*) *(.eh_frame*)
  }
}
LDS

common=(--target=mipsel-linux-gnu -march=mips32r2 -mabi=32 -msoft-float \
  -mno-abicalls -fno-pic -G0 -ffreestanding -fno-builtin -fno-common \
  -fno-stack-protector -fomit-frame-pointer -Os -nostdinc)
clang "${common[@]}" -S "$tmp/good.c" -o "$tmp/good.gcc.s"
python3 "$root/scripts/normalize_mips_local_jumps.py" \
  --input "$tmp/good.gcc.s" --output "$tmp/good.s" --count-file "$tmp/good-normalize.txt"
clang --target=mipsel-linux-gnu -march=mips32r2 -mabi=32 -msoft-float \
  -mno-abicalls -fno-pic -G0 -c "$tmp/good.s" -o "$tmp/good.o"
for kind in stack absolute call local_jump; do
  clang --target=mipsel-linux-gnu -march=mips32r2 -mabi=32 -msoft-float \
    -mno-abicalls -fno-pic -G0 -c "$tmp/$kind.S" -o "$tmp/$kind.o"
done
ld.lld -m elf32ltsmip -static -nostdlib -T "$tmp/loader.lds" "$tmp/good.o" -o "$tmp/good.elf"

python3 "$root/scripts/validate_loader_codegen.py" \
  --elf "$tmp/good.elf" --c-object "$tmp/good.o" \
  --pre-ddr-object "$tmp/good.o:stage_a,stage_b" \
  --objdump llvm-objdump --readelf readelf --nm nm

for kind in stack absolute call local_jump; do
  if python3 "$root/scripts/validate_loader_codegen.py" \
      --elf "$tmp/good.elf" --c-object "$tmp/$kind.o" \
      --pre-ddr-object "$tmp/$kind.o:stage_a" \
      --objdump llvm-objdump --readelf readelf --nm nm >/dev/null 2>&1; then
    echo "validator accepted invalid $kind pre-DDR fixture" >&2
    exit 1
  fi
done

cat > "$tmp/fixed.S" <<'ASM'
.set noreorder
.section .text.start,"ax",@progbits
.globl _start
.type _start,@function
_start:
    b _start
     nop
.size _start, .-_start
ASM
clang --target=mipsel-linux-gnu -march=mips32r2 -mabi=32 -mno-abicalls -fno-pic \
  -c "$tmp/fixed.S" -o "$tmp/fixed.o"
cat > "$tmp/fixed.lds" <<'LDS'
OUTPUT_ARCH(mips)
ENTRY(_start)
SECTIONS {
  . = 0x81000000;
  .text : { *(.text.start) *(.text*) }
  /DISCARD/ : { *(.reginfo) *(.MIPS.abiflags) *(.pdr) *(.comment) *(.got*) }
}
ASSERT(_start == 0x81000000, "bad entry")
LDS
ld.lld -m elf32ltsmip -static -nostdlib -T "$tmp/fixed.lds" "$tmp/fixed.o" -o "$tmp/fixed.elf"
python3 "$root/scripts/validate_fixed_payload.py" \
  --elf "$tmp/fixed.elf" --entry 0x81000000 --readelf readelf --nm nm

echo 'data-free pre-DDR and fixed-payload validator positive/negative fixtures passed.'
