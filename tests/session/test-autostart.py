#!/usr/bin/env python3
# Copyright 2026 Kerem Soke
# SPDX-License-Identifier: GPL-3.0-or-later
"""The start set and phases of the corpus match mate-session's own verdicts, recorded by
tools/session-oracle/run.py in expected.txt, less the window managers, which Polo replaces.
Conditions are re-evaluated live.

  test-autostart.py DUMP
"""

import os
import re
import select
import shutil
import subprocess
import sys
import tempfile

CORPUS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "autostart-corpus")


def check(condition, message):
    if not condition:
        print("check failed: " + message)
        sys.exit(1)


def environment(tmp):
    bin_dir = tmp + "/bin"
    os.makedirs(bin_dir)
    with open(CORPUS + "/installed") as f:
        for name in f.read().split():
            with open(f"{bin_dir}/{name}", "w") as out:
                out.write("#!/bin/sh\n")
            os.chmod(f"{bin_dir}/{name}", 0o755)
    config = tmp + "/config"
    shutil.copytree(CORPUS + "/user", config + "/autostart")
    with open(CORPUS + "/flags") as f:
        for name in f.read().split():
            open(f"{config}/{name}", "w").close()
    schemas = tmp + "/schemas"
    shutil.copytree(CORPUS + "/schemas", schemas)
    subprocess.check_call(["glib-compile-schemas", schemas])
    return dict(os.environ, PATH=bin_dir, HOME=tmp, XDG_CONFIG_HOME=config,
                XDG_CONFIG_DIRS=tmp + "/system-config", XDG_DATA_DIRS=CORPUS + "/data",
                GSETTINGS_BACKEND="memory", GSETTINGS_SCHEMA_DIR=schemas + ":/usr/share/glib-2.0/schemas",
                G_MESSAGES_DEBUG="")


def expect_line(process, wanted):
    ready = select.select([process.stdout], [], [], 10)[0]
    check(ready, f"timed out waiting for {wanted!r}")
    line = process.stdout.readline().strip()
    check(line == wanted, f"got {line!r}, wanted {wanted!r}")


def live_file(dump, env, name, condition, first, created, removed):
    path = f"{env['HOME']}/{name}.desktop"
    with open(path, "w") as f:
        f.write(f"[Desktop Entry]\nType=Application\nName=x\nExec=x\nAutostartCondition={condition}\n")
    flag = f"{env['XDG_CONFIG_HOME']}/live-flag"
    process = subprocess.Popen([dump, "--live", path], env=env, stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE, text=True)
    try:
        expect_line(process, first)
        open(flag, "w").close()
        expect_line(process, created)
        os.unlink(flag)
        expect_line(process, removed)
    finally:
        process.kill()
        process.wait()


def live_setting(dump, env):
    path = f"{env['HOME']}/setting.desktop"
    with open(path, "w") as f:
        f.write("[Desktop Entry]\nType=Application\nName=x\nExec=x\n"
                "AutostartCondition=GSettings org.mate.interface accessibility\n")
    process = subprocess.Popen([dump, "--live", path], env=env, stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE, text=True)
    try:
        expect_line(process, "disabled 1")
        process.stdin.write("org.mate.interface accessibility true\n")
        process.stdin.flush()
        expect_line(process, "changed 1")
        process.stdin.write("org.mate.interface accessibility false\n")
        process.stdin.flush()
        expect_line(process, "changed 0")
        process.stdin.write("org.mate.interface accessibility false\n")
        process.stdin.write("org.mate.interface accessibility true\n")
        process.stdin.flush()
        expect_line(process, "changed 1")
    finally:
        process.kill()
        process.wait()


def unusable_setting(dump, env, key):
    path = f"{env['HOME']}/unusable.desktop"
    with open(path, "w") as f:
        f.write("[Desktop Entry]\nType=Application\nName=x\nExec=x\n"
                f"AutostartCondition=GSettings org.mate.interface {key}\n")
    process = subprocess.Popen([dump, "--live", path], env=env, stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE, text=True)
    try:
        expect_line(process, "disabled 1")
    finally:
        process.kill()
        process.wait()


