#!/usr/bin/env python3
# Copyright 2026 Kerem Soke
# SPDX-License-Identifier: GPL-3.0-or-later
"""The Marco oracle: play a scenario against the real Marco on a
private Xvfb and record what Marco decided.

    run.py scenarios/place-empty.yaml            # writes the fixture
    run.py --all                                 # every scenario
    run.py --repeat 10 scenarios/place-empty.yaml  # stability check

Each scenario (format: README.md) becomes
tests/policy/fixtures/<name>.expected.json, holding the normalized scenario
and a snapshot of Marco's state before the first step and after every step.
Polo's replayer (tests/policy/replay.vala) plays the same steps against
libpolo-policy and compares.

Needs: Xvfb, marco, dbus-run-session, python3-xlib, python3-pyyaml.
Never touches the user's display: Marco runs with --replace on its own
Xvfb, with its own HOME, XDG dirs and GSettings keyfile.
"""

import argparse
import html
import json
import os
import pathlib
import shutil
import signal
import subprocess
import sys
import tempfile
import re
import time

import yaml
from Xlib import X, XK, Xatom, display
from Xlib.ext import xtest
from Xlib.protocol import event as xevent

ROOT = pathlib.Path(__file__).resolve().parent
REPO = ROOT.parent.parent
SCENARIOS = ROOT / "scenarios"
FIXTURES = REPO / "tests" / "policy" / "fixtures"
X_FIXTURES = REPO / "tests" / "xwayland" / "fixtures"
NEEDS_FRAME = {"stack-titlebar-click-raises"}

MARCO_VERSION = "1.28.2"
MARCO_SCHEMA = pathlib.Path(os.environ.get(
    "MARCO_SCHEMA", "/usr/share/glib-2.0/schemas/org.mate.marco.gschema.xml"))
DEFAULT_OUTPUTS = [[1920, 1080]]

# Set before scenario prefs so frame extents do not depend on installed fonts
# or themes (see README.md, "Determinism").
BASE_PREFS = {
    "general/theme": "Menta",
    "general/titlebar-uses-system-font": False,
    "general/titlebar-font": "Noto Sans Bold 10",
    "general/compositing-manager": False,
    "general/num-workspaces": 4,
}

WINDOW_TYPES = ["normal", "desktop", "dock", "dialog", "modal_dialog", "toolbar", "menu",
                "utility", "splashscreen"]
EWMH_TYPE_ATOM = {
    "normal": "_NET_WM_WINDOW_TYPE_NORMAL",
    "desktop": "_NET_WM_WINDOW_TYPE_DESKTOP",
    "dock": "_NET_WM_WINDOW_TYPE_DOCK",
    "dialog": "_NET_WM_WINDOW_TYPE_DIALOG",
    "modal_dialog": "_NET_WM_WINDOW_TYPE_DIALOG",
    "toolbar": "_NET_WM_WINDOW_TYPE_TOOLBAR",
    "menu": "_NET_WM_WINDOW_TYPE_MENU",
    "utility": "_NET_WM_WINDOW_TYPE_UTILITY",
    "splashscreen": "_NET_WM_WINDOW_TYPE_SPLASH",
}
STATES = ["modal", "sticky", "maximized_vert", "maximized_horz", "shaded", "skip_taskbar",
          "skip_pager", "hidden", "fullscreen", "above", "below", "demands_attention"]
GRAVITIES = ["forget", "north_west", "north", "north_east", "west", "center", "east",
             "south_west", "south", "south_east", "static"]
STEP_KINDS = ["map", "unmap", "destroy", "minimize", "click", "pointer", "key", "request_state",
              "configure", "set_hints", "move_to_workspace", "switch_workspace", "activate",
              "wait"]

# XSizeHints flags.
US_POSITION, US_SIZE, P_POSITION, P_SIZE = 1, 2, 4, 8
P_MIN_SIZE, P_MAX_SIZE, P_RESIZE_INC, P_ASPECT = 16, 32, 64, 128
P_BASE_SIZE, P_WIN_GRAVITY = 256, 512


class ScenarioError(Exception):
    pass


