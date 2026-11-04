# Copyright 2026 Kerem Soke
# SPDX-License-Identifier: GPL-3.0-or-later
"""One X window under a window manager, put through the frame states the
visual tests compare. Marco's references and Polo's captures both come from
here, so the client, its title and its size are the same on both sides."""

import time

from Xlib import X, Xatom, display, protocol

WIDTH, HEIGHT = 320, 180
TITLE = "Polo frame"
STATES = ("focused", "unfocused", "maximized", "shaded")
DEFAULT_LAYOUT = "menu:minimize,maximize,close"
# The other button layouts Control Center -> Windows offers, by reference
# name, and the themes and states they are checked in.
LAYOUTS = {"left": "close,minimize,maximize:menu", "left-no-menu": "close,minimize,maximize:"}
LAYOUT_THEMES = ("Menta", "TraditionalOk")
LAYOUT_STATES = ("focused", "maximized")


def references(themes):
    """(theme, reference name, button layout, states) to render or compare."""
    for theme in themes:
        yield theme, theme, DEFAULT_LAYOUT, STATES
        if theme in LAYOUT_THEMES:
            for name, layout in LAYOUTS.items():
                yield theme, f"{theme}@{name}", layout, LAYOUT_STATES


class Scene:
    def __init__(self, name):
        self.d = display.Display(name)
        self.screen = self.d.screen()
        self.root = self.screen.root
        self.window = self._window(TITLE, 120, 100, WIDTH, HEIGHT)
        self.other = self._window("other", 700, 500, 120, 80)
        self.settle(1.0)
        self.focus(self.window)

    def close(self):
        self.d.close()

    def atom(self, name):
        return self.d.intern_atom(name)

    def _window(self, title, x, y, width, height):
        w = self.root.create_window(x, y, width, height, 0, self.screen.root_depth,
                                    X.InputOutput, X.CopyFromParent,
                                    background_pixel=self.screen.white_pixel,
                                    event_mask=X.StructureNotifyMask | X.PropertyChangeMask)
        w.set_wm_name(title)
        w.change_property(self.atom("_NET_WM_NAME"), self.atom("UTF8_STRING"), 8,
                          title.encode())
        w.set_wm_class("polo-frame", "PoloFrame")
        w.set_wm_normal_hints(flags=1, x=x, y=y)
        w.map()
        self.d.sync()
        return w

    def settle(self, seconds=0.4):
        end = time.time() + seconds
        while time.time() < end:
            while self.d.pending_events():
                self.d.next_event()
            time.sleep(0.02)

    def _message(self, window, kind, data):
        event = protocol.event.ClientMessage(window=window, client_type=self.atom(kind),
                                             data=(32, (data + [0] * 5)[:5]))
        self.root.send_event(event, event_mask=X.SubstructureRedirectMask |
                             X.SubstructureNotifyMask)
        self.d.sync()

    def focus(self, window):
        self._message(window, "_NET_ACTIVE_WINDOW", [2, X.CurrentTime])
        self.settle()

    def set_state(self, on, name):
        self._message(self.window, "_NET_WM_STATE",
                      [1 if on else 0, self.atom("_NET_WM_STATE_" + name.upper()), 0, 2])
        self.settle(0.8)

    def enter(self, state):
        """Takes the window from the focused state into state."""
        if state == "unfocused":
            self.focus(self.other)
        elif state == "maximized":
            self._message(self.window, "_NET_WM_STATE",
                          [1, self.atom("_NET_WM_STATE_MAXIMIZED_VERT"),
                           self.atom("_NET_WM_STATE_MAXIMIZED_HORZ"), 2])
            self.settle(0.8)
        elif state == "shaded":
            self.set_state(True, "shaded")
        self.settle(0.6)

    def leave(self, state):
        if state == "unfocused":
            self.focus(self.window)
        elif state == "maximized":
            self._message(self.window, "_NET_WM_STATE",
                          [0, self.atom("_NET_WM_STATE_MAXIMIZED_VERT"),
                           self.atom("_NET_WM_STATE_MAXIMIZED_HORZ"), 2])
            self.settle(0.8)
        elif state == "shaded":
            self.set_state(False, "shaded")
        self.settle(0.6)

    def frame(self, state="focused"):
        """The frame's root rectangle and the client's within it; shaded, the titlebar."""
        geometry = self.window.get_geometry()
        origin = self.window.translate_coords(self.root, 0, 0)
        extents = self.window.get_full_property(self.atom("_NET_FRAME_EXTENTS"), Xatom.CARDINAL)
        left, right, top, bottom = extents.value.tolist() if extents else (0, 0, 0, 0)
        x, y = -origin.x, -origin.y
        client = (left, top, geometry.width, geometry.height)
        height = top if state == "shaded" else geometry.height + top + bottom
        return (x - left, y - top, geometry.width + left + right, height), client
