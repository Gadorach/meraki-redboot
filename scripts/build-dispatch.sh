#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)
target=${1:?internal build target required}
shift
mode=${BUILD_MODE:-prompt}
make_cmd=${MAKE_COMMAND:-make}
work_root=${POSTMERKOS_WORK_ROOT:-$root/.work}
for arg in "$@"; do
    case $arg in WORK_ROOT=*) work_root=${arg#WORK_ROOT=} ;; esac
done
export POSTMERKOS_WORK_ROOT=$work_root
mkdir -p "$work_root/logs"

choose_interactively() {
    local answer input=/dev/stdin output=/dev/stderr
    if [[ -r /dev/tty && -w /dev/tty ]]; then
        input=/dev/tty
        output=/dev/tty
    fi
    printf '\nUse Distrobox for compilation? [Y/n]: ' >"$output"
    IFS= read -r answer <"$input" || answer=''
    case "$answer" in
        ''|y|Y|yes|YES|Yes) printf 'distrobox\n' ;;
        n|N|no|NO|No) printf 'native\n' ;;
        *) echo "Invalid selection: $answer" >"$output"; return 2 ;;
    esac
}

case "$mode" in
    prompt)
        if [[ -z ${CI:-} && ( -t 0 || -r /dev/tty ) ]]; then
            mode=$(choose_interactively)
        else
            mode=auto
        fi
        ;;
    native|distrobox|auto) ;;
    *) echo "BUILD_MODE must be prompt, auto, native, or distrobox" >&2; exit 2 ;;
esac

if [[ $mode == auto ]]; then
    if command -v distrobox >/dev/null 2>&1; then mode=distrobox; else mode=native; fi
fi

case "$mode" in
    distrobox)
        exec "$root/scripts/distrobox-build.sh" "$target" "$@"
        ;;
    native)
        "$root/scripts/install-deps.sh"
        prefix=$($root/scripts/toolchain-env.sh --print-prefix)
        set -o pipefail
        "$make_cmd" --no-print-directory -j"${JOBS:-$(nproc 2>/dev/null || echo 1)}" \
            "$target" "CROSS_COMPILE=$prefix" "BUILD_CONTEXT=native" "$@" \
            2>&1 | tee "$work_root/logs/build-native.log"
        exit ${PIPESTATUS[0]}
        ;;
esac
