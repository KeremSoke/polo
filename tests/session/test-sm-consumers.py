#!/usr/bin/env python3
# Copyright 2026 Kerem Soke
# SPDX-License-Identifier: GPL-3.0-or-later
"""One test per consumer of org.gnome.SessionManager in the M12.2 audit
(docs/dbus/sm-consumers.md), against real Polo.

  test-sm-consumers.py POLO FAKE_SHELL_UI

GTK, mate-session-save and mate-session-inhibit run for real when installed
(and are skipped otherwise); the others replay the consumer's calls with the
arguments the audit records. Each ends with a line per test: ok, FAIL or skip.
"""

import os
import pathlib
import shutil
import signal
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
PRESENCE = ROOT + "/Presence"
CANCEL, ACCEPT, LOGOUT = -6, -3, 0
results = {}
current = None


def check(condition, message):
    if not condition:
        results.setdefault(current, []).append(message)


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
    path = pathlib.Path(path)
    return path.read_text().splitlines() if path.exists() else []


class Session:
    def __init__(self, polo, fake_ui, response=CANCEL, autostart=None):
        self.dir = pathlib.Path(tempfile.mkdtemp(prefix="polo-consumers-"))
        self.ui_log = self.dir / "ui"
        env = {"FAKE_SHELL_UI_LOG": str(self.ui_log), "FAKE_SHELL_UI_DIALOG_RESPONSE": str(response)}
        if autostart:
            config = self.dir / "config"
            (config / "autostart").mkdir(parents=True)
            for name, text in autostart.items():
                (config / "autostart" / f"{name}.desktop").write_text(text)
            env.update(XDG_CONFIG_HOME=str(config), POLO_TEST_AUTOSTART="1")
        self.seen = []
        self.polo = sm_session.Polo(polo, shell_ui=fake_ui, env_extra=env, before_start=self.subscribe)
        if not self.polo.wait_owned():
            raise RuntimeError(f"polo never owned {NAME}")
        self.bus = self.polo.bus
        self.children = []
        until(lambda: "bound" in lines(self.ui_log), 10)

    def subscribe(self, bus):
        """Before polo starts, so that no early signal such as SessionRunning is missed."""
        for path, interface in ((ROOT, NAME), (PRESENCE, NAME + ".Presence")):
            bus.signal_subscribe(None, interface, None, path, None, Gio.DBusSignalFlags.NONE,
                                 lambda c, s, p, i, name, params: self.seen.append((name, params.unpack())))

    def env(self):
        return dict(os.environ, WAYLAND_DISPLAY="polo-sm-test", XDG_RUNTIME_DIR=self.polo.tmp + "/run",
                    GDK_BACKEND="wayland")

    def call(self, method, args=None, reply="()", path=ROOT, interface=NAME):
        return self.bus.call_sync(NAME, path, interface, method, args, GLib.VariantType(reply),
                                  Gio.DBusCallFlags.NONE, 5000, None).unpack()

    def get(self, path, interface, prop):
        return self.call("Get", GLib.Variant("(ss)", (interface, prop)), "(v)", path,
                         "org.freedesktop.DBus.Properties")[0]

    def error(self, method, args=None, reply="()"):
        try:
            self.call(method, args, reply)
        except GLib.Error as e:
            return Gio.DBusError.get_remote_error(e)
        return None

    def dialogs(self):
        return [line for line in lines(self.ui_log) if line.startswith("show_dialog")]

    def wait_dialog(self, kind, count):
        return until(lambda: len(self.dialogs()) >= count and f"'{kind}'" in self.dialogs()[count - 1])

    def running(self):
        return until(lambda: self.call("IsSessionRunning", reply="(b)") == (True,))

    def spawn(self, argv, **kwargs):
        process = subprocess.Popen(argv, env=self.env(), stdout=subprocess.DEVNULL,
                                   stderr=subprocess.DEVNULL, **kwargs)
        self.children.append(process)
        return process

    def client(self, app_id):
        log = self.dir / f"{app_id}.log"
        self.spawn([sys.executable, str(HERE / "sm-fake-client.py"), str(log), app_id])
        if not until(lambda: any("registered" in line for line in lines(log))):
            raise RuntimeError(f"{app_id} never registered")
        return log

    def ended(self):
        try:
            status = self.polo.process.wait(20)
        except subprocess.TimeoutExpired:
            status = None
        return status == 0

    def close(self):
        for process in self.children:
            if process.poll() is None:
                process.kill()
            process.wait()
        self.polo.stop()
        shutil.rmtree(self.dir, ignore_errors=True)


