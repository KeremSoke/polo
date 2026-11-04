#!/usr/bin/env python3
# Copyright 2026 Kerem Soke
# SPDX-License-Identifier: GPL-3.0-or-later
"""org.gnome.SessionManager as mate-session 1.28 exports it, on Polo.

  test-sm-abi.py POLO FIXTURES

FIXTURES/source holds mate-session-manager 1.28.0's interface XML,
FIXTURES/live what a running X MATE 1.28 session introspected
(tools/capture-sm-abi.py). The live session omits signal argument names, so
the source is the reference; the live capture must agree with it otherwise.
Then Polo's manager, Presence, a client, an inhibitor and an app must have
the reference's methods, signals, properties, argument names and types.
Polo's own additions belong under org.mate.Polo.* only.
"""

import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import sm_abi  # noqa: E402

try:
    from gi.repository import Gio, GLib
except ImportError:
    sys.exit(77)

NAME = "org.gnome.SessionManager"
ROOT = "/org/gnome/SessionManager"
ALLOWED_EXTRA = ("org.freedesktop.DBus.", "org.mate.Polo.")


def load(directory):
    result = {}
    for path in sorted(directory.glob("*.xml")):
        result.update(sm_abi.interfaces(path.read_text()))
    return result


def check_fixtures(source, live):
    problems = []
    for name, members in live.items():
        if name not in source:
            problems.append(f"live {name} is not in the source XML")
            continue
        expected = {}
        for member, value in source[name].items():
            got = members.get(member)
            if member.startswith("signal ") and got and all(n == "" for n, _, _ in got):
                value = tuple(("", t, d) for _, t, d in value)
            expected[member] = value
        problems += sm_abi.differences(expected, members, f"live {name}")
    return problems


def call(bus, path, interface, method, args=None, reply="()"):
    return bus.call_sync(NAME, path, interface, method, args, GLib.VariantType(reply),
                         Gio.DBusCallFlags.NONE, 5000, None).unpack()


def introspect(bus, path):
    return call(bus, path, "org.freedesktop.DBus.Introspectable", "Introspect", reply="(s)")[0]


def check_object(bus, path, expected_names, reference, label):
    try:
        got = sm_abi.interfaces(introspect(bus, path))
    except GLib.Error as e:
        return [f"{label}: cannot introspect {path}: {e.message}"]
    problems = []
    for name in expected_names:
        if name not in got:
            problems.append(f"{label}: {path} lacks {name}")
        else:
            problems += sm_abi.differences(reference[name], got[name], name)
    for name in got:
        if name not in expected_names and not name.startswith(ALLOWED_EXTRA):
            problems.append(f"{label}: {path} has {name}; additions go under org.mate.Polo.*")
    return problems


def check_polo(bus, reference):
    problems = check_object(bus, ROOT, [NAME], reference, "manager")
    problems += check_object(bus, ROOT + "/Presence", [NAME + ".Presence"], reference, "presence")
    try:
        client = call(bus, ROOT, NAME, "RegisterClient",
                      GLib.Variant("(ss)", ("polo-abi-test", "")), "(o)")[0]
        problems += check_object(bus, client, [NAME + ".Client", NAME + ".ClientPrivate"],
                                 reference, "client")
    except GLib.Error as e:
        problems.append(f"client: RegisterClient failed: {e.message}")
    try:
        call(bus, ROOT, NAME, "Inhibit", GLib.Variant("(susu)", ("polo-abi-test", 0, "test", 8)),
             "(u)")
        inhibitors = call(bus, ROOT, NAME, "GetInhibitors", reply="(ao)")[0]
        if inhibitors:
            problems += check_object(bus, inhibitors[-1], [NAME + ".Inhibitor"], reference,
                                     "inhibitor")
        else:
            problems.append("inhibitor: GetInhibitors lists none after Inhibit")
    except GLib.Error as e:
        problems.append(f"inhibitor: Inhibit failed: {e.message}")
    apps = re.findall(r'<node name="(App[0-9]+)"', introspect(bus, ROOT))
    if apps:
        problems += check_object(bus, f"{ROOT}/{apps[0]}", [NAME + ".App"], reference, "app")
    else:
        problems.append(f"app: no {ROOT}/App%u object")
    return problems


def start_polo(polo, tmp):
    for sub in ("run", "data", "none", "config"):
        os.makedirs(f"{tmp}/{sub}", mode=0o700, exist_ok=True)
    env = dict(os.environ, HOME=tmp, XDG_CONFIG_HOME=tmp + "/config",
               XDG_CONFIG_DIRS=tmp + "/none", XDG_DATA_DIRS=tmp + "/data",
               XDG_DATA_HOME=tmp + "/none", XDG_RUNTIME_DIR=tmp + "/run",
               GSETTINGS_BACKEND="memory", POLO_SHELL_UI="")
    for key in ("WAYLAND_DISPLAY", "WAYLAND_SOCKET", "DISPLAY", "XAUTHORITY", "DCONF_PROFILE",
                "POLO_TEST_AUTOSTART"):
        env.pop(key, None)
    return subprocess.Popen([polo, "--session", "--backend=headless", "--renderer=pixman",
                             "--test-mode", "--xwayland=off", "--socket=polo-sm-abi-test",
                             f"--log={tmp}/polo.log"],
                            env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: test-sm-abi.py POLO FIXTURES")
    if "POLO_SM_ABI_BUS" not in os.environ:
        if not shutil.which("dbus-run-session"):
            return 77
        return subprocess.call(["dbus-run-session", "--", sys.executable, *sys.argv],
                               env=dict(os.environ, POLO_SM_ABI_BUS="1"))
    polo, fixtures = sys.argv[1], pathlib.Path(sys.argv[2])
    source = load(fixtures / "source")
    live = load(fixtures / "live")
    problems = check_fixtures(source, live)

    tmp = tempfile.mkdtemp(prefix="polo-sm-abi-")
    process = start_polo(polo, tmp)
    try:
        bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
        owner = None
        for _ in range(400):
            try:
                owner = bus.call_sync("org.freedesktop.DBus", "/org/freedesktop/DBus",
                                      "org.freedesktop.DBus", "GetNameOwner",
                                      GLib.Variant("(s)", (NAME,)), GLib.VariantType("(s)"),
                                      Gio.DBusCallFlags.NONE, 1000, None)
                break
            except GLib.Error:
                if process.poll() is not None:
                    break
                time.sleep(0.025)
        if owner is None:
            problems.append(f"polo never owned {NAME}")
        else:
            problems += check_polo(bus, source)
    finally:
        process.terminate()
        try:
            process.wait(15)
        except subprocess.TimeoutExpired:
            process.kill()
        shutil.rmtree(tmp, ignore_errors=True)

    for line in problems:
        print(line)
    print(f"{len(problems)} difference(s)" if problems else "ok")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
