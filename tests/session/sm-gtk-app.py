#!/usr/bin/env python3
# Copyright 2026 Kerem Soke
# SPDX-License-Identifier: GPL-3.0-or-later
"""A GtkApplication for test-sm-consumers.py: sm-gtk-app.py LOG

GTK 3 registers it with org.gnome.SessionManager (gtkapplication-dbus.c).
It inhibits logout with a window, lifts the inhibitor on SIGUSR1, logs
query-end and exits when GTK quits it at the end of the session.
"""

import signal
import sys

import gi

gi.require_version("Gtk", "3.0")
from gi.repository import GLib, Gtk  # noqa: E402

APP_ID = "org.mate.polo.SmTest"


def main():
    log = sys.argv[1]

    def record(line):
        with open(log, "a") as f:
            f.write(line + "\n")

    app = Gtk.Application(application_id=APP_ID, register_session=True)
    state = {}

    def activate(application):
        window = Gtk.ApplicationWindow(application=application, title="sm test")
        window.show_all()
        state["cookie"] = application.inhibit(window, Gtk.ApplicationInhibitFlags.LOGOUT, "testing GTK")
        record(f"inhibited {state['cookie']}")

    def release():
        if state.get("cookie"):
            app.uninhibit(state.pop("cookie"))
            record("uninhibited")
        return GLib.SOURCE_REMOVE

    app.connect("activate", activate)
    app.connect("query-end", lambda application: record("query-end"))
    GLib.unix_signal_add(GLib.PRIORITY_DEFAULT, signal.SIGUSR1, release)
    status = app.run([sys.argv[0]])
    record("quit")
    return status


if __name__ == "__main__":
    sys.exit(main())
