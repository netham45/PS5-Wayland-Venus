#!/usr/bin/env bash
# Source in Bash; compilation itself must run through env/run.
_ps5wl_repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
export PS5_PAYLOAD_SDK=${PS5_PAYLOAD_SDK:-$_ps5wl_repo/.sdk}
export LLVM_CONFIG=${LLVM_CONFIG:-/usr/bin/llvm-config}
export PS5_HOST=${PS5_HOST:-192.168.3.211}
export PLDMGR_PORT=${PLDMGR_PORT:-8084}
case ":$PATH:" in
    *":$PS5_PAYLOAD_SDK/bin:"*) ;;
    *) export PATH="$PS5_PAYLOAD_SDK/bin:$PATH" ;;
esac
unset _ps5wl_repo
