/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

namespace Polo {
    public enum AutostartPhase {
        INITIALIZATION,
        WINDOW_MANAGER,
        PANEL,
        DESKTOP,
        APPLICATION;

        public static AutostartPhase parse (string? name) {
            switch (name) {
                case "Initialization": return INITIALIZATION;
                case "WindowManager": return WINDOW_MANAGER;
                case "Panel": return PANEL;
                case "Desktop": return DESKTOP;
                default: return APPLICATION;
            }
        }

        public string to_name () {
            switch (this) {
                case INITIALIZATION: return "Initialization";
                case WINDOW_MANAGER: return "WindowManager";
                case PANEL: return "Panel";
                case DESKTOP: return "Desktop";
                default: return "Application";
            }
        }
    }

    /* gsm-autostart-app.c and libegg's eggdesktopfile.c */
    public class AutostartApp : GLib.Object {
        private const string GROUP = "Desktop Entry";
        private const string DESKTOP = "MATE";

        private enum Condition { IF_EXISTS, UNLESS_EXISTS, GSETTINGS, UNKNOWN }

        public string id { get; private set; }
        public string path { get; private set; }
        public AutostartPhase phase { get; private set; default = AutostartPhase.APPLICATION; }
        public bool autorestart { get; private set; default = false; }
        public int delay { get; private set; default = -1; }
        public string? condition_string { get; private set; default = null; }
        /* DESKTOP_AUTOSTART_ID of its latest launch, for RegisterClient. */
        public string startup_id { get; set; default = ""; }
        public string? command { get; private set; default = null; }
        public string? component { get; set; default = null; }
        public bool required { get { return component != null; } }

        public signal void condition_changed (bool condition);

        private GLib.KeyFile file;
        private string type;
        private bool condition = false;
        private GLib.FileMonitor? condition_monitor = null;
        private GLib.Settings? condition_settings = null;

        private AutostartApp () {
        }

        public static AutostartApp? load (string path) {
            var app = new AutostartApp ();
            app.path = path;
            app.id = GLib.Path.get_basename (path);
            try {
                app.file = new GLib.KeyFile ();
                app.file.load_from_file (path, GLib.KeyFileFlags.NONE);
                app.validate ();
            } catch (GLib.Error e) {
                GLib.warning ("Could not parse desktop file %s: %s", path, e.message);
                return null;
            }
            app.read_keys ();
            return app;
        }

        private void validate () throws GLib.Error {
            if (!file.has_group (GROUP)) {
                throw new GLib.KeyFileError.GROUP_NOT_FOUND ("File is not a valid .desktop file");
            }
            var version = raw_string ("Version");
            if (version != null) {
                double number;
                if (double.try_parse (version, out number) && number > 1.0) {
                    throw new GLib.KeyFileError.INVALID_VALUE ("Unrecognized desktop file Version");
                }
            }
            file.get_string (GROUP, "Name");
            type = file.get_string (GROUP, "Type");
            if (type == "Application") {
                command = file.get_string (GROUP, "Exec");
            } else if (type == "Link") {
                file.get_string (GROUP, "URL");
            }
        }

        private void read_keys () {
            var phase_name = optional_string ("X-MATE-Autostart-Phase");
            phase = AutostartPhase.parse (phase_name);
            autorestart = optional_boolean ("X-MATE-AutoRestart");
            condition_string = optional_string ("AutostartCondition");
            monitor_condition ();
            if (phase == AutostartPhase.APPLICATION) {
                try {
                    delay = file.has_key (GROUP, "X-MATE-Autostart-Delay")
                        ? file.get_integer (GROUP, "X-MATE-Autostart-Delay") : 0;
                } catch (GLib.Error e) {
                    delay = 0;
                }
                if (delay < 0) {
                    GLib.warning ("Invalid autostart delay of %d for %s", delay, id);
                    delay = -1;
                }
            }
        }

        public string? working_directory {
            owned get {
                var dir = optional_string ("Path");
                return dir != "" ? dir : null;
            }
        }

