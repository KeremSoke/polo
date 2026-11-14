#!/usr/bin/env python3
# Copyright 2026 Kerem Soke
# SPDX-License-Identifier: GPL-3.0-or-later
"""org.mate.InputDevices1 from polo --session on headless, which has no
libinput devices.

  test-input-devices.py POLO

Polo owns the name, GetDevices answers aa{sv} (empty here) and introspects as
documented, and an unknown method is refused.
"""

import pathlib
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
try:
    from gi.repository import Gio, GLib
    import sm_session
except ImportError:
    sys.exit(77)

NAME, PATH, INTERFACE = "org.mate.InputDevices", "/org/mate/InputDevices", "org.mate.InputDevices1"
failures = []


def check(condition, message):
    if not condition:
        failures.append(message)


def owned(bus):
    try:
        bus.call_sync("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
                      "GetNameOwner", GLib.Variant("(s)", (NAME,)), GLib.VariantType("(s)"),
                      Gio.DBusCallFlags.NONE, 1000, None)
        return True
    except GLib.Error:
        return False


def main():
    status = sm_session.on_private_bus("POLO_INPUT_DEVICES_TEST")
    if status is not None:
        return status
    polo = sm_session.Polo(sys.argv[1])
    try:
        if not polo.wait_owned():
            print("polo never owned org.gnome.SessionManager")
            return 1
        for _ in range(200):
            if owned(polo.bus):
                break
            time.sleep(0.025)
        check(owned(polo.bus), f"{NAME} is not owned")

        reply = polo.bus.call_sync(NAME, PATH, INTERFACE, "GetDevices", None, None,
                                   Gio.DBusCallFlags.NO_AUTO_START, 2000, None)
        check(reply.get_type_string() == "(aa{sv})", f"GetDevices returned {reply.get_type_string()}")
        check(reply.unpack() == ([],), f"headless lists devices: {reply.unpack()}")

        xml = polo.bus.call_sync(NAME, PATH, "org.freedesktop.DBus.Introspectable", "Introspect", None,
                                 GLib.VariantType("(s)"), Gio.DBusCallFlags.NONE, 2000, None).unpack()[0]
        node = Gio.DBusNodeInfo.new_for_xml(xml)
        interface = node.lookup_interface(INTERFACE)
        method = interface.lookup_method("GetDevices") if interface else None
        check(method is not None and [a.signature for a in method.out_args] == ["aa{sv}"],
              "GetDevices is not introspected as () -> aa{sv}")

        try:
            polo.bus.call_sync(NAME, PATH, INTERFACE, "Nonsense", None, None, Gio.DBusCallFlags.NONE, 2000,
                               None)
            check(False, "an unknown method was answered")
        except GLib.Error:
            pass
    finally:
        stopped = polo.stop()
    check(stopped == 0, f"polo exited with {stopped}")
    for failure in failures:
        print("FAIL:", failure)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
