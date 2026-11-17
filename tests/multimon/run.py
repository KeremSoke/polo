#!/usr/bin/env python3
# Copyright 2026 Kerem Soke
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The multi-monitor scenarios: headless Polo with fake monitors, the Wayland
# mate-panel and Caja's desktop, through plugging, unplugging, the lid,
# mirroring, primary changes, mode and scale changes, docks and re-logins.
# After each step the outputs, the primary, the work areas (where the panels
# are), the background owner, the panels' monitor keys and a screenshot are
# compared with what the scenario expects, and a pass/fail table is printed.
#
#   run.py POLO [--only E1,E3] [--break] [--keep]
#
# MULTIMON_PANEL, MULTIMON_CAJA and MULTIMON_MATE_DESKTOP are the install
# prefixes of mate-panel (Wayland, with monitor ids), Caja and mate-desktop
# (with MateMonitorInfo and per-monitor backgrounds); without them the test
# is skipped. MULTIMON_DRIVER, MULTIMON_POLO_SCHEMAS and WESTON_MODULE_MAP
# come from meson. --break changes one expectation on purpose, so the run
# must fail. Everything runs on a private D-Bus session with settings in a
# keyfile in a temporary directory.

import argparse
import glob
import os
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time

LAPTOP = "eDP-1"
EXT = "HDMI-A-1"
LAPTOP_ADD = f"add {LAPTOP} BOE 0a34 0 Panel 1 1600x900,1280x720"
EXT_ADD = f"add {EXT} DEL 4321 12345 U2715H 0 1920x1080,1280x720"
TOP, BOTTOM, EXT_PANEL = 24, 24, 36
PANEL_SLACK = 2
EXT_ID = "Dell_Inc.-U2715H"

COLOURS = {
    "red": (230, 20, 20),
    "blue": (20, 20, 230),
    "green": (20, 200, 20),
}


class Session:
    def __init__(self, polo, work, components, log_name):
        self.work = work
        self.components = components
        self.config = os.path.join(work, "config")
        self.shots = os.path.join(work, "shots", log_name)
        os.makedirs(self.shots, exist_ok=True)
        os.makedirs(self.config, exist_ok=True)
        env = dict(os.environ)
        env.update({
            "GSETTINGS_BACKEND": "keyfile",
            "GSETTINGS_SCHEMA_DIR": os.path.join(work, "schemas"),
            "POLO_TEST_CONFIG_HOME": self.config,
            "POLO_TEST_LOG": os.path.join(work, f"polo-{log_name}.log"),
            "POLO_TEST_DEBUG": "polo",
            "MULTIMON_SHOTS": self.shots,
            "GIO_USE_VFS": "local",
            "NO_AT_BRIDGE": "1",
            "HOME": os.path.join(work, "home"),
        })
        env.pop("WAYLAND_DISPLAY", None)
        env.pop("DISPLAY", None)
        self.stderr = open(os.path.join(work, f"driver-{log_name}.err"), "w")
        self.proc = subprocess.Popen(
            ["dbus-run-session", "--", os.environ["MULTIMON_DRIVER"], polo],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=self.stderr,
            text=True, env=env, start_new_session=True)
        self.errors = []

    def send(self, line):
        self.proc.stdin.write(line + "\n")
        self.proc.stdin.flush()

    def state(self, label):
        self.send(f"state {label}")
        outputs = {}
        while True:
            line = self.proc.stdout.readline()
            if not line:
                raise RuntimeError("the driver stopped (Polo died?)")
            f = line.split()
            if not f:
                continue
            if f[0] == "error":
                self.errors.append(line.strip())
            elif f[0] == "output":
                o = {
                    "on": f[2] == "on",
                    "x": int(f[3]), "y": int(f[4]),
                    "w": int(f[5]), "h": int(f[6]),
                    "scale": int(f[8]), "primary": f[10] == "1",
                    "area": tuple(int(v) for v in f[12:16]),
                    "id": f[17], "owner": int(f[19]), "mirror": f[21],
                }
                ax, ay, aw, ah = o["area"]
                o["top"] = ay - o["y"]
                o["bottom"] = o["y"] + o["h"] - (ay + ah)
                if o["on"]:
                    outputs[f[1]] = o
            elif f[0] == "end":
                return outputs

    def close(self):
        try:
            self.proc.stdin.close()
            self.proc.wait(timeout=60)
        except Exception:
            os.killpg(self.proc.pid, signal.SIGKILL)
            self.proc.wait()
        self.stderr.close()


