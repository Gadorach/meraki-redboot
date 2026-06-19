#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)
archive=${1:-}
[[ -n $archive ]] || { echo "usage: $0 /path/MS42-GPL-sources-3-18-122-master.zip" >&2; exit 2; }
archive=$(realpath "$archive")
[[ -f $archive ]] || { echo "archive not found: $archive" >&2; exit 2; }

for tool in unzip patch sha256sum find sed cp; do
  command -v "$tool" >/dev/null 2>&1 || { echo "missing required tool: $tool" >&2; exit 2; }
done

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

head_member=$(unzip -Z1 "$archive" | awk '/\/linux-3\.18\/arch\/mips\/vcoreiii\/loader\/head\.S$/ && !found {print; found=1}')
[[ -n $head_member ]] || { echo "VCore-III loader source not found in $archive" >&2; exit 1; }
prefix=${head_member%linux-3.18/arch/mips/vcoreiii/loader/head.S}

members=(
  "${prefix}linux-3.18/arch/mips/vcoreiii/loader/head.S"
  "${prefix}linux-3.18/arch/mips/vcoreiii/loader/init.h"
  "${prefix}linux-3.18/arch/mips/vcoreiii/loader/init_jaguar.c"
  "${prefix}linux-3.18/arch/mips/vcoreiii/loader/init_luton26.c"
  "${prefix}linux-3.18/arch/mips/include/asm/mach-vcoreiii/vtss_jaguar_core_regs.h"
  "${prefix}linux-3.18/arch/mips/include/asm/mach-vcoreiii/vtss_jaguar_regs_common.h"
  "${prefix}linux-3.18/arch/mips/include/asm/mach-vcoreiii/vtss_jaguar_regs_devcpu_gcb.h"
  "${prefix}linux-3.18/arch/mips/include/asm/mach-vcoreiii/vtss_jaguar_regs_icpu_cfg.h"
  "${prefix}linux-3.18/arch/mips/include/asm/mach-vcoreiii/vtss_jaguar_regs_twi.h"
  "${prefix}linux-3.18/arch/mips/include/asm/mach-vcoreiii/vtss_jaguar_regs_uart.h"
  "${prefix}linux-3.18/arch/mips/include/asm/mach-vcoreiii/vtss_luton26_core_regs.h"
  "${prefix}linux-3.18/arch/mips/include/asm/mach-vcoreiii/vtss_luton26_regs_common.h"
  "${prefix}linux-3.18/arch/mips/include/asm/mach-vcoreiii/vtss_luton26_regs_devcpu_gcb.h"
  "${prefix}linux-3.18/arch/mips/include/asm/mach-vcoreiii/vtss_luton26_regs_icpu_cfg.h"
  "${prefix}linux-3.18/arch/mips/include/asm/mach-vcoreiii/vtss_luton26_regs_twi.h"
  "${prefix}linux-3.18/arch/mips/include/asm/mach-vcoreiii/vtss_luton26_regs_uart.h"
)
unzip -q "$archive" "${members[@]}" -d "$tmp"

source_root="$tmp/${prefix%/}"
loader_dir="$source_root/linux-3.18/arch/mips/vcoreiii/loader"
header_dir="$source_root/linux-3.18/arch/mips/include/asm/mach-vcoreiii"

# Apply the reviewable policy implementation to the freshly extracted GPL tree.
patch -d "$source_root" -p1 < "$root/patches/0001-vcoreiii-loader-validation-policies.patch"

cp -f "$loader_dir/head.S" "$root/src/head.S"
cp -f "$loader_dir/init.h" "$root/src/init.h"
cp -f "$loader_dir/init_jaguar.c" "$root/src/init_jaguar.c"
cp -f "$loader_dir/init_luton26.c" "$root/src/init_luton26.c"

# Keep imported Vitesse register files under a standalone namespace. Quoted
# includes inside those generated register headers remain valid.
sed -i 's|<vtss_jaguar_regs_common.h>|<vtss/vtss_jaguar_regs_common.h>|' "$root/src/init_jaguar.c"
sed -i 's|<vtss_jaguar_core_regs.h>|<vtss/vtss_jaguar_core_regs.h>|' "$root/src/init_jaguar.c"
sed -i 's|<vtss_luton26_core_regs.h>|<vtss/vtss_luton26_core_regs.h>|' "$root/src/init_luton26.c"

vtss_headers=(
  vtss_jaguar_core_regs.h
  vtss_jaguar_regs_common.h
  vtss_jaguar_regs_devcpu_gcb.h
  vtss_jaguar_regs_icpu_cfg.h
  vtss_jaguar_regs_twi.h
  vtss_jaguar_regs_uart.h
  vtss_luton26_core_regs.h
  vtss_luton26_regs_common.h
  vtss_luton26_regs_devcpu_gcb.h
  vtss_luton26_regs_icpu_cfg.h
  vtss_luton26_regs_twi.h
  vtss_luton26_regs_uart.h
)
mkdir -p "$root/include/vtss"
for name in "${vtss_headers[@]}"; do
  [[ -f $header_dir/$name ]] || { echo "missing GPL register header: $name" >&2; exit 1; }
  cp -f "$header_dir/$name" "$root/include/vtss/$name"
done

archive_sha=$(sha256sum "$archive" | awk '{print $1}')
{
  echo "Source archive: $archive"
  echo "Archive SHA-256: $archive_sha"
  echo "Imported UTC: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "GPL loader path: linux-3.18/arch/mips/vcoreiii/loader"
  echo "GPL register path: linux-3.18/arch/mips/include/asm/mach-vcoreiii"
  echo
  echo "Imported files:"
  for f in "$root/src/head.S" "$root/src/init.h" "$root/src/init_jaguar.c" "$root/src/init_luton26.c" "$root/include/vtss"/*.h; do
    sha256sum "$f" | sed "s|$root/||"
  done
  echo
  echo "Applied patches:"
  sha256sum "$root"/patches/*.patch | sed "s|$root/||"
} > "$root/SOURCE-PROVENANCE.txt"

echo "Refreshed GPL loader sources and Vitesse register headers."
echo "Review SOURCE-PROVENANCE.txt, then run: make variants"
