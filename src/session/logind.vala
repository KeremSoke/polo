/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

namespace Polo {
    [DBus (name = "org.freedesktop.login1.Manager")]
    public interface LoginManager : GLib.Object {
        public abstract async string can_power_off () throws GLib.Error;
        public abstract async string can_reboot () throws GLib.Error;
        public abstract async string can_suspend () throws GLib.Error;
        public abstract async string can_hibernate () throws GLib.Error;
        public abstract async string can_hybrid_sleep () throws GLib.Error;
        public abstract async void power_off (bool interactive) throws GLib.Error;
        public abstract async void reboot (bool interactive) throws GLib.Error;
        public abstract async void suspend (bool interactive) throws GLib.Error;
        public abstract async void hibernate (bool interactive) throws GLib.Error;
        public abstract async GLib.ObjectPath get_session (string session_id) throws GLib.Error;
        [DBus (name = "GetSessionByPID")]
        public abstract async GLib.ObjectPath get_session_by_pid (uint pid) throws GLib.Error;
        public signal void prepare_for_sleep (bool start);
    }

    [DBus (name = "org.freedesktop.login1.Session")]
    public interface LoginSession : GLib.Object {
        public abstract bool active { get; }
        public abstract async void set_idle_hint (bool idle) throws GLib.Error;
        [DBus (name = "Lock")]
        public signal void lock_session ();
        [DBus (name = "Unlock")]
        public signal void unlock_session ();
    }

    [DBus (name = "org.freedesktop.login1.Seat")]
    public interface LoginSeat : GLib.Object {
        public abstract bool can_multi_session { get; }
    }

    public enum PowerAction {
        POWER_OFF,
        REBOOT,
        SUSPEND,
        HIBERNATE,
        HYBRID_SLEEP,
    }

    public class Logind : GLib.Object {
        private const string NAME = "org.freedesktop.login1";
        private const string PATH = "/org/freedesktop/login1";
        private const string SCREENSAVER_SCHEMA = "org.mate.screensaver";
        private const uint SLEEP_LOCK_TIMEOUT_MS = 2000;

        [CCode (cname = "getpid", cheader_filename = "unistd.h")]
        private static extern int getpid ();

        [CCode (cname = "close", cheader_filename = "unistd.h")]
        private static extern int close (int fd);

        public signal void lock_requested ();
        public signal void unlock_requested ();
        public signal void ready ();

        public bool active { get; private set; default = true; }

        private GLib.DBusConnection? system = null;
        private LoginManager? manager = null;
        private LoginSession? session = null;
        private GLib.Settings? screensaver = null;
        private GLib.Cancellable cancellable = new GLib.Cancellable ();
        private bool connected = false;
        private bool connecting = false;
        private int sleep_fd = -1;
        private uint sleep_timer = 0;

        public Logind () {
            var source = GLib.SettingsSchemaSource.get_default ();
            var schema = source != null ? source.lookup (SCREENSAVER_SCHEMA, true) : null;
            if (schema != null && schema.has_key ("lock-enabled")) {
                screensaver = new GLib.Settings.full (schema, null, null);
            }
        }

        public void shutdown () {
            cancellable.cancel ();
            if (sleep_timer != 0) {
                GLib.Source.remove (sleep_timer);
                sleep_timer = 0;
            }
            release_sleep ();
            if (manager != null) {
                manager.prepare_for_sleep.disconnect (on_prepare_for_sleep);
            }
            if (session != null) {
                session.lock_session.disconnect (on_lock);
                session.unlock_session.disconnect (on_unlock);
                ((GLib.DBusProxy) session).g_properties_changed.disconnect (on_session_changed);
            }
            manager = null;
            session = null;
            system = null;
        }

        private async GLib.DBusConnection? connect_bus () throws GLib.Error {
            if (!CommandLine.test_mode) {
                return yield GLib.Bus.get (GLib.BusType.SYSTEM, cancellable);
            }
            string? address = GLib.Environment.get_variable ("POLO_TEST_SYSTEM_BUS");
            if (address == null) {
                return null;
            }
            return yield new GLib.DBusConnection.for_address (
                address, GLib.DBusConnectionFlags.AUTHENTICATION_CLIENT |
                GLib.DBusConnectionFlags.MESSAGE_BUS_CONNECTION, null, cancellable);
        }

