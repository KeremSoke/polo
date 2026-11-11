/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The full-screen magnifier. org.gnome.desktop.a11y.applications
 * screen-magnifier-enabled turns it on (the magnifier media key toggles it),
 * org.gnome.desktop.a11y.magnifier mag-factor is the scale, and
 * org.mate.polo.keybindings has keys to toggle it and to zoom in and out.
 * It follows the pointer, and the focused window unless focus-tracking is
 * none.
 */

namespace Polo.Shell {
    public class Magnifier : GLib.Object {
        public const string TOGGLE = "polo-magnifier";
        public const string ZOOM_IN = "polo-magnifier-zoom-in";
        public const string ZOOM_OUT = "polo-magnifier-zoom-out";
        private const string GROUP = "polo-magnifier";
        private const string APPS_SCHEMA = "org.gnome.desktop.a11y.applications";
        private const string MAGNIFIER_SCHEMA = "org.gnome.desktop.a11y.magnifier";
        private const string KEYS_SCHEMA = "org.mate.polo.keybindings";
        private const string ENABLED = "screen-magnifier-enabled";
        private const double MAX_FACTOR = 32.0;

        private Backend.Server server;
        private Policy.Model model;
        private Input.Bindings bindings;
        private GLib.Settings? apps = null;
        private GLib.Settings? magnifier = null;
        private GLib.Settings? keys = null;

        public Magnifier (Backend.Server server, Policy.Model model, Input.Bindings bindings) {
            this.server = server;
            this.model = model;
            this.bindings = bindings;
            apps = settings (APPS_SCHEMA, ENABLED);
            magnifier = settings (MAGNIFIER_SCHEMA, "mag-factor");
            keys = settings (KEYS_SCHEMA, "magnifier");
            if (apps != null) {
                apps.changed[ENABLED].connect (apply);
            }
            if (magnifier != null) {
                magnifier.changed["mag-factor"].connect (apply);
            }
            if (keys != null) {
                keys.changed.connect (bind_keys);
            }
            bindings.register (TOGGLE, (binding, seat, event) => toggle ());
            bindings.register (ZOOM_IN, (binding, seat, event) => zoom (1));
            bindings.register (ZOOM_OUT, (binding, seat, event) => zoom (-1));
            model.focus_changed.connect (on_focus_changed);
            bind_keys ();
            apply ();
        }

        public void shutdown () {
            model.focus_changed.disconnect (on_focus_changed);
            if (apps != null) {
                apps.changed[ENABLED].disconnect (apply);
            }
            if (magnifier != null) {
                magnifier.changed["mag-factor"].disconnect (apply);
            }
            if (keys != null) {
                keys.changed.disconnect (bind_keys);
            }
            bindings.unregister (TOGGLE);
            bindings.unregister (ZOOM_IN);
            bindings.unregister (ZOOM_OUT);
            bindings.set_custom (GROUP, new GLib.GenericArray<Input.Binding> ());
            server.set_zoom (1.0);
        }

        private static GLib.Settings? settings (string id, string key) {
            var source = GLib.SettingsSchemaSource.get_default ();
            var schema = source != null ? source.lookup (id, true) : null;
            if (schema == null || !schema.has_key (key)) {
                return null;
            }
            return new GLib.Settings.full (schema, null, null);
        }

        private bool enabled () {
            return apps != null && apps.get_boolean (ENABLED);
        }

        private double factor () {
            return magnifier != null ? magnifier.get_double ("mag-factor").clamp (1.0, MAX_FACTOR) : 2.0;
        }

        private void apply () {
            server.set_zoom (enabled () ? factor () : 1.0);
        }

        private void toggle () {
            if (apps != null) {
                apps.set_boolean (ENABLED, !enabled ());
            }
        }

        private void zoom (int step) {
            if (magnifier != null) {
                magnifier.set_double ("mag-factor", (Math.round (factor ()) + step).clamp (1.0, MAX_FACTOR));
            }
            if (step > 0 && apps != null && !enabled ()) {
                apps.set_boolean (ENABLED, true);
            }
        }

        private void on_focus_changed () {
            var window = model.focused;
            if (window == null || server.get_zoom () <= 1.0) {
                return;
            }
            if (magnifier != null && magnifier.get_string ("focus-tracking") == "none") {
                return;
            }
            var frame = window.frame;
            server.zoom_to (frame.x + frame.width / 2.0, frame.y + frame.height / 2.0);
        }

        private void bind_keys () {
            var custom = new GLib.GenericArray<Input.Binding> ();
            string[,] names = {
                { "magnifier", TOGGLE }, { "magnifier-zoom-in", ZOOM_IN }, { "magnifier-zoom-out", ZOOM_OUT }
            };
            for (int i = 0; keys != null && i < names.length[0]; i++) {
                var accelerator = Input.Accelerator ();
                if (Input.Accelerators.parse (keys.get_string (names[i, 0]), out accelerator) &&
                    !accelerator.is_disabled ()) {
                    custom.add (new Input.Binding () {
                        name = names[i, 1], kind = Input.BindingKind.GLOBAL, accelerator = accelerator
                    });
                }
            }
            bindings.set_custom (GROUP, custom);
        }
    }
}
