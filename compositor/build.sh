#!/bin/bash
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "$0")/.." && pwd)
if [[ ${PS5WL_BUILD_ENV:-} != 1 ]]; then
    if [[ ! -x "$ROOT/env/run" ]]; then
        echo "Missing build container entrypoint: $ROOT/env/run" >&2
        exit 1
    fi
    exec "$ROOT/env/run" "$0" "$@"
fi
MODE=${1:-host}
if [[ $# -gt 1 || ( $MODE != host && $MODE != ps5 ) ]]; then
    echo "Usage: $0 host|ps5" >&2
    exit 2
fi
PREFIX="$ROOT/build/$MODE/sysroot"
OUT="$ROOT/build/$MODE/compositor"
export PKG_CONFIG_LIBDIR="$PREFIX/lib/pkgconfig:$PREFIX/share/pkgconfig"
export PKG_CONFIG_PATH=
export PKG_CONFIG_SYSROOT_DIR=
export PATH="$ROOT/build/host/sysroot/bin:$PATH"
args=(--prefix="$PREFIX" --libdir=lib --buildtype=debugoptimized --wrap-mode=nodownload)
if [[ ! -f "$PREFIX/lib/pkgconfig/wlroots-0.20.pc" ]]; then
    echo "Missing pinned $MODE wlroots sysroot ($PREFIX); run make deps-$MODE first." >&2
    exit 1
fi
if [[ $MODE == ps5 ]]; then
    if [[ ! -f "$ROOT/build/ps5/cross.ini" || ! -f "$ROOT/build/ps5/native.ini" ]]; then
        echo "Missing PS5 cross/native file; run make deps-ps5 (cross/setup.sh generates these files)." >&2
        exit 1
    fi
    source "$ROOT/toolchain/env.sh"
    args+=(--cross-file "$ROOT/build/ps5/cross.ini" --native-file "$ROOT/build/ps5/native.ini" --default-library=static)
else
    export CC=clang CXX=clang++ AR=llvm-ar RANLIB=llvm-ranlib
    export LDFLAGS="-fuse-ld=lld -Wl,-rpath,$PREFIX/lib"
fi
if [[ -f "$OUT/meson-private/coredata.dat" ]]; then
    args+=(--reconfigure)
fi
meson setup "$OUT" "$ROOT/compositor" "${args[@]}"
meson compile -C "$OUT" -j "${JOBS:-$(getconf _NPROCESSORS_ONLN)}"
echo "Built $OUT/ps5wl"