def inhibitor_paths(session):
    return session.call("GetInhibitors", reply="(ao)")[0]


def inhibitor(session, path, method, reply):
    return session.call(method, reply=reply, path=path, interface=NAME + ".Inhibitor")[0]


def test(name, needs=None):
    def wrap(function):
        function.test_name = name
        function.needs = needs
        return function
    return wrap


# ---- The shared session ------------------------------------------------------

@test("panel-logout-shutdown")
def panel_dialogs(s):
    check(isinstance(s.call("CanShutdown", reply="(b)")[0], bool), "CanShutdown gave no boolean")
    before = len(s.dialogs())
    s.call("Shutdown")
    check(s.wait_dialog("shutdown", before + 1), f"Shutdown() did not show the shutdown dialog: {s.dialogs()}")
    check(s.running(), "the cancelled shutdown dialog ended the session")
    s.call("Logout", GLib.Variant("(u)", (0,)))
    check(s.wait_dialog("logout", before + 2), f"Logout(0) did not show the logout dialog: {s.dialogs()}")
    check(s.running(), "the cancelled logout dialog ended the session")


@test("caja-inhibit")
def caja_inhibit(s):
    cookie = s.call("Inhibit", GLib.Variant("(susu)", ("caja", 0, "Copying files", 1 | 4)), "(u)")[0]
    check(s.call("IsInhibited", GLib.Variant("(u)", (1,)), "(b)") == (True,), "logout is not inhibited")
    check(s.call("IsInhibited", GLib.Variant("(u)", (4,)), "(b)") == (True,), "suspend is not inhibited")
    check(s.call("IsInhibited", GLib.Variant("(u)", (8,)), "(b)") == (False,), "idle is inhibited")
    before = len(s.dialogs())
    s.call("Logout", GLib.Variant("(u)", (1,)))
    check(s.wait_dialog("inhibit", before + 1) and "'caja', 'Copying files'" in s.dialogs()[-1],
          f"the blocking dialog does not show caja's reason: {s.dialogs()[-1:]}")
    check(s.running(), "cancelling the blocking dialog ended the session")
    s.call("Uninhibit", GLib.Variant("(u)", (cookie,)))
    check(not inhibitor_paths(s), "Uninhibit left caja's inhibitor")


@test("screensaver-inhibit")
def screensaver_inhibit(s):
    cookie = s.call("Inhibit", GLib.Variant("(susu)", ("mate-screensaver", 42, "Playing video", 8)), "(u)")[0]
    paths = inhibitor_paths(s)
    check(s.call("IsInhibited", GLib.Variant("(u)", (8,)), "(b)") == (True,), "idle is not inhibited")
    if paths:
        check(inhibitor(s, paths[0], "GetToplevelXid", "(u)") == 42, "the toplevel xid was not kept")
    s.call("Uninhibit", GLib.Variant("(u)", (cookie,)))
    check(s.call("IsInhibited", GLib.Variant("(u)", (8,)), "(b)") == (False,), "idle is still inhibited")


@test("screensaver-presence")
def screensaver_presence(s):
    interface = NAME + ".Presence"
    check(s.get(PRESENCE, interface, "status") == 0, "Presence status is not available")
    check(isinstance(s.get(PRESENCE, interface, "status-text"), str), "Presence status-text is not a string")
    s.call("SetStatus", GLib.Variant("(u)", (2,)), path=PRESENCE, interface=interface)
    check(until(lambda: ("StatusChanged", (2,)) in s.seen), "no StatusChanged")
    s.call("SetStatusText", GLib.Variant("(s)", ("away",)), path=PRESENCE, interface=interface)
    check(until(lambda: ("StatusTextChanged", ("away",)) in s.seen), "no StatusTextChanged")
    s.call("SetStatus", GLib.Variant("(u)", (0,)), path=PRESENCE, interface=interface)


