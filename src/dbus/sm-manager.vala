/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

namespace Polo.Dbus {
    public enum ManagerPhase {
        STARTUP,
        INITIALIZATION,
        WINDOW_MANAGER,
        PANEL,
        DESKTOP,
        APPLICATION,
        RUNNING,
        QUERY_END_SESSION,
        END_SESSION,
        EXIT;

        public static ManagerPhase from_autostart (AutostartPhase phase) {
            switch (phase) {
                case AutostartPhase.INITIALIZATION:
                    return INITIALIZATION;
                case AutostartPhase.WINDOW_MANAGER:
                    return WINDOW_MANAGER;
                case AutostartPhase.PANEL:
                    return PANEL;
                case AutostartPhase.DESKTOP:
                    return DESKTOP;
                default:
                    return APPLICATION;
            }
        }
    }

    public enum LogoutMode {
        NORMAL = 0,
        NO_CONFIRMATION = 1,
        FORCE = 2,
    }

    public delegate AutostartApp? AppMatcher (string startup_id, string app_id);

    public delegate bool ConditionCheck (string condition);

    public class Manager : GLib.Object {
        public const string NAME = "org.gnome.SessionManager";
        public const string PATH = "/org/gnome/SessionManager";
        public const string RESOURCE = "/org/mate/polo/dbus/org.gnome.SessionManager.xml";
        private const string LOCKDOWN_SCHEMA = "org.mate.lockdown";
        private const string ERROR_PREFIX = "org.gnome.SessionManager.";
        private const string CHECK_ACCELERATED = "mate-session-check-accelerated";

        private ManagerPhase _phase = ManagerPhase.STARTUP;
        public ManagerPhase phase { get { return _phase; } }
        public string renderer { get; private set; default = ""; }
        public LogoutMode logout_mode { get; set; default = LogoutMode.NORMAL; }

        public signal void logout_requested (LogoutMode mode);
        public signal void shutdown_dialog_requested ();
        public signal void shutdown_requested (bool reboot);
        public signal void initialization_error (string message, bool fatal);

        public AppMatcher? match_app = null;
        public ConditionCheck? handles_condition = null;

        private GLib.DBusConnection connection;
        private Registration? registration = null;
        private GLib.Settings? lockdown = null;
        private GLib.HashTable<string, Client> clients =
            new GLib.HashTable<string, Client> (GLib.str_hash, GLib.str_equal);
        private GLib.HashTable<string, Inhibitor> inhibitors =
            new GLib.HashTable<string, Inhibitor> (GLib.str_hash, GLib.str_equal);
        private int pending = 0;
        private GLib.Cancellable cancellable = new GLib.Cancellable ();
        private GLib.Subprocess? checking = null;
        private GLib.HashTable<string, uint> watches = new GLib.HashTable<string, uint> (GLib.str_hash, GLib.str_equal);

        public Manager (GLib.DBusConnection connection) {
            this.connection = connection;
            var source = GLib.SettingsSchemaSource.get_default ();
            var schema = source != null ? source.lookup (LOCKDOWN_SCHEMA, true) : null;
            if (schema != null && schema.has_key ("disable-log-out")) {
                lockdown = new GLib.Settings.full (schema, null, null);
            }
        }

        public void register () throws GLib.Error {
            var info = Registration.load (RESOURCE, NAME);
            registration = new Registration (connection, PATH, info, on_method, on_property);
        }

        public void unregister () {
            if (registration != null) {
                registration.unregister ();
                registration = null;
            }
            watches.foreach ((name, watch) => GLib.Bus.unwatch_name (watch));
            watches.remove_all ();
            finish_pending ();
            foreach (unowned Client client in clients.get_values ()) {
                client.unexport ();
            }
            clients.remove_all ();
        }

        private void finish_pending () {
            if (pending == 0) {
                return;
            }
            if (checking != null) {
                checking.force_exit ();
            }
            cancellable.cancel ();
            var context = GLib.MainContext.default ();
            int64 deadline = GLib.get_monotonic_time () + 2 * GLib.TimeSpan.SECOND;
            while (pending > 0 && GLib.get_monotonic_time () < deadline) {
                if (!context.iteration (false)) {
                    GLib.Thread.usleep (1000);
                }
            }
        }

        public void set_phase (ManagerPhase next) {
            if (next == _phase) {
                return;
            }
            if (_phase == ManagerPhase.STARTUP) {
                check_accelerated.begin ();
            }
            _phase = next;
            if (next == ManagerPhase.RUNNING) {
                emit ("SessionRunning", null);
            }
        }

        public void session_over () {
            set_phase (ManagerPhase.EXIT);
            emit ("SessionOver", null);
        }

        public GLib.List<unowned Client> get_clients () {
            return clients.get_values ();
        }

