# Deskflow on niri

Deskflow clients need the `org.freedesktop.portal.RemoteDesktop` interface to
receive keyboard and pointer input. An available `InputCapture` interface is
for sharing local input and does not provide the receiving side. This bridge
supplies RemoteDesktop on niri, which exposes the virtual keyboard and virtual
pointer protocols but does not ship a RemoteDesktop portal backend.

## Build and install

Install Deskflow separately, then locate the actual `deskflow-core` executable.
Native Deskflow sessions may have an empty portal app id, so the bridge checks
the originating D-Bus process against `/usr/bin/deskflow-core` or an exact
configured executable path.

For a user-local Deskflow installation:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$HOME/.local" \
  -DNKCF_DESKFLOW_EXECUTABLE="$HOME/.local/bin/deskflow-core"
cmake --build build --parallel
ctest --test-dir build --output-on-failure
cmake --install build
```

The packaged `/usr/bin/deskflow-core` remains accepted. For other installation
locations, set `NKCF_DESKFLOW_EXECUTABLE` to the resolved absolute path. Its
default is `<CMAKE_INSTALL_PREFIX>/bin/deskflow-core`. If the binary is a
symlink, use its resolved target because authorization checks `/proc/<pid>/exe`.
Changing the installation path requires rebuilding with the new value. Source
builds under `deskflow-src/build/bin` also need their full path configured;
that suffix alone is no longer accepted.

## Portal routing

Back up the effective niri portal configuration. niri ships a desktop-specific
file at `/usr/share/xdg-desktop-portal/niri-portals.conf`, which takes
precedence over the generic `portals.conf`. Create a user override at
`~/.config/xdg-desktop-portal/niri-portals.conf`, copy the packaged
preferences, and add only this line under `[preferred]`:

```ini
org.freedesktop.impl.portal.RemoteDesktop=niri-kdeconnect
```

The result on niri 26.04 looks like:

```ini
[preferred]
default=gnome;gtk;
org.freedesktop.impl.portal.Access=gtk;
org.freedesktop.impl.portal.Notification=gtk;
org.freedesktop.impl.portal.Secret=gnome-keyring;
org.freedesktop.impl.portal.RemoteDesktop=niri-kdeconnect;
```

Preserve any native ScreenCast/Screenshot providers already present in your
niri configuration. Only the RemoteDesktop line belongs to this bridge.

Restart the services when no active screen-sharing session needs preserving:

```sh
systemctl --user daemon-reload
systemctl --user restart xdg-desktop-portal.service
systemctl --user restart niri-kdeconnect-portal.service
busctl --user introspect org.freedesktop.portal.Desktop \
  /org/freedesktop/portal/desktop org.freedesktop.portal.RemoteDesktop
```

The public interface should expose `CreateSession`, `SelectDevices`, `Start`
and `ConnectToEIS`. Configure Deskflow as a client and connect it to your
Deskflow server. Keep TLS and peer verification enabled and compare the
server's fingerprint before trusting it. The server must include the client's
computer name in its layout and allow its TCP connection to the configured
Deskflow port (24800 by default).

## Validation and limits

Automated tests use a real libei/libeis connection with a recording input sink
to check device discovery, pointer coordinates, wheel units and direction,
key/button delivery, and releases on emulation stop or disconnect. The tests do
not connect to a compositor.

Live validation on niri should check:

- `wayland-info` lists both the virtual pointer and virtual keyboard managers.
- `niri msg outputs` shows the logical output layout used for absolute motion.
- The self-test commands move the pointer on every monitor in a scaled layout.
- Keys typed through Deskflow reach focused applications.
- Remote Super/Alt shortcuts trigger niri bindings on niri builds that include
  the October 2026 Smithay virtual-keyboard rework. Older niri releases may
  deliver keys to applications without running compositor binds.

The bridge provides input only. It does not expose the Clipboard portal or
enable file transfers. Multi-monitor and scaled-output layouts use the union of
niri's logical output rectangles through `zxdg_output_manager_v1`; stream-level
absolute mapping is not implemented. Keyboard layout selection follows the
backend's default XKB keymap.

To roll back, stop `niri-kdeconnect-portal.service`, restore the previous niri
portal configuration, reload user units, and restart
`xdg-desktop-portal.service`. The existing niri installation and Deskflow
installation can remain installed.
