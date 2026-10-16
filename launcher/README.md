# PS5 Wayland home-screen launcher

The installer registers a **PS5 Wayland** icon (`PSWL00001`), writes the bundled
ELF to `/data/ps5wl/bin/ps5wl-compositor.elf`, and stays resident to handle its
loopback browser deep link. The browser is only the trigger: the launch handler
uses websrv's `hbldr` route to start a signed BigApp and replace its entry point
with the compositor. It does not install an unsigned executable title or need
kstuff. It refuses to terminate an existing BigApp; close a running game/app
manually before launching Wayland.

## Build and install

Run from the repository root. All compilation uses the pinned container/SDK.
The default bundle is a 60-second foreground diagnostic payload (logs 9582),
not the compositor. It deliberately renders nothing. This allows launch-path
testing before the compositor exists.

```sh
env/run make -C launcher all
tools/ps5ctl run launcher/ps5wl-install.elf
tools/ps5ctl logs --port 9580 --retry 30
```

After a coordinator-approved reboot and restart of the existing jailbreak,
elfldr and Payload Manager, the complete bounded foreground test is one command:

```sh
launcher/test.sh
# Optional launch-only diagnostic instead of rebuilding the compositor:
launcher/test.sh --probe
```

The default command builds the compositor with `PS5WL_TEST_SECONDS=60`, bundles
it as `/data/ps5wl/bin/ps5wl-compositor.elf`, installs the icon, invokes the
same loopback launch handler, and captures launcher plus compositor logs. It
refuses to run while old launcher/uninstaller/probe processes are listed, never
kills console processes, and treats a payload log stream which fails to close
within 75 seconds as a failed bounded test. It leaves the resident installer
available for the final integrated icon/keyboard/mouse session. The host script
has not been run against the paused, pre-reboot console.

Wait for `register ... rc=0x0`, `install result=0`, and `ready` in the logs.
Select **PS5 Wayland** on the PS5 home screen. The browser loads a local page,
then the foreground app should take over. Pull probe logs with:

```sh
tools/ps5ctl logs --port 9582 --retry 30
```

For an automated preflight of the same loopback handler (not evidence that the
home-screen icon was pressed), run `tools/ps5ctl run launcher/ps5wl-launch-test.elf`.
Its HTTP response is logged on 9583. Launcher logs record the foreground child
PID, launch API return value, and any refusal or ptrace/setup error.

To install the actual compositor, stop the existing installer using its
receipt-owned PID (`tools/ps5ctl ps`, then `tools/ps5ctl kill PID`), close the
Wayland app manually, and rebuild with the **absolute** ELF artifact path:

```sh
env/run make -C launcher all PAYLOAD=/absolute/path/to/ps5wl-compositor.elf
tools/ps5ctl run launcher/ps5wl-install.elf
tools/ps5ctl logs --port 9580 --retry 30
# Select PS5 Wayland, then pull compositor logs:
tools/ps5ctl logs --port 9530 --retry 30
```

An installer built with the compositor replaces the diagnostic ELF atomically;
the installed destination and icon stay the same. This bundle installs the ELF,
not external keyboard maps or client binaries; deploy runtime assets required
by the compositor separately using their component's installation procedure.

The icon persists across reboot, but the resident installer does not. After a
reboot, re-establish the jailbreak/elfldr/Payload Manager and run the installer
again before pressing the icon. This project deliberately does **not** edit
Payload Manager config/autoload or replace its payload. No new always-on service
can survive reboot using only those already-running payloads without such a
startup integration.

## Files and ports

- `/user/app/PSWL00001/sce_sys/{param.json,icon0.png}`: browser icon registration.
- `/data/ps5wl/bin/ps5wl-compositor.elf`: bundled executable.
- `/system_ex/app/PSWL00002/{eboot.bin,sce_sys/param.json}`: unique signed BigApp
  launch container, created on first launch. `eboot.bin` is copied locally from
  `/system_ex/app/NPXS40106/eboot.bin`; no Sony binary is distributed here.
- 9580: pull logs. 9581: HTTP, bound to `127.0.0.1` only. 9582: diagnostic
  foreground payload logs. 9583: optional loopback test payload logs.

