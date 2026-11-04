#!/usr/bin/env python3
# Copyright 2026 Kerem Soke
# SPDX-License-Identifier: GPL-3.0-or-later
"""Polo against a fake org.freedesktop.login1 on a bus of its own.

  test-logind.py POLO FAKE_SHELL_UI

Polo in test mode reaches logind only on POLO_TEST_SYSTEM_BUS, so nothing
here can touch the real system.
"""

import os
import pathlib
import select
import shutil
import subprocess
import sys
import tempfile
import time

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
try:
    from gi.repository import Gio, GLib
    import sm_session
except ImportError:
    sys.exit(77)

NAME, ROOT = sm_session.NAME, sm_session.ROOT
LOGIND = "org.freedesktop.login1"
LOGIND_PATH = "/org/freedesktop/login1"
SESSION_PATH = LOGIND_PATH + "/session/polo_test"
SLEEP = 5
failures = []

XML = """
<node>
  <interface name="org.freedesktop.login1.Manager">
    <method name="CanPowerOff"><arg type="s" direction="out"/></method>
    <method name="CanReboot"><arg type="s" direction="out"/></method>
    <method name="CanSuspend"><arg type="s" direction="out"/></method>
    <method name="CanHibernate"><arg type="s" direction="out"/></method>
    <method name="CanHybridSleep"><arg type="s" direction="out"/></method>
    <method name="PowerOff"><arg type="b" direction="in"/></method>
    <method name="Reboot"><arg type="b" direction="in"/></method>
    <method name="Suspend"><arg type="b" direction="in"/></method>
    <method name="Hibernate"><arg type="b" direction="in"/></method>
    <method name="GetSession"><arg type="s" direction="in"/><arg type="o" direction="out"/></method>
    <method name="GetSessionByPID"><arg type="u" direction="in"/><arg type="o" direction="out"/></method>
    <method name="Inhibit">
      <arg type="s" direction="in"/><arg type="s" direction="in"/>
      <arg type="s" direction="in"/><arg type="s" direction="in"/>
      <arg type="h" direction="out"/>
    </method>
    <signal name="PrepareForSleep"><arg type="b"/></signal>
  </interface>
  <interface name="org.freedesktop.login1.Session">
    <property name="Active" type="b" access="read"/>
    <method name="SetIdleHint"><arg type="b" direction="in"/></method>
    <signal name="Lock"/>
    <signal name="Unlock"/>
  </interface>
  <interface name="org.freedesktop.login1.Seat">
    <property name="CanMultiSession" type="b" access="read"/>
  </interface>
</node>
"""


def check(condition, message):
    if not condition:
        failures.append(message)


def until(condition, seconds=10):
    context = GLib.MainContext.default()
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        while context.pending():
            context.iteration(False)
        if condition():
            return True
        time.sleep(0.02)
    return False


class FakeLogind:
    def __init__(self, address):
        self.calls = []
        self.inhibitors = []
        self.active = True
        self.bus = Gio.DBusConnection.new_for_address_sync(
            address, Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT |
            Gio.DBusConnectionFlags.MESSAGE_BUS_CONNECTION, None, None)
        node = Gio.DBusNodeInfo.new_for_xml(XML)
        self.bus.register_object(LOGIND_PATH, node.lookup_interface(LOGIND + ".Manager"), self.on_call, None, None)
        self.bus.register_object(SESSION_PATH, node.lookup_interface(LOGIND + ".Session"), self.on_call,
                                 self.on_get, None)
        self.bus.register_object(LOGIND_PATH + "/seat/auto", node.lookup_interface(LOGIND + ".Seat"), None,
                                 self.on_get, None)
        Gio.bus_own_name_on_connection(self.bus, LOGIND, Gio.BusNameOwnerFlags.NONE, None, None)

    def on_get(self, connection, sender, path, interface, prop):
        if prop == "Active":
            return GLib.Variant("b", self.active)
        return GLib.Variant("b", True)

    def on_call(self, connection, sender, path, interface, method, parameters, invocation):
        self.calls.append((method, parameters.unpack()))
        if method.startswith("Can"):
            invocation.return_value(GLib.Variant("(s)", ("yes",)))
        elif method in ("GetSession", "GetSessionByPID"):
            invocation.return_value(GLib.Variant("(o)", (SESSION_PATH,)))
        elif method == "Inhibit":
            read_end, write_end = os.pipe()
            self.inhibitors.append((parameters.unpack(), read_end))
            invocation.return_value_with_unix_fd_list(GLib.Variant("(h)", (0,)),
                                                      Gio.UnixFDList.new_from_array([write_end]))
        else:
            invocation.return_value(None)

    def called(self, method, args=None):
        return any(m == method and (args is None or a == args) for m, a in self.calls)

    def emit(self, path, interface, name, parameters=None):
        self.bus.emit_signal(None, path, interface, name, parameters)

    def set_active(self, active):
        self.active = active
        changed = GLib.Variant("(sa{sv}as)", (LOGIND + ".Session", {"Active": GLib.Variant("b", active)}, []))
        self.emit(SESSION_PATH, "org.freedesktop.DBus.Properties", "PropertiesChanged", changed)


def released(read_end):
    ready, _, _ = select.select([read_end], [], [], 0)
    return bool(ready) and os.read(read_end, 1) == b""


