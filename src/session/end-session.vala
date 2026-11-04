/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

namespace Polo {
    public enum EndAction {
        LOGOUT,
        SHUTDOWN,
        REBOOT,
    }

    public class EndSession : GLib.Object {
        public const int RESPONSE_LOGOUT = 0;
        public const int RESPONSE_SWITCH_USER = 1;
        public const int RESPONSE_SHUTDOWN = 2;
        public const int RESPONSE_REBOOT = 3;
        public const int RESPONSE_HIBERNATE = 4;
        public const int RESPONSE_SLEEP = 5;
        private const int RESPONSE_NONE = -1;
        private const int RESPONSE_ACCEPT = -3;
        private const int RESPONSE_CANCEL = -6;

        private const string SESSION_SCHEMA = "org.mate.session";
        private const string LOCKDOWN_SCHEMA = "org.mate.lockdown";
        private const string GETTEXT_DOMAIN = "mate-session-manager";
        private const uint FIRST_DIALOG = 0x30000000;
        private const uint QUERY_TIMEOUT_MS = 1000;
        private const uint PHASE_TIMEOUT_MS = 30000;
        private const uint STOP_TIMEOUT_MS = 1000;
        private const uint TERM_TIMEOUT_MS = 1000;
        private const uint FLAG_FORCEFUL = 1;
        private const string LOGIND = "org.freedesktop.login1";
        private const string LOGIND_PATH = "/org/freedesktop/login1";

        [CCode (cname = "kill", cheader_filename = "signal.h")]
        private static extern int kill (int pid, int sig);

        [CCode (cname = "getpid", cheader_filename = "unistd.h")]
        private static extern int getpid ();

        [CCode (cname = "g_dgettext", cheader_filename = "glib.h")]
        private static extern unowned string dgettext (string domain, string msgid);

        private delegate bool Done ();

        public signal void finished (EndAction action);
        public signal void action_unavailable (int response);

        private Dbus.Manager manager;
        private Backend.ShellUi shell_ui;
        private GLib.Settings? session = null;
        private GLib.Settings? lockdown = null;
        private GLib.Cancellable cancellable = new GLib.Cancellable ();

        private bool confirming = false;
        private bool ending = false;
        private EndAction action = EndAction.LOGOUT;
        private Dbus.LogoutMode mode = Dbus.LogoutMode.NORMAL;
        private GLib.GenericSet<Dbus.Client> waiting =
            new GLib.GenericSet<Dbus.Client> (GLib.direct_hash, GLib.direct_equal);

        private uint next_dialog = FIRST_DIALOG;
        private uint dialog = 0;
        private int answer = RESPONSE_NONE;
        private GLib.SourceFunc? answered = null;
        private bool blocking = false;
        private bool reshow = false;

        private Done? done = null;
        private GLib.SourceFunc? woken = null;

        public EndSession (Dbus.Manager manager, Backend.ShellUi shell_ui) {
            this.manager = manager;
            this.shell_ui = shell_ui;
            session = settings_if_installed (SESSION_SCHEMA);
            lockdown = settings_if_installed (LOCKDOWN_SCHEMA);
            manager.logout_requested.connect (request_logout);
            manager.shutdown_dialog_requested.connect (request_shutdown_dialog);
            manager.shutdown_requested.connect (request_power_off);
            manager.client_response.connect (on_client_response);
            manager.client_removed.connect (on_client_removed);
            manager.inhibitors_changed.connect (on_inhibitors_changed);
            shell_ui.dialog_response.connect (on_dialog_response);
            shell_ui.unbound.connect (on_unbound);
        }

        public void shutdown () {
            cancellable.cancel ();
            manager.logout_requested.disconnect (request_logout);
            manager.shutdown_dialog_requested.disconnect (request_shutdown_dialog);
            manager.shutdown_requested.disconnect (request_power_off);
            manager.client_response.disconnect (on_client_response);
            manager.client_removed.disconnect (on_client_removed);
            manager.inhibitors_changed.disconnect (on_inhibitors_changed);
            shell_ui.dialog_response.disconnect (on_dialog_response);
            shell_ui.unbound.disconnect (on_unbound);
            if (dialog != 0) {
                shell_ui.hide_dialog (dialog);
                dialog = 0;
            }
        }