        public GLib.List<unowned Inhibitor> get_inhibitors () {
            return inhibitors.get_values ();
        }

        public bool is_inhibited (uint flags) {
            foreach (unowned Inhibitor inhibitor in inhibitors.get_values ()) {
                if ((inhibitor.flags & flags) != 0) {
                    return true;
                }
            }
            return false;
        }

        private void emit (string signal_name, GLib.Variant? parameters) {
            try {
                connection.emit_signal (null, PATH, NAME, signal_name, parameters);
            } catch (GLib.Error e) {
                Logging.scope (LogDomain.POLO).warning ("cannot emit %s: %s", signal_name, e.message);
            }
        }

        private static void fail (GLib.DBusMethodInvocation invocation, string error, string message) {
            invocation.return_dbus_error (ERROR_PREFIX + error, message);
        }

        private bool log_out_locked_down () {
            return lockdown != null && lockdown.get_boolean ("disable-log-out");
        }

        private GLib.Variant? on_property (string property) {
            return property == "Renderer" ? new GLib.Variant.string (renderer) : null;
        }

        private void on_method (string sender, string method, GLib.Variant parameters,
                                owned GLib.DBusMethodInvocation invocation) {
            switch (method) {
                case "Setenv":
                {
                    string variable, value;
                    parameters.get ("(ss)", out variable, out value);
                    if (phase > ManagerPhase.INITIALIZATION) {
                        fail (invocation, "NotInInitialization",
                              "Setenv interface is only available during the Initialization phase");
                        return;
                    }
                    SessionEnv.setenv (variable, value);
                    invocation.return_value (null);
                    break;
                }
                case "InitializationError":
                {
                    string message;
                    bool fatal;
                    parameters.get ("(sb)", out message, out fatal);
                    if (phase > ManagerPhase.INITIALIZATION) {
                        fail (invocation, "NotInInitialization",
                              "InitializationError interface is only available during the Initialization phase");
                        return;
                    }
                    invocation.return_value (null);
                    initialization_error (message, fatal);
                    break;
                }
                case "RegisterClient":
                {
                    string app_id, startup_id;
                    parameters.get ("(ss)", out app_id, out startup_id);
                    register_client.begin (sender, app_id, startup_id, (owned) invocation);
                    break;
                }
                case "UnregisterClient":
                {
                    string path;
                    parameters.get ("(o)", out path);
                    var client = clients.lookup (path);
                    if (client == null) {
                        fail (invocation, "NotRegistered", "Unable to unregister client");
                        return;
                    }
                    client.status = ClientStatus.UNREGISTERED;
                    invocation.return_value (null);
                    break;
                }
                case "Inhibit":
                {
                    string app_id, reason;
                    uint xid, flags;
                    parameters.get ("(susu)", out app_id, out xid, out reason, out flags);
                    inhibit (sender, app_id, xid, reason, flags, (owned) invocation);
                    break;
                }
                case "Uninhibit":
                {
                    uint cookie;
                    parameters.get ("(u)", out cookie);
                    unowned Inhibitor? found = find_cookie (cookie);
                    if (found == null) {
                        fail (invocation, "GeneralError", "Unable to uninhibit: Invalid cookie");
                        return;
                    }
                    string path = found.path;
                    remove_inhibitor (path);
                    invocation.return_value (null);
                    break;
                }
                case "IsInhibited":
                {
                    uint flags;
                    parameters.get ("(u)", out flags);
                    invocation.return_value (new GLib.Variant ("(b)", is_inhibited (flags)));
                    break;
                }
                case "GetClients":
                {
                    invocation.return_value (paths (clients.get_keys ()));
                    break;
                }
                case "GetInhibitors":
                {
                    invocation.return_value (paths (inhibitors.get_keys ()));
                    break;
                }
                case "IsAutostartConditionHandled":
                {
                    string condition;
                    parameters.get ("(s)", out condition);
                    bool handled = handles_condition != null && handles_condition (condition);
                    invocation.return_value (new GLib.Variant ("(b)", handled));
                    break;
                }
                case "Shutdown":
                {
                    if (!running_or_fail (invocation, "Shutdown") || locked_down_fails (invocation)) {
                        return;
                    }
                    invocation.return_value (null);
                    shutdown_dialog_requested ();
                    break;
                }
                case "CanShutdown":
                {
                    can_shutdown.begin ((owned) invocation);
                    break;
                }
                case "Logout":
                {
                    uint mode;
                    parameters.get ("(u)", out mode);
                    if (!running_or_fail (invocation, "Shutdown") || locked_down_fails (invocation)) {
                        return;
                    }
                    if (mode > LogoutMode.FORCE) {
                        fail (invocation, "InvalidOption", "Unknown logout mode flag");
                        return;
                    }
                    invocation.return_value (null);
                    logout_requested ((LogoutMode) mode);
                    break;
                }
                case "RequestShutdown":
                case "RequestReboot":
                {
                    bool reboot = method == "RequestReboot";
                    if (!running_or_fail (invocation, reboot ? "RequestReboot" : "RequestShutdown")) {
                        return;
                    }
                    invocation.return_value (null);
                    shutdown_requested (reboot);
                    break;
                }
                case "IsSessionRunning":
                {
                    invocation.return_value (new GLib.Variant ("(b)", phase == ManagerPhase.RUNNING));
                    break;
                }
                default:
                {
                    invocation.return_dbus_error ("org.freedesktop.DBus.Error.UnknownMethod",
                                                  "No such method %s".printf (method));
                    break;
                }
            }
        }

