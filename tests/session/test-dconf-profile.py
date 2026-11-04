#!/usr/bin/env python3
# Copyright 2026 Kerem Soke
# SPDX-License-Identifier: GPL-3.0-or-later
"""The polo dconf profile and its locks, read without writing anything.

  test-dconf-profile.py SOURCE_DIR

Compiles data/dconf/db/polo.d into a temporary database and reads it
through a profile like /etc/dconf/profile/polo, with file-db: standing in
for system-db:polo. In it the six plugins msd must leave to Polo are off
and locked; under the default profile, as in the X session, they are not.
"""

import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

PLUGINS = ["xrandr", "keyboard", "mouse", "media-keys", "a11y-keyboard", "background"]
SCHEMA = "org.mate.SettingsDaemon.plugins.{}"


def gsettings(env, *args):
    return subprocess.run(["gsettings", *args], env=env, capture_output=True, text=True,
                          check=True).stdout.strip()


def main():
    source = pathlib.Path(sys.argv[1])
    if not shutil.which("dconf") or not shutil.which("gsettings"):
        return 77
    schemas = gsettings(dict(os.environ), "list-schemas").split()
    if any(SCHEMA.format(p) not in schemas for p in PLUGINS):
        return 77
    failures = []
    profile_text = (source / "data/dconf/profile/polo").read_text().split()
    if profile_text != ["user-db:user", "system-db:polo"]:
        failures.append(f"/etc/dconf/profile/polo is {profile_text}")

    tmp = pathlib.Path(tempfile.mkdtemp(prefix="polo-dconf-"))
    try:
        subprocess.check_call(["dconf", "compile", str(tmp / "polo"),
                               str(source / "data/dconf/db/polo.d")])
        (tmp / "profile").write_text(f"user-db:user\nfile-db:{tmp / 'polo'}\n")
        base = {k: v for k, v in os.environ.items() if k not in ("DCONF_PROFILE", "GSETTINGS_BACKEND")}
        base.update(XDG_CONFIG_HOME=str(tmp / "config"), GSETTINGS_BACKEND="dconf")
        polo = dict(base, DCONF_PROFILE=str(tmp / "profile"))
        for plugin in PLUGINS:
            schema = SCHEMA.format(plugin)
            if gsettings(polo, "get", schema, "active") != "false":
                failures.append(f"{schema} active is not false in the Polo session")
            if gsettings(polo, "writable", schema, "active") != "false":
                failures.append(f"{schema} active is writable in the Polo session")
            if gsettings(base, "writable", schema, "active") != "true":
                failures.append(f"{schema} active is locked outside the Polo session")
        if gsettings(polo, "writable", SCHEMA.format("sound"), "active") != "true":
            failures.append("a plugin Polo does not replace is locked in the Polo session")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    for line in failures:
        print(line)
    print(f"{len(failures)} failure(s)" if failures else "ok")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
