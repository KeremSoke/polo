#!/usr/bin/env python3
# Copyright 2026 Kerem Soke
# SPDX-License-Identifier: GPL-3.0-or-later
"""Real applications on a headless Polo under GL, read from their own Wayland traces: the
window maps, takes keyboard focus, maximizes with Alt+F10 and goes fullscreen with its own key,
with no protocol error. Each runs with a private home directory and D-Bus session. An application
that is not installed is reported and skipped.

  matrix.py POLO POLO-INPUT

Exits 77 unless POLO_APP_MATRIX is set, as a run takes minutes.
"""

import json
import glob
import os
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time

GTK3 = """
import gi
gi.require_version("Gtk", "3.0")
from gi.repository import Gtk, Gdk
w = Gtk.Window(title="matrix")
w.add(Gtk.Entry())
w.connect("key-press-event", lambda _, e: e.keyval == Gdk.KEY_F11 and (w.fullscreen(), True)[1])
w.show_all()
Gtk.main()
"""

GTK4 = """
import gi
gi.require_version("Gtk", "4.0")
from gi.repository import Gtk, Gdk, GLib
w = Gtk.Window(title="matrix")
w.set_child(Gtk.Entry())
c = Gtk.EventControllerKey()
c.connect("key-pressed", lambda _, k, *a: k == Gdk.KEY_F11 and (w.fullscreen(), True)[1])
w.add_controller(c)
w.present()
GLib.MainLoop().run()
"""

QML = """
import QtQuick 2.0
Rectangle { width: 300; height: 200; color: "white" }
"""

CTRL, ALT, SHIFT, F10, F11, J, F = 29, 56, 42, 68, 87, 36, 33

# name, program, command, environment, fullscreen keys, click first
APPS = [
    ("gtk3-demo", "gtk3-demo", ["gtk3-demo"], {}, None, False),
    ("GTK 3 window", "python3", [sys.executable, "-c", GTK3], {}, [F11], False),
    ("GTK 4 window", "python3", [sys.executable, "-c", GTK4], {}, [F11], False),
    ("gtk4-demo", "gtk4-demo", ["gtk4-demo"], {}, None, False),
    ("kate (Qt 6)", "kate", ["kate", "-n"], {"QT_QPA_PLATFORM": "wayland"}, [F11], False),
    ("qmlscene", "qmlscene", ["qmlscene", "PROFILE/matrix.qml"],
     {"QT_QPA_PLATFORM": "wayland"}, None, False),
    ("Firefox", "firefox",
     ["firefox", "--no-remote", "--new-instance", "--profile", "PROFILE", "about:blank"],
     {"MOZ_ENABLE_WAYLAND": "1"}, [F11], True),
    ("Chromium", "chromium-browser",
     ["chromium-browser", "--ozone-platform=wayland", "--user-data-dir=PROFILE",
      "--no-first-run", "--disable-gpu", "about:blank"], {}, [F11], False),
    ("VS Code", "/usr/share/code/code",
     ["/usr/share/code/code", "--ozone-platform=wayland", "--user-data-dir=PROFILE",
      "--extensions-dir=PROFILE/ext", "--disable-gpu", "--new-window"],
     {"ELECTRON_RUN_AS_NODE": None, "VSCODE_PID": None, "VSCODE_IPC_HOOK": None,
      "VSCODE_IPC_HOOK_CLI": None}, [F11], False),
    ("LibreOffice", "libreoffice",
     ["libreoffice", "--norestore", "--nologo", "-env:UserInstallation=file://PROFILE", "--writer"],
     {"SAL_USE_VCLPLUGIN": "gtk3"}, [CTRL, SHIFT, J], False),
    ("mpv", "mpv", ["mpv", "--idle", "--force-window", "--vo=gpu", "--gpu-context=wayland"], {},
     [F], False),
    ("SuperTux (SDL)", "supertux2", ["supertux2"], {"SDL_VIDEODRIVER": "wayland"}, [F11], False),
]
NOT_SCRIPTED = ["Steam (XWayland)", "OBS"]


def chord(send, keys):
    for key in keys:
        send(f"key {key} 1")
        time.sleep(0.05)
    for key in reversed(keys):
        send(f"key {key} 0")
        time.sleep(0.05)


def passed(result):
    return (result["mapped"] and result["focused"] and result["maximized"] and
            result["fullscreen"] is not False and not result["protocol_errors"] and
            result["alive"] and result["polo_exit"] == 0)


