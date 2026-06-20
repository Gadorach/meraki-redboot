#!/usr/bin/env bash
set -euo pipefail
# shellcheck source=scripts/toolchain-config.sh
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)/toolchain-config.sh"

case "${1:-}" in
    --print-root)
        postmerkos_toolchain_root
        ;;
    --print-prefix)
        postmerkos_cross_prefix
        ;;
    --print-id)
        printf '%s\n' "$POSTMERKOS_TOOLCHAIN_ID"
        ;;
    --print-host-flavor)
        postmerkos_toolchain_host_flavor
        ;;
    --print-work-root)
        postmerkos_work_root
        ;;
    --export|'')
        root=$(postmerkos_toolchain_root)
        prefix=$(postmerkos_cross_prefix)
        export LEGACY_TOOLCHAIN_ROOT="$root"
        export CROSS_COMPILE="$prefix"
        export PATH="$root/bin:$PATH"
        if [[ ${1:-} == --export ]]; then
            printf 'export LEGACY_TOOLCHAIN_ROOT=%q\n' "$root"
            printf 'export CROSS_COMPILE=%q\n' "$prefix"
            printf 'export PATH=%q\n' "$root/bin:$PATH"
        fi
        ;;
    *)
        echo "usage: $0 [--print-root|--print-prefix|--print-id|--print-host-flavor|--print-work-root|--export]" >&2
        exit 2
        ;;
esac
