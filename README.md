# niri-kdeconnect-fix

This project has pretty much vibecoded, so use it on your risk.
Tested on CachyOS with KDE Connect 1.34.4 and niri 26.04 (8ed0da4)
- Mouse input works
- Keyboard input works

Based on [hypr-kdeconnect-fix](https://github.com/gfhdhytghd/hypr-kdeconnect-fix) by lin (MIT), ported to niri.

A small user-level `xdg-desktop-portal` RemoteDesktop backend that makes KDE
Connect and Deskflow remote input work on [niri](https://github.com/YaLTeR/niri).

KDE Connect and Deskflow use `org.freedesktop.portal.RemoteDesktop` to inject
mouse and keyboard input on Wayland. A typical niri setup routes ScreenCast and
Screenshot through the GNOME/GTK portals but has no backend for RemoteDesktop,
so remote input has nowhere to go. This bridge supplies exactly that one portal
interface and injects events through:

- `zwlr_virtual_pointer_manager_v1` / `zwlr_virtual_pointer_v1`
- `zwp_virtual_keyboard_manager_v1` / `zwp_virtual_keyboard_v1`

Both protocols are exposed by niri, so no compositor plugin or patch is needed.
This is a standalone Wayland client and D-Bus service, not a niri plugin.

## Status

Implemented:

- relative pointer motion
- absolute pointer motion
- pointer button events
- smooth and discrete scrolling
- keyboard keycode input
- keyboard keysym input through `xkbcommon`
- libei `ConnectToEIS` sender clients through a minimal `libeis` bridge

Not implemented:

- touchscreen events
- InputCapture/share-input-devices edge capture
- a permission dialog

KDE Connect 26.04+ prefers `ConnectToEIS` on Wayland. This bridge accepts that
path and translates incoming libei pointer, scroll, and keyboard events into the
same virtual-input backend used by the RemoteDesktop `Notify*` methods.

Deskflow can use this backend to receive keyboard and pointer input on niri.
See [Deskflow setup](docs/deskflow.md).

### niri version notes

niri has exposed the wlr virtual pointer and virtual keyboard protocols for a
long time. Virtual keyboard keymap handling and compositor key bindings were
reworked in niri's Smithay dependency in October 2026. On older niri releases
(including 26.04) remote input reaches applications, but keys sent through
`zwp_virtual_keyboard_v1` may not trigger niri's own compositor bindings. Use a
recent niri build if you need remote Super/Alt shortcuts to control niri itself.
This limitation is in the compositor, not in this bridge.

## Quick start (niri)

A minimal end-to-end setup. See the sections below for details and security
notes.

### 1. Install dependencies

Arch Linux:

```sh
sudo pacman -S cmake gcc pkgconf qt6-base wayland libxkbcommon libei xdg-desktop-portal
```

Debian / Ubuntu:

```sh
sudo apt install cmake g++ pkg-config qt6-base-dev libwayland-dev \
  libxkbcommon-dev libei-dev xdg-desktop-portal
```

If suitable system `libei`/`libeis` packages are unavailable, CMake builds the
bundled version and additionally requires:

```sh
sudo apt install meson ninja-build python3-jinja2
```

### 2. Build, test, and install

```sh
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
cmake --install build
```

Expected test result: `100% tests passed`.

Make sure `~/.local/bin` is on `PATH`:

```sh
echo "$PATH" | tr ':' '\n' | grep -Fqx "$HOME/.local/bin" \
  && echo OK \
  || echo "add ~/.local/bin to PATH"
```

### 3. Route the RemoteDesktop portal to this backend

niri uses `niri-portals.conf`, which takes precedence over the generic
`portals.conf`. Adding RemoteDesktop only to `portals.conf` has no effect.

```sh
mkdir -p ~/.config/xdg-desktop-portal
cp /usr/share/xdg-desktop-portal/niri-portals.conf \
   ~/.config/xdg-desktop-portal/niri-portals.conf

grep -q '^org.freedesktop.impl.portal.RemoteDesktop=' \
  ~/.config/xdg-desktop-portal/niri-portals.conf \
  || sed -i '/^\[preferred\]/a org.freedesktop.impl.portal.RemoteDesktop=niri-kdeconnect;' \
     ~/.config/xdg-desktop-portal/niri-portals.conf
```

On niri 26.04 the result should look like:

```ini
[preferred]
default=gnome;gtk;
org.freedesktop.impl.portal.Access=gtk;
org.freedesktop.impl.portal.Notification=gtk;
org.freedesktop.impl.portal.Secret=gnome-keyring;
org.freedesktop.impl.portal.RemoteDesktop=niri-kdeconnect;
```

### 4. Restart the portal

```sh
systemctl --user daemon-reload
systemctl --user stop niri-kdeconnect-portal.service 2>/dev/null || true
systemctl --user restart xdg-desktop-portal.service
```

The backend is D-Bus activated. The user unit has no `[Install]` section, so
`systemctl --user enable` is neither needed nor meaningful.

### 5. Verify

```sh
busctl --user introspect \
  org.freedesktop.portal.Desktop \
  /org/freedesktop/portal/desktop \
  org.freedesktop.portal.RemoteDesktop

wayland-info | grep -E 'zwlr_virtual_pointer_manager_v1|zwp_virtual_keyboard_manager_v1'
```

`CreateSession`, `SelectDevices`, `Start`, `ConnectToEIS`, and the `Notify*`
methods should be listed. To test direct input injection (this moves the
pointer):

```sh
~/.local/bin/niri-kdeconnect-portal --self-test-motion 120 0
~/.local/bin/niri-kdeconnect-portal --self-test-absolute 1000 500
~/.local/bin/niri-kdeconnect-portal --self-test-scroll 0 120
~/.local/bin/niri-kdeconnect-portal --self-test-scroll-discrete 0 3
```

If KDE Connect was already running before the portal restart, restart
`kdeconnectd` and reopen Remote Input on the phone; see
[Troubleshooting](#troubleshooting).

## Dependencies

Build-time:

- CMake 3.20+
- a C++23 compiler
- Qt 6.5+ Core/DBus
- `pkg-config`
- `wayland-client` 1.20+
- `wayland-scanner`
- `xkbcommon` 1.5+
- system `libeis` 1.6+ and, when building tests, `libei` 1.6+ (optional)
- when suitable system libraries are unavailable: Meson 0.57+, Ninja, Python 3,
  and the Python `jinja2` module to build the bundled libei 1.6.0 submodule

Runtime:

- niri exposing `zwlr_virtual_pointer_manager_v1` and
  `zwp_virtual_keyboard_manager_v1` (verified with niri 26.04)
- `xdg-desktop-portal`
- `libeis` when using the system-library build
- KDE Connect or Deskflow

On Arch-like systems:

```sh
sudo pacman -S cmake gcc pkgconf qt6-base wayland libxkbcommon libei xdg-desktop-portal
```

## Build

Clone with submodules, or initialize them in an existing checkout:

```sh
git clone --recurse-submodules https://github.com/risenburn/niri-kdeconnect-fix.git
cd niri-kdeconnect-fix
# For an existing checkout:
git submodule update --init --recursive
```

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
cmake --install build
```

CMake prefers system libeis/libei 1.6+. If a required library is missing or too
old, it builds the pinned `third_party/libei` submodule with Meson and links it
statically. The bundled build does not install or replace system libraries and
does not require libsystemd/libelogind; upstream tools, documentation and tests
are not built. Our own tests remain enabled by default.

On Debian, the additional bundled-build tools can be installed with:

```sh
sudo apt install meson ninja-build python3-jinja2
```

To force the bundled build even when suitable system libraries exist, add
`-DNKCF_USE_BUNDLED_LIBEI=ON` to the CMake configure command. After changing
this option or `BUILD_TESTING`, use a fresh build directory.

This installs:

- `~/.local/bin/niri-kdeconnect-portal`
- `~/.local/share/xdg-desktop-portal/portals/niri-kdeconnect.portal`
- `~/.local/share/dbus-1/services/org.freedesktop.impl.portal.desktop.niri_kdeconnect.service`
- `~/.local/share/systemd/user/niri-kdeconnect-portal.service`

The default build enables compiler/linker hardening on Linux, including PIE,
RELRO/BIND_NOW, stack protector, and fortify checks when the compiler supports
them.

By default, the generated portal metadata is visible only when
`XDG_CURRENT_DESKTOP=niri`. To test another desktop id temporarily, set it at
configure time:

```sh
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$HOME/.local" \
  -DNKCF_PORTAL_USE_IN='niri;your-session-id'
```

## Portal configuration for niri

niri ships a desktop-specific portal configuration at
`/usr/share/xdg-desktop-portal/niri-portals.conf`. Desktop-specific files take
precedence over the generic `portals.conf`, so adding RemoteDesktop only to
`~/.config/xdg-desktop-portal/portals.conf` often has no effect on niri.

Create a user override that keeps niri's existing providers and adds only the
RemoteDesktop backend. On niri 26.04 the packaged file contains:

```ini
[preferred]
default=gnome;gtk;
org.freedesktop.impl.portal.Access=gtk;
org.freedesktop.impl.portal.Notification=gtk;
org.freedesktop.impl.portal.Secret=gnome-keyring;
```

Write the override to `~/.config/xdg-desktop-portal/niri-portals.conf` with the
same content plus one line:

```ini
[preferred]
default=gnome;gtk;
org.freedesktop.impl.portal.Access=gtk;
org.freedesktop.impl.portal.Notification=gtk;
org.freedesktop.impl.portal.Secret=gnome-keyring;
org.freedesktop.impl.portal.RemoteDesktop=niri-kdeconnect;
```

If your niri installation ships different preferences (for example a native
ScreenCast backend), copy those lines into the override instead of replacing
them. Only the `RemoteDesktop` line is specific to this bridge.

Reload user units and restart the portal frontend after changing metadata or
`niri-portals.conf`:

```sh
systemctl --user daemon-reload
systemctl --user restart xdg-desktop-portal
```

If D-Bus already activated an older bridge process, stop the user unit first:

```sh
systemctl --user stop niri-kdeconnect-portal.service
```

If it was started by plain D-Bus activation instead of systemd, verify the
binary path before killing it:

```sh
pid="$(busctl --user status org.freedesktop.impl.portal.desktop.niri_kdeconnect 2>/dev/null | awk -F= '/^PID=/{print $2; exit}')"
if [ -n "$pid" ] && [ "$(readlink -f "/proc/$pid/exe" 2>/dev/null)" = "$HOME/.local/bin/niri-kdeconnect-portal" ]; then
  kill "$pid"
fi
```

## Verification

Check that the public portal frontend exposes RemoteDesktop:

```sh
busctl --user introspect \
  org.freedesktop.portal.Desktop \
  /org/freedesktop/portal/desktop \
  org.freedesktop.portal.RemoteDesktop
```

Check that niri advertises the virtual input protocols:

```sh
wayland-info | grep -E 'zwlr_virtual_pointer_manager_v1|zwp_virtual_keyboard_manager_v1'
```

Check the logical output layout that absolute pointer motion is mapped onto:

```sh
niri msg outputs
```

Check direct virtual pointer injection. These commands move the pointer in the
current niri session:

```sh
niri-kdeconnect-portal --self-test-motion 120 0
niri-kdeconnect-portal --self-test-absolute 1440 900
niri-kdeconnect-portal --self-test-scroll 0 120
niri-kdeconnect-portal --self-test-scroll-discrete 0 3
```

## Updating

After changing the sources, rebuild, rerun the tests, and reinstall:

```sh
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
cmake --install build
systemctl --user daemon-reload
systemctl --user restart xdg-desktop-portal.service
```

If a previously installed backend process is still running, stop it first:

```sh
systemctl --user stop niri-kdeconnect-portal.service 2>/dev/null || true
```

## How It Works

1. KDE Connect or Deskflow calls the public portal frontend at
   `org.freedesktop.portal.Desktop`.
2. `xdg-desktop-portal` reads `niri-portals.conf` and forwards RemoteDesktop
   backend calls to `org.freedesktop.impl.portal.desktop.niri_kdeconnect`.
3. This bridge accepts sessions from the portal frontend and creates virtual
   input devices on niri's Wayland socket.
4. If the client calls `ConnectToEIS`, libei events are received through
   `libeis` and translated to the same virtual input path.
5. If a client uses the older RemoteDesktop `Notify*` calls, those are handled
   directly.
6. Pointer events are sent through `zwlr_virtual_pointer_v1`.
7. Keyboard events are sent through `zwp_virtual_keyboard_v1`.
8. Absolute pointer coordinates are mapped onto the union of niri's logical
   output rectangles obtained through `zxdg_output_manager_v1`, so scaled and
   multi-monitor layouts behave correctly.

The protocol XML files are included in this repository and compiled with
`wayland-scanner`, so the build does not depend on a local compositor source
tree.

## Security Notes

niri exposes the virtual input protocols to trusted, non-sandboxed Wayland
clients. This bridge does not add a graphical permission prompt on top of that,
so it narrows the D-Bus attack surface instead:

- RemoteDesktop and Session methods are accepted only from the current
  `org.freedesktop.portal.Desktop` owner.
- Sessions are bound to that D-Bus sender and capped in number.
- Accepted app ids are exact KDE Connect desktop ids and
  `org.deskflow.deskflow`; substring matches are rejected. Native clients with
  an empty or `surface-transient` app id must pass additional process checks.
  KDE Connect must own its D-Bus name and use a known daemon path. Deskflow's
  originating D-Bus process must match `/usr/bin/deskflow-core` or the exact
  absolute path set by `NKCF_DESKFLOW_EXECUTABLE` at build time; a matching
  basename or build-directory suffix is insufficient.
- Notify calls are checked against the selected device mask and bounded before
  being forwarded to Wayland.

Use it as a local-session compatibility bridge. Do not install it system-wide on
multi-user machines unless that trust model is acceptable.

## Troubleshooting

If KDE Connect still does not move the pointer:

```sh
busctl --user status org.freedesktop.impl.portal.desktop.niri_kdeconnect
journalctl --user _COMM=niri-kdeconnect --since '10 min ago' --no-pager
```

Restart KDE Connect whenever `xdg-desktop-portal` or this backend was
(re)started after `kdeconnectd`. KDE Connect keeps its old RemoteDesktop session
handle and never recreates it, so remote input silently stops and
`xdg-desktop-portal` logs `g_hash_table_lookup: assertion 'hash_table != NULL'
failed` for every movement:

```sh
kquitapp6 kdeconnectd || pkill -x kdeconnectd
kdeconnectd >/dev/null 2>&1 & disown   # or reopen KDE Connect / log in again
```

Then reopen Remote Input on the phone and move your finger once to create a
new session. If it still fails, collect both sides:

```sh
journalctl --user _COMM=niri-kdeconnect --since '10 min ago' --no-pager
QT_LOGGING_RULES='kdeconnect.plugin.mousepad.debug=true' kdeconnectd
```

If the RemoteDesktop interface is missing from the public frontend, check:

```sh
cat ~/.config/xdg-desktop-portal/niri-portals.conf
cat /usr/share/xdg-desktop-portal/niri-portals.conf
ls ~/.local/share/xdg-desktop-portal/portals/niri-kdeconnect.portal
systemctl --user restart xdg-desktop-portal
```

If an old binary keeps running after reinstall:

```sh
systemctl --user stop niri-kdeconnect-portal.service
pid="$(busctl --user status org.freedesktop.impl.portal.desktop.niri_kdeconnect 2>/dev/null | awk -F= '/^PID=/{print $2; exit}')"
if [ -n "$pid" ] && [ "$(readlink -f "/proc/$pid/exe" 2>/dev/null)" = "$HOME/.local/bin/niri-kdeconnect-portal" ]; then
  kill "$pid"
fi
```

## Uninstall

```sh
systemctl --user stop niri-kdeconnect-portal.service 2>/dev/null || true

rm -f ~/.local/bin/niri-kdeconnect-portal
rm -f ~/.local/share/xdg-desktop-portal/portals/niri-kdeconnect.portal
rm -f ~/.local/share/dbus-1/services/org.freedesktop.impl.portal.desktop.niri_kdeconnect.service
rm -f ~/.local/share/systemd/user/niri-kdeconnect-portal.service
systemctl --user daemon-reload
systemctl --user restart xdg-desktop-portal
```

Also remove the `org.freedesktop.impl.portal.RemoteDesktop=niri-kdeconnect` line
from `~/.config/xdg-desktop-portal/niri-portals.conf` (or delete the override if
it exists only for this bridge).
