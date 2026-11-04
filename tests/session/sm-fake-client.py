#!/usr/bin/env python3
# Copyright 2026 Kerem Soke
# SPDX-License-Identifier: GPL-3.0-or-later
"""A D-Bus session client for test-end-session.py.

  sm-fake-client.py LOG APP_ID [BEHAVIOUR,...]

Registers with org.gnome.SessionManager, appends a line per ClientPrivate
signal to LOG, answers QueryEndSession and EndSession with ok, and exits on
Stop. BEHAVIOUR: refuse=REASON answers the query with not ok, silent-query
does not answer it, ignore-stop stays on Stop, ignore-term ignores SIGTERM.
"""

import signal
import sys

from gi.repository import Gio, GLib

NAME = "org.gnome.SessionManager"
ROOT = "/org/gnome/SessionManager"


def main():
    log, app_id = sys.argv[1], sys.argv[2]
    behaviour = sys.argv[3].split(",") if len(sys.argv) > 3 else []
    if "ignore-term" in behaviour:
        signal.signal(signal.SIGTERM, signal.SIG_IGN)

    def record(line):
        with open(log, "a") as f:
            f.write(f"{app_id} {line}\n")

    bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
    loop = GLib.MainLoop()
    path = None

    def respond(is_ok, reason=""):
        bus.call_sync(NAME, path, NAME + ".ClientPrivate", "EndSessionResponse",
                      GLib.Variant("(bs)", (is_ok, reason)), None, Gio.DBusCallFlags.NONE, 5000, None)

    def on_signal(connection, sender, object_path, interface, name, parameters):
        record(f"{name} {parameters.unpack()}")
        if name == "QueryEndSession":
            if "silent-query" in behaviour:
                return
            refuse = [b[len("refuse="):] for b in behaviour if b.startswith("refuse=")]
            respond(not refuse, refuse[0] if refuse else "")
        elif name == "EndSession":
            respond(True)
        elif name == "Stop" and "ignore-stop" not in behaviour:
            loop.quit()

    path = bus.call_sync(NAME, ROOT, NAME, "RegisterClient", GLib.Variant("(ss)", (app_id, "")),
                         GLib.VariantType("(o)"), Gio.DBusCallFlags.NONE, 5000, None).unpack()[0]
    bus.signal_subscribe(None, NAME + ".ClientPrivate", None, path, None, Gio.DBusSignalFlags.NONE,
                         on_signal)
    record(f"registered {path}")
    loop.run()
    record("stopped")
    return 0


if __name__ == "__main__":
    sys.exit(main())
