/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * mate-session 1.28's gsm-session-save.c and the saved-session parts of its
 * main.c, plus Polo's opt-in window state (org.mate.polo.session
 * restore-window-state), matched by app id and title as Marco's session.c
 * matches by SM_CLIENT_ID.
 */

namespace Polo {
    namespace SavedSession {
        private const string DISCARD_KEY = Autostart.DISCARD_KEY;
        private const string SESSION_SCHEMA = "org.mate.session";

        public string directory () {
            return Autostart.saved_session_directory ();
        }

        private string new_directory () {
            return GLib.Path.build_filename (GLib.Environment.get_user_config_dir (), "mate-session",
                                             "saved-session.new");
        }

        private GLib.Settings? session_settings () {
            var source = GLib.SettingsSchemaSource.get_default ();
            var schema = source != null ? source.lookup (SESSION_SCHEMA, true) : null;
            return schema != null ? new GLib.Settings.full (schema, null, null) : null;
        }

        private bool session_flag (string key) {
            var settings = session_settings ();
            return settings != null && settings.settings_schema.has_key (key) && settings.get_boolean (key);
        }

        public bool auto_save_enabled () {
            return session_flag ("auto-save-session");
        }

        public bool is_login_window () {
            return GLib.Environment.get_variable ("XDG_SESSION_CLASS") == "greeter";
        }

        public void record_session_start () {
            var settings = session_settings ();
            if (settings != null && settings.settings_schema.has_key ("session-start")) {
                settings.set_int ("session-start", (int) (GLib.get_real_time () / GLib.TimeSpan.SECOND));
                GLib.Settings.sync ();
            }
        }

        private bool ensure_directory (string path) {
            if (GLib.DirUtils.create_with_parents (path, 0755) == 0) {
                return true;
            }
            Logging.scope (LogDomain.POLO).warning ("could not create directory for saved session: %s", path);
            return false;
        }

        private void empty_directory (string path) {
            try {
                var dir = GLib.Dir.open (path);
                unowned string? name;
                while ((name = dir.read_name ()) != null) {
                    GLib.FileUtils.unlink (GLib.Path.build_filename (path, name));
                }
            } catch (GLib.FileError e) {
                return;
            }
        }

        /* runs each entry's discard command unless the new session kept it. */
        private void clear (string path, GLib.GenericSet<string>? kept) {
            GLib.Dir dir;
            try {
                dir = GLib.Dir.open (path);
            } catch (GLib.FileError e) {
                Logging.scope (LogDomain.POLO).warning ("error loading saved session directory: %s", e.message);
                return;
            }
            unowned string? name;
            while ((name = dir.read_name ()) != null) {
                var file = GLib.Path.build_filename (path, name);
                var keyfile = new GLib.KeyFile ();
                try {
                    keyfile.load_from_file (file, GLib.KeyFileFlags.NONE);
                    string discard = keyfile.get_string (GLib.KeyFileDesktop.GROUP, DISCARD_KEY);
                    if (kept == null || !kept.contains (discard)) {
                        string[] argv;
                        GLib.Shell.parse_argv (discard, out argv);
                        GLib.Process.spawn_async (null, argv, null, GLib.SpawnFlags.SEARCH_PATH, null, null);
                    }
                } catch (GLib.Error e) {
                }
                GLib.FileUtils.unlink (file);
            }
        }

        /* gsm_session_save (). */
        public void save (Dbus.Manager manager) {
            unowned LogScope log = Logging.scope (LogDomain.POLO);
            string saved = directory ();
            string fresh = new_directory ();
            if (!ensure_directory (saved) || !ensure_directory (fresh)) {
                return;
            }
            empty_directory (fresh);
            var kept = new GLib.GenericSet<string> (GLib.str_hash, GLib.str_equal);
            try {
                foreach (unowned Dbus.Client client in manager.get_clients ()) {
                    var keyfile = client.save ();
                    if (keyfile == null) {
                        continue;
                    }
                    GLib.FileUtils.set_contents (GLib.Path.build_filename (fresh, client.startup_id + ".desktop"),
                                                 keyfile.to_data ());
                    if (keyfile.has_key (GLib.KeyFileDesktop.GROUP, DISCARD_KEY)) {
                        kept.add (keyfile.get_string (GLib.KeyFileDesktop.GROUP, DISCARD_KEY));
                    }
                }
            } catch (GLib.Error e) {
                log.warning ("error saving session: %s", e.message);
                clear (fresh, null);
                GLib.DirUtils.remove (fresh);
                return;
            }
            clear (saved, kept);
            GLib.DirUtils.remove (saved);
            if (GLib.FileUtils.rename (fresh, saved) != 0) {
                log.warning ("cannot move %s to %s", fresh, saved);
            }
        }
    }

    public class WindowStates : GLib.Object {
        private const string POLO_SESSION_SCHEMA = "org.mate.polo.session";

        private class Saved {
            public string app_id;
            public string title;
            public int workspace;
            public bool sticky;
            public bool above;
            public bool minimized;
            public bool maximized_h;
            public bool maximized_v;
            public Marco.Rect rect;
        }

        private GLib.Settings? settings = null;
        private GLib.GenericArray<Saved> pending = new GLib.GenericArray<Saved> ();

