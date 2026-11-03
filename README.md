# Polo

Polo is a port of Marco's behavior and window management policies to Wayland, built on libweston and written in Vala and C. It's a window manager, compositor, and session authority for the MATE Wayland desktop. It
takes over, step by step, what Marco, mate-session-manager and the
compositor-facing parts of mate-settings-daemon do on X11. 

I am working on this, because Wayfire wasn't designed for MATE or other DEs, yet we've been trying to make MATE fit into it through plugins, workarounds, and compromises.

## What works?

 - **Compositor** (DRM, rendering, native Wayland windows)
 - **Input** (Keyboard, mouse, shortcuts, layouts, setting)s
 - **Windows** (Focus, movement, resizing, minimization, tiling)
 - **Workspaces** (Switching, moving windows, names, show desktop)
 - **MATE** (Panel, Caja desktop, workspace switcher)
 - **XWayland** (X11 applications, authentication, startup modes, crash recovery)
 - **Autostart** (like MATE X session did)
 - **Testing** (Seee tests and Marco Oracle readmes (will be written in the future) 

## What's missing?

 - **Decorations** (Port metacity theming) 
 - **Clipboard** (Partly done) 
 - **X11 hints** 
 - **Session** (Partly done) 
 - **Applets** (Almost all work, mate-panel 1.29 needed for workspace pager) 
 - Other things I want to add like wobbly windows.

Over time, Polo will absorb the window-management responsibilities (almost alreadt did) and X11-specific integration that previously belonged to Marco or required external workarounds. 

## Building

### Fedora 44

```sh
sudo dnf install gcc git meson ninja-build python3 pkgconf-pkg-config \
    vala valadoc \
    glib2-devel gtk3-devel gtk-layer-shell-devel \
    wayland-devel wayland-protocols-devel libxkbcommon-devel \
    libinput-devel libevdev-devel libseat-devel systemd-devel \
    libdrm-devel mesa-libgbm-devel libglvnd-devel libdisplay-info-devel \
    pixman-devel cairo-devel pango-devel lcms2-devel libpng-devel \
    libxcb-devel xcb-util-wm-devel xcb-util-errors-devel xcb-util-cursor-devel \
    libXcursor-devel xorg-x11-server-Xwayland-devel \
    libcanberra-devel pulseaudio-libs-devel libSM-devel libICE-devel \
    python3-xlib python3-pyyaml json-glib-devel
```

### Debian 13 / Ubuntu 26.04

```sh
sudo apt install gcc git meson ninja-build python3 pkg-config \
    valac valadoc \
    libglib2.0-dev libgtk-3-dev libgtk-layer-shell-dev \
    libwayland-dev wayland-protocols libxkbcommon-dev \
    libinput-dev libevdev-dev libseat-dev libsystemd-dev libudev-dev \
    libdrm-dev libgbm-dev libegl-dev libgles-dev libdisplay-info-dev \
    libpixman-1-dev libcairo2-dev libpango1.0-dev liblcms2-dev libpng-dev \
    libxcb1-dev libxcb-ewmh-dev libxcb-icccm4-dev libxcb-errors-dev \
    libxcb-cursor-dev libxcursor-dev xwayland \
    libcanberra-dev libpulse-dev libsm-dev libice-dev \
    python3-xlib python3-yaml libjson-glib-dev
```

Polo needs Vala ≥ 0.56, GLib ≥ 2.80, Meson ≥ 1.4 and wayland-protocols ≥ 1.46
(the minimum Weston 16 requires).

### Build and test

```sh
meson setup build
ninja -C build
meson test -C build
build/src/polo --version
```

To start a development session from a TTY:

```sh
dbus-run-session -- meson devenv -C build polo \
  --backend=drm \
  --exec mate-panel \
  --exec "caja -n --force-desktop"
```

If you install it, you can choose `MATE (Polo)` from LightDM greeter you use.

Useful configurations:

| Command | Purpose |
|---|---|
| `meson setup build -Dstrict=true` | C `-Werror`, valac `--fatal-warnings` (CI) |
| `meson setup build-asan -Db_sanitize=address,undefined -Db_lundef=false` | sanitizers |
| `meson test -C build --suite vapi` | VAPI coverage and boundary checks |
| `meson test -C build --suite lint` | clang-format and vala-lint, when installed |

### Running Polo from the build tree

```sh
# Inside a Wayland session: Polo opens as a window.
meson devenv -C build polo --exec gtk3-demo --exec gtk3-widget-factory

# Inside an X11 session: run a parent Weston on X11 first, then Polo inside
# it. --fullscreen makes Polo's output fill the parent's window.
weston --backend=x11 --shell=kiosk --width=1280 --height=800 --socket=polo-parent &
WAYLAND_DISPLAY=polo-parent meson devenv -C build polo --fullscreen --exec gtk3-demo

# Headless, for scripting and tests.
meson devenv -C build polo --backend=headless --renderer=pixman
```

### Logging

```sh
polo --debug-scopes=polo-policy,drm-backend   # debug output of these scopes only
polo --log=/tmp/polo.log
```

Scopes: `polo`, `polo-policy`, `polo-input`, `polo-xwm`, `polo-session`,
`polo-effects` and `polo-protocol`, plus any libweston scope.

## Documentation
I will write detailed docs for each subproject and test soon.

## Licence

GPL-3.0-or-later. Imported code keeps its own compatible licence, see `LICENSES/`.