        /* Expand Exec without document arguments. */
        public string[] argv () throws GLib.ShellError {
            if (command == null) {
                return {};
            }
            var expanded = new GLib.StringBuilder ();
            for (int i = 0; i < command.length; i++) {
                if (command[i] != '%' || i + 1 == command.length) {
                    expanded.append_c (command[i]);
                    continue;
                }
                i++;
                switch (command[i]) {
                    case '%':
                        expanded.append_c ('%');
                        break;
                    case 'c':
                        expanded.append (GLib.Shell.quote (optional_string ("Name") ?? ""));
                        break;
                    case 'k':
                        expanded.append (GLib.Shell.quote (path));
                        break;
                    case 'i':
                        var icon = optional_string ("Icon");
                        if (icon != null) {
                            expanded.append ("--icon ").append (GLib.Shell.quote (icon));
                        }
                        break;
                    default:
                        break;
                }
            }
            string[] arguments;
            GLib.Shell.parse_argv (expanded.str, out arguments);
            return arguments;
        }

        public string describe () {
            switch (component) {
                case "panel": return "panel";
                case "filemanager": return "file manager";
                case "dock": return "dock";
                default: return optional_string ("Name") ?? id;
            }
        }

        public bool provides (string service) {
            string[]? services = optional_list ("X-MATE-Provides");
            return services != null && service in services;
        }

        public bool is_disabled () {
            return optional_boolean ("Hidden") || !can_launch ();
        }

        private bool can_launch () {
            if (type != "Application" && type != "Link") {
                return false;
            }
            string[]? only_show_in = optional_list ("OnlyShowIn");
            if (only_show_in != null && !(DESKTOP in only_show_in)) {
                return false;
            }
            string[]? not_show_in = optional_list ("NotShowIn");
            if (not_show_in != null && DESKTOP in not_show_in) {
                return false;
            }
            if (type == "Application") {
                var try_exec = optional_string ("TryExec");
                if (try_exec != null && GLib.Environment.find_program_in_path (try_exec) == null) {
                    return false;
                }
            }
            return true;
        }

        public bool is_conditionally_disabled () {
            if (condition_string == null) {
                return false;
            }
            string key;
            var kind = parse_condition (condition_string, out key);
            bool disabled;
            switch (kind) {
                case Condition.IF_EXISTS:
                    disabled = !GLib.FileUtils.test (condition_path (key), GLib.FileTest.EXISTS);
                    break;
                case Condition.UNLESS_EXISTS:
                    disabled = GLib.FileUtils.test (condition_path (key), GLib.FileTest.EXISTS);
                    break;
                case Condition.GSETTINGS:
                    disabled = condition_settings == null ||
                        !condition_settings.get_boolean (key.split (" ", 2)[1]);
                    break;
                default:
                    return true;
            }
            condition = !disabled;
            return disabled;
        }

        private static Condition parse_condition (string text, out string key) {
            var length = text.index_of_char (' ');
            if (length < 0) {
                length = text.length;
            }
            key = text.substring (length).chug ();
            unowned string given = text;
            if (given.ascii_ncasecmp ("if-exists", length) == 0) {
                return Condition.IF_EXISTS;
            }
            if (given.ascii_ncasecmp ("unless-exists", length) == 0) {
                return Condition.UNLESS_EXISTS;
            }
            if (given.ascii_ncasecmp ("MATE", length) == 0 || given.ascii_ncasecmp ("GSettings", length) == 0) {
                return Condition.GSETTINGS;
            }
            key = "";
            return Condition.UNKNOWN;
        }

        private static string condition_path (string key) {
            return GLib.Path.build_filename (GLib.Environment.get_user_config_dir (), key);
        }

        private void monitor_condition () {
            if (condition_string == null || is_disabled ()) {
                return;
            }
            string key;
            var kind = parse_condition (condition_string, out key);
            switch (kind) {
                case Condition.IF_EXISTS:
                case Condition.UNLESS_EXISTS:
                    monitor_file (condition_path (key), kind == Condition.IF_EXISTS);
                    break;
                case Condition.GSETTINGS:
                    monitor_setting (key);
                    break;
                default:
                    break;
            }
        }