        private bool running_or_fail (GLib.DBusMethodInvocation invocation, string interface_name) {
            if (phase == ManagerPhase.RUNNING) {
                return true;
            }
            fail (invocation, "NotInRunning",
                  "%s interface is only available during the Running phase".printf (interface_name));
            return false;
        }

        private bool locked_down_fails (GLib.DBusMethodInvocation invocation) {
            if (!log_out_locked_down ()) {
                return false;
            }
            fail (invocation, "LockedDown", "Logout has been locked down");
            return true;
        }

        private static GLib.Variant paths (GLib.List<unowned string> keys) {
            var builder = new GLib.VariantBuilder (new GLib.VariantType ("ao"));
            foreach (unowned string key in keys) {
                builder.add ("o", key);
            }
            return new GLib.Variant ("(ao)", builder);
        }

        private async void register_client (string sender, string app_id, string startup_id,
                                            owned GLib.DBusMethodInvocation invocation) {
            if (phase >= ManagerPhase.QUERY_END_SESSION) {
                fail (invocation, "NotInRunning", "Unable to register client");
                return;
            }
            string id = startup_id;
            if (startup_id == "") {
                id = generate_startup_id ();
            } else {
                foreach (unowned Client other in clients.get_values ()) {
                    if (other.startup_id == startup_id) {
                        fail (invocation, "AlreadyRegistered", "Unable to register client");
                        return;
                    }
                }
            }
            var client = new Client (id, sender);
            AutostartApp? app = match_app != null ? match_app (startup_id, app_id) : null;
            client.app_id = app != null ? app.id : app_id;
            client.status = ClientStatus.REGISTERED;
            clients.insert (client.path, client);
            watch (sender);

            pending++;
            try {
                var reply = yield connection.call ("org.freedesktop.DBus", "/org/freedesktop/DBus",
                                                   "org.freedesktop.DBus", "GetConnectionUnixProcessID",
                                                   new GLib.Variant ("(s)", sender), new GLib.VariantType ("(u)"),
                                                   GLib.DBusCallFlags.NONE, 5000, cancellable);
                uint pid;
                reply.get ("(u)", out pid);
                client.pid = pid;
            } catch (GLib.Error e) {
                Logging.scope (LogDomain.POLO).info ("no process for %s: %s", sender, e.message);
            }
            pending--;
            if (clients.lookup (client.path) != client) {
                fail (invocation, "GeneralError", "Unable to register client");
                return;
            }
            try {
                client.export (connection);
            } catch (GLib.Error e) {
                Logging.scope (LogDomain.POLO).warning ("cannot export %s: %s", client.path, e.message);
                clients.remove (client.path);
                fail (invocation, "GeneralError", "Unable to register client");
                return;
            }
            invocation.return_value (new GLib.Variant ("(o)", client.path));
            emit ("ClientAdded", new GLib.Variant ("(o)", client.path));
        }

        private void inhibit (string sender, string app_id, uint xid, string reason, uint flags,
                              owned GLib.DBusMethodInvocation invocation) {
            if (logout_mode == LogoutMode.FORCE) {
                fail (invocation, "GeneralError", "Forced logout cannot be inhibited");
                return;
            }
            if (app_id == "") {
                fail (invocation, "GeneralError", "Application ID not specified");
                return;
            }
            if (reason == "") {
                fail (invocation, "GeneralError", "Reason not specified");
                return;
            }
            if (flags == 0) {
                fail (invocation, "GeneralError", "Invalid inhibit flags");
                return;
            }
            uint cookie = 0;
            do {
                cookie = (uint) GLib.Random.int_range (1, int32.MAX);
            } while (find_cookie (cookie) != null);
            var inhibitor = new Inhibitor (app_id, xid, reason, flags, sender, cookie);
            inhibitors.insert (inhibitor.path, inhibitor);
            watch (sender);
            invocation.return_value (new GLib.Variant ("(u)", cookie));
            emit ("InhibitorAdded", new GLib.Variant ("(o)", inhibitor.path));
        }

