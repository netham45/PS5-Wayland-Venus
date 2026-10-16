#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
if [[ ${PS5WL_BUILD_ENV:-} != 1 ]]; then exec "$root/env/run" "$0" "$@"; fi
source "$root/toolchain/env.sh"
exec python3 "$root/deps/mesa/build.py" "$@"
