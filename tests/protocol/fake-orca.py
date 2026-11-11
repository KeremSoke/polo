#!/usr/bin/env python3
# Copyright 2026 Kerem Soke
# SPDX-License-Identifier: GPL-3.0-or-later
"""A screen reader's side of org.freedesktop.a11y.KeyboardMonitor, as
libatspi's a11y manager device uses it, for test-keyboard-monitor.

  orca LOG

It takes org.gnome.Orca.KeyboardMonitor and watches the keyboard, then reads
commands from stdin, one a line: grab, ungrab, watch, unwatch,
"keygrabs MODIFIER,... KEYSYM:MASK,..." (either list may be "-"), quit.
LOG gets "watch ok" or "watch denied", "<command> ok|denied" and one
"key RELEASED STATE KEYSYM UNICHAR KEYCODE" line for each KeyEvent.
"""

import sys

from gi.repository import Gio, GLib

NAME = "org.freedesktop.a11y.Manager"
PATH = "/org/freedesktop/a11y/Manager"
INTERFACE = "org.freedesktop.a11y.KeyboardMonitor"

log = open(sys.argv[1], "a", buffering=1)
bus = Gio.bus_get_sync(Gio.BusType.SESSION)
loop = GLib.MainLoop()

bus.call_sync("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
              "RequestName", GLib.Variant("(su)", ("org.gnome.Orca.KeyboardMonitor", 0)),
              None, Gio.DBusCallFlags.NONE, -1, None)


def call(method, args=None):
    try:
        bus.call_sync(NAME, PATH, INTERFACE, method, args, None, Gio.DBusCallFlags.NONE, 5000, None)
        return "ok"
    except GLib.Error:
        return "denied"


def on_key(connection, sender, path, interface, signal, params):
    released, state, keysym, unichar, keycode = params.unpack()
    log.write("key %d %d %d %d %d\n" % (released, state, keysym, unichar, keycode))


bus.signal_subscribe(None, INTERFACE, "KeyEvent", PATH, None, Gio.DBusSignalFlags.NONE, on_key)
log.write("watch %s\n" % call("WatchKeyboard"))


def parse_grabs(words):
    modifiers = [] if words[0] == "-" else [int(k, 0) for k in words[0].split(",")]
    strokes = []
    if words[1] != "-":
        for pair in words[1].split(","):
            keysym, mask = pair.split(":")
            strokes.append((int(keysym, 0), int(mask, 0)))
    return GLib.Variant("(aua(uu))", (modifiers, strokes))


def on_line(channel, condition):
    line = sys.stdin.readline()
    if not line or line.strip() == "quit":
        loop.quit()
        return False
    words = line.split()
    command = words[0]
    if command == "keygrabs":
        result = call("SetKeyGrabs", parse_grabs(words[1:]))
    else:
        result = call({"grab": "GrabKeyboard", "ungrab": "UngrabKeyboard",
                       "watch": "WatchKeyboard", "unwatch": "UnwatchKeyboard"}[command])
    log.write("%s %s\n" % (command, result))
    return True


GLib.io_add_watch(GLib.IOChannel.unix_new(sys.stdin.fileno()), GLib.PRIORITY_DEFAULT,
                  GLib.IOCondition.IN | GLib.IOCondition.HUP, on_line)
loop.run()