        private static GLib.Settings? settings_if_installed (string id) {
            var source = GLib.SettingsSchemaSource.get_default ();
            var schema = source != null ? source.lookup (id, true) : null;
            return schema != null ? new GLib.Settings.full (schema, null, null) : null;
        }

        private static unowned string msm (string msgid) {
            return dgettext (GETTEXT_DOMAIN, msgid);
        }

        private bool busy () {
            return confirming || ending || manager.phase >= Dbus.ManagerPhase.QUERY_END_SESSION;
        }

        public void request_logout (Dbus.LogoutMode requested) {
            if (busy ()) {
                return;
            }
            bool prompt = session == null || session.get_boolean ("logout-prompt");
            if (requested == Dbus.LogoutMode.NORMAL && prompt) {
                confirm.begin (false);
            } else {
                run.begin (EndAction.LOGOUT, requested);
            }
        }

        public void request_shutdown_dialog () {
            if (busy ()) {
                return;
            }
            confirm.begin (true);
        }

        public void request_power_off (bool reboot) {
            if (busy ()) {
                return;
            }
            run.begin (reboot ? EndAction.REBOOT : EndAction.SHUTDOWN, Dbus.LogoutMode.NORMAL);
        }

        private async void confirm (bool shutdown) {
            confirming = true;
            var actions = new GLib.VariantBuilder (new GLib.VariantType ("as"));
            if (shutdown) {
                string[] queries = { "CanSuspend", "CanHibernate", "CanReboot", "CanPowerOff" };
                string[] names = { "suspend", "hibernate", "reboot", "shutdown" };
                for (int i = 0; i < queries.length; i++) {
                    if (yield logind_can (queries[i])) {
                        actions.add ("s", names[i]);
                    }
                }
            } else if (yield can_switch_user ()) {
                actions.add ("s", "switch-user");
            }
            var payload = new GLib.VariantBuilder (new GLib.VariantType ("a{sv}"));
            payload.add ("{sv}", "dialog", new GLib.Variant.string (shutdown ? "shutdown" : "logout"));
            payload.add ("{sv}", "timeout",
                         new GLib.Variant.int32 (session != null ? session.get_int ("logout-timeout") : 60));
            payload.add ("{sv}", "actions", actions.end ());
            int response = yield ask (payload.end (), shutdown ? RESPONSE_SHUTDOWN : RESPONSE_LOGOUT);
            confirming = false;
            if (cancellable.is_cancelled ()) {
                return;
            }
            switch (response) {
                case RESPONSE_LOGOUT:
                    run.begin (EndAction.LOGOUT, Dbus.LogoutMode.NO_CONFIRMATION);
                    break;
                case RESPONSE_SHUTDOWN:
                    run.begin (EndAction.SHUTDOWN, Dbus.LogoutMode.NORMAL);
                    break;
                case RESPONSE_REBOOT:
                    run.begin (EndAction.REBOOT, Dbus.LogoutMode.NORMAL);
                    break;
                case RESPONSE_SWITCH_USER:
                case RESPONSE_HIBERNATE:
                case RESPONSE_SLEEP:
                    action_unavailable (response);
                    break;
                default:
                    break;
            }
        }

        private async void run (EndAction requested, Dbus.LogoutMode requested_mode) {
            if (ending) {
                return;
            }
            if (manager.log_out_locked_down ()) {
                Logging.scope (LogDomain.POLO).warning ("Unable to logout: Logout has been locked down");
                return;
            }
            ending = true;
            action = requested;
            mode = requested_mode;
            manager.logout_mode = mode;
            if (!yield query_end_session ()) {
                cancel ();
                ending = false;
                return;
            }
            yield end_session ();
            yield exit ();
            if (cancellable.is_cancelled ()) {
                return;
            }
            manager.session_over ();
            finished (action);
        }

        private uint flags () {
            return mode == Dbus.LogoutMode.FORCE ? FLAG_FORCEFUL : 0;
        }

        private void wait_for_all () {
            waiting.remove_all ();
            foreach (unowned Dbus.Client client in manager.get_clients ()) {
                waiting.add (client);
            }
        }

