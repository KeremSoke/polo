#!/usr/bin/env python3
# Copyright 2026 Kerem Soke
# SPDX-License-Identifier: GPL-3.0-or-later
"""XSMP as mate-session 1.28's gsm-xsmp-client.c speaks it, with real Polo.

  test-xsmp.py POLO XSMP_TEST_CLIENT

A libSM client autostarted through SESSION_MANAGER registers, gets the
initial local SaveYourself, appears on D-Bus as a session client, gets a
global SaveYourself with interaction at the query and a saving one at the
end of the session, then Die; with auto-save-session on its restart
command is saved. A client that asks to interact and cancels keeps the
session running.
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


def call(bus, method, args=None, reply="()", path=ROOT, interface=NAME):
    return bus.call_sync(NAME, path, interface, method, args, GLib.VariantType(reply),
                         Gio.DBusCallFlags.NONE, 5000, None).unpack()


def lines(path):
    return path.read_text().splitlines() if path.exists() else []


def start(polo, client, tmp, mode):
    config = tmp / "config"
    (config / "autostart").mkdir(parents=True, exist_ok=True)
    log = tmp / f"xsmp-{mode}.log"
    (config / "autostart" / f"polo-xsmp-{mode}.desktop").write_text(
        "[Desktop Entry]\nType=Application\nName=XSMP test client\n"
        f"Exec={client} {log} {mode}\nX-MATE-Autostart-Phase=Application\n")
    session = sm_session.Polo(polo, env_extra={
        "XDG_CONFIG_HOME": str(config), "GSETTINGS_BACKEND": "keyfile",
        "GSETTINGS_SCHEMA_DIR": str(tmp / "schemas"), "POLO_TEST_AUTOSTART": "1",
        "ICEAUTHORITY": str(tmp / "ICEauthority")})
    if not session.wait_owned():
        raise RuntimeError(f"polo never owned {NAME}")
    if not until(lambda: any(line.startswith("registered") for line in lines(log))):
        raise RuntimeError(f"{mode}: the XSMP client never registered: {lines(log)}")
    return session, log


def normal(polo, client, tmp):
    session, log = start(polo, client, tmp, "normal")
    try:
        _, client_id, pid = next(line for line in lines(log) if line.startswith("registered")).split()
        check(until(lambda: "SaveYourself 1 0 0 0" in lines(log)),
              f"normal: no initial local SaveYourself: {lines(log)}")
        paths = call(session.bus, "GetClients", reply="(ao)")[0]
        check(len(paths) == 1, f"normal: the XSMP client is not a session client: {paths}")
        if paths:
            client_iface = NAME + ".Client"
            check(call(session.bus, "GetStartupId", reply="(s)", path=paths[0], interface=client_iface) ==
                  (client_id,), "normal: GetStartupId is not the XSMP client id")
            check(until(lambda: call(session.bus, "GetUnixProcessId", reply="(u)", path=paths[0],
                                     interface=client_iface) == (int(pid),)),
                  "normal: GetUnixProcessId is not SmProcessID")
            check(call(session.bus, "GetRestartStyleHint", reply="(u)", path=paths[0],
                       interface=client_iface) == (1,), "normal: SmRestartIfRunning is not gsm's 1")

        call(session.bus, "Logout", GLib.Variant("(u)", (1,)))
        try:
            status = session.process.wait(20)
        except subprocess.TimeoutExpired:
            status = None
        check(status == 0, f"normal: polo did not end the session: {status}")
        seen = lines(log)
        check("SaveYourself 0 1 2 0" in seen, f"normal: no interactive global SaveYourself at the query: {seen}")
        check("SaveYourself 2 1 0 1" in seen, f"normal: no saving SaveYourself at the end of session: {seen}")
        check("Die" in seen, f"normal: no Die: {seen}")
        saved = tmp / "config" / "mate-session" / "saved-session" / f"{client_id}.desktop"
        text = saved.read_text() if saved.exists() else ""
        check(f"X-MATE-Autostart-startup-id={client_id}" in text and str(client) in text,
              f"normal: the client was not saved with its restart command: {text!r}")
    finally:
        session.stop()


def interact_cancel(polo, client, tmp):
    session, log = start(polo, client, tmp, "interact-cancel")
    try:
        call(session.bus, "Logout", GLib.Variant("(u)", (1,)))
        check(until(lambda: "ShutdownCancelled" in lines(log)),
              f"interact-cancel: the shutdown was not cancelled: {lines(log)}")
        check(until(lambda: call(session.bus, "IsSessionRunning", reply="(b)") == (True,)),
              "interact-cancel: the session is not Running again")
        call(session.bus, "Logout", GLib.Variant("(u)", (2,)))
        try:
            status = session.process.wait(20)
        except subprocess.TimeoutExpired:
            status = None
        check(status == 0, f"interact-cancel: a forced logout did not end the session: {status}")
        check("SaveYourself 0 1 0 1" in lines(log), f"interact-cancel: the forced query let it interact: {lines(log)}")
    finally:
        session.stop()


def main():
    status = sm_session.on_private_bus("POLO_XSMP_BUS")
    if status is not None:
        return status
    if not shutil.which("glib-compile-schemas"):
        return 77
    polo, client = sys.argv[1], pathlib.Path(sys.argv[2]).resolve()
    for run in (normal, interact_cancel):
        tmp = pathlib.Path(tempfile.mkdtemp(prefix="polo-xsmp-"))
        (tmp / "schemas").mkdir()
        (tmp / "schemas" / "org.mate.session.gschema.xml").write_text(SCHEMA)
        subprocess.check_call(["glib-compile-schemas", str(tmp / "schemas")])
        try:
            run(polo, client, tmp)
        except RuntimeError as e:
            failures.append(str(e))
        finally:
            shutil.rmtree(tmp, ignore_errors=True)
    for line in failures:
        print(line)
    print(f"{len(failures)} failure(s)" if failures else "ok")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
