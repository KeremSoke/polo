# Changelog

Polo follows [Semantic Versioning](https://semver.org/). The public surface is
what MATE and its users rely on: the `org.mate.Marco.*` and `org.mate.polo.*`
settings it reads, the session D-Bus interfaces, the Wayland protocols it
offers, the command line and the installed files. The private libweston is not
part of it.

## 1.0.1 — 2026-11-13

### Fixed
- The magnifier no longer freezes the screen on some GPUs (AMD).
- Desktop (Caja) right-click menus show above windows.
- The magnifier no longer crashes Polo on DRM.
- Install no longer fails on a broken dconf database of another program.
- The notification daemon is trusted, so notifications show again.
- The panel no longer crashes when a monitor is turned off and on again

### Changed
- A missing Xwayland is reported clearly at build time and when Polo starts it.

## 1.0.0 — 2026-11-11

First release.

### Window management
- Marco's window management policies and settings (`org.mate.Marco.*`),
  themes and keybindings, on Wayland and for X11 programs through Polo's own
  XWayland window manager.
- Alt+Tab switcher, window menus, dialogs and on-screen displays drawn by
  `polo-shell-ui`, announced to screen readers.
- Tiling by keys and by dragging, with a preview; workspaces.
- Effects (`org.mate.polo.effects`), including wobbly windows.

### Session
- `polo --session` is the MATE session manager, compatible with
  mate-session-manager 1.28's D-Bus API: autostart phases, inhibitors,
  Presence, log out, shut down and restart dialogs, logind, saved sessions and
  XSMP.
- mate-settings-daemon runs with the plugins that cannot work on Wayland
  turned off in the Polo session only.

### Outputs and input
- DRM backend with hotplug, saved screen layouts
  (`~/.config/mate/polo/monitors.xml`), DPMS, gamma and output zoom.
- Idle notification and inhibition, pointer gestures, cursor shapes,
  explicit synchronisation.

### Desktop integration
- Screenshots and screen sharing through xdg-desktop-portal, with
  `polo-screenshot` and a screen sharing chooser.
- Layer shell for mate-panel and other shells.

### Packaging
- Installs its patched libweston 16.0.1 privately under `$libdir/polo`.
- Fedora spec in `packaging/fedora/`.

### Known limitations
- Fedora's mate-panel is built for X11 only; the panel needs a
  Wayland-enabled build.
- GTK 3.24 leaks a little memory per closed window on Wayland; `polo-shell-ui`
  works around it for its own windows.
