#!/usr/bin/env python3
# Copyright 2026 Kerem Soke
# SPDX-License-Identifier: GPL-3.0-or-later
"""What a build made for users offers: no polo_test_v1, no weston_debug_v1,
even to a client Polo trusts, and --debug-kill binds nothing.

  test-release-surfaces.py POLO LIST-GLOBALS

Registered only for builds without -Dtest-protocol, -Ddebug-protocol and
-Ddebug-kill.
"""

import os
import signal
import subprocess
import sys
import tempfile
import time

FORBIDDEN = ("polo_test_v1", "weston_debug_v1")
failures = []


def check(condition, message):
    if not condition:
        failures.append(message)
        print("FAIL:", message, file=sys.stderr)


def globals_seen(polo, lister, runtime, extra):
    out = os.path.join(runtime, "globals.txt")
    log_path = os.path.join(runtime, "polo.log")
    env = dict(os.environ)
    env.update(XDG_RUNTIME_DIR=runtime, POLO_SHELL_UI="", POLO_XWAYLAND="off", GSETTINGS_BACKEND="memory")
    with open(log_path, "w") as log:
        proc = subprocess.Popen([polo, "--backend=headless", "--renderer=pixman",
                                 "--socket=polo-release-%d" % os.getpid(),
                                 "--exec=%s %s" % (lister, out)] + extra,
                                env=env, stdout=log, stderr=subprocess.STDOUT)
        deadline = time.monotonic() + 20
        names = []
        while time.monotonic() < deadline and proc.poll() is None:
            if os.path.exists(out):
                with open(out) as f:
                    names = f.read().split()
                if "done" in names:
                    break
            time.sleep(0.05)
        alive = proc.poll() is None
        proc.send_signal(signal.SIGTERM)
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
    with open(log_path, errors="replace") as f:
        text = f.read()
    if os.path.exists(out):
        os.unlink(out)
    return names, alive, text


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    polo, lister = sys.argv[1], sys.argv[2]
    with tempfile.TemporaryDirectory(prefix="polo-release-") as runtime:
        os.chmod(runtime, 0o700)
        names, alive, log = globals_seen(polo, lister, runtime, [])
        check(alive, "Polo did not stay up")
        check("done" in names, "the trusted client listed no globals")
        check("wl_compositor" in names and "zwlr_layer_shell_v1" in names,
              "a trusted client lacks ordinary or privileged globals: %s" % names)
        for name in FORBIDDEN:
            check(name not in names, "a build for users offers %s" % name)

        names, alive, log = globals_seen(polo, lister, runtime, ["--debug-kill"])
        check(alive, "--debug-kill stopped Polo from starting")
        check("this build has no emergency exit" in log, "--debug-kill was not reported as ignored")
    if failures:
        return 1
    print("release surfaces: none")
    return 0


if __name__ == "__main__":
    sys.exit(main())
