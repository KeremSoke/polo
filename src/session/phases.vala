/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

namespace Polo {
    public class SessionPhases : GLib.Object {
        private const string SCHEMA = "org.mate.polo.session";
        private const int DEFAULT_TIMEOUT = 10;

        private const AutostartPhase[] ORDER = {
            AutostartPhase.INITIALIZATION, AutostartPhase.WINDOW_MANAGER, AutostartPhase.PANEL,
            AutostartPhase.DESKTOP, AutostartPhase.APPLICATION,
        };

        public signal void running ();
        public signal void phase_started (AutostartPhase phase);

        private const uint MESSAGE_DIALOG_ID = 0x40000000;

        private Spawn spawn;
        private Backend.ShellUi shell_ui;
        private uint next_dialog = MESSAGE_DIALOG_ID;
        private GLib.HashTable<AutostartApp, RestartPolicy> restarts =
            new GLib.HashTable<AutostartApp, RestartPolicy> (GLib.direct_hash, GLib.direct_equal);
        private GLib.GenericSet<GLib.Subprocess> withdrawn =
            new GLib.GenericSet<GLib.Subprocess> (GLib.direct_hash, GLib.direct_equal);
        private Shell.Desktop desktop;
        private GLib.List<AutostartApp> apps;
        private int timeout = DEFAULT_TIMEOUT;
        private string[] trusted_components = {};
        private string[] x11_components = {};
        private bool stopped = false;
        private GLib.GenericSet<uint> timers = new GLib.GenericSet<uint> (GLib.direct_hash, GLib.direct_equal);
        private GLib.HashTable<AutostartApp, GLib.Subprocess> processes =
            new GLib.HashTable<AutostartApp, GLib.Subprocess> (GLib.direct_hash, GLib.direct_equal);
        private GLib.HashTable<int, AutostartApp> pending =
            new GLib.HashTable<int, AutostartApp> (GLib.direct_hash, GLib.direct_equal);
        private SourceFunc? phase_done = null;
        private GLib.Settings? settings = null;

        public SessionPhases (Spawn spawn, Shell.Desktop desktop, Backend.ShellUi shell_ui,
                              owned GLib.List<AutostartApp> apps) {
            this.spawn = spawn;
            this.shell_ui = shell_ui;
            this.desktop = desktop;
            this.apps = (owned) apps;
            var source = GLib.SettingsSchemaSource.get_default ();
            var schema = source != null ? source.lookup (SCHEMA, true) : null;
            if (schema != null) {
                settings = new GLib.Settings.full (schema, null, null);
                timeout = settings.get_int ("phase-timeout");
                trusted_components = settings.get_strv ("trusted-components");
                x11_components = settings.get_strv ("x11-components");
            } else {
                Logging.scope (LogDomain.POLO).warning ("%s is not installed", SCHEMA);
            }
            desktop.client_mapped.connect (on_client_mapped);
        }

        public async void run () {
            foreach (var phase in ORDER) {
                if (stopped) {
                    return;
                }
                Logging.scope (LogDomain.POLO).info ("session phase %s", phase.to_name ());
                phase_started (phase);
                yield run_phase (phase);
            }
            if (!stopped) {
                running ();
            }
        }

        public void stop () {
            stopped = true;
            desktop.client_mapped.disconnect (on_client_mapped);
            timers.foreach ((timer) => {
                GLib.Source.remove (timer);
            });
            timers.remove_all ();
            foreach (var app in apps) {
                app.condition_changed.disconnect (on_condition_changed);
            }
            finish_phase ();
        }

        private async void run_phase (AutostartPhase phase) {
            bool waits = phase < AutostartPhase.APPLICATION && phase != AutostartPhase.WINDOW_MANAGER;
            foreach (var app in apps) {
                if (app.phase != phase) {
                    continue;
                }
                app.condition_changed.connect (on_condition_changed);
                if (app.is_disabled () || app.is_conditionally_disabled ()) {
                    continue;
                }
                if (app.delay > 0) {
                    start_later (app);
                    continue;
                }
                int pid = launch (app);
                if (pid > 0 && waits) {
                    pending.insert (pid, app);
                }
            }
            if (pending.size () == 0) {
                return;
            }

            phase_done = run_phase.callback;
            uint timer = 0;
            timer = GLib.Timeout.add_seconds (timeout, () => {
                timers.remove (timer);
                pending.foreach ((pid, app) => {
                    Logging.scope (LogDomain.POLO).info ("session phase went on without %s", app.id);
                });
                finish_phase ();
                return GLib.Source.REMOVE;
            });
            timers.add (timer);
            yield;
            if (timer in timers) {
                timers.remove (timer);
                GLib.Source.remove (timer);
            }
        }

        private void finish_phase () {
            pending.remove_all ();
            if (phase_done != null) {
                var done = (owned) phase_done;
                phase_done = null;
                GLib.Idle.add ((owned) done);
            }
        }

        private void settled (int pid) {
            if (pending.remove (pid) && pending.size () == 0) {
                finish_phase ();
            }
        }

        private void on_client_mapped (int pid) {
            settled (pid);
        }

