#!/usr/bin/env python3
# Copyright 2026 Kerem Soke
# SPDX-License-Identifier: GPL-3.0-or-later
"""Replays Marco oracle scenarios through Xwayland and Polo's XWM/X adapter,
then compares the resulting X properties with fixtures recorded with Marco frames.

  replay-x.py POLO POLO-INPUT --fixtures DIR [--passing FILE] [--only NAME] [-v]

Exits non-zero only when a scenario in the passing file fails. Exits 77 without
Xwayland or python3-xlib.
"""

import argparse
import json
import os
import pathlib
import shutil
import subprocess
import sys
import time

if not shutil.which("Xwayland"):
    sys.exit(77)
try:
    sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "tools" / "marco-oracle"))
    import run as oracle
    from Xlib import xauth
except ImportError:
    sys.exit(77)

# Polo's cookie is FamilyWild, which python-xlib does not match.
xauth.Xauthority.get_best_auth = lambda self, *args, **kwargs: self.entries[0][3:5]

BTN = {1: 0x110, 2: 0x112, 3: 0x111}
KEYS = {"Alt": 56, "Control": 29, "Primary": 29, "Shift": 42, "Super": 125, "Mod4": 125,
        "Tab": 15, "Left": 105, "Right": 106, "Up": 103, "Down": 108, "F10": 68, "c": 46}


class Mismatch(Exception):
    pass


class XReplay(oracle.Oracle):
    def __init__(self, scenario, polo, polo_input):
        super().__init__(scenario)
        self.polo = polo
        self.polo_input = polo_input

    def start(self):
        width, height = self.scenario["screen"]
        env = dict(os.environ)
        env.pop("DISPLAY", None)
        env.pop("XAUTHORITY", None)
        env["TMPDIR"] = self.tmp
        env["GSETTINGS_BACKEND"] = "memory"
        env["POLO_TEST_WIDTH"] = f"--width={width}"
        env["POLO_TEST_HEIGHT"] = f"--height={height}"
        env["POLO_TEST_PREFS"] = ";".join(f"{k}={oracle.gvariant_text(v)}"
                                          for k, v in self.scenario["prefs"].items())
        self.input = subprocess.Popen([self.polo_input, self.polo], env=env, text=True,
                                      stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                      stderr=open(os.path.join(self.tmp, "polo.log"), "w"),
                                      start_new_session=True)
        self.procs.append(self.input)
        first = self.input.stdout.readline().split()
        if len(first) != 2:
            raise RuntimeError("Polo did not start: " + self.log_tail())
        os.environ["XAUTHORITY"] = first[1]
        self.d = self.wait_for_display(first[0])
        self.root = self.d.screen().root
        check = self.atom("_NET_SUPPORTING_WM_CHECK")
        for _ in range(400):
            if self.root.get_full_property(check, oracle.Xatom.WINDOW):
                break
            time.sleep(0.025)
        else:
            raise RuntimeError("the XWM did not start: " + self.log_tail())
        self.time_window = self.root.create_window(-10, -10, 1, 1, 0, oracle.X.CopyFromParent,
                                                   oracle.X.InputOnly, oracle.X.CopyFromParent,
                                                   event_mask=oracle.X.PropertyChangeMask,
                                                   override_redirect=True)
        self.warp(*self.scenario["pointer"])
        self.settle()

    def log_tail(self):
        with open(os.path.join(self.tmp, "polo.log"), errors="replace") as f:
            return f.read()[-1500:]

    def send(self, command):
        self.input.stdin.write(command + "\n")
        self.input.stdin.flush()
        if self.input.stdout.readline().strip() != "ok":
            raise RuntimeError("polo-input stopped: " + self.log_tail())

    def warp(self, x, y):
        self.send(f"move {int(x)} {int(y)}")
        time.sleep(0.05)

    def key_combo(self, combo):
        mods, rest = [], combo
        while rest.startswith("<"):
            end = rest.index(">")
            mods.append(rest[1:end])
            rest = rest[end + 1:]
        codes = [KEYS[m] for m in mods]
        for code in codes:
            self.send(f"key {code} 1")
        self.send(f"key {KEYS[rest]} 1")
        self.send(f"key {KEYS[rest]} 0")
        for code in reversed(codes):
            self.send(f"key {code} 0")

    def run_step(self, step):
        if step["kind"] == "click":
            self.warp(*self.target_point(step))
            self.send(f"button {BTN[step['button']]} 1")
            self.send(f"button {BTN[step['button']]} 0")
        else:
            super().run_step(step)

    def stop(self):
        if self.d:
            try:
                self.d.close()
            except Exception:
                pass
        if self.input.poll() is None:
            self.input.stdin.close()
            try:
                self.input.wait(5)
            except subprocess.TimeoutExpired:
                pass
        super().stop()

    def replay(self, expected):
        where = "initial state"
        try:
            self.start()
            for strut in self.scenario["struts"]:
                self.dock(strut, self.scenario["outputs"])
                # Polo stacks by surface arrival, which can swap two quick maps.
                self.settle()
            compare(where, expected[0], self.settle())
            for i, step in enumerate(self.scenario["steps"]):
                where = f"step {i + 1} ({step['kind']})"
                self.run_step(step)
                compare(where, expected[i + 1], self.settle())
        finally:
            self.stop()


