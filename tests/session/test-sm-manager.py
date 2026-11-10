#!/usr/bin/env python3
# Copyright 2026 Kerem Soke
# SPDX-License-Identifier: GPL-3.0-or-later
"""org.gnome.SessionManager's methods with gsm-manager.c 1.28's semantics.

  test-sm-manager.py POLO

Polo runs without autostart, so its session is Running from the start.
"""

import os
import pathlib
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
try:
    from gi.repository import Gio, GLib
    import sm_session
except ImportError:
    sys.exit(77)

NAME, ROOT = sm_session.NAME, sm_session.ROOT
ERROR = "org.gnome.SessionManager."
failures = []


def check(condition, message):
    if not condition:
        failures.append(message)


def call(bus, method, args=None, reply="()"):
    return bus.call_sync(NAME, ROOT, NAME, method, args, GLib.VariantType(reply),
                         Gio.DBusCallFlags.NONE, 5000, None).unpack()


def error_of(bus, method, args=None, reply="()"):
    try:
        call(bus, method, args, reply)
    except GLib.Error as e:
        return Gio.DBusError.get_remote_error(e)
    return None


class Signals:
    def __init__(self, bus):
        self.seen = []
        bus.signal_subscribe(NAME, NAME, None, ROOT, None, Gio.DBusSignalFlags.NONE,
                             lambda c, s, p, i, name, params: self.seen.append((name, params.unpack())))

    def wait(self, name, value=None):
        context = GLib.MainContext.default()
        end = time.monotonic() + 5
        while time.monotonic() < end:
            while context.pending():
                context.iteration(False)
            for seen in self.seen:
                if seen[0] == name and (value is None or seen[1] == value):
                    return True
            time.sleep(0.01)
        return False


def other_connection():
    address = Gio.dbus_address_get_for_bus_sync(Gio.BusType.SESSION, None)
    return Gio.DBusConnection.new_for_address_sync(
        address, Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT |
        Gio.DBusConnectionFlags.MESSAGE_BUS_CONNECTION, None, None)


def phases(bus):
    check(call(bus, "IsSessionRunning", reply="(b)") == (True,), "the session is not Running")
    check(error_of(bus, "Setenv", GLib.Variant("(ss)", ("POLO_X", "1"))) == ERROR + "NotInInitialization",
          "Setenv was accepted after the Initialization phase")
    check(error_of(bus, "InitializationError", GLib.Variant("(sb)", ("x", False))) ==
          ERROR + "NotInInitialization", "InitializationError was accepted after Initialization")
    check(error_of(bus, "Logout", GLib.Variant("(u)", (7,))) == ERROR + "InvalidOption",
          "Logout took an unknown mode")
    check(call(bus, "IsAutostartConditionHandled", GLib.Variant("(s)", ("GSettings x y",)),
               reply="(b)") == (False,), "an unknown autostart condition is handled")
    check(len(call(bus, "CanShutdown", reply="(b)")) == 1, "CanShutdown gave no answer")
    renderer = bus.call_sync(NAME, ROOT, "org.freedesktop.DBus.Properties", "Get",
                             GLib.Variant("(ss)", (NAME, "Renderer")), GLib.VariantType("(v)"),
                             Gio.DBusCallFlags.NONE, 5000, None).unpack()[0]
    check(isinstance(renderer, str), "Renderer is not a string")