def keyfile_path(work):
    return os.path.join(work, "config", "glib-2.0", "settings", "keyfile")


def read_keys(work):
    sections, current = {}, None
    try:
        for line in open(keyfile_path(work)):
            line = line.strip()
            m = re.match(r"^\[(.*)\]$", line)
            if m:
                current = sections.setdefault(m.group(1), {})
            elif current is not None and "=" in line:
                k, v = line.split("=", 1)
                current[k] = v
    except FileNotFoundError:
        pass
    return sections


def panel_keys(work):
    keys = read_keys(work)
    return {name: (keys.get(f"org/mate/panel/toplevels/{name}", {}).get("monitor-id"),
                   keys.get(f"org/mate/panel/toplevels/{name}", {}).get("monitor"))
            for name in ("top", "bottom", "ext")}


def write_pictures(work):
    from PIL import Image
    for name, rgb in COLOURS.items():
        Image.new("RGB", (64, 64), rgb).save(os.path.join(work, f"{name}.png"))
    im = Image.new("RGB", (200, 100), COLOURS["green"])
    im.paste(COLOURS["red"], (100, 0, 200, 100))
    im.save(os.path.join(work, "halves.png"))


def build_schemas(work, components):
    out = os.path.join(work, "schemas")
    os.makedirs(out, exist_ok=True)
    dirs = ["/usr/share/glib-2.0/schemas"]
    dirs += [os.path.join(components[c], "share", "glib-2.0", "schemas")
             for c in ("mate_desktop", "panel", "caja")]
    dirs += os.environ.get("MULTIMON_POLO_SCHEMAS", "").split(":")
    for d in dirs:
        for f in glob.glob(os.path.join(d, "*.xml")):
            shutil.copy(f, out)
    subprocess.run(["glib-compile-schemas", out], check=True,
                   stderr=subprocess.DEVNULL)


def write_clients(work, components):
    libs, data = [], []
    for c in ("mate_desktop", "panel", "caja"):
        p = components[c]
        libs += [os.path.join(p, "lib64"), os.path.join(p, "lib")]
        data.append(os.path.join(p, "share"))
    script = os.path.join(work, "clients.sh")
    with open(script, "w") as f:
        f.write(f"""#!/bin/sh
export XDG_CONFIG_HOME={work}/config HOME={work}/home GSETTINGS_BACKEND=keyfile
export GSETTINGS_SCHEMA_DIR={work}/schemas GDK_BACKEND=wayland NO_AT_BRIDGE=1 GIO_USE_VFS=local
export LD_LIBRARY_PATH={":".join(libs)}${{MULTIMON_LD_LIBRARY_PATH:+:$MULTIMON_LD_LIBRARY_PATH}}
export XDG_DATA_DIRS={":".join(data)}:/usr/share
{components["panel"]}/bin/mate-panel >> {work}/panel.log 2>&1 &
echo $! > {work}/pids
{components["caja"]}/bin/caja -n --force-desktop >> {work}/caja.log 2>&1 &
echo $! >> {work}/pids
wait
""")
    return script


def clients_alive(work):
    try:
        pids = [int(p) for p in open(os.path.join(work, "pids")).read().split()]
    except (FileNotFoundError, ValueError):
        return False
    alive = True
    for pid in pids:
        try:
            os.kill(pid, 0)
            with open(f"/proc/{pid}/stat") as stat:
                if stat.read().split()[2] == "Z":
                    alive = False
        except (ProcessLookupError, FileNotFoundError):
            alive = False
    return len(pids) == 2 and alive


