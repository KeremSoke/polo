#!/usr/bin/env python3
# Copyright 2026 Kerem Soke
# SPDX-License-Identifier: GPL-3.0-or-later
"""Saved sessions as mate-session 1.28 keeps them, with real Polo.

  test-saved-session.py POLO

With auto-save-session on: a saved-session entry starts at login with its
saved startup id, session-start is set, EndSession carries the save flag,
and logout replaces the saved session, running the old entry's discard
command. D-Bus clients are not saved, as in mate-session.
"""

import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import time

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
try:
    from gi.repository import Gio, GLib
    import sm_session
except ImportError:
    sys.exit(77)

NAME, ROOT = sm_session.NAME, sm_session.ROOT
failures = []

SCHEMA = """<schemalist>
  <schema id="org.mate.session" path="/org/mate/desktop/session/">
    <key name="auto-save-session" type="b"><default>true</default></key>
    <key name="session-start" type="i"><default>0</default></key>
    <key name="logout-prompt" type="b"><default>false</default></key>
    <key name="logout-timeout" type="i"><default>60</default></key>
    <key name="idle-delay" type="i"><default>5</default></key>
    <key name="default-session" type="as"><default>[]</default></key>
    <key name="required-components-list" type="as"><default>[]</default></key>
  </schema>
</schemalist>
"""


def check(condition, message):
    if not condition:
        failures.append(message)


def until(condition, seconds=10):
    context = GLib.MainContext.default()
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        while context.pending():
            context.iteration(False)
        if condition():
            return True
        time.sleep(0.05)
    return False


def script(path, body):
    path.write_text("#!/bin/sh\n" + body)
    path.chmod(0o755)
    return path


def main():
    status = sm_session.on_private_bus("POLO_SAVED_SESSION_BUS")
    if status is not None:
        return status
    if not shutil.which("glib-compile-schemas"):
        return 77
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="polo-saved-"))
    config = tmp / "config"
    saved = config / "mate-session" / "saved-session"
    saved.mkdir(parents=True)
    (tmp / "schemas").mkdir()
    (tmp / "schemas" / "org.mate.session.gschema.xml").write_text(SCHEMA)
    subprocess.check_call(["glib-compile-schemas", str(tmp / "schemas")])

    started, discarded = tmp / "started", tmp / "discarded"
    app = script(tmp / "saved-app.sh", f'echo "$DESKTOP_AUTOSTART_ID" > {started}\nexec sleep 30\n')
    discard = script(tmp / "discard.sh", f"touch {discarded}\n")
    (saved / "10saved.desktop").write_text(
        "[Desktop Entry]\nType=Application\nName=Saved\n"
        f"Exec={app}\nX-MATE-Autostart-startup-id=10saved\n"
        f"X-MATE-Autostart-discard-exec={discard}\n")

    polo = sm_session.Polo(sys.argv[1], env_extra={
        "XDG_CONFIG_HOME": str(config), "GSETTINGS_BACKEND": "keyfile",
        "GSETTINGS_SCHEMA_DIR": str(tmp / "schemas"), "POLO_TEST_AUTOSTART": "1"})
    client_log = tmp / "clients"
    client = None
    try:
        if not polo.wait_owned():
            print(f"polo never owned {NAME}")
            return 1
        check(until(lambda: started.exists() and started.read_text().strip() == "10saved"),
              "the saved entry did not start with its saved startup id")
        keyfile = config / "glib-2.0" / "settings" / "keyfile"
        check(until(lambda: keyfile.exists() and "session-start=" in keyfile.read_text()),
              "session-start was not set")

        client = subprocess.Popen([sys.executable, str(HERE / "sm-fake-client.py"), str(client_log),
                                   "polo-saved-client"])
        check(until(lambda: client_log.exists() and "registered" in client_log.read_text()),
              "the fake client did not register")
        polo.bus.call_sync(NAME, ROOT, NAME, "Logout", GLib.Variant("(u)", (1,)), None,
                           Gio.DBusCallFlags.NONE, 5000, None)
        try:
            exit_status = polo.process.wait(20)
        except subprocess.TimeoutExpired:
            exit_status = None
        check(exit_status == 0, f"polo did not end the session: {exit_status}")
        log = client_log.read_text() if client_log.exists() else ""
        check("EndSession (2,)" in log, f"EndSession did not carry the save flag:\n{log}")
        check(until(discarded.exists, 5), "the old entry's discard command did not run")
        check(saved.is_dir() and not any(saved.iterdir()),
              "the saved session was not replaced by an empty one (D-Bus clients are not saved)")
        check(not (config / "mate-session" / "saved-session.new").exists(),
              "saved-session.new was left behind")
    finally:
        if client is not None and client.poll() is None:
            client.kill()
        if client is not None:
            client.wait()
        polo.stop()
        shutil.rmtree(tmp, ignore_errors=True)
    for line in failures:
        print(line)
    print(f"{len(failures)} failure(s)" if failures else "ok")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
