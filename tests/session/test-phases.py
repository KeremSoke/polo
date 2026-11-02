#!/usr/bin/env python3
# Copyright 2026 Kerem Soke
# SPDX-License-Identifier: GPL-3.0-or-later
"""polo --session starts the autostart set by phase. Every directory it reads is private, so
nothing installed on the machine starts.

  test-phases.py POLO MAP_CLIENT X_CLIENT SHELL_UI
"""

import os
import shutil
import stat
import subprocess
import sys
import tempfile
import time

POLO, MAP_CLIENT, X_CLIENT, SHELL_UI = sys.argv[1:5]
SETTLE = 0.5


GDM = """
import sys
from gi.repository import Gio, GLib
XML = \'<node><interface name="org.gnome.DisplayManager.Manager"><method name="RegisterSession">\' \\
      \'<arg type="a{sv}" direction="in"/></method></interface></node>\'
def call(conn, sender, path, iface, method, params, inv):
    with open(sys.argv[1], "a") as out:
        out.write("register\\n")
    inv.return_value(None)
bus = Gio.bus_get_sync(Gio.BusType.SESSION)
bus.register_object("/org/gnome/DisplayManager/Manager",
                    Gio.DBusNodeInfo.new_for_xml(XML).interfaces[0], call)
Gio.bus_own_name_on_connection(bus, "org.gnome.DisplayManager", 0,
                               lambda *a: print("ready", flush=True), None)
GLib.MainLoop().run()
"""


def check(condition, message):
    if not condition:
        print("check failed: " + message)
        sys.exit(1)


def wait_for(condition, seconds=10):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if condition():
            return True
        time.sleep(0.02)
    return condition()


class Session:
    def __init__(self, settings=""):
        self.tmp = tempfile.mkdtemp(prefix="pph-", dir="/tmp")
        self.log = self.tmp + "/log"
        self.config = self.tmp + "/config"
        self.autostart = self.config + "/autostart"
        os.makedirs(self.autostart)
        os.makedirs(self.tmp + "/run", mode=0o700)
        os.makedirs(self.tmp + "/data/applications")
        os.makedirs(self.tmp + "/none")
        self.mark = self.tmp + "/mark"
        with open(self.mark, "w") as f:
            f.write('#!/bin/sh\nname=$1\nshift\n'
                    'env > "$POLO_TEST_LOG.$name.env"\npwd > "$POLO_TEST_LOG.$name.pwd"\n'
                    'printf "%s\\n" "$@" > "$POLO_TEST_LOG.$name.args"\n'
                    'echo "start $name" >> "$POLO_TEST_LOG"\n'
                    'if [ -n "$POLO_TEST_HANG" ]; then exec sleep 60; fi\n')
        os.chmod(self.mark, os.stat(self.mark).st_mode | stat.S_IEXEC)
        self.settings = settings
        self.process = None
        self.path_prefix = None
        self.shell_ui = ""

    def entry(self, name, *lines, directory=None, exec_line=None):
        with open(f"{directory or self.autostart}/{name}.desktop", "w") as f:
            f.write(f"[Desktop Entry]\nType=Application\nName={name}\n")
            f.write(f"Exec={exec_line or self.mark + ' ' + name}\n")
            f.write("".join(line + "\n" for line in lines))

    def start(self, *args, hang=False, autostart=True, session=True):
        env = dict(os.environ, HOME=self.tmp, XDG_CONFIG_HOME=self.config,
                   XDG_CONFIG_DIRS=self.tmp + "/none", XDG_DATA_DIRS=self.tmp + "/data",
                   XDG_DATA_HOME=self.tmp + "/none", XDG_RUNTIME_DIR=self.tmp + "/run",
                   GSETTINGS_SCHEMA_DIR=os.environ["GSETTINGS_SCHEMA_DIR"] + ":/usr/share/glib-2.0/schemas",
                   GSETTINGS_BACKEND="memory", POLO_SHELL_UI=self.shell_ui, POLO_TEST_LOG=self.log,
                   POLO_TEST_SETTINGS=self.settings)
        if autostart:
            env["POLO_TEST_AUTOSTART"] = "1"
        if self.path_prefix:
            env["PATH"] = self.path_prefix + ":" + env["PATH"]
        if hang:
            env["POLO_TEST_HANG"] = "1"
        for key in ("WAYLAND_DISPLAY", "WAYLAND_SOCKET", "DISPLAY", "XAUTHORITY", "DCONF_PROFILE"):
            env.pop(key, None)
        command = [POLO, "--backend=headless", "--renderer=pixman", "--test-mode",
                   "--socket=polo-phases-test", f"--log={self.tmp}/polo.log", *args]
        if session:
            command.insert(1, "--session")
        self.process = subprocess.Popen(command, env=env, stdout=subprocess.DEVNULL,
                                        stderr=subprocess.DEVNULL)
        self.started = time.monotonic()

    def lines(self):
        try:
            with open(self.log) as f:
                return f.read().splitlines()
        except FileNotFoundError:
            return []

    def env_of(self, name):
        with open(f"{self.log}.{name}.env") as f:
            return dict(line.split("=", 1) for line in f.read().splitlines() if "=" in line)

    def close(self):
        if self.process and self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(10)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()
        shutil.rmtree(self.tmp, ignore_errors=True)


