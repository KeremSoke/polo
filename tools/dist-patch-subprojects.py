#!/usr/bin/env python3
# Copyright 2026 Kerem Soke
# SPDX-License-Identifier: GPL-3.0-or-later
"""meson dist script: apply each wrap's diff_files to its subproject in the
dist tree, which meson archives from the subproject's unpatched commit. A
tarball then builds without fetching or patching anything."""

import configparser
import os
import pathlib
import subprocess
import sys

root = pathlib.Path(os.environ["MESON_DIST_ROOT"])
subprojects = root / "subprojects"
for wrap in sorted(subprojects.glob("*.wrap")):
    config = configparser.ConfigParser(interpolation=None)
    config.read(wrap)
    section = config.sections()[0]
    directory = subprojects / config[section].get("directory", wrap.stem)
    diffs = [d.strip().rstrip(",") for d in config[section].get("diff_files", "").split("\n")]
    if not directory.is_dir():
        continue
    for diff in filter(None, diffs):
        patch = subprojects / "packagefiles" / diff
        result = subprocess.run(["patch", "-p1", "--forward", "--silent", "-d", str(directory), "-i", str(patch)])
        if result.returncode != 0:
            sys.exit(f"{diff} does not apply to {directory.name}")
        print(f"applied {diff}")
