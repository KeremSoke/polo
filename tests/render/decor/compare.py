#!/usr/bin/env python3
# Copyright 2026 Kerem Soke
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compares Polo's frames with Marco's.

  compare.py POLO POLO-INPUT POLO-SHELL-UI

For each theme with references in reference/ (made by
tools/marco-render/render.py), runs a headless Polo with the same settings,
puts tests/render/decor/xframe.py's window through the same states and button
layouts and compares the frame pixel by pixel: the client area and the
corners Marco's shape removes are left out. The references come from Marco
without its compositor, where what a theme leaves undrawn is black, so Polo's
desktop is black here. POLO_TEST_CAPTURE_DIR keeps Polo's frames and the
differences. Exits 77 without Xwayland, python3-xlib or Pillow.
"""

import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
try:
    from PIL import Image
    import xframe
    from Xlib import xauth
except ImportError:
    sys.exit(77)
if not shutil.which("Xwayland"):
    sys.exit(77)

# Polo's cookie is FamilyWild, which python-xlib does not match.
xauth.Xauthority.get_best_auth = lambda self, *args, **kwargs: self.entries[0][3:5]

ROOT_COLOR = (255, 0, 255)
# A channel this far off is a different pixel; text antialiasing stays under it.
TOLERANCE = 48
# The share of differing frame pixels each theme may have.
THRESHOLD = 0.005
LOOSER = {}


def run_theme(theme, name, layout, states, polo, polo_input, shell_ui, keep, config):
    env = dict(os.environ)
    env.pop("DISPLAY", None)
    env.pop("XAUTHORITY", None)
    env["GSETTINGS_BACKEND"] = "keyfile"
    env["XDG_CONFIG_HOME"] = config
    env["GTK_THEME"] = theme
    env["POLO_SHELL_UI"] = shell_ui
    env["POLO_TEST_PREFS"] = (f"general/theme='{theme}';general/titlebar-font='Sans Bold 10';"
                              "general/titlebar-uses-system-font=false;"
                              "general/compositing-manager=false;"
                              f"general/button-layout='{layout}'")
    capture = tempfile.NamedTemporaryFile(suffix=".ppm", delete=False).name
    env["POLO_INPUT_CAPTURE"] = capture
    proc = subprocess.Popen([polo_input, polo], env=env, text=True, stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    failures = []
    try:
        first = proc.stdout.readline().split()
        if len(first) != 2:
            return [f"{name}: Polo did not start"]
        os.environ["XAUTHORITY"] = first[1]
        scene = xframe.Scene(first[0])
        scene.settle(1.5)
        for state in states:
            scene.enter(state)
            scene.settle(0.5)
            (x, y, width, height), (cx, cy, cw, ch) = scene.frame(state)
            proc.stdin.write("capture 0 0\n")
            proc.stdin.flush()
            if proc.stdout.readline().strip() != "ok":
                failures.append(f"{name}-{state}: no capture")
                scene.leave(state)
                continue
            polo_frame = Image.open(capture).convert("RGB").crop((x, y, x + width, y + height))
            reference = Image.open(HERE / "reference" / f"{name}-{state}.png").convert("RGB")
            failure = compare(f"{name}-{state}", reference, polo_frame,
                              None if state == "shaded" else (cx, cy, cw, ch),
                              LOOSER.get(theme, THRESHOLD), keep)
            if failure:
                failures.append(failure)
            scene.leave(state)
        scene.close()
    finally:
        proc.stdin.close()
        proc.wait(timeout=30)
        os.unlink(capture)
    return failures


def compare(name, reference, polo_frame, client, threshold, keep):
    if reference.size != polo_frame.size:
        return f"{name}: Polo's frame is {polo_frame.size}, Marco's {reference.size}"
    width, height = reference.size
    ref = reference.load()
    got = polo_frame.load()
    considered = differing = 0
    diff = Image.new("RGB", reference.size, (0, 0, 0))
    out = diff.load()
    for y in range(height):
        for x in range(width):
            if client and client[0] <= x < client[0] + client[2] and \
                    client[1] <= y < client[1] + client[3]:
                continue
            if ref[x, y] == ROOT_COLOR:
                continue
            considered += 1
            if max(abs(a - b) for a, b in zip(ref[x, y], got[x, y])) > TOLERANCE:
                differing += 1
                out[x, y] = (255, 0, 0)
    share = differing / considered if considered else 1.0
    print(f"{name}: {differing} of {considered} frame pixels differ ({share:.2%})")
    if keep:
        polo_frame.save(keep / f"{name}-polo.png")
        diff.save(keep / f"{name}-diff.png")
    if share > threshold:
        return f"{name}: {share:.2%} of the frame differs from Marco (allowed {threshold:.1%})"
    return None


def main():
    if len(sys.argv) != 4:
        sys.exit("usage: compare.py POLO POLO-INPUT POLO-SHELL-UI")
    polo, polo_input, shell_ui = sys.argv[1:]
    keep = os.environ.get("POLO_TEST_CAPTURE_DIR")
    keep = pathlib.Path(keep) if keep else None
    themes = sorted({p.name.rsplit("-", 1)[0].split("@")[0]
                     for p in (HERE / "reference").glob("*.png")})
    failures = []
    with tempfile.TemporaryDirectory(prefix="polo-decor-") as config:
        keyfile = pathlib.Path(config) / "glib-2.0" / "settings" / "keyfile"
        keyfile.parent.mkdir(parents=True)
        keyfile.write_text("[org/mate/desktop/background]\npicture-options='none'\n"
                           "color-shading-type='solid'\nprimary-color='#000000'\n")
        for theme, name, layout, states in xframe.references(themes):
            if not (pathlib.Path("/usr/share/themes") / theme / "metacity-1").is_dir():
                print(f"{name}: not installed, skipped")
                continue
            failures += run_theme(theme, name, layout, states, polo, polo_input, shell_ui, keep,
                                  config)
    for failure in failures:
        print(failure)
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