def check_restarts(dump):
    def delays(*seconds):
        result = subprocess.run([dump, "--restart", *map(str, seconds)], capture_output=True,
                                text=True, timeout=10)
        return [int(line) for line in result.stdout.split()]

    check(delays(0, 1, 2, 3, 4) == [250, 500, 1000, 2000, -1], "backoff or the limit is wrong")
    check(delays(0, 59) == [250, 500], "a crash inside the window was forgotten")
    check(delays(0, 60) == [250, 250], "a crash outside the window still counts")
    check(delays(0, 20, 40, 59, 70, 75) == [250, 500, 1000, 2000, 2000, -1], "wrong sliding window")


def main():
    dump = sys.argv[1]
    tmp = tempfile.mkdtemp(prefix="polo-autostart-")
    try:
        env = environment(tmp)
        system = env["XDG_CONFIG_DIRS"]
        os.makedirs(system)
        os.symlink(CORPUS + "/system", system + "/autostart")
        result = subprocess.run([dump], env=env, capture_output=True, text=True, timeout=30)
        check(result.returncode == 0, result.stderr)
        with open(CORPUS + "/expected.txt") as f:
            recorded = f.read().splitlines()
        window_managers = [line for line in recorded if line.startswith("polo-wm")]
        check(len(window_managers) == 1, f"mate-session's window managers: {window_managers}")
        expected = [line for line in recorded if line not in window_managers]
        got = result.stdout.splitlines()
        for line in sorted(set(expected) ^ set(got)):
            print(("mate-session " if line in expected else "polo         ") + line)
        check(expected == got, "the verdicts differ from mate-session's")
        check("Polo is the window manager: ignoring polo-wm" in result.stderr,
              "no notice about the window manager")

        required = {line.split("\t")[0] for line in result.stderr.splitlines()
                    if line.count("\t") == 3 and line.endswith("\t1")}
        check(required == {"polo-panel.desktop", "polo-fm-alt.desktop"}, f"required: {required}")
        check(len(os.listdir(CORPUS + "/system")) >= 40, "the corpus shrank")

        alone = dict(env, XDG_CONFIG_DIRS=tmp + "/nothing")
        result_alone = subprocess.run([dump], env=alone, capture_output=True, text=True, timeout=30)
        check("polo-panel.desktop" in result_alone.stdout, result_alone.stdout)
        check("polo-fm.desktop\t" in result_alone.stdout, "the configured file manager was not added")
        check("polo-wm" not in result_alone.stdout, "a window manager would start")
        check("polo-fm.desktop\t0\t-1\t1" in result_alone.stderr or
              "polo-fm.desktop\t0\t0\t1" in result_alone.stderr, "the file manager is not required")

        path = tmp + "/schemas/org.mate.applications-at-visual.gschema.xml"
        with open(path) as f:
            schema = f.read()
        with open(path, "w") as f:
            f.write(re.sub(r'(name="startup"[^>]*>\s*<default>)true', r"\1false", schema))
        subprocess.check_call(["glib-compile-schemas", tmp + "/schemas"])
        without = subprocess.run([dump], env=env, capture_output=True, text=True, timeout=30)
        check("polo-at-visual" not in without.stdout and "polo-at-mobility" in without.stdout,
              "startup=false did not keep the assistive app out")

        restart = dict(line.split("\t")[:2] for line in result.stderr.splitlines() if "\t" in line)
        check(restart["polo-restart.desktop"] == "1", "X-MATE-AutoRestart was not read")
        check(restart["polo-restart-gnome.desktop"] == "0", "X-GNOME-AutoRestart was read")
        check(restart["polo-restart-bad.desktop"] == "0", "a bad X-MATE-AutoRestart was read")

        live_file(dump, env, "if", "if-exists live-flag", "disabled 1", "changed 1", "changed 0")
        live_file(dump, env, "unless", "unless-exists live-flag", "disabled 0", "changed 0",
                  "changed 1")
        live_setting(dump, env)
        check_restarts(dump)
        unusable_setting(dump, env, "polo-no-such-key")
        unusable_setting(dump, env, "font-name")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("ok")


if __name__ == "__main__":
    main()
