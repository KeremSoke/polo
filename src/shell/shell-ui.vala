/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Starts polo-shell-ui over a trusted socket pair, offers polo_shell_ui_v1
 * to that client alone, and restarts the helper when it exits: at most
 * RESTARTS times within RESTART_WINDOW, then Polo carries on without it.
 * POLO_SHELL_UI names another helper; empty means none.
 */

namespace Polo.Shell {
    public class ShellUi : GLib.Object {
        public const int RESTARTS = 5;
        public const int64 RESTART_WINDOW = 60 * GLib.TimeSpan.SECOND;

        public Backend.ShellUi protocol { get; construct; }

        private Spawn spawn;
        private Trust trust;
        private string? path;
        private GLib.Subprocess? process = null;
        private int64[] restarts = {};
        private bool stopped = false;

        public ShellUi (Backend.Server server, Spawn spawn, Trust trust) {
            Object (protocol: server.get_shell_ui ());
            this.spawn = spawn;
            this.trust = trust;
            path = GLib.Environment.get_variable ("POLO_SHELL_UI");
            if (path == null) {
                path = GLib.Path.build_filename (Config.LIBEXECDIR, "polo-shell-ui");
            }
            protocol.bound.connect (on_bound);
            protocol.unbound.connect (on_unbound);
        }

        public void start () {
            if (path == "") {
                Logging.scope (LogDomain.POLO).info ("polo-shell-ui is off (POLO_SHELL_UI is empty)");
                return;
            }
            launch ();
        }

        public void shutdown () {
            stopped = true;
            protocol.bound.disconnect (on_bound);
            protocol.unbound.disconnect (on_unbound);
            trust.shell_ui_client = null;
            if (process != null) {
                process.send_signal (GLib.ProcessSignal.TERM);
                process = null;
            }
        }

        private void launch () {
            string[] argv = { path };
            unowned Backend.Client client;
            try {
                process = spawn.launch_trusted (argv, out client);
            } catch (GLib.Error e) {
                Logging.scope (LogDomain.POLO).warning ("cannot start %s: %s; Polo runs without it",
                                                        path, e.message);
                trust.shell_ui_client = null;
                return;
            }
            trust.shell_ui_client = client;
            wait.begin (process);
        }

        private async void wait (GLib.Subprocess helper) {
            try {
                yield helper.wait_async ();
            } catch (GLib.Error e) {
                return;
            }
            if (stopped || helper != process) {
                return;
            }
            process = null;
            trust.shell_ui_client = null;

            int64 now = GLib.get_monotonic_time ();
            int64[] recent = {};
            foreach (var time in restarts) {
                if (now - time < RESTART_WINDOW) {
                    recent += time;
                }
            }
            restarts = recent;
            if (restarts.length >= RESTARTS) {
                Logging.scope (LogDomain.POLO).warning (
                    "polo-shell-ui exited %d times within a minute; not restarting it", RESTARTS + 1);
                return;
            }
            restarts += now;
            launch ();
        }

        private void on_bound (Backend.Client client) {
            Logging.scope (LogDomain.POLO).info ("polo-shell-ui is ready (pid %d)", client.get_pid ());
        }

        private void on_unbound () {
            Logging.scope (LogDomain.POLO).debug ("polo-shell-ui let go of polo_shell_ui_v1");
        }
    }
}
