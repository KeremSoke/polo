# Multi-output scenarios

These scenarios need two side-by-side monitors. The unprivileged X servers
tested on Fedora 44 do not expose them to Marco correctly:

- **Xvfb** `+xinerama` stacks every screen at (0,0), and its RandR cannot
  create monitors (`xrandr --setmonitor` fails with BadValue).
- **Xephyr** `+xinerama` places the screens correctly, but its RandR
  describes only the first screen. Marco follows RandR's screen size
  (`meta_screen_resize`), so it believes the screen is 1920 px wide and
  clamps every window to the first monitor.
- **Xephyr** with one wide screen split by `xrandr --setmonitor` keeps an
  undeletable whole-screen monitor for its own output, and changing that
  output's mode shrinks the framebuffer.

The oracle (`run.py`) refuses scenarios with more than one output rather than
recording wrong data.