def inhibitors(bus, signals):
    for args, what in ((("", 0, "r", 8), "no app id"), (("a", 0, "", 8), "no reason"),
                       (("a", 0, "r", 0), "no flags")):
        check(error_of(bus, "Inhibit", GLib.Variant("(susu)", args), "(u)") == ERROR + "GeneralError",
              f"Inhibit with {what} was accepted")
    cookie = call(bus, "Inhibit", GLib.Variant("(susu)", ("polo-test", 42, "testing", 4 | 8)), "(u)")[0]
    check(cookie > 0, "Inhibit gave cookie 0")
    listed = call(bus, "GetInhibitors", reply="(ao)")[0]
    check(len(listed) == 1 and signals.wait("InhibitorAdded", (listed[0],)),
          "the inhibitor was not listed and announced")
    inhibitor = NAME + ".Inhibitor"
    if listed:
        path = listed[0]
        check(client_call(bus, path, inhibitor, "GetAppId", reply="(s)") == ("polo-test",), "GetAppId")
        check(client_call(bus, path, inhibitor, "GetReason", reply="(s)") == ("testing",), "GetReason")
        check(client_call(bus, path, inhibitor, "GetFlags", reply="(u)") == (12,), "GetFlags")
        check(client_call(bus, path, inhibitor, "GetToplevelXid", reply="(u)") == (42,), "GetToplevelXid")
        check(client_error(bus, path, inhibitor, "GetClientId") == inhibitor + ".NotSet",
              "a D-Bus inhibitor has a client id")
    check(call(bus, "IsInhibited", GLib.Variant("(u)", (8,)), "(b)") == (True,), "idle is not inhibited")
    check(call(bus, "IsInhibited", GLib.Variant("(u)", (1,)), "(b)") == (False,), "logout is inhibited")
    check(error_of(bus, "Uninhibit", GLib.Variant("(u)", (cookie ^ 1,))) == ERROR + "GeneralError",
          "Uninhibit took an unknown cookie")
    call(bus, "Uninhibit", GLib.Variant("(u)", (cookie,)))
    check(signals.wait("InhibitorRemoved", (listed[0],)), "InhibitorRemoved was not sent")
    for args, what in ((("a", 0, "r", 32), "an unknown flag"), (("a" * 1025, 0, "r", 8), "a 1025 byte app id"),
                       (("a", 0, "r" * 4097, 8), "a 4097 byte reason")):
        check(error_of(bus, "Inhibit", GLib.Variant("(susu)", args), "(u)") == ERROR + "GeneralError",
              f"Inhibit with {what} was accepted")
    cookies = [call(bus, "Inhibit", GLib.Variant("(susu)", ("polo-test", 0, "many", 8)), "(u)")[0]
               for _ in range(64)]
    check(error_of(bus, "Inhibit", GLib.Variant("(susu)", ("polo-test", 0, "one more", 8)), "(u)") ==
          ERROR + "GeneralError", "a connection held more than 64 inhibitors")
    for held in cookies:
        call(bus, "Uninhibit", GLib.Variant("(u)", (held,)))
    call(bus, "Uninhibit", GLib.Variant("(u)", (call(bus, "Inhibit", GLib.Variant("(susu)", ("polo-test", 0, "again", 8)), "(u)")[0],)))
    check(call(bus, "IsInhibited", GLib.Variant("(u)", (8,)), "(b)") == (False,),
          "idle is still inhibited after Uninhibit")

    other = other_connection()
    call(other, "Inhibit", GLib.Variant("(susu)", ("polo-test", 0, "gone", 1)), "(u)")
    path = call(bus, "GetInhibitors", reply="(ao)")[0][0]
    other.close_sync(None)
    check(signals.wait("InhibitorRemoved", (path,)), "an inhibitor outlived its connection")
    check(call(bus, "IsInhibited", GLib.Variant("(u)", (1,)), "(b)") == (False,),
          "a vanished connection still inhibits logout")


def client_call(bus, path, interface, method, args=None, reply="()"):
    return bus.call_sync(NAME, path, interface, method, args, GLib.VariantType(reply),
                         Gio.DBusCallFlags.NONE, 5000, None).unpack()


def client_error(bus, path, interface, method, args=None):
    try:
        client_call(bus, path, interface, method, args)
    except GLib.Error as e:
        return Gio.DBusError.get_remote_error(e)
    return None