@test("power-manager-session")
def power_manager_inhibitors(s):
    answers = []

    def recheck(connection, sender, path, interface, name, parameters):
        answers.append((name, s.call("IsInhibited", GLib.Variant("(u)", (8,)), "(b)")[0],
                        s.call("IsInhibited", GLib.Variant("(u)", (4,)), "(b)")[0]))

    ids = [s.bus.signal_subscribe(NAME, NAME, member, ROOT, None, Gio.DBusSignalFlags.NONE, recheck)
           for member in ("InhibitorAdded", "InhibitorRemoved")]
    cookie = s.call("Inhibit", GLib.Variant("(susu)", ("caja", 0, "Copying files", 4)), "(u)")[0]
    check(until(lambda: ("InhibitorAdded", False, True) in answers), f"IsInhibited after InhibitorAdded: {answers}")
    s.call("Uninhibit", GLib.Variant("(u)", (cookie,)))
    check(until(lambda: ("InhibitorRemoved", False, False) in answers),
          f"IsInhibited after InhibitorRemoved: {answers}")
    for i in ids:
        s.bus.signal_unsubscribe(i)
    check(s.get(PRESENCE, NAME + ".Presence", "status") in (0, 1, 2, 3), "Presence status for the power manager")


@test("power-applet-inhibit")
def power_applet_inhibit(s):
    cookie = s.call("Inhibit", GLib.Variant("(susu)", ("inhibit-applet", 0, "Manual inhibit", 1 | 2 | 4 | 8)),
                    "(u)")[0]
    for flag in (1, 2, 4, 8):
        check(s.call("IsInhibited", GLib.Variant("(u)", (flag,)), "(b)") == (True,), f"flag {flag} not inhibited")
    s.call("Uninhibit", GLib.Variant("(u)", (cookie,)))


@test("system-info-renderer")
def system_info_renderer(s):
    proxy = Gio.DBusProxy.new_sync(s.bus, Gio.DBusProxyFlags.NONE, None, NAME, ROOT, NAME, None)
    renderer = proxy.get_cached_property("Renderer")
    check(renderer is not None and renderer.get_type_string() == "s", "Renderer is not a cached string property")


@test("session-save", needs="mate-session-save")
def session_save_dialogs(s):
    before = len(s.dialogs())
    s.spawn(["mate-session-save", "--logout-dialog"]).wait(15)
    check(s.wait_dialog("logout", before + 1), f"--logout-dialog showed no logout dialog: {s.dialogs()}")
    s.spawn(["mate-session-save", "--shutdown-dialog"]).wait(15)
    check(s.wait_dialog("shutdown", before + 2), f"--shutdown-dialog showed no shutdown dialog: {s.dialogs()}")
    check(s.running(), "a cancelled mate-session-save dialog ended the session")


@test("session-inhibit", needs="mate-session-inhibit")
def session_inhibit(s):
    process = s.spawn(["mate-session-inhibit", "--inhibit", "logout:automount", "--reason", "testing",
                       "sleep", "30"])
    check(until(lambda: len(inhibitor_paths(s)) == 1), "mate-session-inhibit did not inhibit")
    paths = inhibitor_paths(s)
    if paths:
        check(inhibitor(s, paths[0], "GetReason", "(s)") == "testing", "the reason was not kept")
        check(inhibitor(s, paths[0], "GetFlags", "(u)") == 1 | 16, "logout and automount were not both kept")
        check(s.call("IsInhibited", GLib.Variant("(u)", (1,)), "(b)") == (True,), "logout is not inhibited")
    process.send_signal(signal.SIGTERM)
    check(until(lambda: not inhibitor_paths(s)), "the inhibitor outlived mate-session-inhibit")


