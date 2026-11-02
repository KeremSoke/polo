#!/usr/bin/env python3
# Copyright 2026 Kerem Soke
# SPDX-License-Identifier: GPL-3.0-or-later
"""--debug-scopes prints debug output of exactly the named scopes;
info always prints; --log writes to a file; a bad --log path fails."""

import os
import subprocess
import sys
import tempfile

polo = sys.argv[1]
SCOPES = ["polo", "polo-policy", "polo-input", "polo-xwm", "polo-session",
          "polo-effects", "polo-protocol"]


def run(*args):
    return subprocess.run([polo, "--log-self-test", *args], capture_output=True, text=True)


def debug_scopes(stderr):
    found = set()
    for line in stderr.splitlines():
        if line.endswith(": self-test debug"):
            found.add(line.split("] ", 1)[1].rsplit(": ", 1)[0])
    return found


r = run()
assert r.returncode == 0, r
assert debug_scopes(r.stderr) == set(), r.stderr
assert "polo: self-test info" in r.stderr, r.stderr
assert "self-test glib debug" not in r.stderr, r.stderr
assert "self-test glib message" in r.stderr, r.stderr

for scope in SCOPES:
    r = run(f"--debug-scopes={scope}")
    assert debug_scopes(r.stderr) == {scope}, (scope, r.stderr)

r = run("--debug-scopes=polo-input, polo-xwm,not-a-scope")
assert debug_scopes(r.stderr) == {"polo-input", "polo-xwm"}, r.stderr

r = run("--debug-scopes=polo")
assert "self-test glib debug" in r.stderr, r.stderr

with tempfile.TemporaryDirectory() as tmp:
    log = os.path.join(tmp, "polo.log")
    r = run(f"--log={log}", "--debug-scopes=polo-session")
    assert r.returncode == 0 and r.stderr == "", r
    text = open(log).read()
    assert "polo: self-test info" in text and "polo-session: self-test debug" in text, text

r = run("--log=/nonexistent-polo-dir/polo.log")
assert r.returncode == 1 and "Cannot open log file" in r.stderr, r
print("logging ok")