        public AutostartApp? register_client (string startup_id, string app_id) {
            AutostartApp? found = null;
            foreach (var app in apps) {
                if (startup_id != "" && app.startup_id == startup_id) {
                    found = app;
                    break;
                }
            }
            if (found == null && app_id != "") {
                foreach (var app in apps) {
                    if (app.id == app_id) {
                        found = app;
                        break;
                    }
                }
            }
            if (found != null) {
                int pid = -1;
                pending.foreach ((key, app) => {
                    if (app == found) {
                        pid = key;
                    }
                });
                if (pid >= 0) {
                    settled (pid);
                }
            }
            return found;
        }

        /* IsAutostartConditionHandled */
        public bool handles_condition (string condition) {
            foreach (var app in apps) {
                if (app.condition_string == condition && !app.is_disabled ()) {
                    return true;
                }
            }
            return false;
        }

        public unowned GLib.List<AutostartApp> get_apps () {
            return apps;
        }

        private void start_later (AutostartApp app) {
            uint timer = 0;
            timer = GLib.Timeout.add_seconds (app.delay, () => {
                timers.remove (timer);
                if (!app.is_disabled () && !app.is_conditionally_disabled ()) {
                    launch (app);
                }
                return GLib.Source.REMOVE;
            });
            timers.add (timer);
        }

        private void on_condition_changed (AutostartApp app, bool condition) {
            var process = processes.lookup (app);
            if (condition && process == null) {
                launch (app);
            } else if (!condition && process != null) {
                withdrawn.add (process);
                process.send_signal (GLib.ProcessSignal.TERM);
            }
        }

        private int launch (AutostartApp app) {
            unowned LogScope log = Logging.scope (LogDomain.POLO);
            string[] argv;
            try {
                argv = app.argv ();
            } catch (GLib.Error e) {
                log.warning ("cannot start %s: %s", app.id, e.message);
                return -1;
            }
            if (argv.length == 0) {
                log.info ("%s has nothing to run", app.id);
                return -1;
            }
            try {
                var flags = is_trusted (app, argv[0]) ? SpawnFlags.TRUSTED : SpawnFlags.NONE;
                if (is_listed (app, argv[0], x11_components)) {
                    flags |= SpawnFlags.X11;
                }
                app.startup_id = Dbus.Manager.generate_startup_id ();
                var process = spawn.launch (argv, flags, app.working_directory,
                                            { "DESKTOP_AUTOSTART_ID=" + app.startup_id });
                processes.insert (app, process);
                watch.begin (app, process);
                return int.parse (process.get_identifier () ?? "-1");
            } catch (GLib.Error e) {
                log.warning ("cannot start %s: %s", app.id, e.message);
                return -1;
            }
        }

        private bool is_trusted (AutostartApp app, string program) {
            return app.required || is_listed (app, program, trusted_components);
        }

        private static bool is_listed (AutostartApp app, string program, string[] list) {
            var name = app.id.has_suffix (".desktop") ? app.id[0 : app.id.length - ".desktop".length] : app.id;
            return name in list || GLib.Path.get_basename (program) in list;
        }

        private async void watch (AutostartApp app, GLib.Subprocess process) {
            int pid = int.parse (process.get_identifier () ?? "-1");
            try {
                yield process.wait_async ();
            } catch (GLib.Error e) {
                return;
            }
            if (processes.lookup (app) == process) {
                processes.remove (app);
            }
            settled (pid);
            if (!withdrawn.remove (process) && !stopped) {
                maybe_restart (app, process);
            }
        }

        private void maybe_restart (AutostartApp app, GLib.Subprocess process) {
            bool clean = process.get_if_exited () && process.get_exit_status () == 0;
            if (!app.autorestart && (!app.required || clean)) {
                return;
            }
            var policy = restarts.lookup (app);
            if (policy == null) {
                policy = new RestartPolicy ();
                restarts.insert (app, policy);
            }
            int delay = policy.next_delay (GLib.get_monotonic_time ());
            unowned LogScope log = Logging.scope (LogDomain.POLO);
            if (delay < 0) {
                log.warning ("%s keeps crashing: not restarting it", app.id);
                if (app.required) {
                    report_crashing (app);
                }
                return;
            }
            log.info ("restarting %s in %d ms", app.id, delay);
            uint timer = 0;
            timer = GLib.Timeout.add (delay, () => {
                timers.remove (timer);
                if (!processes.contains (app) && !app.is_disabled () && !app.is_conditionally_disabled ()) {
                    launch (app);
                }
                return GLib.Source.REMOVE;
            });
            timers.add (timer);
        }

        private void report_crashing (AutostartApp app) {
            var message = "The %s keeps crashing, so it will not be restarted.".printf (app.describe ());
            if (shell_ui.is_bound ()) {
                shell_ui.show_dialog (next_dialog++, Backend.DialogKind.MESSAGE, null, message);
                return;
            }
            string[] argv = { "zenity", "--error", "--title", Shell.NotResponding.marco ("Marco"),
                              "--text", message };
            try {
                spawn.launch (argv);
            } catch (GLib.Error e) {
                Logging.scope (LogDomain.POLO).warning ("cannot show \"%s\": %s", message, e.message);
            }
        }
    }
}
