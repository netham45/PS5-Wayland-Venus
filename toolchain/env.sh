#!/usr/bin/env bash
# Source in Bash; compilation itself must run through env/run.
_ps5wl_repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
export PS5_PAYLOAD_SDK=${PS5_PAYLOAD_SDK:-$_ps5wl_repo/.sdk}
export LLVM_CONFIG=${LLVM_CONFIG:-/usr/bin/llvm-config}
export PS5_HOST=${PS5_HOST:-192.168.3.211}
export PLDMGR_PORT=${PLDMGR_PORT:-8084}
# Installation sources this file before the SDK exists. Never compile on the host.
if [[ ${PS5WL_BUILD_ENV:-} == 1 && -x $PS5_PAYLOAD_SDK/bin/prospero-clang ]]; then
    "$_ps5wl_repo/cross/bootstrap-compat.sh" || return
fi
# Re-sourcing must keep proxies first without accumulating duplicate entries.
IFS=: read -r -a _ps5wl_path_entries <<< "$PATH"
PATH="$_ps5wl_repo/cross/bin:$PS5_PAYLOAD_SDK/bin"
for _ps5wl_entry in "${_ps5wl_path_entries[@]}"; do
    case "$_ps5wl_entry" in
        "$_ps5wl_repo/cross/bin"|"$PS5_PAYLOAD_SDK/bin") ;;
        *) PATH+=":$_ps5wl_entry" ;;
    esac
done
export PATH
unset _ps5wl_repo _ps5wl_entry _ps5wl_path_entries
