#!/usr/bin/env python3
# Copyright 2026 Kerem Soke
# SPDX-License-Identifier: GPL-3.0-or-later
"""What an install puts where, checked on a staged install (meson install
--destdir): the programs and Polo's private libweston with the run path that
finds it, the session entry, schema, dconf profile, database and locks,
systemd unit, portal configuration and man pages; nothing outside the prefix
and /etc/dconf; and nothing made for tests.

  test-install-layout.py BUILD-DIR

Registered only for builds without the test and debug options.
"""

import configparser
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

failures = []


def check(condition, message):
    if not condition:
        failures.append(message)
        print("FAIL:", message, file=sys.stderr)


def run(command, **kwargs):
    return subprocess.run(command, capture_output=True, text=True, **kwargs)


def build_options(build):
    out = run(["meson", "introspect", build, "--buildoptions"])
    if out.returncode != 0:
        sys.exit("meson introspect: " + out.stderr)
    return {o["name"]: o["value"] for o in json.loads(out.stdout)}


def read_keyfile(path):
    parser = configparser.ConfigParser(interpolation=None, strict=False)
    parser.optionxform = str
    parser.read(path)
    return parser


def main():
    if len(sys.argv) < 2:
        print("usage: test-install-layout.py BUILD-DIR", file=sys.stderr)
        return 2
    build = sys.argv[1]
    options = build_options(build)
    prefix = options["prefix"]
    info = run(["meson", "introspect", build, "--projectinfo"])
    version = json.loads(info.stdout)["version"] if info.returncode == 0 else None
    with tempfile.TemporaryDirectory(prefix="polo-install-") as stage:
        out = run(["meson", "install", "-C", build, "--no-rebuild", "--destdir", stage])
        check(out.returncode == 0, "meson install failed:\n" + out.stdout + out.stderr)

        def under(*parts):
            return os.path.join(stage, *[p.lstrip("/") for p in parts])

        bindir = os.path.join(prefix, options["bindir"])
        libexecdir = os.path.join(prefix, options["libexecdir"])
        libdir = os.path.join(prefix, options["libdir"])
        datadir = os.path.join(prefix, options["datadir"])
        mandir = os.path.join(prefix, options["mandir"])
        private = os.path.join(libdir, "polo")

        for program in (os.path.join(bindir, "polo"),
                        os.path.join(bindir, "polo-wayland.sh"),
                        os.path.join(bindir, "polo-screenshot"),
                        os.path.join(libexecdir, "polo-shell-ui"),
                        os.path.join(libexecdir, "polo-share-chooser")):
            check(os.access(under(program), os.X_OK), "%s is not an installed program" % program)

        libweston = [f for f in os.listdir(under(private)) if re.match(r"libweston-\d+\.so\.\d+$", f)] \
            if os.path.isdir(under(private)) else []
        check(len(libweston) == 1, "expected one libweston in %s, found %s" % (private, libweston))
        modules = os.path.join(private, "libweston-16")
        for module in ("drm-backend.so", "gl-renderer.so", "headless-backend.so", "wayland-backend.so"):
            check(os.path.isfile(under(modules, module)), "%s is not installed" % os.path.join(modules, module))
        check(not os.path.exists(under(libdir, "libweston-16.so.0")),
              "libweston is also installed in %s, where it meets the distribution's" % libdir)

        for path in (os.path.join(datadir, "wayland-sessions", "polo.desktop"),
                     os.path.join(datadir, "glib-2.0", "schemas", "org.mate.polo.gschema.xml"),
                     os.path.join(datadir, "polo", "xdg", "xdg-desktop-portal", "mate-portals.conf"),
                     "/etc/dconf/profile/polo", "/etc/dconf/db/polo.d/00-polo",
                     "/etc/dconf/db/polo.d/locks/00-polo",
                     os.path.join(mandir, "man1", "polo.1"), os.path.join(mandir, "man7", "polo-session.7")):
            check(os.path.isfile(under(path)) and os.path.getsize(under(path)) > 0,
                  "%s is missing or empty" % path)
        check(not os.path.exists(under(datadir, "xdg-desktop-portal", "mate-portals.conf")),
              "mate-portals.conf is installed where it replaces mate-desktop's for every session")

        units = []
        for root, _, files in os.walk(stage):
            units += [os.path.join(root, f) for f in files if f == "polo-session.target"]
        if options.get("systemd"):
            check(len(units) == 1 and "/systemd/user/" in units[0],
                  "polo-session.target is not in a systemd user unit directory: %s" % units)
            if units:
                text = open(units[0]).read()
                check("BindsTo=graphical-session.target" in text, "polo-session.target does not bind the graphical session")
        else:
            check(not units, "polo-session.target is installed without systemd support")

        entry = read_keyfile(under(datadir, "wayland-sessions", "polo.desktop"))
        section = entry["Desktop Entry"] if entry.has_section("Desktop Entry") else {}
        check(section.get("Exec") == "polo --session", "polo.desktop runs %r" % section.get("Exec"))
        check(section.get("TryExec") == "polo", "polo.desktop tries %r" % section.get("TryExec"))
        check(section.get("Type") == "Application", "polo.desktop is not an Application")
        if shutil.which("desktop-file-validate"):
            out = run(["desktop-file-validate", under(datadir, "wayland-sessions", "polo.desktop")])
            # Display managers read DesktopNames; the validator does not know the key.
            problems = [l for l in (out.stdout + out.stderr).splitlines() if l and '"DesktopNames"' not in l]
            check(not problems, "desktop-file-validate rejects polo.desktop: %s" % problems)

        if shutil.which("glib-compile-schemas"):
            out = run(["glib-compile-schemas", "--strict", "--dry-run",
                       under(datadir, "glib-2.0", "schemas")])
            check(out.returncode == 0, "the installed schema does not compile: " + out.stderr)

        profile = open(under("/etc/dconf/profile/polo")).read().split()
        check(profile == ["user-db:user", "system-db:polo"], "the dconf profile reads %s" % profile)
        database = read_keyfile(under("/etc/dconf/db/polo.d/00-polo"))
        defaults = {"/%s/%s" % (s, key) for s in database.sections() for key in database[s]}
        locks = {line.strip() for line in open(under("/etc/dconf/db/polo.d/locks/00-polo"))
                 if line.strip() and not line.startswith("#")}
        check(defaults == locks and defaults,
              "the dconf database and its locks differ: %s" % sorted(defaults ^ locks))

        stray = []
        for root, _, files in os.walk(stage):
            for name in files:
                path = os.path.join(root, name)[len(stage):]
                if not (path.startswith(prefix.rstrip("/") + "/") or path.startswith("/etc/dconf/")):
                    stray.append(path)
                if re.match(r"(test-|soak|fake-|list-globals|check-)", name) or "polo_test" in name:
                    stray.append(path)
        check(not stray, "installed outside the prefix and /etc/dconf, or made for tests: %s" % stray[:10])

        polo = under(bindir, "polo")
        if shutil.which("readelf") and os.path.exists(polo):
            dynamic = run(["readelf", "-d", polo]).stdout
            runpaths = re.findall(r"\(R(?:UN)?PATH\)\s+Library r(?:un)?path: \[(.*?)\]", dynamic)
            check(runpaths == [private], "polo's run path is %s, not %s" % (runpaths, private))
        if os.path.exists(polo):
            env = dict(os.environ, LD_LIBRARY_PATH=under(private))
            out = run([polo, "--version"], env=env)
            check(out.returncode == 0 and "libweston" in out.stdout and "(private)" in out.stdout,
                  "the installed polo --version says %r %r" % (out.stdout, out.stderr[:200]))

        for page in ("man1/polo.1", "man7/polo-session.7"):
            path = under(mandir, page)
            if not os.path.exists(path):
                continue
            text = open(path).read()
            check(text.lstrip().startswith(".\\\"") or ".TH " in text, "%s has no title line" % page)
            check("@" not in text.replace("\\@", ""), "%s has an unreplaced @ placeholder" % page)
            if version:
                check("Polo " + version in text, "%s does not name version %s" % (page, version))
            if shutil.which("groff"):
                lint = run(["groff", "-man", "-ww", "-z", path])
                check(lint.stderr.strip() == "", "groff warns about %s: %s" % (page, lint.stderr.strip()))

    if failures:
        return 1
    print("install layout: all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
