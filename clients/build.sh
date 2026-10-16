#!/bin/bash
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "$0")/.." && pwd)
if [[ ${PS5WL_BUILD_ENV:-} != 1 ]]; then exec "$ROOT/env/run" "$0" "$@"; fi
MODE=${1:-host}
if [[ $# -gt 1 || ( $MODE != host && $MODE != ps5 ) ]]; then
    echo "Usage: $0 host|ps5" >&2; exit 2
fi
PREFIX="$ROOT/build/$MODE/sysroot"
OUT="$ROOT/build/$MODE/clients"
export PKG_CONFIG_LIBDIR="$PREFIX/lib/pkgconfig:$PREFIX/share/pkgconfig"
export PKG_CONFIG_PATH= PKG_CONFIG_SYSROOT_DIR=
export PATH="$ROOT/build/host/sysroot/bin:$PATH"
args=(--prefix="$PREFIX" --libdir=lib --buildtype=debugoptimized --wrap-mode=nodownload)
cross_signature=
if [[ $MODE == ps5 ]]; then
    source "$ROOT/toolchain/env.sh"
    "$ROOT/cross/bootstrap-compat.sh"
    cross_signature=$(sha256sum "$ROOT/build/ps5/cross.ini" "$ROOT/build/ps5/native.ini" \
        "$ROOT/cross/compiler.sh" "$ROOT/build/ps5/runtime/libps5wl-fd-compat.a")
    args+=(--cross-file "$ROOT/build/ps5/cross.ini" --native-file "$ROOT/build/ps5/native.ini" --default-library=static)
else
    export CC=clang CXX=clang++ AR=llvm-ar RANLIB=llvm-ranlib
    export LDFLAGS="-fuse-ld=lld -Wl,-rpath,$PREFIX/lib"
fi
if [[ -f "$OUT/meson-private/coredata.dat" ]]; then
    if [[ $MODE == ps5 && ( ! -f "$OUT/.ps5wl-toolchain" ||
            $(<"$OUT/.ps5wl-toolchain") != "$cross_signature" ) ]]; then
        # Meson caches compiler paths; reconfigure alone cannot migrate to the
        # compatibility proxy or track libraries injected by that proxy.
        args+=(--wipe)
    else
        args+=(--reconfigure)
    fi
fi
meson setup "$OUT" "$ROOT/clients" "${args[@]}"
if [[ $MODE == ps5 ]]; then printf '%s\n' "$cross_signature" >"$OUT/.ps5wl-toolchain"; fi
meson compile -C "$OUT" -j "${JOBS:-$(getconf _NPROCESSORS_ONLN)}"
echo "Built $OUT/ps5wl-simple-shm and $OUT/ps5wl-demo (PS5 suffix: .elf)"