# ---- Scenario normalization (the contract with replay.vala) ---------------

def normalize(raw, name):
    """Fill defaults and validate, producing exactly what the fixture stores."""
    if not isinstance(raw, dict):
        raise ScenarioError("scenario must be a mapping")
    allowed = {"description", "outputs", "pointer", "prefs", "struts", "steps"}
    unknown = set(raw) - allowed
    if unknown:
        raise ScenarioError(f"unknown top-level keys: {sorted(unknown)}")
    if not raw.get("description"):
        raise ScenarioError("every scenario needs a one-line description")

    outputs = raw.get("outputs", DEFAULT_OUTPUTS)
    if not outputs or any(len(o) != 2 for o in outputs):
        raise ScenarioError("outputs is a list of [width, height]")
    total_w = sum(o[0] for o in outputs)
    pointer = raw.get("pointer", [outputs[0][0] // 2, outputs[0][1] // 2])

    prefs = dict(BASE_PREFS)
    for key, value in (raw.get("prefs") or {}).items():
        if "/" not in key:
            key = "general/" + key
        prefs[key] = value

    struts = []
    for i, strut in enumerate(raw.get("struts") or []):
        edge = strut["edge"]
        if edge not in ("top", "bottom", "left", "right"):
            raise ScenarioError(f"strut {i}: bad edge {edge}")
        struts.append({
            "id": strut.get("id", f"dock-{edge}-{i}"),
            "edge": edge,
            "thickness": int(strut["thickness"]),
            "output": int(strut.get("output", 0)),
        })

    steps = []
    for i, step in enumerate(raw.get("steps") or []):
        if not isinstance(step, dict) or len(step) != 1:
            raise ScenarioError(f"step {i}: one {{kind: args}} mapping per step")
        kind, args = next(iter(step.items()))
        if kind not in STEP_KINDS:
            raise ScenarioError(f"step {i}: unknown kind {kind}")
        steps.append({"kind": kind, **normalize_step(kind, args or {}, i)})

    return {
        "name": name,
        "description": raw["description"],
        "outputs": outputs,
        "screen": [total_w, max(o[1] for o in outputs)],
        "pointer": pointer,
        "prefs": dict(sorted(prefs.items())),
        "struts": struts,
        "steps": steps,
    }


def normalize_step(kind, args, i):
    def need(key):
        if key not in args:
            raise ScenarioError(f"step {i} ({kind}) needs {key}")
        return args[key]

    if kind == "map":
        window_type = args.get("type", "normal")
        if window_type not in WINDOW_TYPES:
            raise ScenarioError(f"step {i}: bad window type {window_type}")
        states = args.get("state", [])
        for s in states:
            if s not in STATES:
                raise ScenarioError(f"step {i}: bad state {s}")
        hints = normalize_hints(args.get("hints", {}), i)
        return {
            "id": need("id"),
            "size": list(args.get("size", [400, 300])),
            "type": window_type,
            "transient_for": args.get("transient_for"),
            "position": args.get("position"),
            "position_source": args.get("position_source", "program"),
            "hints": hints,
            "state": sorted(states),
            "user_time": args.get("user_time"),
            "input": bool(args.get("input", True)),
        }
    if kind in ("unmap", "destroy"):
        return {"id": need("id")}
    if kind == "click":
        where = args.get("where", "client")
        if where not in ("client", "titlebar"):
            raise ScenarioError(f"step {i}: where is client or titlebar")
        return {"id": need("id"), "where": where, "at": args.get("at"),
                "button": int(args.get("button", 1))}
    if kind == "pointer":
        # Accept either a root position or a window's client centre.
        if "id" in args:
            return {"id": args["id"], "to": None}
        return {"id": None, "to": list(need("to"))}
    if kind == "minimize":
        return {"id": need("id")}
    if kind == "key":
        return {"combo": need("combo")}
    if kind == "request_state":
        action = args.get("action", "add")
        if action not in ("add", "remove", "toggle"):
            raise ScenarioError(f"step {i}: bad action {action}")
        states = need("state")
        states = [states] if isinstance(states, str) else list(states)
        for s in states:
            if s not in STATES:
                raise ScenarioError(f"step {i}: bad state {s}")
        return {"id": need("id"), "action": action, "state": states}
    if kind == "configure":
        return {"id": need("id"), "position": args.get("position"), "size": args.get("size")}
    if kind == "set_hints":
        return {"id": need("id"), "hints": normalize_hints(need("hints"), i)}
    if kind == "move_to_workspace":
        return {"id": need("id"), "workspace": int(need("workspace"))}
    if kind == "switch_workspace":
        return {"workspace": int(need("workspace"))}
    if kind == "activate":
        source = args.get("source", "pager")
        if source not in ("application", "pager"):
            raise ScenarioError(f"step {i}: source is application or pager")
        return {"id": need("id"), "source": source, "time": args.get("time", "now")}
    if kind == "wait":
        return {"ms": int(args.get("ms", 100))}
    raise AssertionError(kind)


def normalize_hints(hints, i):
    out = {}
    for key in ("min", "max", "inc", "base"):
        if key in hints:
            out[key] = list(hints[key])
    if "aspect" in hints:
        out["aspect"] = [list(hints["aspect"][0]), list(hints["aspect"][1])]
    if "gravity" in hints:
        if hints["gravity"] not in GRAVITIES:
            raise ScenarioError(f"step {i}: bad gravity {hints['gravity']}")
        out["gravity"] = hints["gravity"]
    unknown = set(hints) - {"min", "max", "inc", "base", "aspect", "gravity"}
    if unknown:
        raise ScenarioError(f"step {i}: unknown hints {sorted(unknown)}")
    return out


# ---- GSettings keyfile ------------------------------------------------------

SCHEMA_PATHS = {
    "general": "org/mate/marco/general",
    "workspace-names": "org/mate/marco/workspace-names",
    "global-keybindings": "org/mate/marco/global-keybindings",
    "window-keybindings": "org/mate/marco/window-keybindings",
    "keybinding-commands": "org/mate/marco/keybinding-commands",
}


def gvariant_text(value):
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, int):
        return str(value)
    if isinstance(value, str):
        return "'" + value.replace("\\", "\\\\").replace("'", "\\'") + "'"
    raise ScenarioError(f"unsupported pref value {value!r}")


def upstream_defaults():
    """Every Marco key with the default from Marco's own schema XML.

    Written into the keyfile so distribution overrides (Fedora's
    10_mate-fedora.gschema.override sets center-new-windows=false, for
    example) never leak into fixtures: the oracle always measures upstream
    Marco, the same defaults Polo.MarcoPrefs.defaults () has.
    """
    xml = MARCO_SCHEMA.read_text(encoding="utf-8")
    defaults = {}
    for schema in SCHEMA_PATHS:
        schema_id = "org.mate.Marco." + schema
        body = re.search(r'<schema id="%s".*?</schema>' % re.escape(schema_id), xml, re.S)
        for k in re.finditer(r'<key name="([^"]+)"[^>]*>(.*?)</key>', body.group(0), re.S):
            text = re.search(r"<default>(.*?)</default>", k.group(2), re.S).group(1)
            defaults[f"{schema}/{k.group(1)}"] = html.unescape(text.strip())
    return defaults


def write_keyfile(path, prefs):
    groups = {}
    texts = upstream_defaults()
    for key, value in prefs.items():
        if key not in texts:
            raise ScenarioError(f"unknown Marco key {key}")
        texts[key] = gvariant_text(value)
    for key, text in texts.items():
        schema, name = key.split("/", 1)
        if schema not in SCHEMA_PATHS:
            raise ScenarioError(f"unknown pref schema in {key}")
        groups.setdefault(SCHEMA_PATHS[schema], []).append((name, text))
    with open(path, "w", encoding="utf-8") as f:
        for group, entries in groups.items():
            f.write(f"[{group}]\n")
            for name, text in entries:
                f.write(f"{name}={text}\n")
            f.write("\n")


# ---- Running Marco ------------------------------------------------------------

def free_display():
    for n in range(90, 200):
        if not os.path.exists(f"/tmp/.X11-unix/X{n}") and not os.path.exists(f"/tmp/.X{n}-lock"):
            return n
    raise RuntimeError("no free X display number")


class Oracle:
    def __init__(self, scenario, undecorated=False):
        self.scenario = scenario
        self.undecorated = undecorated
        self.tmp = tempfile.mkdtemp(prefix="polo-oracle-")
        self.procs = []
        self.windows = {}   # Xlib window by label
        self.order = []     # Creation order
        self.destroyed = set()
        self.d = None

    # -- set-up and tear-down --

    def wait_for_display(self, name):
        for _ in range(200):
            try:
                return display.Display(name)
            except Exception:
                time.sleep(0.025)
        raise RuntimeError(f"X server {name} did not start")

    def start(self):
        outputs = self.scenario["outputs"]
        width, height = self.scenario["screen"]
        quiet = {"stdout": subprocess.DEVNULL, "stderr": subprocess.DEVNULL}

        if len(outputs) != 1:
            raise ScenarioError("multi-output scenarios need a multi-head X server "
                                "(scenarios/pending-multihead/README.md)")
        self.display_name = f":{free_display()}"
        self.procs.append(self.spawn(
            ["Xvfb", self.display_name, "-screen", "0", f"{width}x{height}x24",
             "-nolisten", "tcp", "-noreset", "-dpi", "96"], **quiet))

        self.d = self.wait_for_display(self.display_name)
        self.root = self.d.screen().root

        home = os.path.join(self.tmp, "home")
        config = os.path.join(home, ".config")
        os.makedirs(os.path.join(config, "glib-2.0", "settings"))
        write_keyfile(os.path.join(config, "glib-2.0", "settings", "keyfile"),
                      self.scenario["prefs"])
        env = {
            "PATH": os.environ.get("PATH", "/usr/bin:/bin"),
            "DISPLAY": self.display_name,
            "HOME": home,
            "XDG_CONFIG_HOME": config,
            "XDG_DATA_HOME": os.path.join(home, ".local", "share"),
            "XDG_CACHE_HOME": os.path.join(home, ".cache"),
            "XDG_RUNTIME_DIR": self.tmp,
            "GSETTINGS_BACKEND": "keyfile",
            "LANG": "C.UTF-8",
            # Prevent GIO from starting gvfs daemons on the private bus.
            "GIO_USE_VFS": "local",
            "GVFS_DISABLE_FUSE": "1",
        }
        self.procs.append(self.spawn(
            ["dbus-run-session", "--", "marco", "--replace", "--no-composite", "--sm-disable"],
            env=env, stdout=subprocess.DEVNULL,
            stderr=open(os.path.join(self.tmp, "marco.log"), "w")))

        check = self.atom("_NET_SUPPORTING_WM_CHECK")
        for _ in range(400):
            if self.root.get_full_property(check, Xatom.WINDOW):
                break
            time.sleep(0.025)
        else:
            raise RuntimeError("Marco did not start; see " + self.tmp + "/marco.log")

        # Property window used to read the server time.
        self.time_window = self.root.create_window(-10, -10, 1, 1, 0, X.CopyFromParent,
                                                   X.InputOnly, X.CopyFromParent,
                                                   event_mask=X.PropertyChangeMask,
                                                   override_redirect=True)
        self.warp(*self.scenario["pointer"])
        self.settle()

    def spawn(self, cmd, **kwargs):
        """Start a child in its own process group, so stop () can take down
        everything it starts (dbus-run-session's bus, anything activated on
        it) and nothing outlives the run."""
        return subprocess.Popen(cmd, start_new_session=True, **kwargs)

    def stop(self):
        for proc in reversed(self.procs):
            try:
                os.killpg(proc.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            try:
                proc.wait(5)
            except subprocess.TimeoutExpired:
                os.killpg(proc.pid, signal.SIGKILL)
                proc.wait()
        shutil.rmtree(self.tmp, ignore_errors=True)

    # -- X helpers --

    def atom(self, name):
        return self.d.intern_atom(name)

    def server_time(self):
        self.time_window.change_property(self.atom("POLO_ORACLE_TIME"), Xatom.CARDINAL, 32, [0])
        self.d.sync()
        while True:
            ev = self.d.next_event()
            if ev.type == X.PropertyNotify and ev.window == self.time_window:
                return ev.time

    def warp(self, x, y):
        # XWarpPointer generates the same crossing (Enter/Leave) events as
        # real motion, which sloppy and mouse focus depend on.
        self.root.warp_pointer(int(x), int(y))
        self.d.sync()

    def client_message(self, window, type_name, data):
        data = (list(data) + [0] * 5)[:5]
        ev = xevent.ClientMessage(window=window, client_type=self.atom(type_name),
                                  data=(32, data))
        self.root.send_event(ev, event_mask=X.SubstructureRedirectMask |
                             X.SubstructureNotifyMask)
        self.d.sync()

    def drain(self):
        while self.d.pending_events():
            self.d.next_event()

    def settle(self, timeout=3.0):
        """Wait until two snapshots 80 ms apart agree."""
        deadline = time.monotonic() + timeout
        previous = None
        while time.monotonic() < deadline:
            time.sleep(0.08)
            self.d.sync()
            self.drain()
            current = self.snapshot()
            if current == previous:
                return current
            previous = current
        return previous

    # -- windows --

    def size_hints(self, args, width, height):
        hints = args.get("hints", {})
        flags, data = 0, [0] * 18
        if args.get("position") is not None:
            flags |= US_POSITION if args.get("position_source") == "user" else P_POSITION
            data[1], data[2] = args["position"]
        flags |= P_SIZE
        data[3], data[4] = width, height
        if "min" in hints:
            flags |= P_MIN_SIZE
            data[5], data[6] = hints["min"]
        if "max" in hints:
            flags |= P_MAX_SIZE
            data[7], data[8] = hints["max"]
        if "inc" in hints:
            flags |= P_RESIZE_INC
            data[9], data[10] = hints["inc"]
        if "aspect" in hints:
            flags |= P_ASPECT
            (data[11], data[12]), (data[13], data[14]) = hints["aspect"]
        if "base" in hints:
            flags |= P_BASE_SIZE
            data[15], data[16] = hints["base"]
        if "gravity" in hints:
            flags |= P_WIN_GRAVITY
            data[17] = GRAVITIES.index(hints["gravity"])
        data[0] = flags
        return data

    def set_hints(self, window, args, width, height):
        window.change_property(self.atom("WM_NORMAL_HINTS"), self.atom("WM_SIZE_HINTS"), 32,
                               self.size_hints(args, width, height))

    def map(self, args):
        label = args["id"]
        if label in self.windows:
            raise ScenarioError(f"window {label} mapped twice")
        width, height = args["size"]
        x, y = args["position"] or (0, 0)
        w = self.root.create_window(x, y, width, height, 0, X.CopyFromParent, X.InputOutput,
                                    X.CopyFromParent, background_pixel=0x4070a0,
                                    event_mask=X.StructureNotifyMask | X.PropertyChangeMask)
        w.set_wm_name(label)
        w.set_wm_class(label, "PoloOracle")
        w.change_property(self.atom("_NET_WM_NAME"), self.atom("UTF8_STRING"), 8,
                          label.encode())
        w.change_property(self.atom("WM_PROTOCOLS"), Xatom.ATOM, 32,
                          [self.atom("WM_DELETE_WINDOW")])
        # WM_HINTS: flags InputHint (1), input
        w.change_property(self.atom("WM_HINTS"), self.atom("WM_HINTS"), 32,
                          [1, 1 if args["input"] else 0, 0, 0, 0, 0, 0, 0, 0])
        self.set_hints(w, args, width, height)
        w.change_property(self.atom("_NET_WM_WINDOW_TYPE"), Xatom.ATOM, 32,
                          [self.atom(EWMH_TYPE_ATOM[args["type"]])])
        states = list(args["state"])
        if args["type"] == "modal_dialog" and "modal" not in states:
            states.append("modal")
        if states:
            w.change_property(self.atom("_NET_WM_STATE"), Xatom.ATOM, 32,
                              [self.atom("_NET_WM_STATE_" + s.upper()) for s in states])
        if args["transient_for"]:
            w.change_property(self.atom("WM_TRANSIENT_FOR"), Xatom.WINDOW, 32,
                              [self.windows[args["transient_for"]].id])
        if args["user_time"] is not None:
            when = self.server_time() if args["user_time"] == "now" else int(args["user_time"])
            w.change_property(self.atom("_NET_WM_USER_TIME"), Xatom.CARDINAL, 32, [when])
        if self.undecorated:
            w.change_property(self.atom("_MOTIF_WM_HINTS"), self.atom("_MOTIF_WM_HINTS"), 32,
                              [2, 0, 0, 0, 0])
        self.windows[label] = w
        self.order.append(label)
        w.map()
        self.d.sync()

    def dock(self, strut, outputs):
        """A panel along one output edge, with _NET_WM_STRUT_PARTIAL."""
        ox = sum(o[0] for o in outputs[:strut["output"]])
        ow, oh = outputs[strut["output"]]
        screen_w = sum(o[0] for o in outputs)
        screen_h = max(o[1] for o in outputs)
        t = strut["thickness"]
        edge = strut["edge"]
        # left, right, top, bottom, left_start_y, left_end_y, right_start_y,
        # right_end_y, top_start_x, top_end_x, bottom_start_x, bottom_end_x
        partial = [0] * 12
        if edge == "top":
            rect = (ox, 0, ow, t)
            partial[2], partial[8], partial[9] = t, ox, ox + ow - 1
        elif edge == "bottom":
            rect = (ox, oh - t, ow, t)
            partial[3], partial[10], partial[11] = screen_h - oh + t, ox, ox + ow - 1
        elif edge == "left":
            rect = (ox, 0, t, oh)
            partial[0], partial[4], partial[5] = ox + t, 0, oh - 1
        else:
            rect = (ox + ow - t, 0, t, oh)
            partial[1], partial[6], partial[7] = screen_w - (ox + ow) + t, 0, oh - 1
        args = {"id": strut["id"], "size": [rect[2], rect[3]], "type": "dock",
                "transient_for": None, "position": [rect[0], rect[1]],
                "position_source": "program", "hints": {}, "state": [], "user_time": None,
                "input": False}
        self.map(args)
        w = self.windows[strut["id"]]
        w.change_property(self.atom("_NET_WM_STRUT_PARTIAL"), Xatom.CARDINAL, 32, partial)
        w.change_property(self.atom("_NET_WM_STRUT"), Xatom.CARDINAL, 32, partial[:4])
        self.d.sync()

    # -- reading Marco's decisions --

    def window_state(self, label, w):
        try:
            attrs = w.get_attributes()
            geom = w.get_geometry()
            pos = w.translate_coords(self.root, 0, 0)
        except Exception:
            return {"mapped": False}
        extents = w.get_full_property(self.atom("_NET_FRAME_EXTENTS"), Xatom.CARDINAL)
        left, right, top, bottom = extents.value.tolist() if extents else (0, 0, 0, 0)
        cx, cy = -pos.x, -pos.y
        desktop = w.get_full_property(self.atom("_NET_WM_DESKTOP"), Xatom.CARDINAL)
        workspace = desktop.value[0] if desktop else None
        if workspace == 0xFFFFFFFF:
            workspace = -1
        state = w.get_full_property(self.atom("_NET_WM_STATE"), Xatom.ATOM)
        names = []
        for a in (state.value if state else []):
            name = self.d.get_atom_name(a)
            if name.startswith("_NET_WM_STATE_"):
                names.append(name[len("_NET_WM_STATE_"):].lower())
        wm_state = w.get_full_property(self.atom("WM_STATE"), self.atom("WM_STATE"))
        managed = bool(wm_state) and wm_state.value[0] != 0
        return {
            "mapped": managed,
            "visible": attrs.map_state == X.IsViewable,
            "client": [cx, cy, geom.width, geom.height],
            "frame": [cx - left, cy - top, geom.width + left + right,
                      geom.height + top + bottom],
            "extents": [left, right, top, bottom],
            "workspace": workspace,
            "state": sorted(names),
        }

    def snapshot(self):
        by_id = {w.id: label for label, w in self.windows.items()}
        stacking = self.root.get_full_property(self.atom("_NET_CLIENT_LIST_STACKING"),
                                               Xatom.WINDOW)
        active = self.root.get_full_property(self.atom("_NET_ACTIVE_WINDOW"), Xatom.WINDOW)
        current = self.root.get_full_property(self.atom("_NET_CURRENT_DESKTOP"),
                                              Xatom.CARDINAL)
        count = self.root.get_full_property(self.atom("_NET_NUMBER_OF_DESKTOPS"),
                                            Xatom.CARDINAL)
        windows = {}
        for label in self.order:
            if label not in self.destroyed:
                windows[label] = self.window_state(label, self.windows[label])
        return {
            "stacking": [by_id[w] for w in (stacking.value if stacking else []) if w in by_id],
            "active": by_id.get(active.value[0]) if active and active.value else None,
            "current_workspace": current.value[0] if current else 0,
            "n_workspaces": count.value[0] if count else 0,
            "windows": windows,
        }

    # -- steps --

    def target_point(self, step):
        state = self.window_state(step["id"], self.windows[step["id"]])
        if step["where"] == "titlebar":
            fx, fy, fw, _ = state["frame"]
            top = state["extents"][2]
            at = step["at"] or [fw // 2, top // 2]
            return fx + at[0], fy + at[1]
        cx, cy, cw, ch = state["client"]
        at = step["at"] or [cw // 2, ch // 2]
        return cx + at[0], cy + at[1]

    def key_combo(self, combo):
        mods, rest = [], combo
        while rest.startswith("<"):
            end = rest.index(">")
            mods.append(rest[1:end])
            rest = rest[end + 1:]
        mod_keysyms = {"Alt": "Alt_L", "Control": "Control_L", "Primary": "Control_L",
                       "Shift": "Shift_L", "Super": "Super_L", "Mod4": "Super_L"}
        codes = [self.d.keysym_to_keycode(XK.string_to_keysym(mod_keysyms[m])) for m in mods]
        key = self.d.keysym_to_keycode(XK.string_to_keysym(rest))
        if not key or not all(codes):
            raise ScenarioError(f"cannot type {combo}")
        for code in codes:
            xtest.fake_input(self.d, X.KeyPress, code)
        xtest.fake_input(self.d, X.KeyPress, key)
        xtest.fake_input(self.d, X.KeyRelease, key)
        for code in reversed(codes):
            xtest.fake_input(self.d, X.KeyRelease, code)
        self.d.sync()

    def run_step(self, step):
        kind = step["kind"]
        if kind == "map":
            self.map(step)
        elif kind == "unmap":
            self.windows[step["id"]].unmap()
            self.d.sync()
        elif kind == "destroy":
            self.windows[step["id"]].destroy()
            self.destroyed.add(step["id"])
            self.d.sync()
        elif kind == "click":
            x, y = self.target_point(step)
            self.warp(x, y)
            xtest.fake_input(self.d, X.ButtonPress, step["button"])
            xtest.fake_input(self.d, X.ButtonRelease, step["button"])
            self.d.sync()
        elif kind == "pointer":
            if step["id"]:
                cx, cy, cw, ch = self.window_state(step["id"], self.windows[step["id"]])["client"]
                self.warp(cx + cw // 2, cy + ch // 2)
            else:
                self.warp(*step["to"])
        elif kind == "minimize":
            self.client_message(self.windows[step["id"]], "WM_CHANGE_STATE", [3, 0, 0, 0])
        elif kind == "key":
            self.key_combo(step["combo"])
        elif kind == "request_state":
            action = {"remove": 0, "add": 1, "toggle": 2}[step["action"]]
            states = [self.atom("_NET_WM_STATE_" + s.upper()) for s in step["state"]] + [0]
            self.client_message(self.windows[step["id"]], "_NET_WM_STATE",
                                [action, states[0], states[1], 2])
        elif kind == "configure":
            w = self.windows[step["id"]]
            kwargs = {}
            if step["position"]:
                kwargs["x"], kwargs["y"] = step["position"]
            if step["size"]:
                kwargs["width"], kwargs["height"] = step["size"]
            w.configure(**kwargs)
            self.d.sync()
        elif kind == "set_hints":
            w = self.windows[step["id"]]
            geom = w.get_geometry()
            self.set_hints(w, {"hints": step["hints"]}, geom.width, geom.height)
            self.d.sync()
        elif kind == "move_to_workspace":
            self.client_message(self.windows[step["id"]], "_NET_WM_DESKTOP",
                                [step["workspace"], 2])
        elif kind == "switch_workspace":
            self.client_message(self.root, "_NET_CURRENT_DESKTOP",
                                [step["workspace"], self.server_time()])
        elif kind == "activate":
            when = self.server_time() if step["time"] == "now" else int(step["time"])
            source = 1 if step["source"] == "application" else 2
            self.client_message(self.windows[step["id"]], "_NET_ACTIVE_WINDOW",
                                [source, when, 0])
        elif kind == "wait":
            time.sleep(step["ms"] / 1000)

    def run(self):
        snapshots = []
        try:
            self.start()
            for strut in self.scenario["struts"]:
                self.dock(strut, self.scenario["outputs"])
            snapshots.append(self.settle())
            for step in self.scenario["steps"]:
                self.run_step(step)
                snapshots.append(self.settle())
        finally:
            self.stop()
        return snapshots


def fixture_for(path, undecorated=False):
    name = path.stem
    with open(path, encoding="utf-8") as f:
        scenario = normalize(yaml.safe_load(f), name)
    snapshots = Oracle(scenario, undecorated).run()
    return {
        "marco": MARCO_VERSION,
        "scenario": scenario,
        "snapshots": snapshots,
    }


def dump(fixture):
    return json.dumps(fixture, indent=1, sort_keys=True) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("scenarios", nargs="*", type=pathlib.Path)
    parser.add_argument("--all", action="store_true", help="every scenarios/*.yaml")
    parser.add_argument("--repeat", type=int, default=1,
                        help="run each scenario N times and require identical output")
    parser.add_argument("--check", action="store_true",
                        help="do not write; fail if a fixture would change")
    parser.add_argument("--validate", action="store_true",
                        help="only check scenario syntax (no X needed)")
    parser.add_argument("--undecorated", action="store_true",
                        help="every window without a frame, for tests/xwayland/fixtures")
    args = parser.parse_args()
    fixtures = X_FIXTURES if args.undecorated else FIXTURES

    paths = sorted(SCENARIOS.glob("*.yaml")) if args.all else args.scenarios
    if not paths:
        parser.error("no scenarios given")

    status = 0
    for path in paths:
        if args.validate:
            with open(path, encoding="utf-8") as f:
                normalize(yaml.safe_load(f), path.stem)
            continue
        if args.undecorated and path.stem in NEEDS_FRAME:
            continue
        outputs = [dump(fixture_for(path, args.undecorated)) for _ in range(args.repeat)]
        if len(set(outputs)) != 1:
            print(f"UNSTABLE {path.stem}: {len(set(outputs))} different results "
                  f"in {args.repeat} runs")
            status = 1
            continue
        target = fixtures / f"{path.stem}.expected.json"
        if args.check:
            old = target.read_text(encoding="utf-8") if target.exists() else None
            if old != outputs[0]:
                print(f"CHANGED {path.stem}")
                status = 1
            else:
                print(f"same    {path.stem}")
        else:
            fixtures.mkdir(parents=True, exist_ok=True)
            target.write_text(outputs[0], encoding="utf-8")
            print(f"wrote   {target.relative_to(REPO)}"
                  + (f" (stable over {args.repeat} runs)" if args.repeat > 1 else ""))
    return status


if __name__ == "__main__":
    sys.exit(main())
