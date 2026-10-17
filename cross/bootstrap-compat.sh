#!/usr/bin/env bash
set -euo pipefail
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
if [[ ${PS5WL_BUILD_ENV:-} != 1 ]]; then exec "$root/env/run" "$0" "$@"; fi
sdk=${PS5_PAYLOAD_SDK:-$root/.sdk}
identity=$sdk
if [[ -f $sdk/.ps5wl-sdk-sha256 ]]; then identity+=" $(<"$sdk/.ps5wl-sdk-sha256")"; fi
source_file=$root/deps/ps5/shim/fd_compat.c
ppoll_source=$root/deps/ps5/shim/ppoll_compat.c
out=$root/build/ps5/runtime
mkdir -p "$out"
(
    flock 9
    archive=$out/libps5wl-fd-compat.a
    if [[ -s $archive && -f $out/sdk.identity && $(<"$out/sdk.identity") == "$identity" &&
          $source_file -ot $archive && $ppoll_source -ot $archive &&
          $0 -ot $archive && $sdk/bin/prospero-clang -ot $archive ]]; then exit 0; fi
    stage=$(mktemp -d "$out/.compat.XXXXXX")
    trap 'rm -rf -- "$stage"' EXIT
    wrappers=(fcntl dup3 open openat shm_open socket socketpair accept4 recvmsg mkostemp pipe2)
    objects=()
    for name in "${wrappers[@]}"; do
        object=$stage/$name.o
        "$sdk/bin/prospero-clang" -std=c11 -O2 -fPIC -fvisibility=hidden \
            -D"PS5WL_WRAP_${name^^}" -c "$source_file" -o "$object"
        objects+=("$object")
    done
    "$sdk/bin/prospero-clang" -std=c11 -O2 -fPIC -fvisibility=hidden \
        -c "$ppoll_source" -o "$stage/ppoll.o"
    objects+=("$stage/ppoll.o")
    "$sdk/bin/prospero-ar" rcs "$stage/libps5wl-fd-compat.a" "${objects[@]}"
    mv "$stage/libps5wl-fd-compat.a" "$archive"
    printf '%s' "$identity" > "$out/sdk.identity"
) 9>"$out/compat.lock"