@test("gtk-application", needs="gtk")
def gtk_application(s):
    log = s.dir / "gtk.log"
    process = s.spawn([sys.executable, str(HERE / "sm-gtk-app.py"), str(log)])
    check(until(lambda: any(line.startswith("inhibited") for line in lines(log)), 15),
          f"the GtkApplication did not inhibit: {lines(log)}")
    clients = s.call("GetClients", reply="(ao)")[0]
    ids = [s.call("GetAppId", reply="(s)", path=c, interface=NAME + ".Client")[0] for c in clients]
    check("org.mate.polo.SmTest" in ids, f"GTK did not register with RegisterClient: {ids}")
    paths = inhibitor_paths(s)
    check(paths and inhibitor(s, paths[0], "GetReason", "(s)") == "testing GTK",
          "GTK's inhibitor reason is missing")
    process.send_signal(signal.SIGUSR1)
    check(until(lambda: not inhibitor_paths(s)), "GTK's Uninhibit did not remove its inhibitor")
    process.terminate()
    process.wait(10)
    check(until(lambda: "org.mate.polo.SmTest" not in [
        s.call("GetAppId", reply="(s)", path=c, interface=NAME + ".Client")[0]
        for c in s.call("GetClients", reply="(ao)")[0]]), "the GTK client outlived its process")


# ---- Autostart phases ----------------------------------------------------------

def autostart_entries(tmp):
    env_file = tmp / "qt-scale-factor"
    init = tmp / "setenv.sh"
    init.write_text("#!/bin/sh\nexec gdbus call --session --dest org.gnome.SessionManager "
                    "--object-path /org/gnome/SessionManager "
                    "--method org.gnome.SessionManager.Setenv QT_SCALE_FACTOR 2\n")
    init.chmod(0o755)
    app = tmp / "read-env.sh"
    app.write_text(f'#!/bin/sh\necho "$QT_SCALE_FACTOR" > {env_file}\nexec sleep 30\n')
    app.chmod(0o755)
    entries = {
        "polo-setenv": f"[Desktop Entry]\nType=Application\nName=Setenv\nExec={init}\n"
                       "X-MATE-Autostart-Phase=Initialization\n",
        "polo-read-env": f"[Desktop Entry]\nType=Application\nName=Read env\nExec={app}\n"
                         "X-MATE-Autostart-Phase=Application\n",
        "polo-a11y": "[Desktop Entry]\nType=Application\nName=AT\nExec=sleep 30\n"
                     "AutostartCondition=GSettings org.mate.interface accessibility\n",
    }
    return entries, env_file


@test("xsettings-setenv")
def xsettings_setenv(s, env_file):
    check(until(lambda: env_file.exists() and env_file.read_text().strip() == "2", 20),
          "Setenv in the Initialization phase did not reach a later app")
    check(s.error("Setenv", GLib.Variant("(ss)", ("QT_FONT_DPI", "96"))) == NAME + ".NotInInitialization",
          "Setenv was accepted after Initialization")


@test("background-session-running")
def background_session_running(s, env_file):
    check(until(lambda: any(name == "SessionRunning" for name, _ in s.seen), 30), "no SessionRunning")


@test("a11y-autostart-condition")
def a11y_autostart_condition(s, env_file):
    check(s.call("IsAutostartConditionHandled",
                 GLib.Variant("(s)", ("GSettings org.mate.interface accessibility",)), "(b)") == (True,),
          "an autostart app's condition is not handled")
    check(s.call("IsAutostartConditionHandled", GLib.Variant("(s)", ("GSettings org.example nope",)),
                 "(b)") == (False,), "an unknown condition is handled")


# ---- Ending the session ----------------------------------------------------------