def fake_gdm(log):
    process = subprocess.Popen([sys.executable, "-c", GDM, log], stdout=subprocess.PIPE, text=True)
    check(process.stdout.readline().strip() == "ready", "the fake GDM did not start")
    return process


def order_and_wait():
    gdm = None
    s = Session("org.mate.polo.session/phase-timeout=30;"
                "org.mate.polo.session/trusted-components=['listed','allowed-program']")
    try:
        s.entry("init", "X-MATE-Autostart-Phase=Initialization")
        s.entry("wm", "X-MATE-Autostart-Phase=WindowManager")
        s.entry("panel", "X-MATE-Autostart-Phase=Panel",
                exec_line=f"{MAP_CLIENT} {s.log} panel layer 300")
        s.entry("desktop", "X-MATE-Autostart-Phase=Desktop",
                exec_line=f"{MAP_CLIENT} {s.log} desktop window 300")
        s.entry("x-window", "X-MATE-Autostart-Phase=Desktop",
                exec_line=f"/usr/bin/env X_TEST_WM_PID=1 {X_CLIENT} {s.tmp}/x-out")
        s.entry("app")
        s.entry("hidden", "Hidden=true")
        s.entry("conditional", "AutostartCondition=if-exists flag")
        s.entry("delayed", "X-MATE-Autostart-Delay=3")
        s.entry("listed")
        s.entry("codes", "Path=" + s.tmp + "/none", "Icon=polo-icon",
                exec_line=f"{s.mark} codes %f %U %% %c %i %k")
        gdm = fake_gdm(s.log)
        s.start()
        check(wait_for(lambda: "start app" in s.lines(), 25), f"never reached Application: {s.lines()}")
        elapsed = time.monotonic() - s.started
        check(elapsed < 20, f"a phase waited for its timeout: {elapsed:.1f}s")
        lines = s.lines()
        order = [lines.index(x) for x in ("start init", "start panel", "mapping panel", "start desktop",
                                          "mapping desktop", "start app")]
        check(order == sorted(order), f"wrong phase order: {lines}")
        check(wait_for(lambda: "start wm" in s.lines(), 5), "the WindowManager phase skipped")
        check(wait_for(lambda: "register" in s.lines(), 5), "GDM was not told")
        check(s.lines().index("register") > s.lines().index("mapping desktop"),
              f"GDM was told before the session was up: {s.lines()}")

        check("start hidden" not in lines, "a hidden entry started")
        check("start delayed" not in lines, "a delayed entry started at once")
        check(wait_for(lambda: "start delayed" in s.lines(), 8), "a delayed entry never started")
        check(wait_for(lambda: {"start listed", "start codes"} <= set(s.lines()), 5),
              f"the Application phase did not finish starting: {s.lines()}")

        env = s.env_of("app")
        check(env.get("XDG_ACTIVATION_TOKEN") and
              env.get("DESKTOP_STARTUP_ID") == env.get("XDG_ACTIVATION_TOKEN"), "no activation token")
        check("WAYLAND_SOCKET" not in env, "a plain app was trusted")
        check("WAYLAND_SOCKET" in s.env_of("listed"), "the allow-list by desktop id was ignored")

        with open(f"{s.log}.codes.args") as f:
            arguments = f.read().splitlines()
        check(arguments == ["%", "codes", "--icon", "polo-icon", f"{s.autostart}/codes.desktop"],
              f"Exec codes: {arguments}")
        with open(f"{s.log}.codes.pwd") as f:
            check(f.read().strip() == s.tmp + "/none", "Path was ignored")

        check("start conditional" not in s.lines(), "an entry started against its condition")
        open(s.config + "/flag", "w").close()
        check(wait_for(lambda: "start conditional" in s.lines(), 5), "the condition did not start it")
    finally:
        s.close()
        if gdm is not None:
            gdm.kill()
            gdm.wait()


