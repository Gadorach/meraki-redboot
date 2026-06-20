#!/usr/bin/env bash
set -euo pipefail
script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)
# shellcheck source=scripts/toolchain-config.sh
source "$script_dir/toolchain-config.sh"

prefix=${1:-$(postmerkos_cross_prefix)}
gcc=${prefix}gcc
ld=${prefix}ld
objdump=${prefix}objdump
readelf=${prefix}readelf
manifest="$(dirname "$(dirname "$prefix")")/POSTMERKOS-TOOLCHAIN-MANIFEST.txt"

for tool in "$gcc" "$ld" "$objdump" "$readelf"; do
    if [[ ! -x $tool ]]; then
        echo "missing pinned source-built toolchain executable: $tool" >&2
        echo "Run 'make toolchain', or run 'make all' and choose Distrobox." >&2
        exit 2
    fi
done

version=$($gcc -dumpversion 2>/dev/null || true)
target=$($gcc -dumpmachine 2>/dev/null || true)
ld_version=$($ld --version 2>/dev/null | head -n1 || true)
if [[ $target != "$POSTMERKOS_TOOLCHAIN_TARGET" ]]; then
    echo "unsupported compiler target: $gcc reports '$target'" >&2
    echo "Expected exactly $POSTMERKOS_TOOLCHAIN_TARGET." >&2
    exit 2
fi
if [[ $version != "$POSTMERKOS_TOOLCHAIN_EXPECTED_GCC" ]]; then
    if [[ ${ALLOW_UNVERIFIED_TOOLCHAIN:-0} != 1 ]]; then
        echo "unsupported loader compiler: $gcc reports '$version'" >&2
        echo "Expected exactly GCC $POSTMERKOS_TOOLCHAIN_EXPECTED_GCC." >&2
        echo "Set ALLOW_UNVERIFIED_TOOLCHAIN=1 only for a deliberate experiment." >&2
        exit 2
    fi
    echo "WARNING: accepting unverified GCC $version because ALLOW_UNVERIFIED_TOOLCHAIN=1" >&2
fi
if [[ $ld_version != *"$POSTMERKOS_TOOLCHAIN_EXPECTED_BINUTILS"* ]]; then
    if [[ ${ALLOW_UNVERIFIED_TOOLCHAIN:-0} != 1 ]]; then
        echo "unsupported linker: $ld_version" >&2
        echo "Expected GNU binutils $POSTMERKOS_TOOLCHAIN_EXPECTED_BINUTILS." >&2
        exit 2
    fi
    echo "WARNING: accepting unverified linker because ALLOW_UNVERIFIED_TOOLCHAIN=1" >&2
fi

if [[ -f $manifest ]]; then
    grep -Fxq "id=$POSTMERKOS_TOOLCHAIN_ID" "$manifest" || {
        echo "toolchain manifest ID mismatch: $manifest" >&2
        exit 2
    }
    grep -Fxq "gcc_sha512=$POSTMERKOS_GCC_SHA512" "$manifest" || {
        echo "toolchain manifest GCC source hash mismatch: $manifest" >&2
        exit 2
    }
    grep -Fxq "binutils_sha256=$POSTMERKOS_BINUTILS_SHA256" "$manifest" || {
        echo "toolchain manifest binutils source hash mismatch: $manifest" >&2
        exit 2
    }
elif [[ ${ALLOW_UNVERIFIED_TOOLCHAIN:-0} != 1 ]]; then
    echo "missing source-built toolchain manifest: $manifest" >&2
    exit 2
fi

probe_dir=$(mktemp -d)
trap 'rm -rf "$probe_dir"' EXIT
cat > "$probe_dir/probe.c" <<'PROBE'
static const char message[] = "legacy-pic-probe";
static int helper(unsigned int value) { return (int)(value ^ 0x47u); }
int probe(void) { return helper((unsigned int)message[0]); }
PROBE

if ! "$gcc" -EL -mabi=32 -march=mips32r2 -msoft-float \
        -mno-abicalls -fPIC -G 65535 \
        -fcall-used-s0 -fcall-used-s1 -fcall-used-s2 -fcall-used-s3 \
        -fcall-used-s4 -fcall-used-s5 -fcall-used-s6 -fcall-used-s7 \
        -finline-functions -finline-limit=100000 \
        -ffreestanding -fno-builtin -fno-common -fno-stack-protector \
        -fomit-frame-pointer -Os -nostdinc \
        -c "$probe_dir/probe.c" -o "$probe_dir/probe.o" \
        >"$probe_dir/stdout" 2>"$probe_dir/stderr"; then
    cat "$probe_dir/stderr" >&2
    echo "The selected compiler does not support the required historical PIC and pre-DDR register model." >&2
    exit 2
fi

machine=$($readelf -hW "$probe_dir/probe.o" | sed -n 's/^ *Machine: *//p')
[[ $machine == *MIPS* ]] || {
    echo "toolchain probe did not produce a MIPS object: $machine" >&2
    exit 2
}

printf 'toolchain verified: %s (GCC %s; %s)\n' \
  "$POSTMERKOS_TOOLCHAIN_ID" "$version" "$ld_version"