        public WindowStates () {
            var source = GLib.SettingsSchemaSource.get_default ();
            var schema = source != null ? source.lookup (POLO_SESSION_SCHEMA, true) : null;
            if (schema != null && schema.has_key ("restore-window-state")) {
                settings = new GLib.Settings.full (schema, null, null);
            }
        }

        public static string path () {
            return GLib.Path.build_filename (GLib.Environment.get_user_config_dir (), "polo", "saved-windows");
        }

        public bool enabled () {
            return settings != null && settings.get_boolean ("restore-window-state");
        }

        private static bool saveable (Window window) {
            return window.app_id != null && window.app_id != "" && window.transient_for == null &&
                   window.window_type == Marco.WindowType.NORMAL;
        }

        public void save (Policy.Model model) {
            if (!enabled ()) {
                return;
            }
            var keyfile = new GLib.KeyFile ();
            int n = 0;
            foreach (unowned Window window in model.get_windows ()) {
                if (!saveable (window)) {
                    continue;
                }
                string group = "window%d".printf (n++);
                bool maximized = window.has_state (WindowState.MAXIMIZED_H) ||
                                 window.has_state (WindowState.MAXIMIZED_V) ||
                                 window.has_state (WindowState.FULLSCREEN);
                var rect = maximized ? window.saved_rect : window.client_rect ();
                keyfile.set_string (group, "app-id", window.app_id);
                keyfile.set_string (group, "title", window.title ?? "");
                keyfile.set_integer (group, "workspace", window.workspace);
                keyfile.set_boolean (group, "sticky", window.has_state (WindowState.STICKY));
                keyfile.set_boolean (group, "above", window.has_state (WindowState.ABOVE));
                keyfile.set_boolean (group, "minimized", window.has_state (WindowState.MINIMIZED));
                keyfile.set_boolean (group, "maximized-horizontally", window.has_state (WindowState.MAXIMIZED_H));
                keyfile.set_boolean (group, "maximized-vertically", window.has_state (WindowState.MAXIMIZED_V));
                keyfile.set_integer_list (group, "geometry", { rect.x, rect.y, rect.width, rect.height });
            }
            try {
                GLib.DirUtils.create_with_parents (GLib.Path.get_dirname (path ()), 0700);
                GLib.FileUtils.set_contents (path (), keyfile.to_data ());
            } catch (GLib.Error e) {
                Logging.scope (LogDomain.POLO).warning ("cannot save window state: %s", e.message);
            }
        }

        /* Once per login: what was saved, for the windows that map next. */
        public void load () {
            pending = new GLib.GenericArray<Saved> ();
            if (!enabled () || !GLib.FileUtils.test (path (), GLib.FileTest.EXISTS)) {
                return;
            }
            var keyfile = new GLib.KeyFile ();
            try {
                keyfile.load_from_file (path (), GLib.KeyFileFlags.NONE);
                foreach (unowned string group in keyfile.get_groups ()) {
                    var saved = new Saved ();
                    saved.app_id = keyfile.get_string (group, "app-id");
                    saved.title = keyfile.get_string (group, "title");
                    saved.workspace = keyfile.get_integer (group, "workspace");
                    saved.sticky = keyfile.get_boolean (group, "sticky");
                    saved.above = keyfile.get_boolean (group, "above");
                    saved.minimized = keyfile.get_boolean (group, "minimized");
                    saved.maximized_h = keyfile.get_boolean (group, "maximized-horizontally");
                    saved.maximized_v = keyfile.get_boolean (group, "maximized-vertically");
                    int[] geometry = keyfile.get_integer_list (group, "geometry");
                    if (geometry.length == 4) {
                        saved.rect = { geometry[0], geometry[1], geometry[2], geometry[3] };
                        pending.add (saved);
                    }
                }
            } catch (GLib.Error e) {
                Logging.scope (LogDomain.POLO).warning ("cannot read saved window state: %s", e.message);
            }
        }

        private Saved? take (Window window) {
            int by_app = -1;
            int matches = 0;
            for (int i = 0; i < pending.length; i++) {
                if (pending[i].app_id != window.app_id) {
                    continue;
                }
                if (pending[i].title == (window.title ?? "")) {
                    var saved = pending[i];
                    pending.remove_index (i);
                    return saved;
                }
                by_app = i;
                matches++;
            }
            if (matches != 1) {
                return null;
            }
            var saved = pending[by_app];
            pending.remove_index (by_app);
            return saved;
        }

        /* Policy.Model.restore_state: Marco's meta_window_apply_session_info (). */
        public void restore (Window window) {
            if (pending.length == 0 || !saveable (window)) {
                return;
            }
            var saved = take (window);
            if (saved == null) {
                return;
            }
            window.initial_workspace = saved.workspace;
            if (saved.sticky) {
                window.set_state_flags (WindowState.STICKY, true);
            }
            if (saved.above) {
                window.set_state_flags (WindowState.ABOVE, true);
            }
            window.minimize_after_placement = saved.minimized;
            window.maximize_horizontally_after_placement = saved.maximized_h;
            window.maximize_vertically_after_placement = saved.maximized_v;
            window.set_client_rect (saved.rect);
            window.placed = true;
        }
    }
}
