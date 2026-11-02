#!/usr/bin/env python3
# Copyright 2026 Kerem Soke
# SPDX-License-Identifier: GPL-3.0-or-later
"""polo --version names the private libweston, and the binary
resolves libweston-16 from the build tree, never from the system."""

import re
import subprocess
import sys

polo, build_root = sys.argv[1], sys.argv[2]

out = subprocess.run([polo, "--version"], check=True, capture_output=True, text=True).stdout
assert re.search(r"^polo \d+\.\d+\.\d+$", out, re.M), out
assert re.search(r"^libweston 16\.0\.1 \(private\)$", out, re.M), out

ldd = subprocess.run(["ldd", polo], check=True, capture_output=True, text=True).stdout
lines = [l for l in ldd.splitlines() if "libweston" in l]
assert len(lines) == 1, ldd
path = lines[0].split("=>")[1].split("(")[0].strip()
real = subprocess.run(["realpath", path], check=True, capture_output=True, text=True).stdout.strip()
assert "libweston-16.so" in real, real
assert real.startswith(build_root), f"libweston resolved outside the build tree: {real}"
print(out, real)
