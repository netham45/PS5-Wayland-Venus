#!/usr/bin/env bash
set -euo pipefail
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
if [[ ${PS5WL_BUILD_ENV:-} != 1 ]]; then exec "$root/env/run" "$0" "$@"; fi
source "$root/toolchain/env.sh"
mkdir -p "$root/build/ps5/linktest"
read -r -a flags <<< "$("$root/cross/pkg-config" --cflags --libs wlroots-0.20 wayland-server)"
prospero-clang -std=c11 -DWLR_USE_UNSTABLE -I"$root/common/ps5log" \
    "$root/deps/ps5/linktest/main.c" "$root/common/ps5log/ps5log.c" \
    "${flags[@]}" -pthread -o "$root/build/ps5/linktest/linktest.elf"
