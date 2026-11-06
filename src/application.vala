/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Polo.Application owns the main loop, the backend server and everything
 * built on it, and tears them down in reverse order.
 */

namespace Polo {
    private class CommandLine {
        public static bool version = false;
        public static string? log_file = null;
        public static string? debug_scopes = null;
        public static bool log_self_test = false;
        public static string? backend = null;
        public static string? renderer = null;
        public static int width = 0;
        public static int height = 0;
        public static int scale = 1;
        public static string? scales = null;
        public static int outputs = 1;
        public static bool fullscreen = false;
        public static string? socket = null;
        [CCode (array_length = false, array_null_terminated = true)]
        public static string[]? exec = null;
        public static bool test_mode = false;
        public static bool debug_kill = false;
        public static string? xwayland = null;
        public static bool session = false;

        public const GLib.OptionEntry[] ENTRIES = {
            { "session", 0, 0, GLib.OptionArg.NONE, ref session,
              "Run as the MATE session: eager XWayland, trusted components, the Polo dconf profile", null },
            { "debug-kill", 0, 0, GLib.OptionArg.NONE, ref debug_kill,
              "Enable Ctrl+Alt+Backspace emergency exit", null },
            { "xwayland", 0, 0, GLib.OptionArg.STRING, ref xwayland,
              "off, lazy (start with the first X client) or eager (default: lazy)", "MODE" },
            { "version", 0, 0, GLib.OptionArg.NONE, ref version,
              "Print version information and exit", null },
            { "backend", 0, 0, GLib.OptionArg.STRING, ref backend,
              "drm, wayland or headless (default: wayland inside a Wayland session, else drm)",
              "NAME" },
            { "renderer", 0, 0, GLib.OptionArg.STRING, ref renderer,
              "auto, gl or pixman", "NAME" },
            { "width", 0, 0, GLib.OptionArg.INT, ref width,
              "Width of nested or headless outputs", "PIXELS" },
            { "height", 0, 0, GLib.OptionArg.INT, ref height,
              "Height of nested or headless outputs", "PIXELS" },
            { "scale", 0, 0, GLib.OptionArg.INT, ref scale,
              "Scale of nested or headless outputs", "N" },
            { "outputs", 0, 0, GLib.OptionArg.INT, ref outputs,
              "Number of nested or headless outputs", "N" },
            { "scales", 0, 0, GLib.OptionArg.STRING, ref scales,
              "Scale of each nested or headless output, as 1,2", "LIST" },
            { "fullscreen", 0, 0, GLib.OptionArg.NONE, ref fullscreen,
              "Open the nested output fullscreen", null },
            { "socket", 0, 0, GLib.OptionArg.STRING, ref socket,
              "Wayland socket name (default: first free wayland-N)", "NAME" },
            { "exec", 0, 0, GLib.OptionArg.STRING_ARRAY, ref exec,
              "Run COMMAND as a client once started; repeatable", "COMMAND" },
            { "log", 0, 0, GLib.OptionArg.FILENAME, ref log_file,
              "Write the log to FILE instead of stderr", "FILE" },
            { "debug-scopes", 0, 0, GLib.OptionArg.STRING, ref debug_scopes,
              "Print debug messages of these comma-separated scopes", "A,B" },
            { "test-mode", 0, GLib.OptionFlags.HIDDEN, GLib.OptionArg.NONE, ref test_mode,
              "Enable polo_test_v1 (test builds only)", null },
            { "log-self-test", 0, GLib.OptionFlags.HIDDEN, GLib.OptionArg.NONE,
              ref log_self_test, "Log one message per scope and exit", null },
            { null }
        };
    }