def compare(where, marco, polo):
    for field in ("active", "current_workspace", "n_workspaces", "stacking"):
        if marco[field] != polo[field]:
            raise Mismatch(f"{where}: {field} is {polo[field]}, Marco {marco[field]}")
    for label, m in marco["windows"].items():
        p = polo["windows"].get(label, {"mapped": False})
        if m["mapped"] != p["mapped"]:
            raise Mismatch(f"{where}: {label} mapped is {p['mapped']}, Marco {m['mapped']}")
        if not m["mapped"]:
            continue
        # Not "visible": Polo hides an X window in the compositor and leaves it mapped in X.
        for field in ("frame", "workspace", "state"):
            if m[field] != p[field]:
                raise Mismatch(f"{where}: {label} {field} is {p[field]}, Marco {m[field]}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("polo")
    parser.add_argument("polo_input")
    parser.add_argument("--fixtures", type=pathlib.Path, required=True)
    parser.add_argument("--passing", type=pathlib.Path)
    parser.add_argument("--only")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()

    must_pass = set()
    if args.passing:
        must_pass = {line.strip() for line in args.passing.read_text().splitlines()
                     if line.strip() and not line.startswith("#")}
    passed = failed = regressions = 0
    newly = []
    for path in sorted(args.fixtures.glob("*.expected.json")):
        name = path.name[:-len(".expected.json")]
        if args.only and name != args.only:
            continue
        fixture = json.loads(path.read_text())
        status, detail = "pass", ""
        try:
            XReplay(fixture["scenario"], args.polo, args.polo_input).replay(fixture["snapshots"])
            passed += 1
            if args.passing and name not in must_pass:
                newly.append(name)
        except Mismatch as e:
            detail = str(e)
            failed += 1
            status = "fail"
            if name in must_pass:
                status = "FAIL (regression)"
                regressions += 1
        except RuntimeError as e:
            detail = str(e)
            failed += 1
            regressions += 1
            status = "ERROR"
        print(f"{name:<42} {status}", flush=True)
        if detail and (args.verbose or status != "fail"):
            print(f"    {detail}", flush=True)
    print(f"\n{passed} passed, {failed} failed, {passed + failed} scenarios"
          + (f", {regressions} REGRESSIONS" if regressions else ""))
    for name in newly:
        print(f"now passing (add to {args.passing}): {name}")
    return 1 if regressions else 0


if __name__ == "__main__":
    sys.exit(main())
