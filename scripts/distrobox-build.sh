#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)
container=${DISTROBOX_NAME:-postmerkos-vcoreiii-loader}
image=${DISTROBOX_IMAGE:-docker.io/library/ubuntu:22.04}
target=${1:-all}
shift || true
make_args=("$@")

# `all` is the host-facing auto-routing target. Inside the container invoke the
# native implementation directly so an inner build never re-enters Distrobox.
case $target in
  all) target=local-all ;;
  recovery-payloads) target=local-recovery-payloads ;;
esac

if ! command -v distrobox >/dev/null 2>&1; then
  echo "distrobox is required on the host." >&2
  exit 2
fi

if ! distrobox list --no-color 2>/dev/null | awk -F'|' 'NR > 1 {name=$2; gsub(/^[[:space:]]+|[[:space:]]+$/, "", name); print name}' | grep -Fxq "$container"; then
  echo "Creating distrobox '$container' from '$image'..."
  distrobox create --yes --name "$container" --image "$image"
fi

# Distrobox normally exposes the host home at the same path. Explicitly check
# the project path before trying to install or build anything.
quoted_root=$(printf '%q' "$root")
quoted_target=$(printf '%q' "$target")
quoted_args=()
for arg in "${make_args[@]}"; do
  quoted_args+=("$(printf '%q' "$arg")")
done

inner="set -euo pipefail; test -d $quoted_root || { echo 'Project directory is not visible inside distrobox: $root' >&2; exit 2; }; cd $quoted_root; ./scripts/install-deps.sh; make -j\$(nproc) $quoted_target"
if ((${#quoted_args[@]})); then
  inner+=" ${quoted_args[*]}"
fi

distrobox enter "$container" -- bash -lc "$inner"
