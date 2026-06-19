#!/usr/bin/env bash
set -euo pipefail

required=(mipsel-linux-gnu-gcc mipsel-linux-gnu-ld mipsel-linux-gnu-objcopy mipsel-linux-gnu-objdump mipsel-linux-gnu-nm make python3 unzip patch file)
all_present=1
for tool in "${required[@]}"; do
  command -v "$tool" >/dev/null 2>&1 || all_present=0
done
if [[ $all_present -eq 1 ]]; then
  echo "Standalone VCore-III loader build dependencies are already installed."
  exit 0
fi

packages=(
  build-essential
  gcc-mipsel-linux-gnu
  binutils-mipsel-linux-gnu
  make
  python3
  unzip
  patch
  file
  ca-certificates
)

if ! command -v apt-get >/dev/null 2>&1; then
  echo "This helper supports Debian/Ubuntu containers (apt-get)." >&2
  echo "Install an equivalent MIPS little-endian GNU cross compiler and binutils manually." >&2
  exit 2
fi

sudo_cmd=()
if [[ ${EUID:-$(id -u)} -ne 0 ]]; then
  if command -v sudo >/dev/null 2>&1; then
    sudo_cmd=(sudo)
  else
    echo "Run this script as root or install sudo." >&2
    exit 2
  fi
fi

export DEBIAN_FRONTEND=noninteractive
"${sudo_cmd[@]}" apt-get update
"${sudo_cmd[@]}" apt-get install -y --no-install-recommends "${packages[@]}"

echo "Installed standalone VCore-III loader build dependencies."
