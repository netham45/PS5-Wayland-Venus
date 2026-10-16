#!/usr/bin/env bash
set -euo pipefail
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
if [[ ${PS5WL_BUILD_ENV:-} != 1 ]]; then exec "$root/env/run" "$0" "$@"; fi
compiler=$1
shift
sdk=${PS5_PAYLOAD_SDK:-$root/.sdk}
link=1
for arg in "$@"; do
    case "$arg" in
        -c|-S|-E|-M|-MM|-fsyntax-only|--version|-dump*|-print-*|--print-*) link=0 ;;
    esac
done
if (( ! link )); then exec "$sdk/bin/$compiler" "$@"; fi
"$root/cross/bootstrap-compat.sh"
exec "$sdk/bin/$compiler" "$@" "$root/build/ps5/runtime/libps5wl-fd-compat.a" \
    -Wl,--wrap=fcntl,--wrap=dup3,--wrap=open,--wrap=openat,--wrap=shm_open \
    -Wl,--wrap=socket,--wrap=socketpair,--wrap=accept4,--wrap=recvmsg,--wrap=mkostemp,--wrap=pipe2