def stuck_phase_times_out():
    s = Session("org.mate.polo.session/phase-timeout=1")
    try:
        s.entry("stuck", "X-MATE-Autostart-Phase=Initialization")
        s.entry("panel", "X-MATE-Autostart-Phase=Panel")
        s.start(hang=True)
        check(wait_for(lambda: "start stuck" in s.lines(), 10), "never started")
        check("start panel" not in s.lines(), "the phase did not wait")
        time.sleep(SETTLE)
        check("start panel" not in s.lines(), "the phase did not wait for its timeout")
        check(wait_for(lambda: "start panel" in s.lines(), 3), "the timeout did not end the phase")
    finally:
        s.close()


def running_sleep(s):
    children = subprocess.run(["pgrep", "-P", str(s.process.pid)], capture_output=True, text=True)
    for pid in children.stdout.split():
        try:
            with open(f"/proc/{pid}/cmdline") as f:
                if f.read().startswith("sleep"):
                    return True
        except OSError:
            pass
    return False


def condition_stops():
    s = Session()
    try:
        s.entry("watched", "AutostartCondition=if-exists flag", "X-MATE-AutoRestart=true")
        open(s.config + "/flag", "w").close()
        s.start(hang=True)
        check(wait_for(lambda: "start watched" in s.lines(), 10), "never started")
        check(wait_for(lambda: running_sleep(s), 5), "the app is not running")
        os.unlink(s.config + "/flag")
        check(wait_for(lambda: not running_sleep(s), 5), "the app kept running with its condition false")
        time.sleep(SETTLE)
        with open(s.tmp + "/polo.log") as f:
            check("restarting watched" not in f.read(), "an app its condition stopped counted as a crash")
    finally:
        s.close()


def x11_components():
    s = Session("org.mate.polo.session/x11-components=['by-id','x-mark']")
    try:
        program = s.tmp + "/x-mark"
        shutil.copy(s.mark, program)
        s.entry("by-id")
        s.entry("by-program", exec_line=f"{program} by-program")
        s.entry("native")
        s.start()
        check(wait_for(lambda: {"start by-id", "start by-program", "start native"} <= set(s.lines()), 10),
              s.lines())
        check(s.env_of("by-id").get("GDK_BACKEND") == "x11", "x11-components by desktop id")
        check(s.env_of("by-program").get("GDK_BACKEND") == "x11", "x11-components by program")
        check("GDK_BACKEND" not in s.env_of("native"), "a native app was sent to X")
    finally:
        s.close()


def x11_defaults():
    source = os.path.dirname(os.path.abspath(__file__)) + "/../../data/org.mate.polo.gschema.xml"
    with open(source) as f:
        text = f.read()
    for name in ("mate-settings-daemon", "mate-power-manager", "mate-screensaver"):
        check(text.count(f"'{name}'") >= 1 and
              f"'{name}'" in text[text.index('"x11-components"'):], f"{name} needs X by default")


def required_and_allowed_program():
    s = Session("org.mate.polo.session/trusted-components=['allowed-mark']")
    try:
        program = s.tmp + "/allowed-mark"
        shutil.copy(s.mark, program)
        s.entry("program", exec_line=f"{program} program")
        s.start()
        check(wait_for(lambda: "start program" in s.lines(), 10), "never started")
        check("WAYLAND_SOCKET" in s.env_of("program"), "the allow-list by program was ignored")
        def logged():
            with open(s.tmp + "/polo.log") as f:
                return "display manager: " in f.read()
        check(wait_for(logged, 5), "a session with no GDM did not get past registering")
        check(s.process.poll() is None, "polo stopped with no GDM on the bus")
    finally:
        s.close()


