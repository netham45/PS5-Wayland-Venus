#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
if [ "${PS5WL_BUILD_ENV:-}" != 1 ]; then
    if [ ! -x "$ROOT/env/run" ]; then
        echo "Missing build container entrypoint: $ROOT/env/run" >&2
        exit 1
    fi
    exec "$ROOT/env/run" "$0" "$@"
fi
exec python3 "$ROOT/deps/host/build.py" "$@"
