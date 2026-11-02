#!/usr/bin/env python3
# Copyright 2026 Kerem Soke
# SPDX-License-Identifier: GPL-3.0-or-later
"""polo --session: refuses a bus with a session manager or none at all, and otherwise
starts eager XWayland, trusts what it launches and takes the dconf profile. Sets the session
environment and tells D-Bus activation, a fake systemd and a fake GDM on a private bus.

  test-session.py POLO systemd|no-systemd
"""

import configparser
import glob
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time

WITH_SYSTEMD = sys.argv[2] == "systemd"

OWNER = """
import gi, sys
from gi.repository import Gio, GLib
loop = GLib.MainLoop()
Gio.bus_own_name(Gio.BusType.SESSION, "org.gnome.SessionManager", 0, None,
                 lambda *a: (print("owned", flush=True)), None)
loop.run()
"""


FAKES = """
import sys
from gi.repository import Gio, GLib
out = open(sys.argv[1], "a", buffering=1)
XML = \"\"\"<node>
<interface name="org.freedesktop.systemd1.Manager">
<method name="SetEnvironment"><arg type="as" direction="in"/></method>
<method name="UnsetEnvironment"><arg type="as" direction="in"/></method>
<method name="StartUnit"><arg type="s" direction="in"/><arg type="s" direction="in"/><arg type="o" direction="out"/></method>
<method name="StopUnit"><arg type="s" direction="in"/><arg type="s" direction="in"/><arg type="o" direction="out"/></method>
</interface>
<interface name="org.gnome.DisplayManager.Manager">
<method name="RegisterSession"><arg type="a{sv}" direction="in"/></method>
</interface></node>\"\"\"
info = Gio.DBusNodeInfo.new_for_xml(XML)
def call(conn, sender, path, iface, method, params, inv):
    if method.endswith("Unit"):
        out.write(method + " " + " ".join(params.unpack()) + "\\n")
        inv.return_value(GLib.Variant("(o)", ("/job/1",)))
        return
    out.write(method + " " + " ".join(params.unpack()[0]) + "\\n")
    inv.return_value(None)
bus = Gio.bus_get_sync(Gio.BusType.SESSION)
bus.register_object("/org/freedesktop/systemd1", info.interfaces[0], call)
bus.register_object("/org/gnome/DisplayManager/Manager", info.interfaces[1], call)
names = ["org.freedesktop.systemd1", "org.gnome.DisplayManager"]
owned = []
def got(*a):
    owned.append(1)
    if len(owned) == 2:
        print("ready", flush=True)
for name in names:
    Gio.bus_own_name_on_connection(bus, name, 0, got, None)
GLib.MainLoop().run()
"""


def target_unit():
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        "../../data/systemd/polo-session.target")
    parser = configparser.ConfigParser(strict=False, interpolation=None)
    parser.optionxform = str
    parser.read(path)
    unit = parser["Unit"]
    check(unit["BindsTo"] == "graphical-session.target", "no BindsTo")
    check(unit["PropagatesStopTo"] == "graphical-session.target",
          "units pinning the graphical session outlive it")
    check(unit["Before"] == "graphical-session.target xdg-desktop-portal.service",
          "wrong Before: a target waits for what it Wants, and the portal is after the session")
    check(unit["Wants"] == "xdg-desktop-portal.service", "no portal")
    with open(path) as f:
        check("autostart" not in f.read(), "the target pulls systemd's autostart")


def read_env(path):
    with open(path) as f:
        return dict(line.split("=", 1) for line in f.read().splitlines() if "=" in line)


def wait_for(condition):
    for _ in range(400):
        if condition():
            return True
        time.sleep(0.025)
    return False


