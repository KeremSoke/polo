#!/usr/bin/env python3
# Copyright 2026 Kerem Soke
# SPDX-License-Identifier: GPL-3.0-or-later
"""Renders the visual tests' reference frames with Marco under Xvfb.

  render.py [THEME...]

Writes tests/render/decor/reference/THEME-STATE.png for each theme (default:
all of THEMES), and THEME@LAYOUT-STATE.png for xframe.py's other button
layouts, taken with ImageMagick's import from
the root window. Marco runs in a throwaway home on the keyfile settings
backend and a private D-Bus, so the session's settings are never touched.
Marco styles frames from gtk-theme-name and takes the default window icon
from gtk-icon-theme-name, which a MATE session's XSETTINGS gives; here
settings.ini does.
"""

import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import time

REPO = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tests" / "render" / "decor"))
import xframe  # noqa: E402

THEMES = ("Menta", "BlueMenta", "TraditionalOk", "Blue-Submarine", "GreenLaguna")
OUT = REPO / "tests" / "render" / "decor" / "reference"
DISPLAY = ":87"
ROOT_COLOR = 0xff00ff
# org.mate.interface icon-theme's default, which Polo's helper follows.
ICON_THEME = "menta"


def keyfile(home, theme, layout):
    path = pathlib.Path(home) / ".config" / "glib-2.0" / "settings" / "keyfile"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        "[org/mate/marco/general]\n"
        f"theme='{theme}'\n"
        "titlebar-font='Sans Bold 10'\n"
        "titlebar-uses-system-font=false\n"
        "compositing-manager=false\n"
        f"button-layout='{layout}'\n")
    settings = pathlib.Path(home) / ".config" / "gtk-3.0" / "settings.ini"
    settings.parent.mkdir(parents=True, exist_ok=True)
    settings.write_text(f"[Settings]\ngtk-theme-name={theme}\ngtk-icon-theme-name={ICON_THEME}\n")


def render(theme, name, layout, states, env):
    from PIL import Image
    from Xlib import display

    keyfile(env["HOME"], theme, layout)
    marco = subprocess.Popen(["marco", "--replace", "--no-composite"], env=env,
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        time.sleep(2.0)
        d = display.Display(DISPLAY)
        root = d.screen().root
        root.change_attributes(background_pixel=ROOT_COLOR)
        root.clear_area()
        d.sync()
        scene = xframe.Scene(DISPLAY)
        for state in states:
            scene.enter(state)
            scene.settle(1.5)
            (x, y, width, height), _ = scene.frame(state)
            shot = subprocess.run(["import", "-display", DISPLAY, "-window", "root", "png:-"],
                                  check=True, capture_output=True, env=env).stdout
            path = OUT / f"{name}-{state}.png"
            with open(path, "wb") as f:
                f.write(shot)
            Image.open(path).crop((x, y, x + width, y + height)).save(path)
            print(f"{path.relative_to(REPO)}: {width}x{height}")
            scene.leave(state)
        scene.close()
        d.close()
    finally:
        marco.terminate()
        marco.wait()


def main():
    for tool in ("Xvfb", "marco", "import"):
        if not shutil.which(tool):
            sys.exit(f"render.py: {tool} is missing")
    OUT.mkdir(parents=True, exist_ok=True)
    themes = sys.argv[1:] or THEMES
    with tempfile.TemporaryDirectory(prefix="marco-render-") as home:
        env = {"PATH": os.environ["PATH"], "HOME": home, "DISPLAY": DISPLAY,
               "GSETTINGS_BACKEND": "keyfile", "XDG_CONFIG_HOME": f"{home}/.config",
               "XDG_RUNTIME_DIR": home, "LANG": "C.UTF-8", "NO_AT_BRIDGE": "1"}
        xvfb = subprocess.Popen(["Xvfb", DISPLAY, "-screen", "0", "1024x768x24", "-nolisten", "tcp"],
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            time.sleep(1.0)
            for theme, name, layout, states in xframe.references(themes):
                env["GTK_THEME"] = theme
                render(theme, name, layout, states, env)
        finally:
            xvfb.terminate()
            xvfb.wait()


if __name__ == "__main__":
    main()
