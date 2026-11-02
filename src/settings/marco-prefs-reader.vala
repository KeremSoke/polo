/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Reads every org.mate.Marco.* schema into the policy's MarcoPrefs. Changes
 * are coalesced into one idle, which pushes a new snapshot to the model and
 * emits prefs_changed.
 */

namespace Polo.Settings {
    public class MarcoPrefsReader : GLib.Object {
        private const string[] SCHEMAS = {
            "general", "org.mate.Marco.general",
            "workspace-names", "org.mate.Marco.workspace-names",
            "global-keybindings", "org.mate.Marco.global-keybindings",
            "window-keybindings", "org.mate.Marco.window-keybindings",
            "keybinding-commands", "org.mate.Marco.keybinding-commands",
        };

        private Policy.Model model;
        private MarcoPrefsTracker tracker;
        private GLib.GenericArray<GLib.Settings> settings = new GLib.GenericArray<GLib.Settings> ();
        private GLib.HashTable<string, GLib.Variant> pending =
            new GLib.HashTable<string, GLib.Variant> (GLib.str_hash, GLib.str_equal);
        private uint changed_idle = 0;

        public signal void prefs_changed (PrefsChange mask);

        [CCode (cname = "g_dgettext", cheader_filename = "glib.h")]
        private static extern unowned string dgettext (string domain, string msgid);

        /* GTK's own test: the gtk30 translation of "default:LTR". */
        private static bool locale_is_rtl () {
            return dgettext ("gtk30", "default:LTR") == "default:RTL";
        }

        public MarcoPrefsReader (Policy.Model model) {
            this.model = model;
            tracker = new MarcoPrefsTracker (locale_is_rtl ());
            tracker.rejected.connect ((key, reason) => {
                Logging.scope (LogDomain.POLO).warning ("Marco preference %s: %s", key, reason);
            });

            var source = GLib.SettingsSchemaSource.get_default ();
            var initial = new GLib.HashTable<string, GLib.Variant> (GLib.str_hash, GLib.str_equal);
            for (int i = 0; i < SCHEMAS.length; i += 2) {
                var schema = source != null ? source.lookup (SCHEMAS[i + 1], true) : null;
                if (schema == null) {
                    Logging.scope (LogDomain.POLO).info ("no %s schema; using Marco's defaults",
                                                         SCHEMAS[i + 1]);
                    continue;
                }
                var s = new GLib.Settings.full (schema, null, null);
                s.set_data<string> ("polo-prefix", SCHEMAS[i]);
                s.changed.connect (on_changed);
                settings.add (s);
                foreach (unowned string key in schema.list_keys ()) {
                    initial.insert (SCHEMAS[i] + "/" + key, s.get_value (key));
                }
            }
            tracker.update (initial);
            model.prefs = tracker.current;
        }

        public void shutdown () {
            foreach (var s in settings) {
                s.changed.disconnect (on_changed);
            }
            settings.remove_range (0, settings.length);
            if (changed_idle != 0) {
                GLib.Source.remove (changed_idle);
                changed_idle = 0;
            }
        }

        private void on_changed (GLib.Settings s, string key) {
            pending.insert (s.get_data<string> ("polo-prefix") + "/" + key, s.get_value (key));
            if (changed_idle == 0) {
                changed_idle = GLib.Idle.add (flush);
            }
        }

        private bool flush () {
            changed_idle = 0;
            var mask = tracker.update (pending);
            pending.remove_all ();
            if (mask != PrefsChange.NONE) {
                model.prefs = tracker.current;
                prefs_changed (mask);
            }
            return GLib.Source.REMOVE;
        }
    }
}
