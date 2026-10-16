#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
if [[ ${PS5WL_BUILD_ENV:-} != 1 ]]; then
    exec "$repo/env/run" "$repo/toolchain/install-sdk.sh" "$@"
fi
source "$repo/toolchain/env.sh"
readarray -t pin < <(python3 - "$repo/toolchain/sdk.lock.json" <<'PY'
import json, sys
pin = json.load(open(sys.argv[1]))
for key in ('name', 'tag', 'url', 'sha256'):
    print(pin[key])
PY
)
[[ ${#pin[@]} == 4 ]] || exit 1
name=${pin[0]}; tag=${pin[1]}; url=${pin[2]}; checksum=${pin[3]}
sdk=$(realpath -m -- "$PS5_PAYLOAD_SDK")
case "$sdk" in
    "$repo"/*) ;;
    *) echo 'SDK destination must be inside the repository' >&2; exit 1 ;;
esac
cache="$repo/.cache/downloads"
mkdir -p "$cache"
# Serialize installation when several build entry points request the SDK.
exec 9>"$cache/sdk-install.lock"
flock 9
archive="$cache/$name-$tag.zip"
if [[ ! -f $archive ]]; then
    curl --fail --location --retry 3 "$url" --output "$archive.part"
    mv -- "$archive.part" "$archive"
fi
printf '%s  %s\n' "$checksum" "$archive" | sha256sum --check --status || {
    echo "SDK checksum mismatch: $archive (remove it and retry)" >&2
    exit 1
}
echo "SDK $tag SHA256 verified: $checksum"
if [[ -f $sdk/.ps5wl-sdk-sha256 && $(<"$sdk/.ps5wl-sdk-sha256") == "$checksum" ]]; then
    for file in bin/prospero-clang bin/prospero-clang++ toolchain/prospero.ini \
                toolchain/prospero.cmake target/lib/libc++.a target/lib/libc++abi.a \
                target/lib/libunwind.a target/include/c++/v1/vector; do
        [[ -f $sdk/$file ]] || { echo "Incomplete SDK: $sdk/$file" >&2; exit 1; }
    done
    echo "SDK $tag already installed at $sdk (no changes)"
    exit 0
fi
if [[ -e $sdk ]]; then
    echo "Refusing to overwrite an unrecognized SDK at $sdk" >&2
    exit 1
fi
stage=$(mktemp -d "$cache/sdk-install.XXXXXX")
trap 'rm -rf -- "$stage"' EXIT
unzip -q "$archive" -d "$stage"
[[ -x $stage/$name/bin/prospero-clang && -f $stage/$name/target/lib/libc++.a ]]
printf '%s\n' "$checksum" > "$stage/$name/.ps5wl-sdk-sha256"
mkdir -p -- "$(dirname -- "$sdk")"
mv -- "$stage/$name" "$sdk"
echo "Installed SDK $tag at $sdk, including upstream libc++, libc++abi and libunwind"
"$sdk/bin/prospero-clang" --version