        private void monitor_file (string file_path, bool created_means_true) {
            try {
                condition_monitor = GLib.File.new_for_path (file_path).monitor_file (GLib.FileMonitorFlags.NONE);
                condition_monitor.changed.connect ((file, other, event) => {
                    if (event == GLib.FileMonitorEvent.CREATED) {
                        update_condition (created_means_true);
                    } else if (event == GLib.FileMonitorEvent.DELETED) {
                        update_condition (!created_means_true);
                    }
                });
            } catch (GLib.Error e) {
                GLib.warning ("Could not watch %s: %s", file_path, e.message);
            }
        }

        private void monitor_setting (string key) {
            var parts = key.split (" ", 2);
            if (parts.length < 2) {
                return;
            }
            var source = GLib.SettingsSchemaSource.get_default ();
            var schema = source != null ? source.lookup (parts[0], true) : null;
            if (schema == null || !schema.has_key (parts[1]) ||
                !schema.get_key (parts[1]).get_value_type ().equal (GLib.VariantType.BOOLEAN)) {
                return;
            }
            condition_settings = new GLib.Settings.full (schema, null, null);
            condition_settings.changed[parts[1]].connect ((settings, name) => {
                update_condition (settings.get_boolean (name));
            });
            condition_settings.get_boolean (parts[1]);
        }

        private void update_condition (bool value) {
            if (value != condition) {
                condition = value;
                condition_changed (value);
            }
        }

        private string? raw_string (string key) {
            try {
                return file.get_value (GROUP, key);
            } catch (GLib.Error e) {
                return null;
            }
        }

        private string? optional_string (string key) {
            try {
                return file.get_string (GROUP, key);
            } catch (GLib.Error e) {
                return null;
            }
        }

        private bool optional_boolean (string key) {
            try {
                return file.get_boolean (GROUP, key);
            } catch (GLib.Error e) {
                return false;
            }
        }

        private string[]? optional_list (string key) {
            try {
                return file.has_key (GROUP, key) ? file.get_string_list (GROUP, key) : null;
            } catch (GLib.Error e) {
                return null;
            }
        }
    }

    public class RestartPolicy {
        public const int LIMIT = 5;
        public const int64 WINDOW_USEC = 60 * GLib.TimeSpan.SECOND;
        private const uint FIRST_DELAY_MS = 250;

        private int64[] crashes = {};

        /* Milliseconds to wait before the next start, or -1 to stop restarting. */
        public int next_delay (int64 now) {
            int64[] recent = {};
            foreach (var crash in crashes) {
                if (now - crash < WINDOW_USEC) {
                    recent += crash;
                }
            }
            recent += now;
            crashes = recent;
            if (recent.length >= LIMIT) {
                return -1;
            }
            return (int) (FIRST_DELAY_MS << (recent.length - 1));
        }
    }

    namespace Autostart {
        private const string SESSION_SCHEMA = "org.mate.session";
        private const string REQUIRED_SCHEMA = "org.mate.session.required-components";
        private const string WINDOW_MANAGER = "windowmanager";

        /* User and system autostart directories. */
        public string[] directories () {
            string[] dirs = { GLib.Path.build_filename (GLib.Environment.get_user_config_dir (), "autostart") };
            foreach (unowned string dir in GLib.Environment.get_system_data_dirs ()) {
                dirs += GLib.Path.build_filename (dir, "mate", "autostart");
            }
            foreach (unowned string dir in GLib.Environment.get_system_config_dirs ()) {
                dirs += GLib.Path.build_filename (dir, "autostart");
            }
            return dirs;
        }

        /* User and system application directories. */
        private string[] application_directories () {
            string[] dirs = { GLib.Path.build_filename (GLib.Environment.get_user_data_dir (), "applications") };
            foreach (unowned string dir in GLib.Environment.get_system_data_dirs ()) {
                dirs += GLib.Path.build_filename (dir, "applications");
            }
            return dirs;
        }