SCHEMA_PATHS = {
    "org.mate.background": "org/mate/desktop/background",
    "org.mate.caja.desktop": "org/mate/caja/desktop",
    "org.mate.panel": "org/mate/panel/general",
}


def set_keys(work, changes):
    path = keyfile_path(work)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    sections, order = {}, []
    try:
        current = None
        for line in open(path):
            line = line.rstrip("\n")
            m = re.match(r"^\[(.*)\]$", line)
            if m:
                current = m.group(1)
                if current not in sections:
                    sections[current] = {}
                    order.append(current)
            elif current is not None and "=" in line:
                k, v = line.split("=", 1)
                sections[current][k] = v
    except FileNotFoundError:
        pass
    for section, key, value in changes:
        if section not in sections:
            sections[section] = {}
            order.append(section)
        sections[section][key] = value
    tmp = path + ".new"
    with open(tmp, "w") as f:
        for section in order:
            f.write(f"[{section}]\n")
            for k, v in sections[section].items():
                f.write(f"{k}={v}\n")
            f.write("\n")
    os.replace(tmp, path)


def monitor_section(monitor_id):
    return f"org/mate/desktop/background/monitors/{monitor_id}"


def configure(work, ids, ext_monitor):
    bg = SCHEMA_PATHS["org.mate.background"]
    changes = [(bg, "picture-filename", f"'{work}/red.png'"),
               (bg, "picture-options", "'stretched'"),
               (bg, "draw-background", "true")]
    if ext_monitor:
        own = monitor_section(ids[ext_monitor])
        changes += [(own, "picture-filename", f"'{work}/blue.png'"),
                    (own, "picture-options", "'stretched'"),
                    (own, "draw-background", "true"),
                    (own, "use-shared", "false")]
    for key in ("home-icon-visible", "computer-icon-visible", "trash-icon-visible",
                "volumes-visible", "network-icon-visible"):
        changes.append((SCHEMA_PATHS["org.mate.caja.desktop"], key, "false"))
    general = SCHEMA_PATHS["org.mate.panel"]
    changes += [(general, "object-id-list", "@as []"),
                (general, "toplevel-id-list", "['top', 'bottom', 'ext']")]
    panels = [("top", "top", TOP, ""), ("bottom", "bottom", BOTTOM, ids.get(LAPTOP, "")),
              ("ext", "bottom", EXT_PANEL, ids.get(ext_monitor, "") if ext_monitor else "")]
    for name, orientation, size, monitor_id in panels:
        section = f"org/mate/panel/toplevels/{name}"
        changes += [(section, "orientation", f"'{orientation}'"),
                    (section, "expand", "true"),
                    (section, "size", str(size)),
                    (section, "monitor-id", f"'{monitor_id}'")]
    set_keys(work, changes)