    public class Application : GLib.Object {
        private GLib.MainLoop loop;
        private Backend.Server? server;
        private Effects.Engine? effects_engine;
        private Shell.Desktop? desktop;
        private Protocols.ForeignToplevels? foreign_toplevels;
        private Protocols.Workspaces? workspaces;
        private GLib.HashTable<string, GLib.Variant> test_overrides =
            new GLib.HashTable<string, GLib.Variant> (GLib.str_hash, GLib.str_equal);
        private Settings.MarcoPrefsReader? marco_prefs = null;
        private Input.XkbConfig system_keyboard;
        private string? xkb_layout = null;
        private string? xkb_variant = null;
        private string? xkb_options = null;
        private Policy.Model? model;
        private Trust trust = new Trust ();
        private SessionEnv? session_env = null;
        private SessionPhases? phases = null;
        private Session? session_manager = null;
        private EndSession? end_session = null;
        private Logind? logind = null;
        private IdleTracker? idle_tracker = null;
        private Protocols.SessionLock? session_lock = null;
        private Shell.MediaKeyActions? media_key_actions = null;
        private Input.MediaKeys? media_keys = null;
        private Input.AccessX? access_x = null;
        private Protocols.OutputManagement? output_management = null;
        private Dbus.Outputs? outputs_dbus = null;
        private Dbus.ScreenShare? screen_share = null;
        private WindowStates? window_states = null;
#if POLO_HAVE_XSMP
        private XsmpSession? xsmp = null;
#endif
        private const uint CHILD_TERM_SECONDS = 5;
        private Spawn? spawn;
        private Shell.NotResponding? not_responding;
        private Shell.ShellUi? shell_ui;
        private Shell.RenameWorkspace? rename_workspace;
        private Input.CustomShortcuts? custom_shortcuts = null;
        private Input.TestShortcutDirectory? test_shortcuts = null;
        private int exit_status = 0;
        private string cursor_theme = "default";

        public int run (string[] args) {
            var context = new GLib.OptionContext ("- the MATE Wayland compositor and session");
            context.add_main_entries (CommandLine.ENTRIES, null);
            string[] argv = args;
            try {
                context.parse_strv (ref argv);
            } catch (GLib.OptionError e) {
                stderr.printf ("polo: %s\n", e.message);
                return 2;
            }

            if (CommandLine.version) {
                print_version ();
                return 0;
            }

            try {
                Logging.init (CommandLine.log_file, CommandLine.debug_scopes);
            } catch (GLib.Error e) {
                stderr.printf ("polo: %s\n", e.message);
                return 1;
            }

            if (CommandLine.log_self_test) {
                log_self_test ();
                Logging.shutdown ();
                return 0;
            }

            if (CommandLine.session) {
                try {
                    Session.check_no_session_manager ();
                } catch (SessionError e) {
                    stderr.printf ("polo: %s\n", e.message);
                    Logging.scope (LogDomain.POLO).warning ("%s", e.message);
                    Logging.shutdown ();
                    return 1;
                }
                Session.use_dconf_profile ();
                SessionEnv.set_fixed ();
            }

            int status = run_compositor ();
            Logging.shutdown ();
            return status;
        }

