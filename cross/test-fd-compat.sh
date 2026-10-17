#!/usr/bin/env bash
set -euo pipefail
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
if [[ ${PS5WL_BUILD_ENV:-} != 1 ]]; then exec "$root/env/run" "$0" "$@"; fi
out=$root/build/ps5/runtime/native
mkdir -p "$out"
objects=()
wrappers=(fcntl dup3 open openat shm_open socket socketpair accept4 recvmsg mkostemp pipe2)
flags=()
for name in "${wrappers[@]}"; do
    clang -std=c11 -O2 -fPIC -fvisibility=hidden -Wall -Wextra -Werror \
        -D"PS5WL_WRAP_${name^^}" -c "$root/deps/ps5/shim/fd_compat.c" -o "$out/$name.o"
    objects+=("$out/$name.o")
    flags+=("-Wl,--wrap=$name")
done
clang -std=c11 -O2 -fPIC -fvisibility=hidden -Wall -Wextra -Werror -pthread \
    -c "$root/deps/ps5/shim/ppoll_compat.c" -o "$out/ppoll.o"
objects+=("$out/ppoll.o")
flags+=("-Wl,--wrap=ppoll")
llvm-ar rcs "$out/libps5wl-fd-compat.a" "${objects[@]}"
clang -std=c11 -O2 -Wall -Wextra -Werror "$root/deps/ps5/shim/test_fd_compat.c" \
    "$out/libps5wl-fd-compat.a" "${flags[@]}" -o "$out/test-fd-compat"
"$out/test-fd-compat"
clang -std=c11 -O2 -Wall -Wextra -Werror -pthread \
    "$root/deps/ps5/shim/test_ppoll_compat.c" \
    "$out/libps5wl-fd-compat.a" "${flags[@]}" -ldl -o "$out/test-ppoll-compat"
"$out/test-ppoll-compat"