        private unowned Inhibitor? find_cookie (uint cookie) {
            foreach (unowned Inhibitor inhibitor in inhibitors.get_values ()) {
                if (inhibitor.cookie == cookie) {
                    return inhibitor;
                }
            }
            return null;
        }

        private void remove_inhibitor (string path) {
            if (inhibitors.remove (path)) {
                emit ("InhibitorRemoved", new GLib.Variant ("(o)", path));
            }
        }

        private void remove_client (string path) {
            var client = clients.lookup (path);
            if (client == null) {
                return;
            }
            client.unexport ();
            clients.remove (path);
            emit ("ClientRemoved", new GLib.Variant ("(o)", path));
        }

        private void watch (string bus_name) {
            if (bus_name in watches) {
                return;
            }
            watches[bus_name] = GLib.Bus.watch_name_on_connection (connection, bus_name,
                                                                   GLib.BusNameWatcherFlags.NONE, null,
                                                                   on_name_vanished);
        }

        private void on_name_vanished (GLib.DBusConnection connection, string bus_name) {
            if (bus_name in watches) {
                GLib.Bus.unwatch_name (watches[bus_name]);
                watches.remove (bus_name);
            }
            string[] gone = {};
            foreach (unowned Inhibitor inhibitor in inhibitors.get_values ()) {
                if (inhibitor.bus_name == bus_name) {
                    gone += inhibitor.path;
                }
            }
            foreach (unowned string path in gone) {
                remove_inhibitor (path);
            }
            gone = {};
            foreach (unowned Client client in clients.get_values ()) {
                if (client.bus_name == bus_name) {
                    gone += client.path;
                }
            }
            foreach (unowned string path in gone) {
                remove_client (path);
            }
        }

        private async void can_shutdown (owned GLib.DBusMethodInvocation invocation) {
            bool available = false;
            pending++;
            try {
                var system = yield GLib.Bus.get (GLib.BusType.SYSTEM, cancellable);
                foreach (unowned string query in new string[] { "CanPowerOff", "CanReboot", "CanSuspend",
                                                                 "CanHibernate" }) {
                    var reply = yield system.call ("org.freedesktop.login1", "/org/freedesktop/login1",
                                                   "org.freedesktop.login1.Manager", query, null,
                                                   new GLib.VariantType ("(s)"), GLib.DBusCallFlags.NONE,
                                                   5000, cancellable);
                    string answer;
                    reply.get ("(s)", out answer);
                    if (answer == "yes" || answer == "challenge") {
                        available = true;
                        break;
                    }
                }
            } catch (GLib.Error e) {
                Logging.scope (LogDomain.POLO).info ("CanShutdown: logind: %s", e.message);
            }
            invocation.return_value (new GLib.Variant ("(b)", available));
            pending--;
        }

        private async void check_accelerated () {
            string? helper = null;
            foreach (unowned string dir in new string[] { Config.LIBEXECDIR, "/usr/libexec" }) {
                var path = GLib.Path.build_filename (dir, CHECK_ACCELERATED);
                if (GLib.FileUtils.test (path, GLib.FileTest.IS_EXECUTABLE)) {
                    helper = path;
                    break;
                }
            }
            if (GLib.Environment.get_variable ("DISPLAY") == null || helper == null || CommandLine.test_mode) {
                return;
            }
            pending++;
            try {
                checking = new GLib.Subprocess.newv ({ helper }, GLib.SubprocessFlags.STDOUT_PIPE |
                                                     GLib.SubprocessFlags.STDERR_SILENCE);
                string output;
                yield checking.communicate_utf8_async (null, cancellable, out output, null);
                if (checking.get_if_exited () && checking.get_exit_status () == 0 && output != null) {
                    renderer = output.strip ();
                }
            } catch (GLib.Error e) {
                if (!(e is GLib.IOError.CANCELLED)) {
                    Logging.scope (LogDomain.POLO).info ("cannot check acceleration: %s", e.message);
                }
            }
            checking = null;
            pending--;
        }

        private static int sequence = -1;
        private static uint rand1 = 0;
        private static uint rand2 = 0;

        public static string generate_startup_id () {
            if (rand1 == 0) {
                rand1 = GLib.Random.next_int ();
                rand2 = GLib.Random.next_int ();
            }
            sequence = (sequence + 1) % 10000;
            int64 now = GLib.get_real_time ();
            ulong pid = 0;
            try {
                pid = (ulong) new GLib.Credentials ().get_unix_pid ();
            } catch (GLib.Error e) {
                pid = 0;
            }
            return "10%.04x%.04x%.10lu%.3u%.10lu%.4d".printf (rand1, rand2, (ulong) (now / 1000000),
                                                             (uint) (now % 1000000), pid, sequence);
        }
    }
}
