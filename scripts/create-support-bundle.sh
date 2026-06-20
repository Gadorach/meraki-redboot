#!/usr/bin/env bash
set -euo pipefail
script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)
root=$(cd "$script_dir/.." && pwd -P)
# shellcheck source=scripts/toolchain-config.sh
source "$script_dir/toolchain-config.sh"
work_root=${POSTMERKOS_WORK_ROOT:-$(postmerkos_work_root)}
out_dir=${1:-$work_root/support}
mkdir -p "$out_dir" "$work_root/logs"
stamp=$(date -u +%Y%m%d-%H%M%S)
out="$out_dir/vcoreiii-build-support-$stamp.tar.xz"
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
mkdir -p "$stage/work" "$stage/project"

for path in build out artifacts recovery logs; do
    [[ -e $work_root/$path ]] && cp -a "$work_root/$path" "$stage/work/"
done
while IFS= read -r -d '' file; do
    rel=${file#"$work_root/"}; mkdir -p "$stage/work/$(dirname "$rel")"; cp -a "$file" "$stage/work/$rel"
done < <(find "$work_root/toolchain-build" -type f -name 'toolchain-build.log' -print0 2>/dev/null || true)
while IFS= read -r -d '' file; do
    rel=${file#"$work_root/"}; mkdir -p "$stage/work/$(dirname "$rel")"; cp -a "$file" "$stage/work/$rel"
done < <(find "$work_root/toolchains" -type f -name 'POSTMERKOS-TOOLCHAIN-MANIFEST.txt' -print0 2>/dev/null || true)

cp -a "$root/VERSION" "$root/Makefile" "$stage/project/"
cp -a "$root/scripts/toolchain-config.sh" "$root/scripts/validate_loader_codegen.py" "$stage/project/"
{
    echo "generated_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "project=$root"
    echo "work_root=$work_root"
    echo "toolchain_id=$POSTMERKOS_TOOLCHAIN_ID"
    prefix=$(postmerkos_cross_prefix)
    "$prefix"gcc --version | head -n1 || true
    "$prefix"ld --version | head -n1 || true
    echo
    echo '[work tree]'
    find "$work_root" -maxdepth 5 -type f -printf '%P %s bytes\n' 2>/dev/null | sort
} > "$stage/environment.txt" 2>&1

tar -cJf "$out" -C "$stage" .
sha256sum "$out" > "$out.sha256"
printf '%s\n' "$out"
