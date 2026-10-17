#!/usr/bin/env bash
set -euo pipefail
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
if [[ ${PS5WL_BUILD_ENV:-} != 1 ]]; then exec "$root/env/run" "$0" "$@"; fi
compiler=$1
shift
sdk=${PS5_PAYLOAD_SDK:-$root/.sdk}
link=1
shared=0
for arg in "$@"; do
    case "$arg" in
        -c|-S|-E|-M|-MM|-fsyntax-only|--version|-dump*|-print-*|--print-*) link=0 ;;
        -shared|--shared|-Wl,-shared|-Wl,--shared) shared=1 ;;
    esac
done
if (( ! link )); then exec "$sdk/bin/$compiler" "$@"; fi
"$root/cross/bootstrap-compat.sh"
loader_flags=()
if (( shared )); then loader_flags+=("-Wl,--dynamic-list=$root/cross/public-loader.dyn"); fi
exec "$sdk/bin/$compiler" "$@" "${loader_flags[@]}" "$root/build/ps5/runtime/libps5wl-fd-compat.a" \
    -Wl,--wrap=fcntl,--wrap=dup3,--wrap=open,--wrap=openat,--wrap=shm_open \
    -Wl,--wrap=socket,--wrap=socketpair,--wrap=accept4,--wrap=recvmsg,--wrap=mkostemp,--wrap=pipe2,--wrap=ppoll
