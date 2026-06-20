#!/usr/bin/env bash
set -euo pipefail
script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)
# shellcheck source=scripts/toolchain-config.sh
source "$script_dir/toolchain-config.sh"

root=$(postmerkos_toolchain_root)
prefix=$(postmerkos_cross_prefix)
download_dir=$(postmerkos_toolchain_download_dir)
work_dir=$(postmerkos_toolchain_work_dir)
manifest="$root/POSTMERKOS-TOOLCHAIN-MANIFEST.txt"
jobs=${TOOLCHAIN_JOBS:-${JOBS:-$(nproc 2>/dev/null || echo 1)}}

if [[ ${TOOLCHAIN_REBUILD:-0} == 1 ]]; then
    rm -rf "$root" "$work_dir"
fi

# Import a previously verified v0.4.x toolchain into the project-local .work
# tree. This keeps upgraded source trees self-contained without forcing the
# several-minute source build to run again.
legacy_root=$(postmerkos_legacy_toolchain_root)
if [[ ! -x ${prefix}gcc && ${TOOLCHAIN_IMPORT_LEGACY:-1} == 1 && -x $legacy_root/bin/${POSTMERKOS_TOOLCHAIN_PREFIX_NAME}gcc ]]; then
    legacy_prefix="$legacy_root/bin/$POSTMERKOS_TOOLCHAIN_PREFIX_NAME"
    if "$script_dir/check-toolchain.sh" "$legacy_prefix" >/dev/null 2>&1; then
        echo "Importing verified legacy toolchain into source-local .work: $legacy_root"
        mkdir -p "$(dirname "$root")"
        rm -rf "$root"
        cp -a "$legacy_root" "$root"
    fi
fi

if [[ -x ${prefix}gcc ]]; then
    if "$script_dir/check-toolchain.sh" "$prefix"; then
        exit 0
    fi
    echo "Removing incomplete or invalid cached toolchain: $root" >&2
    rm -rf "$root"
fi

required=(bash make flex bison gawk makeinfo m4 tar bzip2 sha256sum sha512sum mktemp sed awk grep date tee)
for tool in "${required[@]}"; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "Missing host prerequisite '$tool'. Run ./scripts/install-deps.sh first." >&2
        exit 2
    }
done
if ! command -v curl >/dev/null 2>&1 && ! command -v wget >/dev/null 2>&1; then
    echo "Either curl or wget is required to download the pinned GNU sources." >&2
    exit 2
fi

base=$(postmerkos_toolchain_cache_base)
mkdir -p "$base" "$download_dir" "$(dirname "$root")" "$(dirname "$work_dir")"
lock="$base/.${POSTMERKOS_TOOLCHAIN_ID}.install.lock"
if ! mkdir "$lock" 2>/dev/null; then
    echo "Another toolchain installation appears to be active: $lock" >&2
    exit 2
fi
cleanup_lock() { rm -rf "$lock"; }
trap cleanup_lock EXIT

download_file() {
    local output=$1
    shift
    local tmp="${output}.part.$$"
    local url
    [[ -f $output ]] && return 0
    rm -f "$tmp"
    for url in "$@"; do
        echo "Downloading: $url"
        if command -v curl >/dev/null 2>&1; then
            if curl -L --fail --retry 3 --connect-timeout 20 -o "$tmp" "$url"; then
                mv "$tmp" "$output"
                return 0
            fi
        else
            if wget --tries=3 --timeout=30 -O "$tmp" "$url"; then
                mv "$tmp" "$output"
                return 0
            fi
        fi
        rm -f "$tmp"
    done
    echo "Unable to download $(basename "$output") from any configured GNU mirror." >&2
    return 1
}

verify_sha512() {
    local file=$1 expected=$2 actual
    actual=$(sha512sum "$file" | awk '{print $1}')
    if [[ $actual != "$expected" ]]; then
        echo "SHA-512 mismatch for $file" >&2
        echo "expected: $expected" >&2
        echo "actual:   $actual" >&2
        rm -f "$file"
        return 1
    fi
}

verify_sha256() {
    local file=$1 expected=$2 actual
    actual=$(sha256sum "$file" | awk '{print $1}')
    if [[ $actual != "$expected" ]]; then
        echo "SHA-256 mismatch for $file" >&2
        echo "expected: $expected" >&2
        echo "actual:   $actual" >&2
        rm -f "$file"
        return 1
    fi
}

gcc_archive="$download_dir/$POSTMERKOS_GCC_ARCHIVE"
binutils_archive="$download_dir/$POSTMERKOS_BINUTILS_ARCHIVE"
download_file "$gcc_archive" "${POSTMERKOS_GCC_URLS[@]}"
if ! verify_sha512 "$gcc_archive" "$POSTMERKOS_GCC_SHA512"; then
    download_file "$gcc_archive" "${POSTMERKOS_GCC_URLS[@]}"
    verify_sha512 "$gcc_archive" "$POSTMERKOS_GCC_SHA512"