def check(label, outputs, expect, shot, work, start_keys, extra=None):
    problems = []
    want_on = {n for n, e in expect.items() if e is not None}
    if set(outputs) != want_on:
        problems.append(f"lit {sorted(outputs)}, expected {sorted(want_on)}")
    for name, e in expect.items():
        o = outputs.get(name)
        if e is None or o is None:
            continue
        for key in ("x", "y", "w", "h", "scale", "primary", "owner"):
            if key in e and o[key] != e[key]:
                problems.append(f"{name} {key} {o[key]}, expected {e[key]}")
        for key in ("top", "bottom"):
            if key in e and abs(o[key] - e[key]) > PANEL_SLACK * max(1, e[key] // 20):
                problems.append(f"{name} {key} panels {o[key]} px, expected {e[key]}")
        if e.get("mirror") is True and o["mirror"] == "-":
            problems.append(f"{name} mirrors nothing")
    if not clients_alive(work):
        problems.append("mate-panel or Caja is not running")
    if start_keys is not None and panel_keys(work) != start_keys:
        problems.append(f"panel keys changed: {panel_keys(work)}, were {start_keys}")
    problems += check_pixels(outputs, expect, shot)
    if extra:
        problems += extra()
    return problems


def check_pixels(outputs, expect, shot):
    from PIL import Image
    if not os.path.exists(shot) or not outputs:
        return ["no screenshot"] if outputs else []
    im = Image.open(shot).convert("RGB")
    ox = min(o["x"] for o in outputs.values())
    oy = min(o["y"] for o in outputs.values())
    k = im.width / (max(o["x"] + o["w"] for o in outputs.values()) - ox)
    problems = []
    for name, o in outputs.items():
        e = expect.get(name) or {}
        ax, ay, aw, ah = o["area"]
        regions = e.get("regions") or [(0.0, 1.0, e.get("colour"))]
        for fx0, fx1, colour in regions:
            x0 = int((ax - ox + aw * fx0 + 8) * k)
            x1 = int((ax - ox + aw * fx1 - 8) * k)
            y0, y1 = int((ay - oy + 8) * k), int((ay - oy + ah - 8) * k)
            total = dark = match = 0
            step = max(1, int(23 * k))
            for y in range(max(y0, 0), min(y1, im.height), step):
                for x in range(max(x0, 0), min(x1, im.width), step):
                    p = im.getpixel((x, y))
                    total += 1
                    dark += max(p) < 40
                    if colour:
                        c = COLOURS[colour]
                        match += all(abs(p[i] - c[i]) < 40 for i in range(3))
            if total == 0:
                problems.append(f"{name}: nothing to sample")
            elif dark * 100 // total >= 2:
                problems.append(f"{name}: {dark * 100 // total}% black")
            elif colour and match * 100 // total < 90:
                problems.append(f"{name}: {match * 100 // total}% {colour} in {fx0:.1f}-{fx1:.1f}")
    return problems


def extended(primary=LAPTOP, ext_y=0, ext_panel_on=EXT):
    laptop = {"x": 0, "y": 0, "w": 1600, "h": 900, "primary": primary == LAPTOP,
              "top": TOP if primary == LAPTOP else 0,
              "bottom": BOTTOM + (EXT_PANEL if ext_panel_on == LAPTOP else 0),
              "owner": 1, "colour": "red"}
    ext = {"x": 1600, "y": ext_y, "w": 1920, "h": 1080, "primary": primary == EXT,
           "top": TOP if primary == EXT else 0,
           "bottom": EXT_PANEL if ext_panel_on == EXT else 0,
           "owner": 1, "colour": "blue"}
    return {LAPTOP: laptop, EXT: ext}


def laptop_only():
    return {LAPTOP: {"x": 0, "y": 0, "w": 1600, "h": 900, "primary": True, "top": TOP,
                     "bottom": BOTTOM + EXT_PANEL, "owner": 1, "colour": "red"},
            EXT: None}


def external_only():
    return {EXT: {"x": 0, "y": 0, "w": 1920, "h": 1080, "primary": True, "top": TOP,
                  "bottom": BOTTOM + EXT_PANEL, "owner": 1, "colour": "blue"},
            LAPTOP: None}


def drawn_by_polo(expect):
    return {n: (dict(e, owner=2) if e else e) for n, e in expect.items()}


EXTEND = f"apply {LAPTOP}:0,0,1,1600x900 {EXT}:1600,0,1,1920x1080"


def scenarios(work):
    mirror = {n: {"x": 0, "y": 0, "w": 1280, "h": 720, "top": TOP,
                  "bottom": BOTTOM + EXT_PANEL, "owner": 1, "mirror": True}
              for n in (LAPTOP, EXT)}
    monitors_xml = os.path.join(work, "config", "monitors.xml")
    saved = {}

    def remember():
        saved["xml"] = open(monitors_xml).read() if os.path.exists(monitors_xml) else None
        return []

    def unchanged():
        now = open(monitors_xml).read() if os.path.exists(monitors_xml) else None
        return [] if now == saved.get("xml") else ["monitors.xml changed"]

    return [
        ("E1", "Laptop alone, then plug the external", [LAPTOP_ADD], EXT, [
            ([], laptop_only()),
            ([EXT_ADD, "wait 4"], extended()),
        ]),
        ("E2", "Unplug the external", [LAPTOP_ADD, EXT_ADD], EXT, [
            ([f"connect {EXT} 0", "wait 4"], laptop_only()),
        ]),
        ("E3", "Re-plug the same external", [LAPTOP_ADD, EXT_ADD], EXT, [
            ([f"connect {EXT} 0", "wait 4"], laptop_only()),
            ([f"connect {EXT} 1", "wait 4"], extended()),
        ]),
        ("E4", "External right and 200 px higher, kept, re-login", [LAPTOP_ADD, EXT_ADD], EXT, [
            ([f"apply {LAPTOP}:0,0 {EXT}:1600,-200", "wait 4"], extended(ext_y=-200)),
            (["restart", LAPTOP_ADD, "connect headless0 0", EXT_ADD, "spawn CLIENTS", "wait 10"],
             extended(ext_y=-200)),
        ]),
        ("E5", "Apply temporarily, then restore", [LAPTOP_ADD, EXT_ADD], EXT, [
            ([], extended(), remember),
            ([f"apply temporary {LAPTOP}:0,0 {EXT}:1600,-200", "wait 4"], extended(ext_y=-200)),
            ([EXTEND, "wait 4"], extended(), unchanged),
        ]),
        ("E6", "Lid closed with the external, then opened", [LAPTOP_ADD, EXT_ADD], EXT, [
            ([f"connect {LAPTOP} 0", "wait 4"], external_only()),
            ([f"connect {LAPTOP} 1", "wait 4"], extended()),
        ]),
        ("E7", "Mirror on and off", [LAPTOP_ADD, EXT_ADD], EXT, [
            ([f"apply {LAPTOP}:0,0,1,1280x720 {EXT}:0,0,1,1280x720", "wait 4"], mirror),
            ([EXTEND, "wait 4"], extended()),
        ]),
        ("E8", "Primary only, secondary only, extend", [LAPTOP_ADD, EXT_ADD], EXT, [
            ([f"apply {LAPTOP}:0,0 {EXT}:off", "wait 4"], laptop_only()),
            ([f"apply {LAPTOP}:off {EXT}:0,0", "wait 4"], external_only()),
            ([EXTEND, "wait 4"], extended(primary=EXT)),
        ]),
        ("E9", "Primary moved to the external", [LAPTOP_ADD, EXT_ADD], EXT, [
            ([f"apply primary={EXT} {LAPTOP}:0,0 {EXT}:1600,0", "wait 4"], extended(primary=EXT)),
        ]),
        ("E12", "Mode and scale change on the external", [LAPTOP_ADD, EXT_ADD], EXT, [
            ([f"apply {LAPTOP}:0,0 {EXT}:1600,0,1,1280x720"],
             {LAPTOP: {"colour": "red"}, EXT: {"w": 1280, "h": 720}}),
            (["wait 4"], {LAPTOP: extended()[LAPTOP],
                          EXT: dict(extended()[EXT], w=1280, h=720)}),
            ([f"apply {LAPTOP}:0,0 {EXT}:1600,0,2,1920x1080"],
             {LAPTOP: {"colour": "red"}, EXT: {"w": 960, "h": 540, "scale": 2}}),
            (["wait 4"], {LAPTOP: extended()[LAPTOP],
                          EXT: dict(extended()[EXT], w=960, h=540, scale=2)}),
        ]),
        ("E13", "A dock with three monitors, then all removed at once", [LAPTOP_ADD, EXT_ADD], EXT, [
            (["add DP-1 BNQ 7f10 1 GW2470 0 1920x1080", "add DP-2 BNQ 7f11 2 GW2480 0 1920x1080",
              "add DP-3 BNQ 7f12 3 GW2490 0 1920x1080", "wait 4"],
             dict(extended(), **{d: {"owner": 1, "colour": "red"} for d in ("DP-1", "DP-2", "DP-3")})),
            ([f"connect DP-1 0 DP-2 0 DP-3 0 {EXT} 0", "wait 4"], laptop_only()),
        ]),
        ("E14", "draw-background off and on for the external", [LAPTOP_ADD, EXT_ADD], EXT, [
            (["keys EXTSECTION draw-background false", "wait 3"],
             dict(extended(), **{EXT: dict(extended()[EXT], owner=2)})),
            (["keys EXTSECTION draw-background true", "wait 3"], extended()),
        ]),
        ("E15", "Spanned, with the external on its own picture", [LAPTOP_ADD, EXT_ADD], EXT, [
            (["keys org/mate/desktop/background picture-filename 'WORK/halves.png'",
              "keys org/mate/desktop/background picture-options 'spanned'", "wait 4"],
             dict(extended(), **{LAPTOP: dict(extended()[LAPTOP], colour=None,
                                               regions=[(0.0, 0.45, "green"), (0.55, 1.0, "red")])})),
        ]),
        ("E17", "The compositor draws under Caja's icons through display changes",
         [LAPTOP_ADD, EXT_ADD], EXT, [
            (["keys org/mate/desktop/background draw-background false",
              "keys EXTSECTION draw-background false", "wait 4"], drawn_by_polo(extended())),
            ([f"connect {EXT} 0", "wait 4"], drawn_by_polo(laptop_only())),
            ([f"connect {EXT} 1", "wait 4"], drawn_by_polo(extended())),
            ([f"apply {LAPTOP}:0,0 {EXT}:off", "wait 4"], drawn_by_polo(laptop_only())),
            ([f"apply {LAPTOP}:off {EXT}:0,0", "wait 4"], drawn_by_polo(external_only())),
            ([EXTEND, "wait 4"], drawn_by_polo(extended(primary=EXT))),
            ([f"apply {LAPTOP}:0,0,1,1280x720 {EXT}:0,0,1,1280x720", "wait 4"],
             {n: {"owner": 2} for n in (LAPTOP, EXT)}),
            ([EXTEND, "wait 4"], drawn_by_polo(extended(primary=EXT))),
            ([f"connect {LAPTOP} 0", "wait 4"], drawn_by_polo(external_only())),
            ([f"connect {LAPTOP} 1", "wait 4"], drawn_by_polo(extended(primary=EXT))),
        ]),
        ("E16", "Every monitor unplugged, then one back", [LAPTOP_ADD, EXT_ADD], EXT, [
            ([f"connect {LAPTOP} 0 {EXT} 0", "wait 3"], {LAPTOP: None, EXT: None}),
            ([f"connect {LAPTOP} 1", "wait 5"], laptop_only()),
            ([f"connect {EXT} 1", "wait 4"], extended()),
        ]),
    ] + identical_scenarios()


def identical_scenarios():
    a_add = "add HDMI-A-1 DEL 4321 {} U2715H 0 1920x1080,1280x720"
    b_add = "add DP-1 DEL 4321 {} U2715H 0 1920x1080,1280x720"
    out = []
    for sid, title, serial_a, serial_b, follows in (
            ("E10", "Two identical externals, connectors swapped", 111, 222, "serial"),
            ("E11", "Two identical externals without serials, swapped", 0, 0, "connector")):
        before = {LAPTOP: {"primary": True, "top": TOP, "bottom": BOTTOM, "owner": 1, "colour": "red"},
                  "HDMI-A-1": {"bottom": EXT_PANEL, "owner": 1, "colour": "blue"},
                  "DP-1": {"bottom": 0, "owner": 1, "colour": "red"}}
        if follows == "serial":
            after = {LAPTOP: before[LAPTOP],
                     "HDMI-A-1": {"bottom": 0, "owner": 1, "colour": "red"},
                     "DP-1": {"bottom": EXT_PANEL, "owner": 1, "colour": "blue"}}
        else:
            after = before
        out.append((sid, title, [LAPTOP_ADD, a_add.format(serial_a), b_add.format(serial_b)], "HDMI-A-1", [
            ([], before),
            (["remove HDMI-A-1", "remove DP-1", a_add.format(serial_b), b_add.format(serial_a), "wait 5"],
             after),
        ]))
    return out


def run_scenario(polo, work, components, scenario, broken):
    sid, title, monitors, ext_monitor, steps = scenario
    session_dir = os.path.join(work, sid)
    os.makedirs(os.path.join(session_dir, "home", "Desktop"), exist_ok=True)
    for f in ("schemas", "red.png", "blue.png", "green.png", "halves.png"):
        src = os.path.join(work, f)
        dst = os.path.join(session_dir, f)
        if not os.path.exists(dst):
            os.symlink(src, dst)
    clients = write_clients(session_dir, components)
    s = Session(polo, session_dir, components, sid)
    rows = []
    try:
        for line in monitors:
            s.send(line)
        s.send("connect headless0 0")
        ids = {name: o["id"] for name, o in s.state("ids").items()}
        if ext_monitor == EXT:
            ids.setdefault(EXT, EXT_ID)
        configure(session_dir, ids, ext_monitor)
        s.send("wait 1")
        s.send(f"spawn {clients}")
        s.send("wait 10")
        start_keys = None
        for n, step in enumerate(steps, 1):
            commands, expect = step[0], step[1]
            extra = step[2] if len(step) > 2 else None
            for line in commands:
                line = line.replace("CLIENTS", clients).replace("WORK", session_dir)
                line = line.replace("EXTSECTION", monitor_section(ids.get(ext_monitor or "", "")))
                if line.startswith("keys "):
                    _, section, key, value = line.split(" ", 3)
                    set_keys(session_dir, [(section, key, value)])
                else:
                    s.send(line)
            if broken and sid == "E1" and n == 2:
                expect = dict(expect, **{EXT: dict(expect[EXT], primary=True)})
            label = f"{sid}-{n}"
            outputs = s.state(label)
            if start_keys is None:
                start_keys = panel_keys(session_dir)
            problems = check(label, outputs, expect, os.path.join(s.shots, label + ".png"),
                             session_dir, start_keys, extra)
            problems += s.errors
            s.errors = []
            rows.append((label, title if n == 1 else "", problems))
    except RuntimeError as e:
        rows.append((f"{sid}-x", title, [str(e)]))
    finally:
        s.close()
    return rows


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("polo")
    parser.add_argument("--only")
    parser.add_argument("--break", dest="broken", action="store_true")
    parser.add_argument("--keep", action="store_true")
    args = parser.parse_args()

    components = {"panel": os.environ.get("MULTIMON_PANEL"),
                  "caja": os.environ.get("MULTIMON_CAJA"),
                  "mate_desktop": os.environ.get("MULTIMON_MATE_DESKTOP")}
    missing = [k for k, v in components.items() if not v or not os.path.isdir(v)]
    if missing:
        print("skipped: set " + ", ".join("MULTIMON_" + m.upper() for m in missing))
        return 77
    for tool in ("grim", "dbus-run-session"):
        if not shutil.which(tool):
            print(f"skipped: {tool} is missing")
            return 77
    try:
        import PIL  # noqa: F401
    except ImportError:
        print("skipped: python3-pillow is missing")
        return 77

    work = tempfile.mkdtemp(prefix="mm.", dir="/tmp")
    try:
        write_pictures(work)
        build_schemas(work, components)
        only = set(args.only.split(",")) if args.only else None
        rows = []
        for scenario in scenarios(work):
            if only and scenario[0] not in only:
                continue
            started = time.monotonic()
            result = run_scenario(args.polo, work, components, scenario, args.broken)
            for label, title, problems in result:
                print(f"{label:7} {'PASS' if not problems else 'FAIL':4}  {title}", flush=True)
                for p in problems:
                    print(f"          {p}", flush=True)
            print(f"        ({time.monotonic() - started:.0f} s)", flush=True)
            rows += result
        failed = [r for r in rows if r[2]]
        print(f"\n{len(rows) - len(failed)} of {len(rows)} steps passed")
        if args.keep or failed:
            print(f"kept {work}")
        if failed or args.keep:
            work = None
        return 1 if failed else 0
    finally:
        if work:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