        private int run_compositor () {
            unowned LogScope log = Logging.scope (LogDomain.POLO);
            Backend.Config config;
            try {
                config = make_config ();
            } catch (GLib.OptionError e) {
                stderr.printf ("polo: %s\n", e.message);
                return 2;
            }

            if (CommandLine.test_mode) {
                apply_test_settings ();
            }
            loop = new GLib.MainLoop (null, false);
            server = new Backend.Server ();
            model = new Policy.Model (CommandLine.test_mode ? test_prefs () : MarcoPrefs.defaults ());
            effects_engine = new Effects.Engine (settings_if_installed ("org.mate.interface"),
                                                 settings_if_installed ("org.mate.polo.effects"));
            effects_engine.set_reduced_resources (model.prefs.reduced_resources);
            model.notify["prefs"].connect (() => {
                effects_engine.set_reduced_resources (model.prefs.reduced_resources);
            });
            effects_engine.notify["animating"].connect (() => {
                server.set_animating (effects_engine.animating);
            });
            server.frame.connect ((output, usec) => effects_engine.tick (usec));
            if (!CommandLine.test_mode) {
                marco_prefs = new Settings.MarcoPrefsReader (model);
            }
            var store = new Outputs.Store ();
            if (config.kind == Backend.Kind.DRM) {
                store.load.begin (() => loop.quit ());
                loop.run ();
            }
            desktop = new Shell.Desktop (server, model, store,
                                         config.kind == Backend.Kind.DRM || config.manage_outputs,
                                         effects_engine,
                                         system_keyboard);
            foreign_toplevels = new Protocols.ForeignToplevels (server, model, desktop);
            desktop.switcher.set_minimize_target (foreign_toplevels.minimize_target);
            desktop.share_chooser.set_window_lookup (foreign_toplevels.window_for_identifier);
            server.set_global_filter (trust.filter);
            server.set_test_inspector (desktop.inspect);
            if (CommandLine.test_mode) {
                server.set_test_settings (test_setting);
            }
            server.shutdown.connect (() => quit (0));

            log.info ("Polo %s on libweston %s", Config.VERSION, Backend.libweston_version ());
            try {
                server.start (config);
            } catch (GLib.Error e) {
                log.warning ("cannot start: %s", e.message);
                teardown ();
                return 1;
            }
            server.attach (GLib.MainContext.default ());
            effects_engine.deformation_supported = server.supports_deformation ();
#if POLO_HAVE_XWAYLAND
            start_xwayland ();
#endif
            workspaces = new Protocols.Workspaces (server, model, write_preference);
            output_management = new Protocols.OutputManagement (server, desktop);
            foreign_toplevels.set_workspaces (workspaces);

            GLib.Unix.signal_add (GLib.ProcessSignal.INT, () => {
                quit (0);
                return GLib.Source.CONTINUE;
            });
            GLib.Unix.signal_add (GLib.ProcessSignal.TERM, () => {
                quit (0);
                return GLib.Source.CONTINUE;
            });

            if (CommandLine.session) {
                session_manager = new Session ();
                session_manager.own_session_manager ();
                unowned Dbus.Manager? manager = session_manager.manager;
                logind = new Logind ();
                logind.lock_requested.connect (() => {
                    if (session_lock != null) {
                        session_lock.lock_screen.begin ();
                    } else {
                        logind.lock_shown ();
                    }
                });
                logind.unlock_requested.connect (() => {
                    if (session_lock != null) {
                        session_lock.unlock_requested ();
                    }
                });
                logind.start.begin ();
                if (manager != null) {
                    manager.logind = logind;
                    manager.presence.notify["status"].connect (() => {
                        logind.set_idle_hint (manager.presence.status == Dbus.PresenceStatus.IDLE);
                    });
                    end_session = new EndSession (manager, server.get_shell_ui (), logind);
#if POLO_HAVE_XSMP
                    try {
                        xsmp = new XsmpSession (manager);
                    } catch (GLib.Error e) {
                        log.warning ("no XSMP for legacy X11 clients: %s", e.message);
                    }
#endif
                    end_session.finished.connect (() => quit (0));
                    end_session.saving.connect (() => {
                        if (window_states != null) {
                            window_states.save (model);
                        }
                    });
                    manager.initialization_error.connect (on_initialization_error);
                }
                SavedSession.record_session_start ();
                window_states = new WindowStates ();
                window_states.load ();
                model.restore_state = window_states.restore;
                try {
                    outputs_dbus = new Dbus.Outputs (GLib.Bus.get_sync (GLib.BusType.SESSION), desktop,
                                                     server.get_shell_ui ());
                    outputs_dbus.export ();
                    screen_share = new Dbus.ScreenShare (GLib.Bus.get_sync (GLib.BusType.SESSION),
                                                         desktop.share_chooser);
                    screen_share.export ();
                } catch (GLib.Error e) {
                    log.warning ("cannot provide %s: %s", Dbus.Outputs.NAME, e.message);
                    outputs_dbus = null;
                    screen_share = null;
                }
                session_env = new SessionEnv ();
                session_env.publish (server.get_socket_name ());
            }

            spawn = new Spawn (server, desktop.activation_tokens);
            desktop.set_command_launcher ((command) => {
                spawn.launch_command (command);
            });
            desktop.set_error_dialog (show_error);
            desktop.decorations.attach (server.get_shell_ui ());
            not_responding = new Shell.NotResponding (model, spawn, desktop, server.get_shell_ui ());
            rename_workspace = new Shell.RenameWorkspace (model, desktop, server.get_shell_ui (), write_preference);
            Input.ShortcutDirectory shortcut_directory;
            if (CommandLine.test_mode) {
                test_shortcuts = new Input.TestShortcutDirectory ();
                shortcut_directory = test_shortcuts;
            } else {
                shortcut_directory = new Input.DconfShortcutDirectory ();
            }
            custom_shortcuts = new Input.CustomShortcuts (desktop.bindings, shortcut_directory, (command) => {
                spawn.launch_command (command);
            }, show_error);
            shell_ui = new Shell.ShellUi (server, spawn, trust);
            shell_ui.start ();
            if (screen_share != null && !CommandLine.test_mode) {
                Dbus.ScreenShare.start_portal (spawn);
            }
            if (CommandLine.exec != null) {
                foreach (unowned string command in CommandLine.exec) {
                    try {
                        spawn.launch_command (command, CommandLine.session ? SpawnFlags.TRUSTED
                                                                           : SpawnFlags.NONE);
                    } catch (GLib.Error e) {
                        log.warning ("cannot run \"%s\": %s", command, e.message);
                    }
                }
            }

            if (CommandLine.session) {
                if (!CommandLine.test_mode || GLib.Environment.get_variable ("POLO_TEST_AUTOSTART") == "1") {
                    phases = new SessionPhases (spawn, desktop, server.get_shell_ui (), Autostart.session_apps ());
                    phases.running.connect (SessionEnv.register_with_display_manager);
                    unowned Dbus.Manager? manager = session_manager.manager;
                    if (manager != null) {
                        phases.phase_started.connect ((phase) => {
                            manager.set_phase (Dbus.ManagerPhase.from_autostart (phase));
                        });
                        phases.running.connect (() => manager.set_phase (Dbus.ManagerPhase.RUNNING));
                        manager.match_app = phases.register_client;
                        manager.handles_condition = phases.handles_condition;
                        manager.add_apps (phases.get_apps ());
                    }
                    phases.run.begin ();
                } else {
                    SessionEnv.register_with_display_manager ();
                    if (session_manager.manager != null) {
                        session_manager.manager.set_phase (Dbus.ManagerPhase.RUNNING);
                    }
                }
            }

            unowned Dbus.Manager? sm = session_manager != null ? session_manager.manager : null;
            idle_tracker = new IdleTracker (server, sm != null ? sm.presence : null, logind);
            session_lock = new Protocols.SessionLock (server, spawn, sm != null ? sm.presence : null);
            session_lock.shown.connect (() => {
                if (logind != null) {
                    logind.lock_shown ();
                }
            });
            media_key_actions = new Shell.MediaKeyActions (server, model, desktop, spawn, logind, () => {
                if (session_lock != null) {
                    session_lock.lock_screen.begin ();
                }
            }, show_error);
            media_keys = new Input.MediaKeys (media_key_actions.media_keys_settings (), media_key_actions);
            desktop.set_media_keys (media_keys);
            access_x = new Input.AccessX (server, settings_if_installed (Input.AccessX.SCHEMA),
                                          () => desktop.ring_bell (), ask_access_x);
            desktop.set_access_x (access_x);
            loop.run ();
            log.info ("shutting down");
            desktop.set_access_x (null);
            access_x.shutdown ();
            access_x = null;
            desktop.set_media_keys (null);
            media_keys.shutdown ();
            media_keys = null;
            media_key_actions.shutdown ();
            media_key_actions = null;
            session_lock.shutdown ();
            session_lock = null;
            if (window_states != null) {
                model.restore_state = null;
                window_states = null;
            }
            idle_tracker.shutdown ();
            idle_tracker = null;
            if (phases != null) {
                phases.stop ();
            }
            if (end_session != null) {
                end_session.shutdown ();
                end_session = null;
            }
            if (screen_share != null) {
                screen_share.unexport ();
                screen_share = null;
            }
            if (outputs_dbus != null) {
                outputs_dbus.unexport ();
                outputs_dbus = null;
            }
#if POLO_HAVE_XSMP
            if (xsmp != null) {
                xsmp.shutdown ();
                xsmp = null;
            }
#endif
            if (logind != null) {
                if (session_manager != null && session_manager.manager != null) {
                    session_manager.manager.logind = null;
                }
                logind.shutdown ();
                logind = null;
            }
            if (CommandLine.session) {
                if (shell_ui != null) {
                    shell_ui.shutdown ();
                    shell_ui = null;
                }
                spawn.end_children (loop, CHILD_TERM_SECONDS);
                session_manager.release_session_manager ();
            }
            if (session_env != null) {
                session_env.withdraw ();
            }
            teardown ();
            return exit_status;
        }

