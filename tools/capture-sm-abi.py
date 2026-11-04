#!/usr/bin/env python3
# Copyright 2026 Kerem Soke
# SPDX-License-Identifier: GPL-3.0-or-later
"""Captures mate-session's D-Bus interfaces from a running X MATE session.

  capture-sm-abi.py OUTDIR

Run it on the MATE session's bus. Writes OUTDIR/manager.xml, presence.xml,
client.xml, inhibitor.xml and app.xml: `gdbus introspect --xml` of
/org/gnome/SessionManager, its Presence object, the first client, an inhibitor
and the first app. The inhibitor is an idle inhibitor this script takes and
drops; the client is one already registered.
"""

import pathlib
import re
import sys

from gi.repository import Gio, GLib

NAME = "org.gnome.SessionManager"
ROOT = "/org/gnome/SessionManager"


def introspect(bus, path):
    reply = bus.call_sync(NAME, path, "org.freedesktop.DBus.Introspectable", "Introspect",
                          None, GLib.VariantType("(s)"), Gio.DBusCallFlags.NONE, 5000, None)
    return reply.unpack()[0]


def children(xml, prefix):
    return sorted((n for n in re.findall(r'<node name="([^"]+)"', xml) if n.startswith(prefix)),
                  key=lambda n: int(re.sub(r"\D", "", n) or 0))


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: capture-sm-abi.py OUTDIR")
    out = pathlib.Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
    manager = bus.call_sync(NAME, ROOT, NAME, "IsSessionRunning", None, None,
                            Gio.DBusCallFlags.NONE, 5000, None)
    if not manager.unpack()[0]:
        sys.exit("the session manager is not running a session")
    cookie = bus.call_sync(NAME, ROOT, NAME, "Inhibit",
                           GLib.Variant("(susu)", ("polo-abi-capture", 0, "ABI capture", 8)),
                           GLib.VariantType("(u)"), Gio.DBusCallFlags.NONE, 5000, None).unpack()[0]
    try:
        root = introspect(bus, ROOT)
        (out / "manager.xml").write_text(root)
        (out / "presence.xml").write_text(introspect(bus, ROOT + "/Presence"))
        inhibitors = bus.call_sync(NAME, ROOT, NAME, "GetInhibitors", None,
                                   GLib.VariantType("(ao)"), Gio.DBusCallFlags.NONE, 5000,
                                   None).unpack()[0]
        clients = bus.call_sync(NAME, ROOT, NAME, "GetClients", None, GLib.VariantType("(ao)"),
                                Gio.DBusCallFlags.NONE, 5000, None).unpack()[0]
        if not inhibitors or not clients:
            sys.exit("no inhibitor or no client to introspect")
        (out / "inhibitor.xml").write_text(introspect(bus, inhibitors[-1]))
        (out / "client.xml").write_text(introspect(bus, clients[0]))
        apps = children(root, "App")
        if apps:
            (out / "app.xml").write_text(introspect(bus, f"{ROOT}/{apps[0]}"))
        else:
            print("no /App objects are exported", file=sys.stderr)
        print(f"{out}: manager, presence, inhibitor {inhibitors[-1]}, client {clients[0]}, "
              f"app {apps[0] if apps else 'none'}")
    finally:
        bus.call_sync(NAME, ROOT, NAME, "Uninhibit", GLib.Variant("(u)", (cookie,)), None,
                      Gio.DBusCallFlags.NONE, 5000, None)


if __name__ == "__main__":
    main()
