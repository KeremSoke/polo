#!/usr/bin/env python3
# Copyright 2026 Kerem Soke
# SPDX-License-Identifier: GPL-3.0-or-later
"""Real X programs on a headless Polo: xterm, xeyes (shape), a GTK 3 window, wmctrl and
xdotool. A program that is not installed is skipped; nothing run is left out silently.

  smoke.py POLO POLO-INPUT
"""

import os
import shutil
import subprocess
import sys
import tempfile
import time

GTK_WINDOW = """
import gi
gi.require_version("Gtk", "3.0")
from gi.repository import Gtk
w = Gtk.Window(title="smoke-gtk")
w.add(Gtk.Label(label="Hello"))
w.connect("destroy", Gtk.main_quit)
w.show_all()
Gtk.main()
"""

if not shutil.which("Xwayland"):
    sys.exit(77)
if not shutil.which("wmctrl"):
    print("wmctrl is needed to read the results")
    sys.exit(77)


class Smoke:
    def __init__(self, polo, polo_input):
        self.tmp = tempfile.mkdtemp(prefix="polo-smoke-")
        env = dict(os.environ, TMPDIR=self.tmp, GSETTINGS_BACKEND="memory",
                   POLO_TEST_WIDTH="--width=1920", POLO_TEST_HEIGHT="--height=1080")
        env.pop("DISPLAY", None)
        env.pop("XAUTHORITY", None)
        self.log = open(os.path.join(self.tmp, "polo.log"), "w")
        self.input = subprocess.Popen([polo_input, polo], env=env, text=True,
                                      stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                      stderr=self.log)
        display, xauth = self.input.stdout.readline().split()
        self.env = dict(os.environ, DISPLAY=display, XAUTHORITY=xauth, GDK_BACKEND="x11",
                        GSETTINGS_BACKEND="memory", NO_AT_BRIDGE="1")
        for name in ("WAYLAND_DISPLAY", "WAYLAND_SOCKET", "DBUS_SESSION_BUS_ADDRESS"):
            self.env.pop(name, None)
        self.clients = []

    def run(self, *argv):
        return subprocess.run(argv, env=self.env, capture_output=True, text=True,
                              timeout=20).stdout

    def start(self, *argv):
        client = subprocess.Popen(argv, env=self.env, stdout=subprocess.DEVNULL,
                                  stderr=subprocess.DEVNULL)
        self.clients.append(client)
        return client

    def send(self, command):
        self.input.stdin.write(command + "\n")
        self.input.stdin.flush()
        assert self.input.stdout.readline().strip() == "ok"

    def listed(self, title):
        return [line for line in self.run("wmctrl", "-l", "-G").splitlines() if title in line]

    def wait_listed(self, title, present=True):
        for _ in range(250):
            if bool(self.listed(title)) == present:
                return
            time.sleep(0.04)
        raise AssertionError(f"{title} {'never listed' if present else 'never left'}: "
                             + self.run("wmctrl", "-l"))

    def geometry(self, title):
        fields = self.listed(title)[0].split()
        return [int(v) for v in fields[2:6]]

    def stop(self):
        for client in self.clients:
            if client.poll() is None:
                client.kill()
                client.wait()
        self.input.stdin.close()
        status = self.input.wait(15)
        assert status == 0, f"polo-input exited {status}"
        shutil.rmtree(self.tmp, ignore_errors=True)


def wait_for(check, message):
    for _ in range(100):
        if check():
            return
        time.sleep(0.05)
    raise AssertionError(message)


def test_desktops(s):
    wait_for(lambda: len(s.run("wmctrl", "-d").splitlines()) == 4, "wmctrl does not list 4 desktops")


def test_xterm(s):
    s.start("xterm", "-T", "smoke-xterm", "-geometry", "80x24+100+100", "-e", "sleep", "100")
    s.wait_listed("smoke-xterm")
    s.run("wmctrl", "-r", "smoke-xterm", "-b", "add,maximized_vert,maximized_horz")
    wait_for(lambda: s.geometry("smoke-xterm")[2] >= 1900, "xterm did not maximize: "
             + str(s.listed("smoke-xterm")))
    s.run("wmctrl", "-r", "smoke-xterm", "-b", "remove,maximized_vert,maximized_horz")
    wait_for(lambda: s.geometry("smoke-xterm")[2] < 1900, "xterm did not unmaximize")
    s.run("wmctrl", "-r", "smoke-xterm", "-t", "2")
    wait_for(lambda: s.listed("smoke-xterm")[0].split()[1] == "2", "xterm did not change desktop")
    s.run("wmctrl", "-r", "smoke-xterm", "-t", "0")
    s.run("wmctrl", "-c", "smoke-xterm")
    s.wait_listed("smoke-xterm", False)


def test_xeyes(s):
    client = s.start("xeyes", "-geometry", "150x100")
    s.wait_listed("xeyes")
    if shutil.which("xdotool"):
        found = s.run("xdotool", "search", "--name", "^xeyes$").split()
        assert found, "xdotool does not find xeyes"
        s.run("xdotool", "windowsize", found[0], "300", "200")
        wait_for(lambda: s.geometry("xeyes")[2:] == [300, 200],
                 f"xdotool did not resize xeyes: {s.listed('xeyes')}")
    client.kill()
    client.wait()
    s.wait_listed("xeyes", False)


def test_gtk(s):
    client = s.start(sys.executable, "-c", GTK_WINDOW)
    s.wait_listed("smoke-gtk")
    s.run("wmctrl", "-a", "smoke-gtk")
    active = s.run("xprop", "-root", "_NET_ACTIVE_WINDOW") if shutil.which("xprop") else ""
    wanted = int(s.listed("smoke-gtk")[0].split()[0], 16)
    assert not active or f"{wanted:#x}" in active, active
    client.kill()
    client.wait()


def test_drag_back(s):
    """X must end where the window is drawn when a drag doubles back faster than X answers."""
    s.start("xterm", "-T", "smoke-drag", "-geometry", "40x10+300+300", "-e", "sleep", "100")
    s.wait_listed("smoke-drag")
    time.sleep(0.5)
    x, y, width, height = s.geometry("smoke-drag")
    grab_x, grab_y = x + width // 2, y + height // 2
    s.send(f"move {grab_x} {grab_y}")
    s.send("key 56 1")
    s.send("button 272 1")
    for dx in (40, 80, 120):
        s.send(f"move {grab_x + dx} {grab_y}")
    time.sleep(0.3)
    for _ in range(20):
        s.send(f"back {grab_x + 120} {grab_y}")
    time.sleep(0.5)
    s.send("button 272 0")
    s.send("key 56 0")
    time.sleep(0.3)
    assert s.geometry("smoke-drag")[0] == x + 120, \
        f"X says {s.geometry('smoke-drag')[0]}, the drag ended at {x + 120}"
    s.run("wmctrl", "-c", "smoke-drag")
    s.wait_listed("smoke-drag", False)


TESTS = [(test_desktops, "wmctrl"), (test_xterm, "xterm"), (test_xeyes, "xeyes"),
         (test_gtk, "python3"), (test_drag_back, "xterm")]


def main():
    s = Smoke(sys.argv[1], sys.argv[2])
    try:
        for test, program in TESTS:
            if not shutil.which(program):
                print(f"skip {test.__name__}: no {program}")
                continue
            test(s)
            print(f"ok   {test.__name__}", flush=True)
    finally:
        s.log.flush()
        if sys.exc_info()[0]:
            print(open(s.log.name, errors="replace").read()[-800:], file=sys.stderr)
    s.stop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