        /*
         * Test mode only, and only with the memory GSettings backend, so a
         * test never writes real settings: POLO_TEST_SETTINGS="schema/key=value;..."
         * before start, polo_test_v1.set_setting while running.
         */
        private static bool memory_settings () {
            return GLib.Environment.get_variable ("GSETTINGS_BACKEND") == "memory";
        }

        private void apply_test_settings () {
            var spec = GLib.Environment.get_variable ("POLO_TEST_SETTINGS");
            if (spec == null || spec == "") {
                return;
            }
            if (!memory_settings ()) {
                GLib.error ("POLO_TEST_SETTINGS needs GSETTINGS_BACKEND=memory");
            }
            foreach (unowned string item in spec.split (";")) {
                var parts = item.split ("=", 2);
                var path = parts[0].split ("/", 2);
                if (parts.length != 2 || path.length != 2) {
                    continue;
                }
                if (test_setting (path[0], path[1], parts[1]) == null) {
                    GLib.error ("POLO_TEST_SETTINGS: cannot set %s", item);
                }
            }
        }

        private string? test_setting (string schema_id, string key, string? value) {
            var parts = schema_id.split (":", 2);
            string? path = parts.length == 2 ? parts[1] : null;
            var source = GLib.SettingsSchemaSource.get_default ();
            var schema = source != null ? source.lookup (parts[0], true) : null;
            if (schema == null || !schema.has_key (key) || (path == null) != (schema.get_path () != null)) {
                return null;
            }
            var settings = new GLib.Settings.full (schema, null, path);
            if (value != null) {
                if (!memory_settings ()) {
                    return null;
                }
                try {
                    var parsed = GLib.Variant.parse (schema.get_key (key).get_value_type (), value);
                    if (!settings.set_value (key, parsed)) {
                        return null;
                    }
                    apply_test_override (schema_id, key, parsed);
                    if (path != null && parts[0] == Input.CustomShortcuts.SCHEMA && test_shortcuts != null &&
                        path.has_prefix (Input.CustomShortcuts.DIR)) {
                        test_shortcuts.add (path);
                    }
                } catch (GLib.VariantParseError e) {
                    return null;
                }
            }
            return settings.get_value (key).print (false);
        }

