#!/bin/bash
# Run only after the coordinator has re-established the jailbreak and elfldr.
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "$0")/.." && pwd)
MODE=${1:-compositor}
if [[ $# -gt 1 || ( "$MODE" != compositor && "$MODE" != --probe ) ]]; then
    echo "Usage: $0 [compositor|--probe]" >&2
    exit 2
fi
processes=$("$ROOT/tools/ps5ctl" ps)
case "$processes" in
    *ps5wl-install.elf*|*ps5wl-uninstall.elf*|*ps5wl-launch-probe*)
        echo "Existing launcher/test process detected; do not relaunch over a stuck session." >&2
        echo "Have the coordinator resolve the documented reboot prerequisite first." >&2
        exit 1
        ;;
esac
if [[ "$MODE" == --probe ]]; then
    "$ROOT/env/run" make -C "$ROOT/launcher" all
    LOG_PORT=9582
else
    "$ROOT/env/run" env PS5WL_TEST_SECONDS=60 "$ROOT/compositor/build.sh" ps5
    "$ROOT/env/run" make -C "$ROOT/launcher" all PAYLOAD="$ROOT/build/ps5/compositor/ps5wl"
    LOG_PORT=9530
fi
"$ROOT/tools/ps5ctl" run "$ROOT/launcher/ps5wl-install.elf"
# This observer is a host process; the trap never signals a console process.
timeout 80 "$ROOT/tools/ps5ctl" logs --port 9580 --retry 15 &
observer=$!
trap 'kill "$observer" 2>/dev/null || true; wait "$observer" 2>/dev/null || true' EXIT
# Registration is synchronous; allow its short service-initialization tail.
sleep 2
"$ROOT/tools/ps5ctl" run "$ROOT/launcher/ps5wl-launch-test.elf"
set +e
timeout 75 "$ROOT/tools/ps5ctl" logs --port "$LOG_PORT" --retry 15
result=$?
set -e
if [[ "$result" != 0 ]]; then
    echo "Payload log capture exited $result (124 means no EOF before 75 seconds)." >&2
    exit "$result"
fi
echo "Payload log stream closed. Review launcher/app results above; this does not prove icon activation or interactive rendering."
echo "The PS5 Wayland icon and resident installer remain available for the final integrated test."