def call(bus, method, args=None, reply="()"):
    result = []
    bus.call(NAME, ROOT, NAME, method, args, GLib.VariantType(reply), Gio.DBusCallFlags.NONE, 10000, None,
             lambda source, res: result.append(source.call_finish(res).unpack()))
    if not until(lambda: result, 15):
        raise RuntimeError(f"{method} did not answer")
    return result[0]


def schemas(tmp):
    os.makedirs(tmp + "/schemas")
    with open(tmp + "/schemas/org.mate.screensaver.gschema.xml", "w") as f:
        f.write('<schemalist><schema id="org.mate.screensaver" path="/org/mate/screensaver/">'
                '<key name="lock-enabled" type="b"><default>true</default></key>'
                '</schema></schemalist>')
    subprocess.check_call(["glib-compile-schemas", tmp + "/schemas"])
    return tmp + "/schemas"


class FakeScreenSaver:
    XML = '<node><interface name="org.mate.ScreenSaver"><method name="Lock"/></interface></node>'

    def __init__(self, bus):
        self.locks = 0
        node = Gio.DBusNodeInfo.new_for_xml(self.XML)
        self.bus = bus
        self.object = bus.register_object("/org/mate/ScreenSaver", node.interfaces[0], self.on_call, None, None)
        self.name = Gio.bus_own_name_on_connection(bus, "org.mate.ScreenSaver", Gio.BusNameOwnerFlags.NONE,
                                                   None, None)

    def on_call(self, connection, sender, path, interface, method, parameters, invocation):
        self.locks += 1
        invocation.return_value(None)

    def close(self):
        Gio.bus_unown_name(self.name)
        self.bus.unregister_object(self.object)


def run(polo, fake_ui, address, tmp):
    fake = FakeLogind(address)
    screensaver = FakeScreenSaver(Gio.bus_get_sync(Gio.BusType.SESSION, None))
    ui_log = tmp + "/ui"
    session = sm_session.Polo(polo, shell_ui=fake_ui, env_extra={
        "POLO_TEST_SYSTEM_BUS": address, "GSETTINGS_SCHEMA_DIR": schemas(tmp),
        "FAKE_SHELL_UI_LOG": ui_log, "FAKE_SHELL_UI_DIALOG_RESPONSE": str(SLEEP)})
    try:
        if not session.wait_owned():
            raise RuntimeError(f"polo never owned {NAME}")
        check(until(lambda: fake.inhibitors), "Polo took no sleep inhibitor")
        if fake.inhibitors:
            args, read_end = fake.inhibitors[0]
            check(args[0] == "sleep" and args[3] == "delay", f"the sleep inhibitor is not a sleep delay: {args}")
        check(fake.called("GetSession") or fake.called("GetSessionByPID"), "Polo did not look up its session")
        check(call(session.bus, "CanShutdown", reply="(b)") == (True,), "CanShutdown did not ask logind")

        fake.emit(LOGIND_PATH, LOGIND + ".Manager", "PrepareForSleep", GLib.Variant("(b)", (True,)))
        check(until(lambda: screensaver.locks == 1), "PrepareForSleep did not lock the screen")
        if fake.inhibitors:
            check(until(lambda: released(fake.inhibitors[0][1]), 5), "the sleep inhibitor outlived the lock")
        fake.emit(LOGIND_PATH, LOGIND + ".Manager", "PrepareForSleep", GLib.Variant("(b)", (False,)))
        check(until(lambda: len(fake.inhibitors) == 2), "Polo did not take the sleep inhibitor again on resume")

        fake.emit(SESSION_PATH, LOGIND + ".Session", "Lock")
        check(until(lambda: screensaver.locks == 2), "the session's Lock signal did not lock the screen")
        fake.set_active(False)
        check(until(lambda: "the session is inactive" in session.tail()), "Polo did not see the session go inactive")
        fake.set_active(True)

        call(session.bus, "Shutdown")
        check(until(lambda: fake.called("Suspend", (True,))), "Suspend from the shutdown dialog did not reach logind")
        dialogs = [line for line in pathlib.Path(ui_log).read_text().splitlines() if line.startswith("show_dialog")]
        check(dialogs and "'suspend'" in dialogs[0] and "'shutdown'" in dialogs[0],
              f"the shutdown dialog does not offer what logind can do: {dialogs}")

        call(session.bus, "RequestShutdown")
        check(until(lambda: fake.called("PowerOff", (True,)), 15), "RequestShutdown did not power off through logind")
        check(until(lambda: session.process.poll() is not None, 15) and session.process.returncode == 0,
              f"polo did not end the session: {session.process.poll()}")
    finally:
        status = session.stop()
        screensaver.close()
        if failures:
            print("logind calls:", fake.calls)
    return status


def main():
    status = sm_session.on_private_bus("POLO_LOGIND_BUS")
    if status is not None:
        return status
    if not shutil.which("dbus-daemon") or not shutil.which("glib-compile-schemas"):
        return 77
    tmp = tempfile.mkdtemp(prefix="polo-logind-")
    daemon = subprocess.Popen(["dbus-daemon", "--session", "--nofork", "--print-address"],
                              stdout=subprocess.PIPE, text=True)
    try:
        address = daemon.stdout.readline().strip()
        run(sys.argv[1], sys.argv[2], address, tmp)
    except RuntimeError as e:
        failures.append(str(e))
    finally:
        daemon.terminate()
        daemon.wait()
        shutil.rmtree(tmp, ignore_errors=True)
    for line in failures:
        print(line)
    print(f"{len(failures)} failure(s)" if failures else "ok")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
