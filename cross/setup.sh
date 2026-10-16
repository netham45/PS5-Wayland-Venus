#!/usr/bin/env bash
set -euo pipefail
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
if [[ ${PS5WL_BUILD_ENV:-} != 1 ]]; then exec "$root/env/run" "$0" "$@"; fi
source "$root/toolchain/env.sh"
mkdir -p "$root/build/ps5"
export PS5WL_ROOT="$root"
python3 - <<'PY'
import os
from pathlib import Path
import configparser
root = Path(os.environ['PS5WL_ROOT'])
sdk = Path(os.environ['PS5_PAYLOAD_SDK'])
# Install layouts use toolchain/; source layouts use host/toolchain/.
tools = sdk / 'toolchain'
if not (tools / 'prospero.ini').is_file():
    tools = sdk / 'host/toolchain'
base = (tools / 'prospero.ini').read_text().replace('@DIRNAME@', str(tools))
(root / 'build/ps5/sdk.ini').write_text(base)
config = configparser.ConfigParser(interpolation=None)
config.read_string(base)
config['binaries']['pkg-config'] = f"'{root}/cross/pkg-config'"
config['binaries']['wayland-scanner'] = f"'{root}/build/host/sysroot/bin/wayland-scanner'"
config['built-in options'].update({
    'prefix': f"'{root}/build/ps5/sysroot'",
    'libdir': "'lib'",
    'default_library': "'static'",
    'b_staticpic': 'true',
    'c_args': f"['-I{root}/build/ps5/sysroot/include']",
    'c_link_args': f"['-L{root}/build/ps5/sysroot/lib']",
})
config['properties'].update({
    'needs_exe_wrapper': 'true',
    'pkg_config_libdir': f"'{root}/build/ps5/sysroot/lib/pkgconfig'",
    'cmake_toolchain_file': f"'{root}/cross/prospero.cmake'",
})
with (root / 'build/ps5/cross.ini').open('w') as stream:
    config.write(stream)
(root / 'build/ps5/native.ini').write_text(f"""[binaries]
pkg-config = '{root}/cross/host-pkg-config'
wayland-scanner = '{root}/build/host/sysroot/bin/wayland-scanner'
[properties]
pkg_config_libdir = ['{root}/build/host/sysroot/lib/pkgconfig', '{root}/build/host/sysroot/share/pkgconfig']
""")
PY
