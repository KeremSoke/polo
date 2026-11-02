#!/usr/bin/env python3
# Copyright 2026 Kerem Soke
# SPDX-License-Identifier: GPL-3.0-or-later
"""Records mate-session's verdicts on tests/session/autostart-corpus in expected.txt.

Runs the real mate-session on Xvfb with a private bus as both session and system bus, a
throwaway HOME, the memory settings backend and a PATH of empty stubs, then kills it at the
end of its start-up summary.

  run.py [--check]
"""

import os
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CORPUS = ROOT + "/tests/session/autostart-corpus"
HERE = os.path.dirname(os.path.abspath(__file__))
PHASES = {"INITIALIZATION": "Initialization", "WINDOW_MANAGER": "WindowManager",
          "PANEL": "Panel", "DESKTOP": "Desktop", "APPLICATION": "Application"}


def main():
    tmp = tempfile.mkdtemp(prefix="pso-", dir="/tmp")
    log = tmp + "/log"
    bin_dir = tmp + "/bin"
    os.makedirs(bin_dir)
    with open(CORPUS + "/installed") as f:
        for name in f.read().split():
            with open(f"{bin_dir}/{name}", "w") as out:
                out.write("#!/bin/sh\nexec /usr/bin/sleep 30\n")
            os.chmod(f"{bin_dir}/{name}", 0o755)
    config = tmp + "/config"
    shutil.copytree(CORPUS + "/user", config + "/autostart")
    with open(CORPUS + "/flags") as f:
        for name in f.read().split():
            open(f"{config}/{name}", "w").close()
    os.makedirs(tmp + "/system-config")
    os.symlink(CORPUS + "/system", tmp + "/system-config/autostart")
    runtime = tmp + "/run"
    os.makedirs(runtime, mode=0o700)
    shim = tmp + "/shim.so"
    subprocess.check_call(["gcc", "-shared", "-fPIC", "-o", shim, HERE + "/syslog-shim.c"])

    schemas = tmp + "/schemas"
    shutil.copytree(CORPUS + "/schemas", schemas)
    subprocess.check_call(["glib-compile-schemas", schemas])

    env = {"PATH": bin_dir, "HOME": tmp, "XDG_CONFIG_HOME": config, "XDG_RUNTIME_DIR": runtime,
           "XDG_CACHE_HOME": tmp + "/cache", "XDG_DATA_HOME": tmp + "/data",
           "XDG_CONFIG_DIRS": tmp + "/system-config", "XDG_DATA_DIRS": CORPUS + "/data",
           "GSETTINGS_BACKEND": "memory", "GSETTINGS_SCHEMA_DIR": schemas + ":/usr/share/glib-2.0/schemas",
           "POLO_ORACLE_LOG": log, "LD_PRELOAD": shim, "LANG": "C"}
    command = ("export DBUS_SYSTEM_BUS_ADDRESS=$DBUS_SESSION_BUS_ADDRESS; "
               f"PATH={bin_dir} exec /usr/bin/mate-session --debug")
    process = subprocess.Popen(
        ["/usr/bin/xvfb-run", "-a", "/usr/bin/dbus-run-session", "--", "/usr/bin/sh", "-c", command],
        env=dict(env, PATH=bin_dir + ":/usr/bin"), start_new_session=True,
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        text = ""
        for _ in range(200):
            time.sleep(0.1)
            if os.path.exists(log):
                with open(log, errors="replace") as f:
                    text = f.read()
                if "Phase APPLICATION" in text:
                    time.sleep(1)
                    with open(log, errors="replace") as f:
                        text = f.read()
                    break
    finally:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.wait()

    lines = []
    phase = None
    for line in text[text.index("App startup summary"):].splitlines():
        found = re.search(r"GsmManager: Phase (\w+)", line)
        if found:
            phase = found.group(1).upper()
            continue
        found = re.search(r"app-id:(\S+)\s+is-disabled:(\d)\s+is-conditionally-disabled:(\d)\s+is-delayed:(\d)", line)
        if found and phase in PHASES:
            lines.append("\t".join((found.group(1), PHASES[phase], *found.group(2, 3, 4))))
    lines.sort()
    shutil.rmtree(tmp, ignore_errors=True)
    if not lines:
        print(text[-3000:])
        return 1
    if "--check" in sys.argv:
        with open(CORPUS + "/expected.txt") as f:
            same = f.read().splitlines() == lines
        print("same" if same else "different")
        return 0 if same else 1
    with open(CORPUS + "/expected.txt", "w") as f:
        f.write("\n".join(lines) + "\n")
    print(len(lines), "verdicts")
    return 0


if __name__ == "__main__":
    sys.exit(main())
