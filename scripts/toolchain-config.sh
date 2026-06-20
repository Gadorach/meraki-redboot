#!/usr/bin/env bash
# Pinned source-built toolchain for the reset-time VCore-III LinuxLoader.

POSTMERKOS_TOOLCHAIN_SCHEMA='3'
POSTMERKOS_TOOLCHAIN_ID='gnu-mipsel-gcc-4.7.3-binutils-2.23.2-v1'
POSTMERKOS_TOOLCHAIN_TARGET='mipsel-linux-gnu'
POSTMERKOS_TOOLCHAIN_PREFIX_NAME='mipsel-linux-gnu-'
POSTMERKOS_TOOLCHAIN_EXPECTED_GCC='4.7.3'
POSTMERKOS_TOOLCHAIN_EXPECTED_BINUTILS='2.23.2'

POSTMERKOS_GCC_ARCHIVE='gcc-4.7.3.tar.bz2'
POSTMERKOS_GCC_URLS=(
  'https://gcc.gnu.org/pub/gcc/releases/gcc-4.7.3/gcc-4.7.3.tar.bz2'
  'https://ftp.gnu.org/gnu/gcc/gcc-4.7.3/gcc-4.7.3.tar.bz2'
  'https://ftpmirror.gnu.org/gcc/gcc-4.7.3/gcc-4.7.3.tar.bz2'
)
POSTMERKOS_GCC_SHA512='5671a2dd3b6ac0d23f305cb11a796aebd823c1462b873136b412e660966143f4e07439bd8926c1443b78442beb6ae370ef91d819ec615920294875b722b7b0bd'

POSTMERKOS_BINUTILS_ARCHIVE='binutils-2.23.2.tar.bz2'
POSTMERKOS_BINUTILS_URLS=(
  'https://ftp.gnu.org/gnu/binutils/binutils-2.23.2.tar.bz2'
  'https://ftpmirror.gnu.org/binutils/binutils-2.23.2.tar.bz2'
  'https://sourceware.org/pub/binutils/releases/binutils-2.23.2.tar.bz2'
)
POSTMERKOS_BINUTILS_SHA256='fe914e56fed7a9ec2eb45274b1f2e14b0d8b4f41906a5194eac6883cfe5c1097'

postmerkos_project_root() {
    if [[ -n ${POSTMERKOS_PROJECT_ROOT:-} ]]; then
        printf '%s\n' "$POSTMERKOS_PROJECT_ROOT"
    else
        cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P
    fi
}

postmerkos_work_root() {
    if [[ -n ${POSTMERKOS_WORK_ROOT:-} ]]; then
        printf '%s\n' "$POSTMERKOS_WORK_ROOT"
    else
        printf '%s/.work\n' "$(postmerkos_project_root)"
    fi
}

postmerkos_toolchain_cache_base() {
    if [[ -n ${POSTMERKOS_TOOLCHAIN_CACHE:-} ]]; then
        printf '%s\n' "$POSTMERKOS_TOOLCHAIN_CACHE"
    else
        printf '%s/toolchains\n' "$(postmerkos_work_root)"
    fi
}

# Compatibility location used by v0.4.0-v0.4.2. A verified toolchain found
# here is copied into the source-local .work tree so a costly rebuild is not
# required after upgrading.
postmerkos_legacy_toolchain_cache_base() {
    if [[ -n ${POSTMERKOS_LEGACY_TOOLCHAIN_CACHE:-} ]]; then
        printf '%s\n' "$POSTMERKOS_LEGACY_TOOLCHAIN_CACHE"
    elif [[ -n ${XDG_CACHE_HOME:-} ]]; then
        printf '%s/postmerkos-vcoreiii/toolchains\n' "$XDG_CACHE_HOME"
    else
        printf '%s/.cache/postmerkos-vcoreiii/toolchains\n' "$HOME"
    fi
}

postmerkos_toolchain_host_flavor() {
    if [[ -n ${POSTMERKOS_TOOLCHAIN_HOST_FLAVOR:-} ]]; then
        printf '%s\n' "$POSTMERKOS_TOOLCHAIN_HOST_FLAVOR"
        return
    fi

    local id=linux version=unknown arch
    arch=$(uname -m)
    if [[ -r /etc/os-release ]]; then
        # shellcheck disable=SC1091
        source /etc/os-release
        id=${ID:-linux}
        version=${VERSION_ID:-rolling}
    fi
    id=${id//[^A-Za-z0-9._-]/_}
    version=${version//[^A-Za-z0-9._-]/_}
    printf '%s-%s-%s\n' "$id" "$version" "$arch"
}

postmerkos_toolchain_root() {
    if [[ -n ${LEGACY_TOOLCHAIN_ROOT:-} ]]; then
        printf '%s\n' "$LEGACY_TOOLCHAIN_ROOT"
    else
        printf '%s/%s/%s\n' \
          "$(postmerkos_toolchain_cache_base)" \
          "$POSTMERKOS_TOOLCHAIN_ID" \
          "$(postmerkos_toolchain_host_flavor)"
    fi
}

postmerkos_legacy_toolchain_root() {
    printf '%s/%s/%s\n' \
      "$(postmerkos_legacy_toolchain_cache_base)" \
      "$POSTMERKOS_TOOLCHAIN_ID" \
      "$(postmerkos_toolchain_host_flavor)"
}

postmerkos_toolchain_download_dir() {
    printf '%s/downloads\n' "$(postmerkos_work_root)"
}

postmerkos_toolchain_work_dir() {
    printf '%s/toolchain-build/%s/%s\n' \
      "$(postmerkos_work_root)" \
      "$POSTMERKOS_TOOLCHAIN_ID" \
      "$(postmerkos_toolchain_host_flavor)"
}

postmerkos_cross_prefix() {
    printf '%s/bin/%s\n' "$(postmerkos_toolchain_root)" "$POSTMERKOS_TOOLCHAIN_PREFIX_NAME"
}