def restarts():
    s = Session("org.mate.session.required-components/panel='req-crash';"
                "org.mate.session.required-components/filemanager='req-clean';"
                "org.mate.session/required-components-list=['panel','filemanager']")
    try:
        crash = s.tmp + "/crash"
        with open(crash, "w") as f:
            f.write(f'#!/bin/sh\necho "start $1" >> "{s.log}"\nexit $2\n')
        os.chmod(crash, 0o755)
        applications = s.tmp + "/data/applications"
        s.entry("req-crash", "X-MATE-Provides=panel", "X-MATE-Autostart-Phase=Panel",
                directory=applications, exec_line=f"{crash} req-crash 3")
        s.entry("req-clean", "X-MATE-Provides=filemanager", directory=applications,
                exec_line=f"{crash} req-clean 0")
        s.entry("auto-clean", "X-MATE-AutoRestart=true", exec_line=f"{crash} auto-clean 0")
        s.entry("plain", exec_line=f"{crash} plain 3")
        zenity = s.tmp + "/zenity"
        with open(zenity, "w") as f:
            f.write(f'#!/bin/sh\nprintf "%s\\n" "$@" > "{s.tmp}/zenity-args"\n')
        os.chmod(zenity, 0o755)
        s.path_prefix = s.tmp
        s.start()
        count = lambda name: s.lines().count("start " + name)
        check(wait_for(lambda: count("req-crash") == 5 and count("auto-clean") == 5, 15),
              f"restarts: {s.lines()}")
        check(wait_for(lambda: os.path.exists(s.tmp + "/zenity-args"), 5), "nothing told the user")
        with open(s.tmp + "/zenity-args") as f:
            check("The panel keeps crashing" in f.read(), "wrong message")
        os.unlink(s.tmp + "/zenity-args")
        time.sleep(3)
        check(count("req-crash") == 5, "a required component restarted past the limit")
        check(count("auto-clean") == 5, "an app restarted past the limit")
        check(count("req-clean") == 1, "a required component that exited cleanly came back")
        check(count("plain") == 1, "an app with no AutoRestart came back")
        check(not os.path.exists(s.tmp + "/zenity-args"), "the user was told about an optional app")
    finally:
        s.close()


def helper_shows_it():
    s = Session("org.mate.session.required-components/panel='req';"
                "org.mate.session/required-components-list=['panel']")
    try:
        crash = s.tmp + "/crash"
        with open(crash, "w") as f:
            f.write(f'#!/bin/sh\necho "start req" >> "{s.log}"\nexit 3\n')
        os.chmod(crash, 0o755)
        s.entry("req", "X-MATE-Provides=panel", directory=s.tmp + "/data/applications", exec_line=crash)
        zenity = s.tmp + "/zenity"
        with open(zenity, "w") as f:
            f.write(f'#!/bin/sh\n: > "{s.tmp}/zenity-ran"\n')
        os.chmod(zenity, 0o755)
        s.path_prefix = s.tmp
        s.shell_ui = SHELL_UI
        for name in ("icons", "mime", "glycin-loaders"):
            if os.path.exists("/usr/share/" + name):
                os.symlink("/usr/share/" + name, f"{s.tmp}/data/{name}")
        s.start()
        def warned():
            with open(s.tmp + "/polo.log") as f:
                return "keeps crashing" in f.read()
        check(wait_for(lambda: os.path.exists(s.tmp + "/polo.log") and warned(), 15), "no give-up")
        time.sleep(0.5)
        check(not os.path.exists(s.tmp + "/zenity-ran"), "zenity ran with the helper bound")
        time.sleep(1)
        with open(s.tmp + "/polo.log") as f:
            text = f.read()
        check("polo-shell-ui killed by signal" not in text and "polo-shell-ui exited" not in text,
              "the helper died showing the dialog")
    finally:
        s.close()


