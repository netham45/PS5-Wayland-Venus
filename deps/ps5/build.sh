#!/usr/bin/env bash
set -euo pipefail
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
if [[ ${PS5WL_BUILD_ENV:-} != 1 ]]; then exec "$root/env/run" "$0" "$@"; fi
source "$root/toolchain/env.sh"
"$root/cross/setup.sh"
exec python3 "$root/deps/ps5/build.py" "$@"
