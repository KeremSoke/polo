#!/usr/bin/env python3
# Copyright 2026 Kerem Soke
# SPDX-License-Identifier: GPL-3.0-or-later
"""The end of a session, as gsm-manager.c 1.28 runs it, with real Polo,
the fake polo-shell-ui and sm-fake-client.py clients.

  test-end-session.py POLO FAKE_SHELL_UI
"""

import os
import pathlib
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
ACCEPT, CANCEL = -3, -6
failures = []


def check(condition, message):
    if not condition:
        failures.append(message)


def call(bus, method, args=None, reply="()"):
    return bus.call_sync(NAME, ROOT, NAME, method, args, GLib.VariantType(reply),
                         Gio.DBusCallFlags.NONE, 5000, None).unpack()


def until(condition, seconds=10):
    context = GLib.MainContext.default()
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        while context.pending():
            context.iteration(False)
        if condition():
            return True
        time.sleep(0.02)
    return False


def lines(path):
    try:
        with open(path) as f:
            return f.read().splitlines()
    except OSError:
        return []


class Case:
    def __init__(self, name, polo, fake_ui, response):
        self.name = name
        self.dir = tempfile.mkdtemp(prefix="polo-end-")
        self.ui_log = self.dir + "/ui"
        self.client_log = self.dir + "/clients"
        self.polo = sm_session.Polo(polo, shell_ui=fake_ui, env_extra={
            "FAKE_SHELL_UI_LOG": self.ui_log, "FAKE_SHELL_UI_DIALOG_RESPONSE": str(response)})
        self.clients = []
        self.seen = []
        if not self.polo.wait_owned():
            raise RuntimeError(f"{name}: polo never owned {NAME}")
        self.bus = self.polo.bus
        self.bus.signal_subscribe(NAME, NAME, None, ROOT, None, Gio.DBusSignalFlags.NONE,
                                  lambda c, s, p, i, signal, params: self.seen.append((signal, params.unpack())))
        if not until(lambda: "bound" in lines(self.ui_log)):
            raise RuntimeError(f"{name}: the fake polo-shell-ui never bound")

    def client(self, app_id, behaviour=""):
        process = subprocess.Popen([sys.executable, str(HERE / "sm-fake-client.py"), self.client_log, app_id,
                                    behaviour])
        self.clients.append(process)
        if not until(lambda: any(line.startswith(app_id + " registered") for line in lines(self.client_log))):
            raise RuntimeError(f"{self.name}: {app_id} never registered")
        return process

    def got(self, app_id, signal_line):
        return f"{app_id} {signal_line}" in lines(self.client_log)

    def dialogs(self):
        return [line for line in lines(self.ui_log) if line.startswith("show_dialog")]

    def logout(self, mode):
        call(self.bus, "Logout", GLib.Variant("(u)", (mode,)))

    def ended(self):
        try:
            status = self.polo.process.wait(15)
        except subprocess.TimeoutExpired:
            status = None
        check(status == 0, f"{self.name}: polo did not end the session: {status}")
        check(("SessionOver", None) in self.seen or ("SessionOver", ()) in self.seen,
              f"{self.name}: SessionOver was not sent")

    def close(self):
        for process in self.clients:
            if process.poll() is None:
                process.kill()
            process.wait()
        status = self.polo.stop()
        if failures:
            print(f"--- {self.name}: clients\n" + "\n".join(lines(self.client_log)))
            print(f"--- {self.name}: polo-shell-ui\n" + "\n".join(lines(self.ui_log)))
        return status


def normal(polo, fake_ui):
    case = Case("normal", polo, fake_ui, 0)
    try:
        first, second = case.client("polo-one"), case.client("polo-two")
        case.logout(0)
        case.ended()
        dialogs = case.dialogs()
        check(len(dialogs) == 1 and "'dialog': <'logout'>" in dialogs[0],
              f"normal: logout-prompt did not ask once: {dialogs}")
        for app_id in ("polo-one", "polo-two"):
            for line in ("QueryEndSession (0,)", "EndSession (0,)", "Stop ()", "stopped"):
                check(case.got(app_id, line), f"normal: {app_id} did not get {line}")
        for process in (first, second):
            check(process.wait(5) == 0, "normal: a client did not exit on Stop")
    finally:
        case.close()


