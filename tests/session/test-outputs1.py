#!/usr/bin/env python3
# Copyright 2026 Kerem Soke
# SPDX-License-Identifier: GPL-3.0-or-later
"""org.mate.Polo.Outputs1 on two managed headless outputs.

  test-outputs1.py POLO FAKE_SHELL_UI

GetOutputs, Serial and Changed; a temporary ApplyConfiguration; stale,
unknown and impossible requests refused; and a persistent one confirmed
with mate-settings-daemon's dialog: Restore reverts it, Keep keeps it.
"""

import pathlib
import sys
import tempfile
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
try:
    from gi.repository import Gio, GLib
    import sm_session
except ImportError:
    sys.exit(77)

NAME, PATH, INTERFACE = "org.mate.Polo", "/org/mate/Polo", "org.mate.Polo.Outputs1"
CANCEL, ACCEPT = -6, -3
failures = []


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


class Session:
    def __init__(self, polo, fake_ui, response):
        self.dir = pathlib.Path(tempfile.mkdtemp(prefix="polo-outputs1-"))
        self.ui_log = self.dir / "ui"
        self.polo = sm_session.Polo(polo, shell_ui=fake_ui, env_extra={
            "POLO_TEST_MANAGED_OUTPUTS": "1",
            "FAKE_SHELL_UI_LOG": str(self.ui_log), "FAKE_SHELL_UI_DIALOG_RESPONSE": str(response)}, outputs=2)
        if not self.polo.wait_owned():
            raise RuntimeError("polo never owned org.gnome.SessionManager")
        self.bus = self.polo.bus
        self.changes = []
        self.bus.signal_subscribe(NAME, INTERFACE, "Changed", PATH, None, Gio.DBusSignalFlags.NONE,
                                  lambda c, s, p, i, n, params: self.changes.append(params.unpack()[0]))
        if not until(lambda: self.owned() and self.ui_log.exists() and "bound" in self.ui_log.read_text()):
            raise RuntimeError(f"{NAME} or polo-shell-ui never came up")

    def owned(self):
        reply = self.bus.call_sync("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
                                   "NameHasOwner", GLib.Variant("(s)", (NAME,)), GLib.VariantType("(b)"),
                                   Gio.DBusCallFlags.NONE, 5000, None)
        return reply.unpack()[0]

    def outputs(self):
        return self.bus.call_sync(NAME, PATH, INTERFACE, "GetOutputs", None, GLib.VariantType("(aa{sv})"),
                                  Gio.DBusCallFlags.NONE, 5000, None).unpack()[0]

    def serial(self):
        return self.bus.call_sync(NAME, PATH, "org.freedesktop.DBus.Properties", "Get",
                                  GLib.Variant("(ss)", (INTERFACE, "Serial")), GLib.VariantType("(v)"),
                                  Gio.DBusCallFlags.NONE, 5000, None).unpack()[0]

    def apply(self, serial, outputs, persistent):
        requests = [{key: GLib.Variant(kind, value) for key, (kind, value) in o.items()} for o in outputs]
        try:
            self.bus.call_sync(NAME, PATH, INTERFACE, "ApplyConfiguration",
                               GLib.Variant("(uaa{sv}b)", (serial, requests, persistent)), None,
                               Gio.DBusCallFlags.NONE, 10000, None)
        except GLib.Error as e:
            return Gio.DBusError.get_remote_error(e)
        return None

    def second_x(self):
        return self.outputs()[1]["x"]

    def dialogs(self):
        return [line for line in self.ui_log.read_text().splitlines() if line.startswith("show_dialog")]

    def close(self):
        self.polo.stop()


def enable(output, x):
    width, height, rate, _ = output["modes"][0]
    return {"name": ("s", output["name"]), "enabled": ("b", True), "width": ("i", width),
            "height": ("i", height), "refresh": ("i", rate), "x": ("i", x), "y": ("i", 0)}


def restore_run(polo, fake_ui):
    s = Session(polo, fake_ui, CANCEL)
    try:
        outputs = s.outputs()
        check(len(outputs) == 2, f"GetOutputs gave {len(outputs)} outputs")
        for o in outputs:
            check(o["name"] and o["modes"] and o["scale"] == 1, f"GetOutputs: {o.get('name')} is incomplete: {o}")
        first, second = outputs
        target = first["width"] + 100
        serial = s.serial()
        check(s.apply(serial, [enable(second, target)], False) is None,
              "a temporary configuration was refused")
        check(until(lambda: s.second_x() == target), "ApplyConfiguration did not move the output")
        check(until(lambda: s.serial() > serial and s.changes), "no Changed and no new Serial")
        check(s.apply(serial, [{"name": ("s", second["name"]), "x": ("i", 0)}], False) ==
              INTERFACE + ".Error.Stale", "a stale serial was accepted")
        check(s.apply(s.serial(), [{"name": ("s", "nowhere")}], False) == INTERFACE + ".Error.Invalid",
              "an unknown output was accepted")
        check(s.apply(s.serial(), [{"name": ("s", o["name"]), "enabled": ("b", False)} for o in outputs], False) ==
              INTERFACE + ".Error.Failed", "every output off was accepted")
        check(s.second_x() == target, "a refused configuration moved the output")

        moved = target + 200
        check(s.apply(s.serial(), [{"name": ("s", second["name"]), "x": ("i", moved)}], True) is None,
              "a persistent configuration was refused")
        check(until(lambda: any(" 4 " in line and line.rstrip().endswith("30") for line in s.dialogs())),
              f"no 30 s display confirmation: {s.dialogs()}")
        check(until(lambda: s.second_x() == target), "Restore Previous Configuration did not revert")
    finally:
        s.close()


def keep_run(polo, fake_ui):
    s = Session(polo, fake_ui, ACCEPT)
    try:
        first, second = s.outputs()
        target = first["width"] + 300
        check(s.apply(s.serial(), [enable(second, target)], True) is None,
              "a persistent configuration was refused")
        check(until(lambda: s.dialogs()), "no display confirmation")
        time.sleep(0.5)
        check(s.second_x() == target, "Keep This Configuration did not keep it")
    finally:
        s.close()


def main():
    status = sm_session.on_private_bus("POLO_OUTPUTS1_BUS")
    if status is not None:
        return status
    for run in (restore_run, keep_run):
        try:
            run(sys.argv[1], sys.argv[2])
        except (RuntimeError, GLib.Error) as e:
            failures.append(f"{run.__name__}: {e}")
    for line in failures:
        print(line)
    print(f"{len(failures)} failure(s)" if failures else "ok")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