        /* Test mode has no prefs reader: what a test writes to Marco's keys reaches the model here. */
        private void apply_test_override (string schema_id, string key, GLib.Variant value) {
            if (!schema_id.has_prefix ("org.mate.Marco.")) {
                return;
            }
            test_overrides.insert ("%s/%s".printf (schema_id.substring ("org.mate.Marco.".length), key), value);
            if (model == null) {
                return;
            }
            try {
                model.prefs = MarcoPrefs.with_overrides (test_overrides);
            } catch (PrefsError e) {
                GLib.warning ("%s", e.message);
            }
        }

        private void write_preference (string schema_id, string key, GLib.Variant value) {
            if (CommandLine.test_mode) {
                test_setting (schema_id, key, value.print (false));
                return;
            }
            var source = GLib.SettingsSchemaSource.get_default ();
            var schema = source != null ? source.lookup (schema_id, true) : null;
            if (schema != null && schema.has_key (key)) {
                new GLib.Settings.full (schema, null, null).set_value (key, value);
            }
        }

        /* Test mode: Marco's defaults, with POLO_TEST_PREFS="general/focus-mode='sloppy';..." on top. */
        private MarcoPrefs test_prefs () {
            var defaults = MarcoPrefs.defaults ();
            var spec = GLib.Environment.get_variable ("POLO_TEST_PREFS");
            if (spec == null || spec == "") {
                return defaults;
            }
            unowned GLib.HashTable<string, GLib.Variant> overrides = test_overrides;
            foreach (unowned string item in spec.split (";")) {
                var parts = item.split ("=", 2);
                if (parts.length != 2) {
                    continue;
                }
                var current = defaults.get_value (parts[0]);
                if (current == null) {
                    GLib.error ("POLO_TEST_PREFS: no Marco key %s", parts[0]);
                }
                try {
                    overrides[parts[0]] = GLib.Variant.parse (current.get_type (), parts[1]);
                } catch (GLib.VariantParseError e) {
                    GLib.error ("POLO_TEST_PREFS: %s: %s", parts[0], e.message);
                }
            }
            try {
                return MarcoPrefs.with_overrides (overrides);
            } catch (PrefsError e) {
                GLib.error ("POLO_TEST_PREFS: %s", e.message);
            }
        }

        private void quit (int status) {
            exit_status = status;
            loop.quit ();
        }