def forced(polo, fake_ui):
    case = Case("forced", polo, fake_ui, CANCEL)
    try:
        case.client("polo-busy", "refuse=busy")
        call(case.bus, "Inhibit", GLib.Variant("(susu)", ("polo-test", 0, "saving", 1)), "(u)")
        case.logout(2)
        case.ended()
        check(not case.dialogs(), f"forced: a forced logout asked: {case.dialogs()}")
        check(case.got("polo-busy", "QueryEndSession (1,)"), "forced: the query was not forceful")
        check(case.got("polo-busy", "EndSession (1,)"), "forced: EndSession was not forceful")
    finally:
        case.close()


def inhibited(polo, fake_ui):
    case = Case("inhibited", polo, fake_ui, ACCEPT)
    try:
        case.client("polo-one")
        call(case.bus, "Inhibit", GLib.Variant("(susu)", ("polo-test", 0, "saving", 1)), "(u)")
        case.logout(1)
        case.ended()
        dialogs = case.dialogs()
        check(len(dialogs) == 1 and "'inhibit'" in dialogs[0] and "'polo-test', 'saving'" in dialogs[0],
              f"inhibited: the blocking dialog did not list the inhibitor: {dialogs}")
        check(case.got("polo-one", "QueryEndSession (0,)"), "inhibited: the query was forceful")
        check(case.got("polo-one", "EndSession (1,)"), "inhibited: Log Out Anyway did not force EndSession")
    finally:
        case.close()


def cancelled(polo, fake_ui):
    case = Case("cancelled", polo, fake_ui, CANCEL)
    try:
        case.client("polo-busy", "refuse=busy")
        case.logout(1)
        check(until(lambda: case.got("polo-busy", "CancelEndSession ()")),
              "cancelled: CancelEndSession was not sent")
        dialogs = case.dialogs()
        check(len(dialogs) == 1 and "'inhibit'" in dialogs[0] and "'busy'" in dialogs[0],
              f"cancelled: the blocking dialog did not list the client: {dialogs}")
        check(until(lambda: call(case.bus, "IsSessionRunning", reply="(b)") == (True,)),
              "cancelled: the session is not Running again")
        check(call(case.bus, "GetInhibitors", reply="(ao)") == ([],),
              "cancelled: the client's inhibitor outlived the cancel")
        check(not case.got("polo-busy", "EndSession (0,)"), "cancelled: EndSession was sent")
        case.logout(2)
        case.ended()
    finally:
        case.close()


def timed_out(polo, fake_ui):
    case = Case("timed-out", polo, fake_ui, ACCEPT)
    try:
        stuck = case.client("polo-stuck", "silent-query,ignore-stop,ignore-term")
        case.logout(1)
        check(until(lambda: any(signal == "InhibitorAdded" for signal, _ in case.seen)),
              "timed-out: no inhibitor for the client that did not answer")
        check(until(lambda: any(signal == "InhibitorRemoved" for signal, _ in case.seen)),
              "timed-out: the client's inhibitor outlived its EndSession answer")
        dialogs = case.dialogs()
        check(len(dialogs) == 1 and "'polo-stuck'" in dialogs[0],
              f"timed-out: the blocking dialog did not list the client: {dialogs}")
        case.ended()
        check(case.got("polo-stuck", "Stop ()"), "timed-out: Stop was not sent")
        try:
            status = stuck.wait(5)
        except subprocess.TimeoutExpired:
            status = None
        check(status == -9, f"timed-out: a client that ignores SIGTERM was not killed: {status}")
    finally:
        case.close()


def main():
    status = sm_session.on_private_bus("POLO_END_SESSION_BUS")
    if status is not None:
        return status
    polo, fake_ui = sys.argv[1], sys.argv[2]
    for run in (normal, forced, inhibited, cancelled, timed_out):
        try:
            run(polo, fake_ui)
        except RuntimeError as e:
            failures.append(str(e))
    for line in failures:
        print(line)
    print(f"{len(failures)} failure(s)" if failures else "ok")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