The loader mirrors websrv's read/write remount sequence for `/system_ex` when
creating/removing the unique BigApp container. Like websrv, it does **not** try
to restore a read-only mount: hardware testing found that downgrade/upgrade
cycles were unsafe. It uses kernel credentials, ptrace, and firmware-dependent
signed-entry initialization; this is not a portable retail application.

## Uninstall

Close PS5 Wayland manually. Stop the receipt-owned installer PID first so it
cannot relaunch while its files are being removed. Never kill a system process.

```sh
tools/ps5ctl ps
tools/ps5ctl kill INSTALLER_PID
tools/ps5ctl run launcher/ps5wl-uninstall.elf
tools/ps5ctl logs --port 9580 --retry 30
```

The uninstall payload unregisters `PSWL00001`, removes the icon files, bundled
ELF and its `.new` staging file, and removes the unique BigApp launch container.
It removes empty `/data/ps5wl/bin` and `/data/ps5wl` directories; unrelated files
are retained and explicitly logged, not recursively deleted. Remove separately
installed runtime assets using their owning component's uninstall procedure.
Check `uninstall complete rc=0`. After the uninstall payload exits (30-second
log window), remove the Payload Manager uploads owned by this checkout:

```sh
tools/ps5ctl delete ps5wl-install.elf
tools/ps5ctl delete ps5wl-uninstall.elf
# Only if uploaded for preflight:
tools/ps5ctl delete ps5wl-launch-test.elf
```

Local generated binaries/icon can be removed with `make -C launcher clean`.

## Research and provenance