def clients(bus, signals):
    other = other_connection()
    path = call(other, "RegisterClient", GLib.Variant("(ss)", ("polo-test", "")), "(o)")[0]
    check(path.startswith(ROOT + "/Client"), f"RegisterClient gave {path}")
    check(signals.wait("ClientAdded", (path,)), "ClientAdded was not sent")
    check(path in call(bus, "GetClients", reply="(ao)")[0], "GetClients does not list the client")

    client = NAME + ".Client"
    private = NAME + ".ClientPrivate"
    check(client_call(bus, path, client, "GetAppId", reply="(s)") == ("polo-test",), "GetAppId")
    startup_id = client_call(bus, path, client, "GetStartupId", reply="(s)")[0]
    check(startup_id.startswith("10") and len(startup_id) > 30,
          f"a generated startup id should look like gsm's, not {startup_id}")
    check(client_call(bus, path, client, "GetRestartStyleHint", reply="(u)") == (0,),
          "a D-Bus client's restart style is not never")
    check(client_call(bus, path, client, "GetUnixProcessId", reply="(u)") == (os.getpid(),),
          "GetUnixProcessId is not the client's process")
    check(client_call(bus, path, client, "GetStatus", reply="(u)") == (1,), "the client is not registered")

    check(client_error(bus, path, private, "EndSessionResponse", GLib.Variant("(bs)", (True, ""))) ==
          "org.freedesktop.DBus.Error.Failed", "EndSessionResponse was taken from another connection")
    client_call(other, path, private, "EndSessionResponse", GLib.Variant("(bs)", (True, "")))

    stopped = []
    other.signal_subscribe(None, private, "Stop", path, None, Gio.DBusSignalFlags.NONE,
                           lambda *args: stopped.append(True))
    client_call(bus, path, client, "Stop")
    context = GLib.MainContext.default()
    end = time.monotonic() + 5
    while not stopped and time.monotonic() < end:
        context.iteration(False)
        time.sleep(0.01)
    check(stopped, "Stop did not reach the client")

    call(other, "RegisterClient", GLib.Variant("(ss)", ("polo-test", "10fixed")), "(o)")
    check(error_of(bus, "RegisterClient", GLib.Variant("(ss)", ("polo-test", "10fixed")), "(o)") ==
          ERROR + "AlreadyRegistered", "two clients registered with one startup id")
    check(error_of(bus, "UnregisterClient", GLib.Variant("(o)", (ROOT + "/Client999",))) ==
          ERROR + "NotRegistered", "UnregisterClient took an unknown client")
    call(other, "UnregisterClient", GLib.Variant("(o)", (path,)))
    check(path in call(bus, "GetClients", reply="(ao)")[0],
          "UnregisterClient dropped the client before it left the bus")
    check(client_call(bus, path, client, "GetStatus", reply="(u)") == (0,),
          "an unregistered client is still registered")
    other.close_sync(None)
    check(signals.wait("ClientRemoved", (path,)), "a client outlived its connection")
    check(client_error(bus, path, client, "GetStatus") is not None, "a removed client is still exported")


def presence(bus):
    path, interface = ROOT + "/Presence", NAME + ".Presence"
    properties = "org.freedesktop.DBus.Properties"
    seen = []
    bus.signal_subscribe(NAME, interface, None, path, None, Gio.DBusSignalFlags.NONE,
                         lambda c, s, p, i, name, params: seen.append((name, params.unpack())))

    def wait(name, value):
        context = GLib.MainContext.default()
        end = time.monotonic() + 5
        while time.monotonic() < end:
            while context.pending():
                context.iteration(False)
            if (name, value) in seen:
                return True
            time.sleep(0.01)
        return False

    def get(prop):
        return client_call(bus, path, properties, "Get", GLib.Variant("(ss)", (interface, prop)), "(v)")[0]

    check(get("status") == 0, "the session does not start available")
    client_call(bus, path, interface, "SetStatus", GLib.Variant("(u)", (2,)))
    check(wait("StatusChanged", (2,)) and get("status") == 2, "SetStatus did not make the session busy")
    client_call(bus, path, interface, "SetStatusText", GLib.Variant("(s)", ("away",)))
    check(wait("StatusTextChanged", ("away",)) and get("status-text") == "away", "SetStatusText")
    check(client_error(bus, path, interface, "SetStatusText", GLib.Variant("(s)", ("x" * 141,))) ==
          interface + ".GeneralError", "a status text over 140 bytes was accepted")
    check(get("status-text") == "away", "a refused status text replaced the old one")
    check(client_error(bus, path, interface, "SetStatus", GLib.Variant("(u)", (4,))) ==
          "org.freedesktop.DBus.Error.InvalidArgs", "an unknown status was accepted")
    check(get("status") == 2, "an unknown status changed the status")
    client_call(bus, path, properties, "Set",
                GLib.Variant("(ssv)", (interface, "status", GLib.Variant("u", 0))))
    check(wait("StatusChanged", (0,)) and get("status") == 0, "setting the status property")


def logout(bus, signals, polo):
    call(bus, "Logout", GLib.Variant("(u)", (1,)))
    check(signals.wait("SessionOver"), "SessionOver was not sent")
    try:
        status = polo.process.wait(15)
    except Exception:
        status = None
    check(status == 0, f"polo did not end the session: {status}")


def main():
    status = sm_session.on_private_bus("POLO_SM_MANAGER_BUS")
    if status is not None:
        return status
    polo = sm_session.Polo(sys.argv[1])
    try:
        if not polo.wait_owned():
            print(f"polo never owned {NAME}")
            return 1
        bus = polo.bus
        signals = Signals(bus)
        phases(bus)
        inhibitors(bus, signals)
        clients(bus, signals)
        presence(bus)
        logout(bus, signals, polo)
    finally:
        polo.stop()
    for line in failures:
        print(line)
    print(f"{len(failures)} failure(s)" if failures else "ok")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