        public GLib.List<AutostartApp> scan (string[] dirs) {
            var apps = new GLib.List<AutostartApp> ();
            foreach (unowned string dir in dirs) {
                GLib.Dir handle;
                try {
                    handle = GLib.Dir.open (dir);
                } catch (GLib.Error e) {
                    continue;
                }
                unowned string? name;
                while ((name = handle.read_name ()) != null) {
                    if (name.has_suffix (".desktop") && find_by_id (apps, name) == null) {
                        var app = AutostartApp.load (GLib.Path.build_filename (dir, name));
                        if (app != null) {
                            apps.append (app);
                        }
                    }
                }
            }
            return apps;
        }

        /* Load standard apps without saved-session entries; Polo is the window manager. */
        public GLib.List<AutostartApp> session_apps () {
            var apps = scan (directories ());
            var source = GLib.SettingsSchemaSource.get_default ();
            var session = source != null ? source.lookup (SESSION_SCHEMA, true) : null;
            var required = source != null ? source.lookup (REQUIRED_SCHEMA, true) : null;
            if (session != null) {
                var settings = new GLib.Settings.full (session, null, null);
                foreach (unowned string name in settings.get_strv ("default-session")) {
                    if (name != "") {
                        add_named (ref apps, name);
                    }
                }
                if (required != null) {
                    add_required (ref apps, settings.get_strv ("required-components-list"),
                                  new GLib.Settings.full (required, null, null));
                }
            }
            add_assistive (ref apps, "org.mate.applications-at-mobility");
            add_assistive (ref apps, "org.mate.applications-at-visual");
            var others = new GLib.List<AutostartApp> ();
            foreach (var app in apps) {
                if (!app.provides (WINDOW_MANAGER)) {
                    others.append (app);
                }
            }
            return others;
        }

        private void add_required (ref GLib.List<AutostartApp> apps, string[] components, GLib.Settings settings) {
            foreach (unowned string component in components) {
                if (component == "" || settings.settings_schema.has_key (component) == false) {
                    continue;
                }
                var provider = settings.get_string (component);
                if (component == WINDOW_MANAGER) {
                    if (provider != "marco") {
                        GLib.message ("Polo is the window manager: ignoring %s", provider);
                    }
                    continue;
                }
                if (provider == "") {
                    continue;
                }
                AutostartApp? app = null;
                foreach (var candidate in apps) {
                    if (candidate.provides (component)) {
                        app = candidate;
                        break;
                    }
                }
                if (app == null) {
                    app = add_named (ref apps, provider);
                }
                if (app != null) {
                    app.component = component;
                } else {
                    GLib.warning ("Unable to find provider '%s' of required component '%s'", provider, component);
                }
            }
        }

        private void add_assistive (ref GLib.List<AutostartApp> apps, string schema_id) {
            var source = GLib.SettingsSchemaSource.get_default ();
            var schema = source != null ? source.lookup (schema_id, true) : null;
            if (schema == null) {
                return;
            }
            var settings = new GLib.Settings.full (schema, null, null);
            var name = settings.get_string ("exec");
            if (settings.get_boolean ("startup") && name != "") {
                add_named (ref apps, name);
            }
        }

        private AutostartApp? add_named (ref GLib.List<AutostartApp> apps, string name) {
            var file_name = name + ".desktop";
            var existing = find_by_id (apps, file_name);
            if (existing != null) {
                return existing;
            }
            foreach (unowned string dir in application_directories ()) {
                var path = GLib.Path.build_filename (dir, file_name);
                if (GLib.FileUtils.test (path, GLib.FileTest.EXISTS)) {
                    var app = AutostartApp.load (path);
                    if (app != null) {
                        apps.append (app);
                    }
                    return app;
                }
            }
            return null;
        }

        private AutostartApp? find_by_id (GLib.List<AutostartApp> apps, string id) {
            foreach (var app in apps) {
                if (app.id == id) {
                    return app;
                }
            }
            return null;
        }
    }
}
