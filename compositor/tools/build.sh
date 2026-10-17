#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "$0")/../.." && pwd)
if [[ ${PS5WL_BUILD_ENV:-} != 1 ]]; then
    echo "Use env/run bash compositor/tools/build.sh" >&2
    exit 1
fi
OUT="$ROOT/build/ps5/keepalive"
mkdir -p "$OUT"
"$ROOT/cross/bin/prospero-clang" -O2 -g -Wall -Wextra \
    -I"$ROOT/common/ps5log" -I"$ROOT/compositor/src/backend/ps5" \
    "$ROOT/compositor/tools/keepalive.c" "$ROOT/compositor/src/backend/ps5/power.c" \
    "$ROOT/common/ps5log/ps5log.c" -pthread -lSceSystemService \
    -o "$OUT/ps5wl-keepalive.elf"
