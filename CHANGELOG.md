# Changelog

Polo follows [Semantic Versioning](https://semver.org/). The public surface is
what MATE and its users rely on: the `org.mate.Marco.*` and `org.mate.polo.*`
settings it reads, the session D-Bus interfaces, the Wayland protocols it
offers, the command line and the installed files. The private libweston is not
part of it.

## 1.1.0 — 2026-11-26

### Added
- Hot corners and edges (`org.mate.polo.hot-corners`): pushing or resting the
  pointer in a corner or against an edge opens Expo, the cube, Scale or the
  window switcher, shows the desktop, locks the screen, opens the run dialog or
  runs a command, on every screen or only on the ones chosen.
- Scale (`org.mate.polo.scale`): the windows of the workspace, of every
  workspace or of one application spread out side by side to pick one, with
  Super+W and Super+Shift+W or from a hot corner, on the screen it was opened
  on, on every screen, or the same on all of them.
- The window switcher can be opened and used with the pointer.
- Each workspace and each monitor can have its own wallpaper, and Polo draws
  the wallpaper wherever no desktop draws it.
- MATE's settings tools can name monitors, set the primary, try layouts and
  list input devices, through the MATE output and input device protocols.
- Fractional scaling for programs that support it (`wp_fractional_scale_v1`).
- Ctrl+Alt+F1 to F8 switch to the other virtual terminals.
- Screen, window and area screenshots with mate-screenshot.
- Polo Tweaks is trusted with Polo's settings protocols.

### Changed
- The blurred backgrounds of Expo, Scale and the cube are made twice as fast,
  and Expo keeps them while windows open and close.
- Title bars follow the font DPI setting.
- Xwayland's primary monitor follows Polo's.
- The session starts through `polo-wayland.sh`, and the compiled dconf
  database is installed with Polo.
- mate-settings-daemon's wlrandr plugin stays off in the Polo session.

### Fixed
- A background colour behind a transparent wallpaper showed black in Expo,
  Scale, the cube and on the desktop.
- Windows, modes and programs stay right when a monitor is turned off or
  unplugged, the pointer stays on a screen when a layout is applied, and
  layouts where screens overlap without mirroring are refused.
- Window menus close when another window takes the focus, and popups closed
  before they first showed no longer upset their program.
- Panel popups are placed by their window geometry and kept on screen at the
  left and top edges.
- After switching back from another virtual terminal, the keyboard returns to
  the panel or lock screen that had it.
- Xwayland popups that draw before mapping show, and moving X11 windows far
  off screen no longer goes wrong.
- A window whose frame does not fit its maximized or fullscreen size keeps it.
- Window screenshots include the frame Polo draws.
- The cursor image goes away when no window has the pointer.
- Windows that come back to a monitor are redrawn.
- Large buffers no longer fragment Polo's memory.
- Polo builds without Xwayland again.

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
