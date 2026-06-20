#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)
container=${DISTROBOX_NAME:-postmerkos-vcoreiii-gcc473}
image=${DISTROBOX_IMAGE:-docker.io/library/ubuntu:22.04}
target=${1:-__all-local}
shift || true
make_args=("$@")
work_root=${POSTMERKOS_WORK_ROOT:-$root/.work}
for arg in "${make_args[@]}"; do
  case $arg in WORK_ROOT=*) work_root=${arg#WORK_ROOT=} ;; esac
done

if ! command -v distrobox >/dev/null 2>&1; then
  echo "distrobox is required on the host." >&2
  exit 2
fi

if ! distrobox list --no-color 2>/dev/null | awk -F'|' 'NR > 1 {name=$2; gsub(/^[[:space:]]+|[[:space:]]+$/, "", name); print name}' | grep -Fxq "$container"; then
  echo "Creating distrobox '$container' from '$image'..."
  distrobox create --yes --name "$container" --image "$image"
fi

quoted_root=$(printf '%q' "$root")
quoted_target=$(printf '%q' "$target")
quoted_work_root=$(printf '%q' "$work_root")
quoted_args=()
for arg in "${make_args[@]}"; do quoted_args+=("$(printf '%q' "$arg")"); done

inner="set -euo pipefail; export POSTMERKOS_TOOLCHAIN_HOST_FLAVOR=ubuntu-22.04-x86_64; export POSTMERKOS_WORK_ROOT=$quoted_work_root; test -d $quoted_root || { echo 'Project directory is not visible inside distrobox: $root' >&2; exit 2; }; cd $quoted_root; mkdir -p $quoted_work_root/logs; ./scripts/install-deps.sh; prefix=\$(./scripts/toolchain-env.sh --print-prefix); ./scripts/check-toolchain.sh \"\$prefix\"; set -o pipefail; make --no-print-directory -j\$(nproc) $quoted_target CROSS_COMPILE=\"\$prefix\" BUILD_CONTEXT=distrobox"
if ((${#quoted_args[@]})); then inner+=" ${quoted_args[*]}"; fi
inner+=" 2>&1 | tee $quoted_work_root/logs/build-distrobox.log;"
inner+=' exit ${PIPESTATUS[0]}'

distrobox enter "$container" -- bash -lc "$inner"
