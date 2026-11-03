#!/usr/bin/env python3
# Copyright 2026 Kerem Soke
# SPDX-License-Identifier: GPL-3.0-or-later
"""Enforce Polo's Vala/C boundary.

Rules:
  1  Vala code uses only the allowed packages and never names libweston,
     Wayland, X or xkb namespaces.
  2  Only lib/backend/ and lib/xwm/ include libweston or wayland-server.
  3  lib/marco/, lib/fx/ and lib/bg/ include only libc, glib.h, cairo,
     pango and their own headers (they stay pure); lib/bg/ may also use
     gdk-pixbuf (image decoding) and time.h (slideshow start times), and
     lib/marco/ gdk-pixbuf (theme images, which include SVG).
  4  src/policy/ does not use the backend (polo-backend.vapi) at all.

Usage:
  check-boundary.py [--root DIR] [--expect RULE]

Without --expect, exits 1 if any violation is found. With --expect N
(used by the self-tests in tests/boundary/), exits 0 only if at least one
violation was found and every violation is of rule N.
"""

import argparse
import pathlib
import re
import sys

ALLOWED_VALA_PKGS = {
    "glib-2.0", "gobject-2.0", "gio-2.0", "gio-unix-2.0", "posix",
    "libpulse", "libpulse-mainloop-glib", "libcanberra",
    "polo-config", "polo-backend", "polo-xwm", "polo-marco", "polo-fx", "polo-bg",
    "polo-xsmp",
}
SHELL_UI_ONLY_PKGS = {"gtk+-3.0", "gtk-layer-shell-0", "mate-desktop-2.0", "polo-shell-ui-client"}
# json-glib is test-only; the compositor does not link it.
TESTS_ONLY_PKGS = {"json-glib-1.0", "polo-policy"}

FORBIDDEN_VALA_NS = r"(?:Weston|Wl|Wayland|Xcb|X|Xkb|LibInput|Pixman|Gbm|Drm|EGL|Egl)"
FORBIDDEN_VALA_RE = re.compile(
    r"(?:\busing\s+" + FORBIDDEN_VALA_NS + r"\s*;|(?<![\w.])" + FORBIDDEN_VALA_NS + r"\s*\.)")

LIBWESTON_INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"](libweston/|wayland-server)')
INCLUDE_RE = re.compile(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]')
LIBC_HEADERS = {
    "assert.h", "ctype.h", "errno.h", "float.h", "inttypes.h", "limits.h",
    "math.h", "stdalign.h", "stdarg.h", "stdbool.h", "stddef.h", "stdint.h",
    "stdio.h", "stdlib.h", "string.h", "strings.h",
}
PURE_EXTRA = re.compile(r"^(glib\.h|cairo\.h|cairo/.*|pango/.*)$")
BG_EXTRA = re.compile(r"^(gdk-pixbuf/gdk-pixbuf\.h|time\.h)$")
MARCO_EXTRA = re.compile(r"^gdk-pixbuf/gdk-pixbuf\.h$")

BACKEND_USE_RE = re.compile(r"(?<![\w.])(?:Polo\s*\.\s*)?Backend\s*\.|\busing\s+Polo\.Backend\b")
PKG_PAIR_RE = re.compile(r"""['"]--pkg['"]\s*,\s*['"]([^'"]+)['"]|['"]--pkg=([^'"]+)['"]""")
# Dynamic --pkg arguments are rejected because this check needs a literal.
PKG_DYNAMIC_RE = re.compile(r"""['"]--pkg['"]\s*,\s*(?!['"][^'"]+['"]\s*(?:[,\])]|$))\S""")

SKIP_DIRS = {"subprojects", ".git", "tests/boundary"}


def strip_vala(text):
    """Blank comments and literals while preserving line numbers."""
    def blank(m):
        return re.sub(r"[^\n]", " ", m.group(0))
    pattern = r'//[^\n]*|/\*.*?\*/|"""(?:.|\n)*?"""|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\''
    return re.sub(pattern, blank, text, flags=re.S)


def files(root, patterns):
    for pattern in patterns:
        for path in sorted(root.rglob(pattern)):
            rel = path.relative_to(root).as_posix()
            if any(rel == d or rel.startswith(d + "/") for d in SKIP_DIRS):
                continue
            if rel.split("/")[0].startswith(("build", "_build")):
                continue
            yield path, rel


# Sources built into libpolo-policy.
POLICY_LIBRARY_FILES = {"src/core/window.vala", "src/core/window-adapter.vala"}


def in_policy_library(rel):
    return rel.startswith("src/policy/") or rel in POLICY_LIBRARY_FILES


def check(root):
    violations = []

    def add(rule, rel, line, msg):
        violations.append((rule, f"rule {rule}: {rel}:{line}: {msg}"))

    for path, rel in files(root, ["*.vala"]):
        text = strip_vala(path.read_text(encoding="utf-8"))
        for n, line in enumerate(text.splitlines(), 1):
            m = FORBIDDEN_VALA_RE.search(line)
            if m:
                add(1, rel, n, f"Vala code names a forbidden namespace: {m.group(0).strip()}")
            if in_policy_library(rel) and BACKEND_USE_RE.search(line):
                add(4, rel, n, "the policy library must not use the backend")

    for path, rel in files(root, ["meson.build"]):
        top = rel.split("/")[0]
        if top not in ("src", "shell-ui", "tests"):
            continue
        for n, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            if PKG_DYNAMIC_RE.search(line):
                add(1, rel, n, "--pkg must be followed by a string literal so it can be checked")
            for m in PKG_PAIR_RE.finditer(line):
                pkg = m.group(1) or m.group(2)
                allowed = ALLOWED_VALA_PKGS | (SHELL_UI_ONLY_PKGS if top == "shell-ui" else set()) \
                    | (TESTS_ONLY_PKGS if top == "tests" else set())
                if pkg not in allowed:
                    add(1, rel, n, f"Vala package not allowed here: {pkg}")
                if rel.startswith("src/policy/") and pkg == "polo-backend":
                    add(4, rel, n, "the policy library must not use polo-backend")

    for path, rel in files(root, ["*.c", "*.h"]):
        in_backend = rel.startswith(("lib/backend/", "lib/xwm/"))
        pure = rel.startswith(("lib/marco/", "lib/fx/", "lib/bg/"))
        extra_ok = BG_EXTRA if rel.startswith("lib/bg/") else \
            MARCO_EXTRA if rel.startswith("lib/marco/") else None
        for n, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            if not in_backend and LIBWESTON_INCLUDE_RE.match(line):
                add(2, rel, n, "only lib/backend/ and lib/xwm/ may include libweston or wayland-server")
            if pure:
                m = INCLUDE_RE.match(line)
                if not m:
                    continue
                kind, header = m.groups()
                local = kind == '"' and (path.parent / header).exists()
                if not (local or header in LIBC_HEADERS or PURE_EXTRA.match(header)
                        or (extra_ok and extra_ok.match(header))):
                    add(3, rel, n, f"pure library includes {header}")

    return violations


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=pathlib.Path,
                        default=pathlib.Path(__file__).resolve().parent.parent)
    parser.add_argument("--expect", type=int, choices=[1, 2, 3, 4])
    args = parser.parse_args()

    violations = check(args.root.resolve())
    for _, msg in violations:
        print(msg)

    if args.expect is None:
        if violations:
            print(f"{len(violations)} boundary violation(s).")
            return 1
        return 0

    rules = {rule for rule, _ in violations}
    if rules == {args.expect}:
        return 0
    print(f"expected only rule {args.expect} violations, got rules {sorted(rules) or 'none'}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
