#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)
variant=development
mode=auto

usage() {
  cat <<'USAGE'
Usage: ./build.sh [strict|development|permissive] [--native|--distrobox] [--clean]

Profiles:
  strict       CRC mismatch and legacy size excess are fatal.
  development CRC/legacy-size mismatches warn; hard slot boundary stays fatal.
  permissive   CRC code is omitted; only the hard slot boundary is enforced.

Default behavior uses the native mipsel-linux-gnu- toolchain when installed,
or creates/uses the Ubuntu 22.04 Distrobox build container otherwise.
USAGE
}

clean=0
while (($#)); do
  case "$1" in
    strict|development|permissive) variant=$1 ;;
    --native) mode=native ;;
    --distrobox) mode=distrobox ;;
    --clean) clean=1 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
  shift
done

cd "$root"
((clean)) && make clean

if [[ $mode == auto ]]; then
  if command -v mipsel-linux-gnu-gcc >/dev/null 2>&1; then
    mode=native
  elif command -v distrobox >/dev/null 2>&1; then
    mode=distrobox
  else
    echo "Neither mipsel-linux-gnu-gcc nor distrobox is available." >&2
    exit 2
  fi
fi

case "$mode" in
  native) make -j"$(nproc 2>/dev/null || echo 1)" all VARIANT="$variant" ;;
  distrobox) ./scripts/distrobox-build.sh all "VARIANT=$variant" ;;
esac