        public async void start () {
            if (connected) {
                return;
            }
            if (connecting) {
                GLib.SourceFunc resume = start.callback;
                ulong waiting = 0;
                waiting = ready.connect (() => {
                    disconnect (waiting);
                    resume ();
                });
                yield;
                return;
            }
            connecting = true;
            unowned LogScope log = Logging.scope (LogDomain.POLO);
            try {
                system = yield connect_bus ();
                if (system != null) {
                    manager = yield system.get_proxy<LoginManager> (NAME, PATH, GLib.DBusProxyFlags.NONE,
                                                                    cancellable);
                    manager.prepare_for_sleep.connect (on_prepare_for_sleep);
                    yield find_session ();
                    yield take_sleep ();
                } else {
                    log.info ("test mode: no logind (POLO_TEST_SYSTEM_BUS is not set)");
                }
            } catch (GLib.Error e) {
                log.warning ("logind: %s", e.message);
                manager = null;
            }
            connecting = false;
            connected = true;
            ready ();
        }

        private async void find_session () {
            unowned LogScope log = Logging.scope (LogDomain.POLO);
            GLib.ObjectPath? path = null;
            string? id = GLib.Environment.get_variable ("XDG_SESSION_ID");
            try {
                if (id != null && id != "") {
                    path = yield manager.get_session (id);
                }
            } catch (GLib.Error e) {
                log.info ("logind session %s: %s", id, e.message);
            }
            try {
                if (path == null) {
                    path = yield manager.get_session_by_pid ((uint) getpid ());
                }
                session = yield system.get_proxy<LoginSession> (NAME, path, GLib.DBusProxyFlags.NONE,
                                                                cancellable);
            } catch (GLib.Error e) {
                log.info ("no logind session for Polo: %s", e.message);
                return;
            }
            session.lock_session.connect (on_lock);
            session.unlock_session.connect (on_unlock);
            ((GLib.DBusProxy) session).g_properties_changed.connect (on_session_changed);
            active = session.active;
            log.info ("logind session %s", path);
        }

        private void on_session_changed (GLib.Variant changed, string[] invalidated) {
            bool now = session.active;
            if (now != active) {
                Logging.scope (LogDomain.POLO).info ("the session is %s", now ? "active" : "inactive");
                active = now;
            }
        }

        private void on_lock () {
            lock_requested ();
        }

        private void on_unlock () {
            unlock_requested ();
        }

        public async bool can (PowerAction action) {
            yield start ();
            if (manager == null) {
                return false;
            }
            try {
                string answer;
                switch (action) {
                    case PowerAction.POWER_OFF:
                        answer = yield manager.can_power_off ();
                        break;
                    case PowerAction.REBOOT:
                        answer = yield manager.can_reboot ();
                        break;
                    case PowerAction.SUSPEND:
                        answer = yield manager.can_suspend ();
                        break;
                    case PowerAction.HIBERNATE:
                        answer = yield manager.can_hibernate ();
                        break;
                    default:
                        answer = yield manager.can_hybrid_sleep ();
                        break;
                }
                return answer == "yes" || answer == "challenge";
            } catch (GLib.Error e) {
                Logging.scope (LogDomain.POLO).info ("logind: %s", e.message);
                return false;
            }
        }

        public async bool can_shutdown () {
            foreach (var action in new PowerAction[] { PowerAction.POWER_OFF, PowerAction.REBOOT,
                                                       PowerAction.SUSPEND, PowerAction.HIBERNATE,
                                                       PowerAction.HYBRID_SLEEP }) {
                if (yield can (action)) {
                    return true;
                }
            }
            return false;
        }

        public async bool can_multi_session () {
            yield start ();
            if (system == null) {
                return false;
            }
            try {
                var seat = yield system.get_proxy<LoginSeat> (NAME, PATH + "/seat/auto", GLib.DBusProxyFlags.NONE,
                                                              cancellable);
                return seat.can_multi_session;
            } catch (GLib.Error e) {
                Logging.scope (LogDomain.POLO).info ("logind seat: %s", e.message);
                return false;
            }
        }

