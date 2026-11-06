/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * mate-settings-daemon 1.28's keybindings plugin (msd-keybindings-manager.c):
 * the custom shortcuts mate-keybinding-properties keeps under
 * /org/mate/desktop/keybindings/, one relocatable
 * org.mate.control-center.keybinding (name, action, binding) per
 * subdirectory. Polo binds them as global bindings, so they fire whatever
 * has focus, and runs the action as the plugin does: its words, searched
 * in PATH, with the plugin's dialog when that fails.
 */

namespace Polo.Input {
    public interface ShortcutDirectory : GLib.Object {
        public abstract string[] list ();

        public signal void changed ();
    }

    public class DconfShortcutDirectory : GLib.Object, ShortcutDirectory {
        private DConf.Client client = new DConf.Client ();

        public DconfShortcutDirectory () {
            client.watch_fast (CustomShortcuts.DIR);
            client.changed.connect (on_changed);
        }

        ~DconfShortcutDirectory () {
            client.changed.disconnect (on_changed);
            client.unwatch_fast (CustomShortcuts.DIR);
        }

        private void on_changed (string prefix, string[] changes, string? tag) {
            changed ();
        }

        public string[] list () {
            string[] paths = {};
            foreach (unowned string child in client.list (CustomShortcuts.DIR)) {
                if (DConf.is_rel_dir (child)) {
                    paths += CustomShortcuts.DIR + child;
                }
            }
            return paths;
        }
    }

    public class TestShortcutDirectory : GLib.Object, ShortcutDirectory {
        private GLib.GenericArray<string> paths = new GLib.GenericArray<string> ();

        public void add (string path) {
            foreach (unowned string known in paths) {
                if (known == path) {
                    return;
                }
            }
            paths.add (path);
            changed ();
        }

        public string[] list () {
            return paths.data;
        }
    }

    public class CustomShortcuts : GLib.Object {
        public const string DIR = "/org/mate/desktop/keybindings/";
        public const string SCHEMA = "org.mate.control-center.keybinding";
        private const string PREFIX = "custom-shortcut:";

        private class Entry {
            public string path;
            public GLib.Settings settings;
            public string action;
            public string binding;
        }

        private Bindings bindings;
        private ShortcutDirectory directory;
        private CommandLauncher launcher;
        private ErrorDialog error_dialog;
        private GLib.SettingsSchema? schema;
        private GLib.GenericArray<Entry> entries = new GLib.GenericArray<Entry> ();
        private uint reload_idle = 0;

        [CCode (cname = "g_dgettext", cheader_filename = "glib.h")]
        private static extern unowned string dgettext (string domain, string msgid);

        public CustomShortcuts (Bindings bindings, ShortcutDirectory directory, owned CommandLauncher launcher,
                                owned ErrorDialog error_dialog) {
            this.bindings = bindings;
            this.directory = directory;
            this.launcher = (owned) launcher;
            this.error_dialog = (owned) error_dialog;
            var source = GLib.SettingsSchemaSource.get_default ();
            schema = source != null ? source.lookup (SCHEMA, true) : null;
            if (schema == null) {
                Logging.scope (LogDomain.INPUT).info ("no %s schema; no custom shortcuts", SCHEMA);
                return;
            }
            directory.changed.connect (schedule_reload);
            reload ();
        }

        public void shutdown () {
            directory.changed.disconnect (schedule_reload);
            if (reload_idle != 0) {
                GLib.Source.remove (reload_idle);
                reload_idle = 0;
            }
            clear ();
            bindings.set_custom (new GLib.GenericArray<Binding> ());
        }

        private void schedule_reload () {
            if (reload_idle == 0) {
                reload_idle = GLib.Idle.add (() => {
                    reload_idle = 0;
                    reload ();
                    return GLib.Source.REMOVE;
                });
            }
        }

        private void clear () {
            foreach (var entry in entries) {
                entry.settings.changed.disconnect (on_entry_changed);
                bindings.unregister (PREFIX + entry.path);
            }
            entries.remove_range (0, entries.length);
        }

        private void on_entry_changed (string key) {
            schedule_reload ();
        }

        private static bool disabled (string binding) {
            return binding == "" || binding == "disabled" || binding == "Disabled";
        }

        private void reload () {
            clear ();
            unowned LogScope log = Logging.scope (LogDomain.INPUT);
            var custom = new GLib.GenericArray<Binding> ();
            foreach (unowned string path in directory.list ()) {
                var entry = new Entry ();
                entry.path = path;
                entry.settings = new GLib.Settings.full (schema, null, path);
                entry.settings.changed.connect (on_entry_changed);
                entry.action = entry.settings.get_string ("action");
                entry.binding = entry.settings.get_string ("binding");
                entries.add (entry);
                if (disabled (entry.binding)) {
                    continue;
                }
                Accelerator accelerator;
                if (!Accelerators.parse (entry.binding, out accelerator) || accelerator.is_disabled ()) {
                    log.warning ("Key binding (%s) is invalid", path);
                    continue;
                }
                var name = PREFIX + path;
                custom.add (new Binding () { name = name, kind = BindingKind.GLOBAL, accelerator = accelerator });
                unowned Entry bound = entry;
                bindings.register (name, (binding, seat, event) => run (bound));
            }
            bindings.set_custom (custom);
            log.debug ("%u custom shortcuts", custom.length);
        }

        private void run (Entry entry) {
            try {
                launcher (entry.action);
            } catch (GLib.Error e) {
                Logging.scope (LogDomain.INPUT).warning ("custom shortcut %s: %s", entry.path, e.message);
                error_dialog (GLib.Markup.escape_text (dgettext ("mate-settings-daemon",
                                                                 "Error while trying to run (%s)\n" +
                                                                 "which is linked to the key (%s)")
                                                       .printf (entry.action, entry.binding)));
            }
        }
    }
}