@test("settings-daemon-client")
def end_with_clients(polo, fake_ui):
    s = Session(polo, fake_ui)
    try:
        msd, mpm = s.client("mate-settings-daemon"), s.client("mate-power-manager")
        gtk_log = s.dir / "gtk.log"
        gtk = None
        if have_gtk():
            gtk = s.spawn([sys.executable, str(HERE / "sm-gtk-app.py"), str(gtk_log)])
            until(lambda: any(line.startswith("inhibited") for line in lines(gtk_log)), 15)
            gtk.send_signal(signal.SIGUSR1)
            until(lambda: not inhibitor_paths(s))
        s.call("Logout", GLib.Variant("(u)", (1,)))
        check(s.ended(), "Logout(1) from the panel did not end the session")
        check(until(lambda: ("SessionOver", ()) in s.seen or ("SessionOver", None) in s.seen, 2),
              "no SessionOver")
        for log, who in ((msd, "settings-daemon"), (mpm, "power-manager")):
            seen = "\n".join(lines(log))
            for signal_name in ("QueryEndSession", "EndSession", "Stop"):
                check(f"{signal_name} " in seen, f"{who} did not get {signal_name}: {seen}")
        if gtk:
            check("query-end" in lines(gtk_log) and "quit" in lines(gtk_log),
                  f"GTK did not answer the end of session and quit: {lines(gtk_log)}")
    finally:
        s.close()


@test("smartcard-logout")
def smartcard_logout(polo, fake_ui):
    s = Session(polo, fake_ui)
    try:
        s.call("Logout", GLib.Variant("(u)", (2,)))
        check(s.ended(), "Logout(2) did not end the session")
        check(not s.dialogs(), f"a forced logout asked: {s.dialogs()}")
    finally:
        s.close()


@test("at-properties-logout")
def at_properties_logout(polo, fake_ui):
    s = Session(polo, fake_ui, response=LOGOUT)
    try:
        s.call("Logout", GLib.Variant("(u)", (0,)))
        check(s.ended(), "Logout(0) answered with Log Out did not end the session")
        check(s.dialogs() and "'logout'" in s.dialogs()[0], f"Logout(0) did not ask: {s.dialogs()}")
    finally:
        s.close()


@test("session-save --logout", needs="mate-session-save")
def session_save_logout(polo, fake_ui):
    for option in ("--logout", "--force-logout"):
        s = Session(polo, fake_ui)
        try:
            s.spawn(["mate-session-save", option])
            check(s.ended(), f"mate-session-save {option} did not end the session")
            check(not s.dialogs(), f"mate-session-save {option} asked: {s.dialogs()}")
        finally:
            s.close()


def have_gtk():
    try:
        import gi
        gi.require_version("Gtk", "3.0")
        return True
    except (ImportError, ValueError):
        return False


def available(needs):
    if needs is None:
        return True
    if needs == "gtk":
        return have_gtk()
    return shutil.which(needs) is not None


def run(test_function, *args):
    global current
    current = test_function.test_name
    if not available(test_function.needs):
        results[current] = None
        return
    results.setdefault(current, [])
    try:
        test_function(*args)
    except (RuntimeError, GLib.Error, subprocess.TimeoutExpired) as e:
        results[current].append(f"{type(e).__name__}: {e}")


def main():
    status = sm_session.on_private_bus("POLO_SM_CONSUMERS_BUS")
    if status is not None:
        return status
    polo, fake_ui = sys.argv[1], sys.argv[2]

    shared = Session(polo, fake_ui)
    try:
        for t in (panel_dialogs, caja_inhibit, screensaver_inhibit, screensaver_presence,
                  power_manager_inhibitors, power_applet_inhibit, system_info_renderer,
                  session_save_dialogs, session_inhibit, gtk_application):
            run(t, shared)
    finally:
        shared.close()

    tmp = pathlib.Path(tempfile.mkdtemp(prefix="polo-consumers-env-"))
    entries, env_file = autostart_entries(tmp)
    phased = Session(polo, fake_ui, autostart=entries)
    try:
        for t in (background_session_running, xsettings_setenv, a11y_autostart_condition):
            run(t, phased, env_file)
    finally:
        phased.close()
        shutil.rmtree(tmp, ignore_errors=True)

    for t in (end_with_clients, smartcard_logout, at_properties_logout, session_save_logout):
        run(t, polo, fake_ui)

    failed = 0
    for name, problems in results.items():
        if problems is None:
            print(f"skip  {name}")
        elif problems:
            failed += 1
            print(f"FAIL  {name}")
            for problem in problems:
                print(f"      {problem}")
        else:
            print(f"ok    {name}")
    print(f"{failed} failure(s)" if failed else "ok")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
