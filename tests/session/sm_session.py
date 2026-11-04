# Copyright 2026 Kerem Soke
# SPDX-License-Identifier: GPL-3.0-or-later
"""polo --session on a private bus, for the session manager tests."""

import os
import shutil
import subprocess
import sys
import tempfile
import time

from gi.repository import Gio, GLib

NAME = "org.gnome.SessionManager"
ROOT = "/org/gnome/SessionManager"


def on_private_bus(marker):
    """Re-runs this script under its own dbus-run-session; returns its exit status,
    or None when already there."""
    if marker in os.environ:
        return None
    if not shutil.which("dbus-run-session"):
        return 77
    return subprocess.call(["dbus-run-session", "--", sys.executable, *sys.argv],
                           env=dict(os.environ, **{marker: "1"}))


class Polo:
    """Headless polo --session with no autostart and no Xwayland."""

    def __init__(self, polo):
        self.tmp = tempfile.mkdtemp(prefix="polo-sm-")
        for sub in ("run", "data", "none", "config"):
            os.makedirs(f"{self.tmp}/{sub}", mode=0o700, exist_ok=True)
        env = dict(os.environ, HOME=self.tmp, XDG_CONFIG_HOME=self.tmp + "/config",
                   XDG_CONFIG_DIRS=self.tmp + "/none", XDG_DATA_DIRS=self.tmp + "/data",
                   XDG_DATA_HOME=self.tmp + "/none", XDG_RUNTIME_DIR=self.tmp + "/run",
                   GSETTINGS_BACKEND="memory", POLO_SHELL_UI="")
        for key in ("WAYLAND_DISPLAY", "WAYLAND_SOCKET", "DISPLAY", "XAUTHORITY", "DCONF_PROFILE",
                    "POLO_TEST_AUTOSTART"):
            env.pop(key, None)
        self.stderr_path = self.tmp + "/stderr"
        self.stderr = open(self.stderr_path, "w")
        self.process = subprocess.Popen(
            [polo, "--session", "--backend=headless", "--renderer=pixman", "--test-mode",
             "--xwayland=off", "--socket=polo-sm-test", f"--log={self.tmp}/polo.log"],
            env=env, stdout=subprocess.DEVNULL, stderr=self.stderr)
        self.bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)

    def wait_owned(self):
        for _ in range(400):
            try:
                self.bus.call_sync("org.freedesktop.DBus", "/org/freedesktop/DBus",
                                   "org.freedesktop.DBus", "GetNameOwner",
                                   GLib.Variant("(s)", (NAME,)), GLib.VariantType("(s)"),
                                   Gio.DBusCallFlags.NONE, 1000, None)
                return True
            except GLib.Error:
                if self.process.poll() is not None:
                    return False
                time.sleep(0.025)
        return False

    def tail(self, lines=200):
        """The end of Polo's log and stderr, where sanitizers report."""
        text = []
        for path in (self.tmp + "/polo.log", self.stderr_path):
            try:
                with open(path, errors="replace") as f:
                    text += f.read().splitlines()[-lines:]
            except OSError:
                pass
        return "\n".join(text)

    def stop(self):
        if self.process.poll() is None:
            self.process.terminate()
        try:
            status = self.process.wait(15)
        except subprocess.TimeoutExpired:
            self.process.kill()
            status = self.process.wait()
        self.stderr.close()
        if status != 0:
            print(f"polo exited with {status}:\n{self.tail()}")
        shutil.rmtree(self.tmp, ignore_errors=True)
        return status