        private async bool query_end_session () {
            manager.set_phase (Dbus.ManagerPhase.QUERY_END_SESSION);
            wait_for_all ();
            foreach (var client in waiting.get_values ()) {
                client.query_end_session (flags ());
            }
            yield wait (QUERY_TIMEOUT_MS, () => waiting.length == 0);
            foreach (var client in waiting.get_values ()) {
                Logging.scope (LogDomain.POLO).warning ("Client '%s' failed to reply before timeout",
                                                        client.path);
                if (mode != Dbus.LogoutMode.FORCE) {
                    manager.inhibit_for_client (client, msm ("Not responding"));
                }
            }
            waiting.remove_all ();
            if (cancellable.is_cancelled ()) {
                return false;
            }
            if (!manager.is_inhibited (Dbus.InhibitorFlags.LOGOUT)) {
                return true;
            }
            if ((yield ask_blocking ()) != RESPONSE_ACCEPT) {
                return false;
            }
            mode = Dbus.LogoutMode.FORCE;
            manager.logout_mode = mode;
            return true;
        }

        private async void end_session () {
            manager.set_phase (Dbus.ManagerPhase.END_SESSION);
            wait_for_all ();
            if (waiting.length == 0) {
                return;
            }
            foreach (var client in waiting.get_values ()) {
                client.end_session (flags ());
            }
            yield wait (PHASE_TIMEOUT_MS, () => waiting.length == 0 &&
                        (mode == Dbus.LogoutMode.FORCE || !manager.is_inhibited (Dbus.InhibitorFlags.LOGOUT)));
            waiting.remove_all ();
        }

        private async void exit () {
            manager.set_phase (Dbus.ManagerPhase.EXIT);
            foreach (unowned Dbus.Client client in manager.get_clients ()) {
                client.stop ();
            }
            yield wait (STOP_TIMEOUT_MS, () => manager.get_clients ().length () == 0);
            if (signal_clients (GLib.ProcessSignal.TERM)) {
                yield wait (TERM_TIMEOUT_MS, () => manager.get_clients ().length () == 0);
                signal_clients (GLib.ProcessSignal.KILL);
            }
        }

        private bool signal_clients (GLib.ProcessSignal signum) {
            bool sent = false;
            int self = getpid ();
            foreach (unowned Dbus.Client client in manager.get_clients ()) {
                int pid = (int) client.pid;
                if (pid <= 1 || pid == self) {
                    continue;
                }
                Logging.scope (LogDomain.POLO).info ("client %s (pid %d) is still running; sending signal %d",
                                                     client.path, pid, (int) signum);
                if (kill (pid, (int) signum) == 0) {
                    sent = true;
                }
            }
            return sent;
        }

        private void cancel () {
            manager.remove_client_owned_inhibitors ();
            foreach (unowned Dbus.Client client in manager.get_clients ()) {
                client.cancel_end_session ();
            }
            manager.set_phase (Dbus.ManagerPhase.RUNNING);
            mode = Dbus.LogoutMode.NORMAL;
            manager.logout_mode = mode;
        }

        private void on_client_response (Dbus.Client client, bool is_ok, string reason) {
            if (manager.phase < Dbus.ManagerPhase.QUERY_END_SESSION) {
                return;
            }
            waiting.remove (client);
            if (!is_ok && mode != Dbus.LogoutMode.FORCE) {
                manager.inhibit_for_client (client, reason != "" ? reason : msm ("Not responding"));
            } else {
                manager.remove_client_inhibitors (client);
            }
            wake ();
        }

        private void on_client_removed (Dbus.Client client) {
            waiting.remove (client);
            wake ();
        }

        private async void wait (uint timeout_ms, owned Done until) {
            if (until ()) {
                return;
            }
            done = (owned) until;
            woken = wait.callback;
            uint timer = 0;
            timer = GLib.Timeout.add (timeout_ms, () => {
                timer = 0;
                resume ();
                return GLib.Source.REMOVE;
            });
            yield;
            if (timer != 0) {
                GLib.Source.remove (timer);
            }
        }

        private void wake () {
            if (done != null && done ()) {
                resume ();
            }
        }

