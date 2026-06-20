#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)

required=(gcc g++ make flex bison gawk makeinfo m4 tar bzip2 python3 sha256sum sha512sum file)
all_present=1
for tool in "${required[@]}"; do
  command -v "$tool" >/dev/null 2>&1 || all_present=0
done
if ! command -v curl >/dev/null 2>&1 && ! command -v wget >/dev/null 2>&1; then
  all_present=0
fi

have_gmp_headers=0
have_mpfr_headers=0
have_mpc_headers=0
for prefix in /usr /usr/local; do
  [[ -f $prefix/include/gmp.h || -f $prefix/include/x86_64-linux-gnu/gmp.h ]] && have_gmp_headers=1
  [[ -f $prefix/include/mpfr.h ]] && have_mpfr_headers=1
  [[ -f $prefix/include/mpc.h ]] && have_mpc_headers=1
done

sudo_cmd=()
if [[ ${EUID:-$(id -u)} -ne 0 ]]; then
  if command -v sudo >/dev/null 2>&1; then
    sudo_cmd=(sudo)
  else
    echo "Root privileges or sudo are required to install build dependencies." >&2
    exit 2
  fi
fi

if [[ $all_present -ne 1 || $have_gmp_headers -ne 1 || $have_mpfr_headers -ne 1 || $have_mpc_headers -ne 1 ]]; then
  if command -v apt-get >/dev/null 2>&1; then
    export DEBIAN_FRONTEND=noninteractive
    "${sudo_cmd[@]}" apt-get update
    "${sudo_cmd[@]}" apt-get install -y --no-install-recommends \
      build-essential ca-certificates curl wget git make flex bison gawk texinfo m4 patch file \
      python3 unzip bzip2 xz-utils \
      libgmp-dev libmpfr-dev libmpc-dev zlib1g-dev
  elif command -v pacman >/dev/null 2>&1; then
    "${sudo_cmd[@]}" pacman -S --needed --noconfirm \
      base-devel ca-certificates curl wget git make flex bison gawk texinfo m4 patch file \
      python unzip bzip2 xz gmp mpfr libmpc zlib
  elif command -v dnf >/dev/null 2>&1; then
    "${sudo_cmd[@]}" dnf install -y \
      gcc gcc-c++ make flex bison gawk texinfo m4 git curl wget patch file python3 unzip bzip2 xz \
      gmp-devel mpfr-devel libmpc-devel zlib-devel
  else
    echo "Unsupported package manager." >&2
    echo "Install a host C/C++ compiler, make, flex, bison, gawk, texinfo, m4, patch, bzip2, curl or wget, Python 3," >&2
    echo "and GMP/MPFR/MPC development headers, then rerun the build." >&2
    exit 2
  fi
fi

"$script_dir/install-legacy-toolchain.sh"
echo "Standalone VCore-III loader dependencies and pinned GCC 4.7.3 toolchain are installed."
