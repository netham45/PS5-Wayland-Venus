#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
if [ "${PS5WL_BUILD_ENV:-}" != 1 ]; then
    exec "$ROOT/env/run" "$0" "$@"
fi
exec python3 "$ROOT/compositor/tests/smoke.py" "$@"