        private void ask_access_x (string title, string question, string message, string accept, string reject,
                                   owned Input.AccessX.Reply reply) {
            if (CommandLine.test_mode || spawn == null) {
                reply (true);
                return;
            }
            string[] argv = {
                "zenity", "--question", "--title", title,
                "--text", "<b>%s</b>\n\n%s".printf (GLib.Markup.escape_text (question),
                                                     GLib.Markup.escape_text (message)),
                "--ok-label", accept.replace ("_", ""), "--cancel-label", reject.replace ("_", "")
            };
            try {
                var process = spawn.launch (argv);
                process.wait_async.begin (null, (obj, res) => {
                    try {
                        process.wait_async.end (res);
                    } catch (GLib.Error e) {
                        reply (true);
                        return;
                    }
                    reply (process.get_if_exited () && process.get_exit_status () == 0);
                });
            } catch (GLib.Error e) {
                Logging.scope (LogDomain.POLO).warning ("cannot ask \"%s\": %s", question, e.message);
                reply (true);
            }
        }

        private void show_error (string message) {
            if (spawn == null) {
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

        private static GLib.Settings? settings_if_installed (string id) {
            var source = GLib.SettingsSchemaSource.get_default ();
            var schema = source != null ? source.lookup (id, true) : null;
            return schema != null ? new GLib.Settings.full (schema, null, null) : null;
        }

        private void on_initialization_error (string message, bool fatal) {
            unowned LogScope log = Logging.scope (LogDomain.POLO);
            log.warning ("initialization error%s: %s", fatal ? " (fatal)" : "", message);
            string[] argv = { "zenity", "--error", "--title", Shell.NotResponding.marco ("Marco"),
                              "--text", message };
            try {
                var dialog = spawn.launch (argv);
                if (fatal) {
                    dialog.wait_async.begin (null, () => quit (1));
                }
            } catch (GLib.Error e) {
                log.warning ("cannot show \"%s\": %s", message, e.message);
                if (fatal) {
                    quit (1);
                }
            }
        }

        /* Reverse order of construction. */
        private void teardown () {
            if (custom_shortcuts != null) {
                custom_shortcuts.shutdown ();
                custom_shortcuts = null;
            }
            test_shortcuts = null;
            if (effects_engine != null) {
                effects_engine.shutdown ();
                effects_engine = null;
            }
            if (rename_workspace != null) {
                rename_workspace.shutdown ();
                rename_workspace = null;
            }
            if (shell_ui != null) {
                shell_ui.shutdown ();
                shell_ui = null;
            }
            if (not_responding != null) {
                not_responding.shutdown ();
                not_responding = null;
            }
            if (desktop != null) {
                desktop.set_command_launcher (null);
                desktop.set_error_dialog (null);
            }
            spawn = null;
            if (server != null) {
                server.set_test_inspector (null);
                server.set_test_settings (null);
                server.set_global_filter (null);
            }
            if (desktop != null) {
                desktop.switcher.set_minimize_target (null);
                desktop.share_chooser.set_window_lookup (null);
            }
            if (foreign_toplevels != null) {
                foreign_toplevels.set_workspaces (null);
            }
            if (workspaces != null) {
                workspaces.shutdown ();
                if (output_management != null) {
                    output_management.shutdown ();
                    output_management = null;
                }
                workspaces = null;
            }
            if (foreign_toplevels != null) {
                foreign_toplevels.shutdown ();
            }
            if (desktop != null) {
                desktop.shutdown ();
            }
            if (marco_prefs != null) {
                marco_prefs.shutdown ();
            }
            if (server != null) {
                server.stop ();
            }
            marco_prefs = null;
            foreign_toplevels = null;
            desktop = null;
            model = null;
            server = null;
            Marco.release_fonts ();
        }

        private Backend.Config make_config () throws GLib.OptionError {
            var config = Backend.Config ();

            string backend = CommandLine.backend ??
                (GLib.Environment.get_variable ("WAYLAND_DISPLAY") != null ? "wayland" : "drm");
            switch (backend) {
                case "drm": config.kind = Backend.Kind.DRM; break;
                case "wayland": config.kind = Backend.Kind.WAYLAND; break;
                case "headless": config.kind = Backend.Kind.HEADLESS; break;
                default:
                    throw new GLib.OptionError.BAD_VALUE ("unknown backend \"%s\"", backend);
            }

            switch (CommandLine.renderer ?? "auto") {
                case "auto": config.renderer = Backend.Renderer.AUTO; break;
                case "gl": config.renderer = Backend.Renderer.GL; break;
                case "pixman": config.renderer = Backend.Renderer.PIXMAN; break;
                default:
                    throw new GLib.OptionError.BAD_VALUE ("unknown renderer \"%s\"",
                                                          CommandLine.renderer);
            }

            if (CommandLine.test_mode && !Backend.Server.has_test_protocol ()) {
                throw new GLib.OptionError.BAD_VALUE (
                    "--test-mode needs a build with -Dtest-protocol=true");
            }

            config.width = CommandLine.width;
            config.height = CommandLine.height;
            config.scale = CommandLine.scale;
            config.output_count = CommandLine.outputs;
            config.scales = CommandLine.scales;
            config.fullscreen = CommandLine.fullscreen;
            config.socket_name = CommandLine.socket;
            config.seat = GLib.Environment.get_variable ("XDG_SEAT");
            system_keyboard = Input.Keyboard.system_config (!CommandLine.test_mode);
            config.xkb_model = system_keyboard.model;
            xkb_layout = system_keyboard.layout_names ();
            xkb_variant = system_keyboard.variant_names ();
            xkb_options = system_keyboard.option_names ();
            config.xkb_layout = xkb_layout;
            config.xkb_variant = xkb_variant;
            config.xkb_options = xkb_options;
            config.test_mode = CommandLine.test_mode;
            config.debug_kill = CommandLine.debug_kill;
            /* Test mode only: run DRM's output start-up path headless. */
            config.manage_outputs = CommandLine.test_mode &&
                GLib.Environment.get_variable ("POLO_TEST_MANAGED_OUTPUTS") == "1";
            var source = GLib.SettingsSchemaSource.get_default ();
            var schema = source != null ? source.lookup ("org.mate.peripherals-mouse", true) : null;
            int cursor_size = 24;
            if (schema != null) {
                var mouse = new GLib.Settings.full (schema, null, null);
                var theme = mouse.get_string ("cursor-theme");
                if (theme != "") {
                    cursor_theme = theme;
                }
                cursor_size = mouse.get_int ("cursor-size").clamp (8, 128);
            }
            config.cursor_theme = cursor_theme;
            config.cursor_size = cursor_size;
            GLib.Environment.set_variable ("XCURSOR_THEME", cursor_theme, true);
            GLib.Environment.set_variable ("XCURSOR_SIZE", cursor_size.to_string (), true);
            return config;
        }

        private void print_version () {
            stdout.printf ("polo %s\n", Config.VERSION);
            stdout.printf ("libweston %s (private)\n", Backend.libweston_version ());
            stdout.printf ("Marco reference %s\n", Marco.reference_version ());
            stdout.printf ("effects from %s\n", Fx.source ());
#if POLO_HAVE_XWAYLAND
            stdout.printf ("XWM from %s\n", Xwm.vendored_from ());
#endif
        }

#if POLO_HAVE_XWAYLAND
        private void write_num_workspaces (int count) {
            write_preference ("org.mate.Marco.general", "num-workspaces", new GLib.Variant.int32 (count));
        }

        /* POLO_XWAYLAND chooses like --xwayland; tests get none unless they name it. */
        private void start_xwayland () {
            string mode = GLib.Environment.get_variable ("POLO_XWAYLAND") ?? CommandLine.xwayland ??
                          (CommandLine.session ? "eager" : CommandLine.test_mode ? "off" : "lazy");
            switch (mode) {
                case "off":
                    break;
                case "lazy":
                    desktop.start_xwayland (false);
                    desktop.set_num_workspaces_writer (write_num_workspaces);
                    break;
                case "eager":
                    desktop.start_xwayland (true);
                    desktop.set_num_workspaces_writer (write_num_workspaces);
                    break;
                default:
                    Logging.scope (LogDomain.POLO).warning ("unknown XWayland mode %s; it stays off", mode);
                    break;
            }
        }
#endif

        /* Used by tests/logging to check scope filtering. */
        private void log_self_test () {
            foreach (var domain in LogDomain.all ()) {
                Logging.scope (domain).debug ("self-test debug");
            }
            Logging.scope (LogDomain.POLO).info ("self-test info");
            GLib.debug ("self-test glib debug");
            GLib.message ("self-test glib message");
        }
    }
}