def environment(polo):
    tmp = tempfile.mkdtemp(prefix="polo-session-")
    records = tmp + "/calls"
    child_file = tmp + "/child-env"
    fakes = subprocess.Popen([sys.executable, "-c", FAKES, records], stdout=subprocess.PIPE,
                             text=True)
    process = None
    try:
        check(fakes.stdout.readline().strip() == "ready", "the fake services did not start")
        env = dict(base_env(tmp), GDK_BACKEND="x11", GTK_MODULES="first-module", XDG_MENU_PREFIX="old-",
                   DBUS_SYSTEM_BUS_ADDRESS=os.environ["DBUS_SESSION_BUS_ADDRESS"],
                   POLO_TEST_SETTINGS="org.mate.interface/accessibility=true")
        command = f"sh -c 'env > {child_file}.tmp; mv {child_file}.tmp {child_file}; exec sleep 3'"
        process = subprocess.Popen(polo_args(polo, "--exec", command), env=env,
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        check(wait_for(lambda: os.path.exists(child_file)), "the child did not run")
        child = read_env(child_file)
        for name, value in (("XDG_CURRENT_DESKTOP", "MATE"), ("XDG_SESSION_DESKTOP", "mate"),
                            ("XDG_SESSION_TYPE", "wayland"), ("DESKTOP_SESSION", "mate"),
                            ("XDG_MENU_PREFIX", "mate-"),
                            ("MATE_DESKTOP_SESSION_ID", "this-is-deprecated"),
                            ("QT_QPA_PLATFORM", "wayland;xcb"), ("MOZ_ENABLE_WAYLAND", "1"),
                            ("WAYLAND_DISPLAY", "polo-session-test"),
                            ("GTK_MODULES", "first-module:gail:atk-bridge")):
            check(child.get(name) == value, f"{name} is {child.get(name)!r} in a child")
        check("GDK_BACKEND" not in child, "GDK_BACKEND stayed set")
        check(child.get("XCURSOR_THEME") == "default", "an empty cursor theme was exported")
        for name in ("DISPLAY", "XAUTHORITY", "XCURSOR_THEME", "XCURSOR_SIZE"):
            check(child.get(name), f"{name} is not set in a child")

        def calls():
            with open(records) as f:
                return f.read()

        if not WITH_SYSTEMD:
            check(wait_for(lambda: "RegisterSession" in calls()), "GDM was not told")
            check("Environment" not in calls() and "Unit" not in calls(),
                  f"systemd was told without -Dsystemd: {calls()!r}")
            process.terminate()
            check(process.wait(15) == 0, "polo did not exit cleanly")
            check("Environment" not in calls() and "Unit" not in calls(),
                  f"systemd was told on exit without -Dsystemd: {calls()!r}")
            return

        check(wait_for(lambda: "StartUnit" in calls() and "RegisterSession" in calls()),
              f"systemd or GDM was not told: {calls()!r}")
        sent = calls()
        check("StartUnit polo-session.target replace" in sent, f"wrong start: {sent!r}")
        check(sent.index("SetEnvironment") < sent.index("StartUnit"),
              "the target started before the environment import")
        check("XDG_CURRENT_DESKTOP=MATE" in sent and "WAYLAND_DISPLAY=polo-session-test" in sent and
              f"DISPLAY={child['DISPLAY']}" in sent, f"systemd got {sent!r}")
        check("WAYLAND_SOCKET" not in sent, "a per-child socket went to systemd")

        probe = os.environ["POLO_TEST_PROBE_FILE"]
        starter = subprocess.Popen(["gdbus", "call", "--session", "--dest", "org.freedesktop.DBus",
                                    "--object-path", "/org/freedesktop/DBus", "--method",
                                    "org.freedesktop.DBus.StartServiceByName",
                                    "org.mate.PoloTestProbe", "0"],
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        found = wait_for(lambda: os.path.exists(probe))
        time.sleep(0.2)
        starter.kill()
        starter.wait()
        check(found, "the probe service did not start")
        activated = read_env(probe)
        check(activated.get("XDG_MENU_PREFIX") == "mate-" and
              activated.get("WAYLAND_DISPLAY") == "polo-session-test",
              "D-Bus activation did not get the environment")

        process.terminate()
        check(process.wait(15) == 0, "polo did not exit cleanly")
        unset = [line for line in calls().splitlines() if line.startswith("UnsetEnvironment")]
        check(unset and "WAYLAND_DISPLAY" in unset[0] and "XDG_CURRENT_DESKTOP" in unset[0],
              f"systemd's environment was not cleaned up: {unset}")
        check("StopUnit polo-session.target replace" in calls(), "the target was not stopped")
        check(calls().index("StopUnit") < calls().index("UnsetEnvironment"),
              "the environment went before the target stopped")

        before = calls()
        plain_file = tmp + "/plain-env"
        args = [a for a in polo_args(polo, "--exec", f"sh -c 'env > {plain_file}.tmp; mv {plain_file}.tmp {plain_file}'") if a != "--session"]
        process = subprocess.Popen(args, env=env, stdout=subprocess.DEVNULL,
                                   stderr=subprocess.DEVNULL)
        check(wait_for(lambda: os.path.exists(plain_file)), "the plain child did not run")
        plain = read_env(plain_file)
        check(plain.get("XDG_MENU_PREFIX") == "old-" and plain.get("GDK_BACKEND") == "x11",
              "a plain polo changed the environment")
        process.terminate()
        process.wait(15)
        check(calls() == before, "a plain polo told systemd or GDM")
    finally:
        if process is not None and process.poll() is None:
            process.kill()
            process.wait()
        fakes.kill()
        fakes.wait()
        shutil.rmtree(tmp, ignore_errors=True)


def base_env(tmp):
    env = dict(os.environ, GSETTINGS_BACKEND="memory", POLO_SHELL_UI="", XDG_RUNTIME_DIR=tmp)
    for key in ("WAYLAND_DISPLAY", "WAYLAND_SOCKET", "DISPLAY", "XAUTHORITY", "DCONF_PROFILE",
                "POLO_XWAYLAND", "GTK_MODULES", "GDK_BACKEND"):
        env.pop(key, None)
    return env


def polo_args(polo, *extra):
    return [polo, "--session", "--backend=headless", "--renderer=pixman", "--test-mode",
            "--socket=polo-session-test", *extra]


def check(condition, message):
    if not condition:
        print("check failed:", message)
        sys.exit(1)


def refused_with_owner(polo):
    tmp = tempfile.mkdtemp(prefix="polo-session-")
    owner = subprocess.Popen([sys.executable, "-c", OWNER], stdout=subprocess.PIPE, text=True)
    try:
        check(owner.stdout.readline().strip() == "owned", "could not own the name")
        run = subprocess.run(polo_args(polo), env=base_env(tmp), capture_output=True, text=True,
                             timeout=20)
        check(run.returncode != 0, "started with another session manager running")
        check("another session manager" in run.stderr, f"no error shown: {run.stderr!r}")
        check(not os.path.exists(tmp + "/polo-session-test"), "it made a socket first")

        # Only session mode cares.
        plain = [a for a in polo_args(polo) if a != "--session"]
        process = subprocess.Popen(plain, env=base_env(tmp), stdout=subprocess.DEVNULL,
                                   stderr=subprocess.DEVNULL)
        for _ in range(200):
            if os.path.exists(tmp + "/polo-session-test"):
                break
            time.sleep(0.025)
        check(process.poll() is None and os.path.exists(tmp + "/polo-session-test"),
              "a plain polo was refused too")
        process.terminate()
        process.wait(10)
    finally:
        owner.kill()
        owner.wait()
        shutil.rmtree(tmp, ignore_errors=True)


def refused_with_no_bus(polo):
    tmp = tempfile.mkdtemp(prefix="polo-session-")
    env = dict(base_env(tmp), DBUS_SESSION_BUS_ADDRESS=f"unix:path={tmp}/no-bus")
    run = subprocess.run(polo_args(polo), env=env, capture_output=True, text=True, timeout=20)
    check(run.returncode != 0, "started with no session bus")
    check("session bus" in run.stderr, f"no error shown: {run.stderr!r}")
    shutil.rmtree(tmp, ignore_errors=True)


def x_lock_of(pid):
    for path in glob.glob("/tmp/.X*-lock"):
        try:
            with open(path) as lock:
                if int(lock.read().strip()) == pid:
                    return path
        except (OSError, ValueError):
            pass
    return None


def starts(polo):
    tmp = tempfile.mkdtemp(prefix="polo-session-")
    env_file = tmp + "/child-env"
    log = tmp + "/polo.log"
    env = dict(base_env(tmp), POLO_TEST_DCONF_PROFILE="polo-test-profile")
    process = subprocess.Popen(
        polo_args(polo, f"--log={log}", "--exec", f"sh -c 'env > {env_file}.tmp; mv {env_file}.tmp {env_file}; exec sleep 3'"),
        env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        lock = None
        for _ in range(400):
            lock = x_lock_of(process.pid)
            if lock and os.path.exists(env_file):
                break
            time.sleep(0.025)
        check(lock, "XWayland is not eager: no X display")
        check(os.path.exists(env_file), "the --exec child did not run")
        with open(env_file) as f:
            child = f.read()
        check("DCONF_PROFILE=polo-test-profile" in child.splitlines(),
              "the profile did not reach the child")
        for _ in range(100):
            with open(log, errors="replace") as f:
                text = f.read()
            if "trusted" in text:
                break
            time.sleep(0.05)
        check(", trusted)" in text and "WAYLAND_SOCKET=" in child,
              "a command of the session was not started trusted")
    finally:
        process.terminate()
        status = process.wait(15)
        shutil.rmtree(tmp, ignore_errors=True)
    check(status == 0, f"polo exited with {status}")

    # --xwayland wins over the session's default.
    tmp = tempfile.mkdtemp(prefix="polo-session-")
    process = subprocess.Popen(polo_args(polo, "--xwayland=off"), env=base_env(tmp),
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for _ in range(200):
        if os.path.exists(tmp + "/polo-session-test"):
            break
        time.sleep(0.025)
    time.sleep(0.3)
    check(not x_lock_of(process.pid), "--xwayland=off started X")
    process.terminate()
    process.wait(10)
    shutil.rmtree(tmp, ignore_errors=True)


def desktop_entry():
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "data",
                        "polo.desktop")
    with open(path) as f:
        text = f.read()
    for line in ("Name=MATE (Polo)", "Exec=polo --session", "DesktopNames=MATE",
                 "X-GDM-SessionRegisters=true"):
        check(line + "\n" in text, f"polo.desktop lacks {line}")
    if shutil.which("desktop-file-validate"):
        run = subprocess.run(["desktop-file-validate", path], capture_output=True, text=True)
        # Display managers read DesktopNames, which the validator does not know.
        errors = [line for line in run.stdout.splitlines()
                  if "error:" in line and '"DesktopNames"' not in line]
        check(not errors, errors)


def wrapper(polo):
    script = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "data",
                          "polo-wayland.sh")
    tmp = tempfile.mkdtemp(prefix="polo-wrapper-")
    try:
        for name, body in (("polo", 'echo "polo $*" >> "$WRAPPER_LOG"\n'),
                           ("dbus-run-session", 'echo "bus $*" >> "$WRAPPER_LOG"\n')):
            with open(f"{tmp}/{name}", "w") as f:
                f.write("#!/bin/sh\n" + body)
            os.chmod(f"{tmp}/{name}", 0o755)
        log = tmp + "/log"

        def run(**extra):
            if os.path.exists(log):
                os.unlink(log)
            env = {"PATH": f"{tmp}:/usr/bin:/bin", "WRAPPER_LOG": log, "XDG_RUNTIME_DIR": tmp, **extra}
            subprocess.check_call(["sh", script, "--extra"], env=env, timeout=10)
            with open(log) as f:
                return f.read().strip()

        check(run(DBUS_SESSION_BUS_ADDRESS="unix:path=/x") == "polo --session --extra",
              "the wrapper did not run polo --session on the bus it was given")
        check(run() == "bus -- polo --session --extra", "the wrapper started polo with no bus")
        server = socket.socket(socket.AF_UNIX)
        server.bind(tmp + "/bus")
        check(run() == "polo --session --extra", "the wrapper ignored the user bus")
        server.close()
        check(os.access(script, os.X_OK), "polo-wayland.sh is not executable")

        real = tmp + "/real"
        os.makedirs(real)
        os.symlink(os.path.abspath(polo), real + "/polo")
        env = dict(base_env(tmp), PATH=f"{real}:{os.environ['PATH']}")
        process = subprocess.Popen([script, "--backend=headless", "--renderer=pixman", "--test-mode",
                                    "--socket=polo-wrapper-test"], env=env,
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        check(wait_for(lambda: os.path.exists(tmp + "/polo-wrapper-test")), "polo did not start")
        process.terminate()
        check(process.wait(15) == 0, "the wrapper did not hand over to polo")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def main():
    if "POLO_SESSION_TEST_BUS" not in os.environ:
        if not shutil.which("dbus-run-session"):
            return 77
        tmp = tempfile.mkdtemp(prefix="polo-session-bus-")
        probe = tmp + "/probe-env"
        os.makedirs(tmp + "/services")
        with open(tmp + "/services/org.mate.PoloTestProbe.service", "w") as f:
            f.write("[D-BUS Service]\nName=org.mate.PoloTestProbe\n"
                    f"Exec=/bin/sh -c \"env > {probe}\"\n")
        with open(tmp + "/bus.conf", "w") as f:
            f.write('<busconfig><include>/usr/share/dbus-1/session.conf</include>'
                    f'<servicedir>{tmp}/services</servicedir></busconfig>')
        env = dict(os.environ, POLO_SESSION_TEST_BUS="1", POLO_TEST_PROBE_FILE=probe)
        status = subprocess.call(["dbus-run-session", f"--config-file={tmp}/bus.conf", "--",
                                  sys.executable, *sys.argv], env=env)
        shutil.rmtree(tmp, ignore_errors=True)
        return status
    polo = sys.argv[1]
    desktop_entry()
    refused_with_no_bus(polo)
    refused_with_owner(polo)
    starts(polo)
    environment(polo)
    target_unit()
    wrapper(polo)
    print("ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