        public async bool perform (PowerAction action) {
            yield start ();
            unowned LogScope log = Logging.scope (LogDomain.POLO);
            if (manager == null) {
                log.warning ("no logind to %s", action.to_string ());
                return false;
            }
            try {
                switch (action) {
                    case PowerAction.POWER_OFF:
                        yield manager.power_off (true);
                        break;
                    case PowerAction.REBOOT:
                        yield manager.reboot (true);
                        break;
                    case PowerAction.SUSPEND:
                        yield manager.suspend (true);
                        break;
                    case PowerAction.HIBERNATE:
                        yield manager.hibernate (true);
                        break;
                    default:
                        return false;
                }
                return true;
            } catch (GLib.Error e) {
                log.warning ("logind: %s", e.message);
                return false;
            }
        }

        private async bool has_owner (string name) throws GLib.Error {
            var reply = yield system.call ("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
                                           "NameHasOwner", new GLib.Variant ("(s)", name),
                                           new GLib.VariantType ("(b)"), GLib.DBusCallFlags.NONE, 5000,
                                           cancellable);
            bool owned;
            reply.get ("(b)", out owned);
            return owned;
        }

        public async void switch_to_greeter () {
            yield start ();
            unowned LogScope log = Logging.scope (LogDomain.POLO);
            if (system == null) {
                log.warning ("no system bus to switch user");
                return;
            }
            string? seat_path = GLib.Environment.get_variable ("XDG_SEAT_PATH");
            try {
                if (yield has_owner ("org.gnome.DisplayManager")) {
                    yield system.call ("org.gnome.DisplayManager", "/org/gnome/DisplayManager/LocalDisplayFactory",
                                       "org.gnome.DisplayManager.LocalDisplayFactory", "CreateTransientDisplay",
                                       null, null, GLib.DBusCallFlags.NONE, -1, cancellable);
                } else if (seat_path != null) {
                    yield system.call ("org.freedesktop.DisplayManager", seat_path,
                                       "org.freedesktop.DisplayManager.Seat", "SwitchToGreeter", null, null,
                                       GLib.DBusCallFlags.NO_AUTO_START, -1, cancellable);
                } else {
                    log.warning ("Unable to switch user: no GDM, and no LightDM seat (XDG_SEAT_PATH)");
                }
            } catch (GLib.Error e) {
                log.warning ("Unable to switch user: %s", e.message);
            }
        }

        public async GLib.DBusConnection? bus () {
            yield start ();
            return system;
        }

        public void set_idle_hint (bool idle) {
            if (session == null) {
                return;
            }
            session.set_idle_hint.begin (idle, (obj, result) => {
                try {
                    session.set_idle_hint.end (result);
                } catch (GLib.Error e) {
                    Logging.scope (LogDomain.POLO).info ("SetIdleHint: %s", e.message);
                }
            });
        }

        private bool lock_before_sleep () {
            return screensaver != null && screensaver.get_boolean ("lock-enabled");
        }

        private async void take_sleep () {
            if (sleep_fd >= 0 || system == null || !lock_before_sleep ()) {
                return;
            }
            try {
                GLib.UnixFDList fds;
                var reply = yield system.call_with_unix_fd_list (
                    NAME, PATH, NAME + ".Manager", "Inhibit",
                    new GLib.Variant ("(ssss)", "sleep", "Polo", "Lock the screen before sleep", "delay"),
                    new GLib.VariantType ("(h)"), GLib.DBusCallFlags.NONE, 5000, null, cancellable, out fds);
                int32 index;
                reply.get ("(h)", out index);
                sleep_fd = fds.get (index);
            } catch (GLib.Error e) {
                Logging.scope (LogDomain.POLO).info ("sleep inhibitor: %s", e.message);
            }
        }

        private void release_sleep () {
            if (sleep_fd >= 0) {
                close (sleep_fd);
                sleep_fd = -1;
            }
        }

        public void lock_shown () {
            if (sleep_timer != 0) {
                GLib.Source.remove (sleep_timer);
                sleep_timer = 0;
            }
            release_sleep ();
        }

        private void on_prepare_for_sleep (bool start) {
            if (!start) {
                take_sleep.begin ();
                return;
            }
            if (sleep_fd < 0) {
                return;
            }
            if (!lock_before_sleep ()) {
                release_sleep ();
                return;
            }
            sleep_timer = GLib.Timeout.add (SLEEP_LOCK_TIMEOUT_MS, () => {
                sleep_timer = 0;
                Logging.scope (LogDomain.POLO).info ("the screen did not lock within 2 s; sleeping anyway");
                release_sleep ();
                return GLib.Source.REMOVE;
            });
            lock_requested ();
        }
    }
}