fi
download_file "$binutils_archive" "${POSTMERKOS_BINUTILS_URLS[@]}"
if ! verify_sha256 "$binutils_archive" "$POSTMERKOS_BINUTILS_SHA256"; then
    download_file "$binutils_archive" "${POSTMERKOS_BINUTILS_URLS[@]}"
    verify_sha256 "$binutils_archive" "$POSTMERKOS_BINUTILS_SHA256"
fi

rm -rf "$work_dir"
mkdir -p "$work_dir/src" "$work_dir/build-binutils" "$work_dir/build-gcc"
tar -xjf "$binutils_archive" -C "$work_dir/src"
tar -xjf "$gcc_archive" -C "$work_dir/src"

rm -rf "$root"
mkdir -p "$root"

build_log="$work_dir/toolchain-build.log"
: > "$build_log"
run_logged() {
    echo "+ $*" | tee -a "$build_log"
    "$@" 2>&1 | tee -a "$build_log"
    local rc=${PIPESTATUS[0]}
    return "$rc"
}

echo "Building pinned GNU binutils $POSTMERKOS_TOOLCHAIN_EXPECTED_BINUTILS for $POSTMERKOS_TOOLCHAIN_TARGET"
(
    cd "$work_dir/build-binutils"
    export MAKEINFO=true
    export CFLAGS='-O2 -fcommon'
    export CXXFLAGS='-O2 -fcommon -fpermissive'
    run_logged "$work_dir/src/binutils-2.23.2/configure" \
      --target="$POSTMERKOS_TOOLCHAIN_TARGET" \
      --prefix="$root" \
      --disable-nls \
      --disable-werror \
      --disable-gdb \
      --disable-sim \
      --disable-multilib
    run_logged make -j"$jobs" MAKEINFO=true all-binutils all-gas all-ld
    run_logged make MAKEINFO=true install-binutils install-gas install-ld
)

export PATH="$root/bin:$PATH"
echo "Building pinned GNU GCC $POSTMERKOS_TOOLCHAIN_EXPECTED_GCC C cross-compiler"
(
    cd "$work_dir/build-gcc"
    export MAKEINFO=true
    export CFLAGS='-O2 -std=gnu89 -fcommon'
    export CXXFLAGS='-O2 -std=gnu++98 -fcommon -fpermissive'
    export CFLAGS_FOR_BUILD="$CFLAGS"
    export CXXFLAGS_FOR_BUILD="$CXXFLAGS"
    run_logged "$work_dir/src/gcc-4.7.3/configure" \
      --target="$POSTMERKOS_TOOLCHAIN_TARGET" \
      --prefix="$root" \
      --with-gnu-as \
      --with-gnu-ld \
      --with-newlib \
      --without-headers \
      --without-ppl \
      --without-cloog \
      --disable-bootstrap \
      --disable-nls \
      --disable-shared \
      --disable-threads \
      --disable-multilib \
      --disable-libssp \
      --disable-libgomp \
      --disable-libquadmath \
      --disable-libmudflap \
      --disable-decimal-float \
      --disable-werror \
      --enable-languages=c \
      --with-arch=mips32r2 \
      --with-float=soft
    run_logged make -j"$jobs" MAKEINFO=true all-gcc
    run_logged make MAKEINFO=true install-gcc
)

cat > "$root/POSTMERKOS-TOOLCHAIN-MANIFEST.txt" <<MANIFEST
schema=$POSTMERKOS_TOOLCHAIN_SCHEMA
id=$POSTMERKOS_TOOLCHAIN_ID
host_flavor=$(postmerkos_toolchain_host_flavor)
target=$POSTMERKOS_TOOLCHAIN_TARGET
prefix=$POSTMERKOS_TOOLCHAIN_PREFIX_NAME
gcc_version=$POSTMERKOS_TOOLCHAIN_EXPECTED_GCC
gcc_archive=$POSTMERKOS_GCC_ARCHIVE
gcc_sha512=$POSTMERKOS_GCC_SHA512
binutils_version=$POSTMERKOS_TOOLCHAIN_EXPECTED_BINUTILS
binutils_archive=$POSTMERKOS_BINUTILS_ARCHIVE
binutils_sha256=$POSTMERKOS_BINUTILS_SHA256
installed_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)
MANIFEST

"$script_dir/check-toolchain.sh" "$prefix"
if [[ ${TOOLCHAIN_KEEP_BUILD:-0} != 1 ]]; then
    rm -rf "$work_dir/src" "$work_dir/build-binutils" "$work_dir/build-gcc"
fi

echo "Pinned source-built toolchain installed at: $root"
echo "Build log: $build_log"