def ending(how):
    s = Session()
    try:
        polite = s.tmp + "/polite"
        with open(polite, "w") as f:
            f.write(f"""#!/bin/sh
trap 'echo "term $1" >> "{s.log}"; exit 0' TERM
echo "start $1" >> "{s.log}"
sleep 60 &
wait
""")
        stubborn = s.tmp + "/stubborn"
        with open(stubborn, "w") as f:
            f.write(f"""#!/bin/sh
trap '' TERM
echo $$ > "{s.tmp}/stubborn-pid"
echo "start stubborn" >> "{s.log}"
while :; do /usr/bin/sleep 0.1; done
""")
        os.chmod(polite, 0o755)
        os.chmod(stubborn, 0o755)
        s.entry("polite", "X-MATE-AutoRestart=true", exec_line=f"{polite} polite")
        s.entry("stubborn", exec_line=stubborn)
        s.start()
        check(wait_for(lambda: {"start polite", "start stubborn"} <= set(s.lines()), 15), s.lines())
        owner = subprocess.run(["gdbus", "call", "--session", "--dest", "org.freedesktop.DBus",
                                "--object-path", "/org/freedesktop/DBus", "--method",
                                "org.freedesktop.DBus.NameHasOwner", "org.gnome.SessionManager"],
                               capture_output=True, text=True, timeout=10)
        check("true" in owner.stdout, f"the session manager name is not owned: {owner.stdout}")
        began = time.monotonic()
        if how == "logout":
            answer = subprocess.run(["gdbus", "call", "--session", "--dest", "org.gnome.SessionManager",
                                     "--object-path", "/org/gnome/SessionManager", "--method",
                                     "org.gnome.SessionManager.Logout", "1"],
                                    capture_output=True, text=True, timeout=10)
            check(answer.returncode == 0, f"Logout failed: {answer.stderr}")
        else:
            s.process.terminate()
        if how == "twice":
            time.sleep(1)
            s.process.terminate()
        status = s.process.wait(15)
        with open(s.tmp + "/polo.log") as f:
            check(status == 0, f"polo exited {status} ({how}): {f.read()[-1500:]}")
        took = time.monotonic() - began
        if how == "twice":
            check(took < 3.5, f"a second SIGTERM did not end the wait: {took:.1f}s")
        else:
            check(4.5 < took < 8, f"polo took {took:.1f}s with a child ignoring SIGTERM")
        check("term polite" in s.lines(), f"a child did not get SIGTERM: {s.lines()}")
        check(s.lines().count("start polite") == 1, "an AutoRestart app restarted during the end")
        with open(s.tmp + "/stubborn-pid") as f:
            pid = f.read().strip()
        def gone():
            try:
                with open(f"/proc/{pid}/stat") as f:
                    return f.read().rsplit(")", 1)[1].split()[0] == "Z"
            except OSError:
                return True
        check(wait_for(gone, 3), "a child ignoring SIGTERM survived")
    finally:
        s.close()


def ending_early():
    s = Session()
    try:
        s.entry("short", exec_line="sleep 60")
        s.start()
        check(wait_for(lambda: running_sleep(s), 15), "never started")
        began = time.monotonic()
        s.process.terminate()
        check(s.process.wait(15) == 0, "polo did not exit cleanly")
        took = time.monotonic() - began
        check(took < 3, f"polo waited {took:.1f}s with every child gone")
    finally:
        s.close()


def guards():
    for args in ({"autostart": False}, {"session": False}):
        s = Session()
        try:
            s.entry("guarded")
            s.start(**args)
            time.sleep(1.5)
            check(s.process.poll() is None, "polo stopped")
            check("start guarded" not in s.lines(), f"autostart ran with {args}")
        finally:
            s.close()


def main():
    if "POLO_PHASES_TEST_BUS" not in os.environ:
        if not shutil.which("dbus-run-session"):
            return 77
        env = dict(os.environ, POLO_PHASES_TEST_BUS="1")
        command = ('DBUS_SYSTEM_BUS_ADDRESS="$DBUS_SESSION_BUS_ADDRESS" exec "$@"')
        return subprocess.call(["dbus-run-session", "--", "sh", "-c", command, "sh", sys.executable,
                                *sys.argv], env=env)
    order_and_wait()
    stuck_phase_times_out()
    condition_stops()
    required_and_allowed_program()
    x11_components()
    x11_defaults()
    ending("logout")
    ending("sigterm")
    ending("twice")
    ending_early()
    restarts()
    if SHELL_UI:
        helper_shows_it()
    guards()
    print("ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