def run(polo, polo_input, name, command, extra, fullscreen_keys, click_first):
    tmp = tempfile.mkdtemp(prefix="pmx-", dir="/tmp")
    home, profile = tmp + "/h", tmp + "/p"
    os.makedirs(home)
    os.makedirs(profile)
    with open(profile + "/matrix.qml", "w") as qml:
        qml.write(QML)
    env = dict(os.environ, GSETTINGS_BACKEND="memory", POLO_SHELL_UI="", NO_AT_BRIDGE="1",
               TMPDIR=tmp, HOME=home, XDG_CONFIG_HOME=home + "/.config",
               XDG_DATA_HOME=home + "/.local/share", XDG_CACHE_HOME=home + "/.cache",
               POLO_TEST_RENDERER="--renderer=gl", LIBGL_ALWAYS_SOFTWARE="1")
    for key in ("DISPLAY", "XAUTHORITY", "WAYLAND_DISPLAY", "GDK_BACKEND", "QT_QPA_PLATFORM",
                "GTK_IM_MODULE", "QT_IM_MODULE"):
        env.pop(key, None)
    polo_log = open(tmp + "/polo.log", "w")
    driver = subprocess.Popen([polo_input, polo], env=env, text=True, stdin=subprocess.PIPE,
                              stdout=subprocess.PIPE, stderr=polo_log)
    driver.stdout.readline()

    def send(line):
        driver.stdin.write(line + "\n")
        driver.stdin.flush()
        driver.stdout.readline()

    env["XDG_RUNTIME_DIR"] = glob.glob(tmp + "/polo-test-*")[0]
    env["WAYLAND_DISPLAY"] = f"polo-test-{driver.pid}"
    env["WAYLAND_DEBUG"] = "1"
    for key, value in extra.items():
        if value is None:
            env.pop(key, None)
        else:
            env[key] = value
    log = open(tmp + "/app.log", "w")
    app = subprocess.Popen([part.replace("PROFILE", profile) for part in command], env=env,
                           stdout=log, stderr=log, start_new_session=True, cwd=tmp)

    def trace():
        return open(tmp + "/app.log", errors="replace").read()

    def full_sized(width, height):
        return any(int(w) >= width and int(h) >= height for w, h in
                   re.findall(r"xdg_toplevel#\d+\.configure\((\d+), (\d+),", trace()))

    mapped = False
    deadline = time.monotonic() + 45
    while time.monotonic() < deadline and not mapped:
        text = trace()
        mapped = bool(re.search(r"xdg_surface#\d+\.get_toplevel", text) and
                      re.search(r"-> wl_surface#\d+\.attach\(wl_buffer", text))
        if app.poll() is not None and not mapped:
            break
        time.sleep(0.5)
    time.sleep(6)
    send("move 512 384")
    send("move 516 388")
    time.sleep(0.3)
    focused = bool(re.search(r"wl_keyboard#\d+\.enter", trace()))
    chord(send, [ALT, F10])
    time.sleep(3)
    maximized = full_sized(1000, 700)
    fullscreen = None
    if fullscreen_keys:
        if click_first:
            send("button 272 1")
            send("button 272 0")
            time.sleep(1)
        chord(send, fullscreen_keys)
        time.sleep(5)
        fullscreen = "set_fullscreen" in trace()
    text = trace()
    errors = re.findall(r"[^\n]*wl_display#1\.error\([^\n]*", text)
    alive = app.poll() is None and driver.poll() is None
    for sig in (signal.SIGTERM, signal.SIGKILL):
        try:
            os.killpg(app.pid, sig)
        except ProcessLookupError:
            pass
        time.sleep(1.5)
    driver.stdin.close()
    try:
        status = driver.wait(12)
    except subprocess.TimeoutExpired:
        driver.kill()
        status = "stuck"
    result = {"app": name, "mapped": mapped, "focused": focused, "maximized": maximized,
              "fullscreen": fullscreen, "protocol_errors": len(errors),
              "first_error": errors[0][:200] if errors else "", "alive": alive,
              "polo_exit": status}
    log.close()
    polo_log.close()
    if not passed(result):
        for filename in ("app.log", "polo.log"):
            print(f"--- {name}: {filename} ---", file=sys.stderr)
            with open(tmp + "/" + filename, errors="replace") as output:
                print(output.read(), file=sys.stderr)
    shutil.rmtree(tmp, ignore_errors=True)
    return result


def main():
    if not os.environ.get("POLO_APP_MATRIX"):
        print("set POLO_APP_MATRIX to run the application matrix")
        return 77
    if "POLO_MATRIX_BUS" not in os.environ:
        env = dict(os.environ, POLO_MATRIX_BUS="1", GSETTINGS_BACKEND="memory")
        env.pop("DBUS_SESSION_BUS_ADDRESS", None)
        return subprocess.call(["dbus-run-session", "--", sys.executable, *sys.argv], env=env)
    failed = 0
    only = sys.argv[3:]
    for name, program, command, extra, keys, click in APPS:
        if only and name not in only:
            continue
        if not shutil.which(program):
            print(json.dumps({"app": name, "skipped": "not installed"}), flush=True)
            continue
        result = run(sys.argv[1], sys.argv[2], name, command, extra, keys, click)
        print(json.dumps(result), flush=True)
        if not passed(result):
            failed += 1
    for name in NOT_SCRIPTED:
        print(json.dumps({"app": name, "skipped": "by hand: docs/app-matrix.md"}), flush=True)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