Payload Manager v0.5.2's [app installer](https://github.com/itsPLK/ps5-payload-manager/blob/v0.5.2/src/app_installer.c)
writes only `sce_sys` metadata/icon under `/user/app/PLDM00001`, resolves
`AppInstallTitleDir` by NID `Wudg3Xe3heE`, and falls back to `AppInstallAll`.
Its [param.json](https://github.com/itsPLK/ps5-payload-manager/blob/v0.5.2/assets/param.json)
sets `applicationCategoryType: 65536` and
`deeplinkUri: http://127.0.0.1:8084/`: the shell opens the browser; this is not
an executable title and does not itself grant a foreground BigApp slot.

websrv's [hbldr.c](https://github.com/ps5-payload-dev/websrv/blob/7748df9d2a43822a5ebfda149f43d4f2696d640b/src/ps5/hbldr.c)
copies the signed NPXS40106 entry, launches it through
`sceSystemServiceLaunchApp`, tracks the SceSysCore child via kqueue, follows exec
with ptrace, initializes the signed process, sets payload credentials/argv/env,
and injects the requested ELF into that process. This is the foreground route;
sending the ELF to elfldr alone starts a separate background payload and does
not establish application focus.

The installer also follows websrv's [sys_init](https://github.com/ps5-payload-dev/websrv/blob/7748df9d2a43822a5ebfda149f43d4f2696d640b/src/ps5/sys.c)
by setting its own authid to `0x4801000000000013` before using the installer and
system launch services. It logs the prior authid and effective UID.

`vendor/{elfldr,pt}.{c,h}`, `hbldr.{c,h}` derive from that pinned websrv revision,
Copyright John Törnblom, GPL-3.0-or-later; see `COPYING`. The local hbldr changes
use project-specific IDs, refuse to kill another app,
bound child-event waiting, log launch results, fix failed-launch allocations,
and provide explicit cleanup. Other launcher code is GPL-3.0-or-later too.

## Hardware evidence

The first installer test reached the console on 2026-10-16 and reported
`uid=1`, `firmware=0x10600006` and successful AppInstUtil initialization.
The initial `PS5W00001` registration was rejected by `AppInstallTitleDir`
(`0x80bd000a`). Its digit-containing title prefix and missing websrv authid setup
were corrected before retry; the exact original error cause is not proven.

Corrected installer hardware logs:

```text
started pid=105 uid=1 firmware=0x10600006 mode=install/server
launcher authid previous=0x480000001000000e set_rc=0 effective_uid=0
sceAppInstUtilInitialize rc=0x0
register title=PSWL00001 method=AppInstallTitleDir rc=0x0 bytes=129072
install result=0 errno=32 stable_target=/data/ps5wl/bin/ps5wl-compositor.elf
sceUserServiceInitialize rc=0x0; ready http=127.0.0.1:9581 logs=9580
sceSystemServiceLaunchApp title=PSWL00002 user=437284342 rc=0x19
foreground launch result pid=107 errno=22
```

The positive `LaunchApp` result is application ID `0x19`, not an error code.
The injected foreground diagnostic payload logged its own `pid=107 uid=0`,
`running_bigapp=0x19` once per second. The request came from the automated
loopback test payload, **not** a human pressing the icon. Successful registration
and BigApp injection are proven; visible home-screen presence, actual icon
activation, compositor rendering, and controller focus need the final integrated
interactive session. Stale `errno` values on successful calls are not failures.
The rejected title's two files/directories were removed by a temporary cleanup
payload (all unlink/rmdir results `0`), whose upload/source were then removed.

The first diagnostic returned from `main` at tick 59 but its signed BigApp
process remained in Payload Manager's process list. It was explicitly released
using its known injected PID. The diagnostic now calls `_exit(0)` at 60 seconds;
compositor shutdown must likewise terminate the signed process after releasing
its rendering resources, not merely return from the injected payload thread.

An early implementation tried restoring `/system_ex` read-only after creation.
Subsequent read/write remounts returned success but writes failed `EROFS`; one
further read-only update hung and its receipt-owned uninstall process was
stopped. Those restore/noro experiments were removed. The final implementation
uses websrv's exact read/write remount iovec/options and never downgrades the
system partition. If cleanup still reports `EROFS` on this already-affected
session, reboot/re-establish the jailbreak before rerunning the uninstaller.
The only system-partition additions needing removal are:

```text
/system_ex/app/PSWL00002/eboot.bin
/system_ex/app/PSWL00002/sce_sys/param.json
/system_ex/app/PSWL00002/sce_sys/   (empty directory)
/system_ex/app/PSWL00002/           (empty directory)
```

Do not remove the original `/system_ex/app/NPXS40106` application or alter
system processes to work around a remount failure.

### State left when console actions were paused

- `pid=107`, name `ps5wl-launch-probe`: owned signed BigApp diagnostic, originally
  app ID `0x19`. It finished logging tick 59 but remained listed. Payload Manager
  acknowledged SIGKILL, yet a later process-list recheck still listed it. It
  **never opened VideoOut**, so it has no diagnostic-owned VideoOut handle;
  whether its application slot is still foreground was not queried after the
  pause. Do not kill it as a prerequisite for another component's tests.
- `pid=124`, name `ps5wl-uninstall.elf`: owned uninstall process stopped logging
  during a read-only remount operation. Payload Manager acknowledged SIGKILL,
  but the subsequent process-list recheck still listed it. Its old log ring
  continued to answer on 9580. A successful manager kill response was therefore
  not proof that this process exited.
- Last observed `/system_ex` and `/system_ex/app/PSWL00002` mount flags were
  `0x1041` (`MNT_RDONLY` set), type `exfatfs`, source `/dev/ssd0.system_ex`.
  Read/write nmount returned `0` while subsequent unlink returned `EROFS` (`30`).
  Final code was reverted to the exact shipping websrv read/write-only path;
  no further read-only restore attempts are made.
- The two files and two directories listed immediately above remain the known
  system-partition residue. Early uninstall calls successfully unregistered
  `PSWL00001` (`rc=0`) and reported no error removing the user/data-side files.
  A final installer upload/run was accepted immediately before the pause, but
  its install logs were not observed because old pid124 still held 9580. Do
  **not** infer that the icon is presently registered or that its new target is
  installed merely from that upload acknowledgement.

A reboot releases the stuck processes and their kernel/application state; it
does **not** delete the persistent `PSWL00002` files. Re-establish the console's
existing jailbreak/autoloader so elfldr and Payload Manager are serving again.
No Payload Manager startup/config changes were made by this project. Then run
`launcher/test.sh` to install the real compositor bundle and exercise its
foreground route, or run `ps5wl-uninstall.elf` as documented above if removing
the project. The final rw-only cleanup path and `_exit` diagnostic correction
still need that fresh-session hardware run; no post-reboot result is claimed.