        private void resume () {
            if (woken == null) {
                return;
            }
            var callback = (owned) woken;
            woken = null;
            done = null;
            callback ();
        }

        private async int ask (GLib.Variant payload, int fallback) {
            if (!shell_ui.is_bound ()) {
                Logging.scope (LogDomain.POLO).info ("no polo-shell-ui for the end-session dialog");
                return fallback;
            }
            dialog = next_dialog++;
            answered = ask.callback;
            shell_ui.show_dialog (dialog, Backend.DialogKind.END_SESSION, null, payload.print (true));
            yield;
            return answer;
        }

        private async int ask_blocking () {
            blocking = true;
            int response = RESPONSE_ACCEPT;
            do {
                if (!manager.is_inhibited (Dbus.InhibitorFlags.LOGOUT)) {
                    response = RESPONSE_ACCEPT;
                    break;
                }
                reshow = false;
                response = yield ask (blocking_payload (), RESPONSE_CANCEL);
            } while (reshow && !cancellable.is_cancelled ());
            blocking = false;
            return response;
        }

        private GLib.Variant blocking_payload () {
            var list = new GLib.VariantBuilder (new GLib.VariantType ("a(ss)"));
            foreach (unowned Dbus.Inhibitor inhibitor in manager.get_inhibitors ()) {
                if ((inhibitor.flags & Dbus.InhibitorFlags.LOGOUT) != 0) {
                    list.add ("(ss)", inhibitor.app_id, inhibitor.reason);
                }
            }
            string[] names = { "logout", "shutdown", "reboot" };
            var payload = new GLib.VariantBuilder (new GLib.VariantType ("a{sv}"));
            payload.add ("{sv}", "dialog", new GLib.Variant.string ("inhibit"));
            payload.add ("{sv}", "action", new GLib.Variant.string (names[(int) action]));
            payload.add ("{sv}", "inhibitors", list.end ());
            return payload.end ();
        }

        private void close_dialog (int response) {
            if (dialog == 0) {
                return;
            }
            uint closing = dialog;
            shell_ui.hide_dialog (closing);
            on_dialog_response (closing, response, null);
        }

        private void on_dialog_response (uint id, int response, string? text) {
            if (id != dialog || answered == null) {
                return;
            }
            dialog = 0;
            answer = response;
            var callback = (owned) answered;
            answered = null;
            callback ();
        }

        private void on_inhibitors_changed () {
            if (blocking && dialog != 0) {
                reshow = true;
                close_dialog (RESPONSE_NONE);
            }
        }

        private void on_unbound () {
            close_dialog (RESPONSE_NONE);
        }

        private async bool logind_can (string method) {
            try {
                var system = yield GLib.Bus.get (GLib.BusType.SYSTEM, cancellable);
                var reply = yield system.call (LOGIND, LOGIND_PATH, LOGIND + ".Manager", method, null,
                                               new GLib.VariantType ("(s)"), GLib.DBusCallFlags.NONE, 5000,
                                               cancellable);
                string result;
                reply.get ("(s)", out result);
                return result == "yes" || result == "challenge";
            } catch (GLib.Error e) {
                Logging.scope (LogDomain.POLO).info ("%s: logind: %s", method, e.message);
                return false;
            }
        }

        private async bool can_switch_user () {
            if (lockdown != null && lockdown.get_boolean ("disable-user-switching")) {
                return false;
            }
            try {
                var system = yield GLib.Bus.get (GLib.BusType.SYSTEM, cancellable);
                var reply = yield system.call (LOGIND, LOGIND_PATH + "/seat/auto", "org.freedesktop.DBus.Properties",
                                               "Get", new GLib.Variant ("(ss)", LOGIND + ".Seat", "CanMultiSession"),
                                               new GLib.VariantType ("(v)"), GLib.DBusCallFlags.NONE, 5000,
                                               cancellable);
                GLib.Variant value;
                reply.get ("(v)", out value);
                return value.is_of_type (GLib.VariantType.BOOLEAN) && value.get_boolean ();
            } catch (GLib.Error e) {
                Logging.scope (LogDomain.POLO).info ("CanMultiSession: logind: %s", e.message);
                return false;
            }
        }
    }
}
