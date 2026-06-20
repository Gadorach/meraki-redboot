#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)
variant=development
mode=prompt
clean=0

usage() {
  cat <<'USAGE'
Usage: ./build.sh [strict|development|permissive] [--native|--distrobox|--auto] [--clean]

Profiles:
  strict       CRC mismatch and legacy size excess are fatal; UART disabled.
  development CRC/legacy-size mismatches warn; UART RAM loader enabled.
  permissive   CRC code is omitted; hard boundary only; UART disabled.

Without a mode option, the script presents the same native/Distrobox choice as
`make all`. Both paths build and verify the pinned source-based GNU GCC 4.7.3
and binutils 2.23.2 mipsel-linux-gnu cross-toolchain.
USAGE
}

while (($#)); do
  case "$1" in
    strict|development|permissive) variant=$1 ;;
    --native) mode=native ;;
    --distrobox) mode=distrobox ;;
    --auto) mode=auto ;;
    --clean) clean=1 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
  shift
done

cd "$root"
((clean)) && make clean
exec make all "VARIANT=$variant" "BUILD_MODE=$mode"
