/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * mate-settings-daemon 1.28's xsettings plugin (msd-xsettings-manager.c)
 * for native Wayland clients. GTK on Wayland and xdg-desktop-portal-gtk
 * read org.gnome.desktop.*, not XSETTINGS, so the MATE keys msd publishes
 * as XSETTINGS are copied one way into their GNOME counterparts. The copy
 * lands in the user's dconf database and so also applies to GNOME sessions;
 * org.mate.polo.general mirror-gtk-settings turns it off.
 */

namespace Polo.Settings {
    public class InterfaceMirror : GLib.Object {
        public const string GENERAL_SCHEMA = "org.mate.polo.general";
        public const string ENABLED_KEY = "mirror-gtk-settings";

        private const string MATE_INTERFACE = "org.mate.interface";
        private const string MATE_MOUSE = "org.mate.peripherals-mouse";
        private const string MATE_SOUND = "org.mate.sound";
        private const string MATE_FONTS = "org.mate.font-rendering";
        private const string GNOME_INTERFACE = "org.gnome.desktop.interface";
        private const string GNOME_MOUSE = "org.gnome.desktop.peripherals.mouse";
        private const string GNOME_SOUND = "org.gnome.desktop.sound";
        private const string GNOME_WM = "org.gnome.desktop.wm.preferences";

        private const double DPI_FALLBACK = 96;
        private const double DPI_LOW_REASONABLE_VALUE = 50;
        private const double DPI_HIGH_REASONABLE_VALUE = 500;

        private const string[] TABLE = {
            MATE_INTERFACE, "gtk-theme", GNOME_INTERFACE, "gtk-theme",
            MATE_INTERFACE, "icon-theme", GNOME_INTERFACE, "icon-theme",
            MATE_INTERFACE, "font-name", GNOME_INTERFACE, "font-name",
            MATE_INTERFACE, "document-font-name", GNOME_INTERFACE, "document-font-name",
            MATE_INTERFACE, "monospace-font-name", GNOME_INTERFACE, "monospace-font-name",
            MATE_INTERFACE, "cursor-blink", GNOME_INTERFACE, "cursor-blink",
            MATE_INTERFACE, "cursor-blink-time", GNOME_INTERFACE, "cursor-blink-time",
            MATE_INTERFACE, "gtk-key-theme", GNOME_INTERFACE, "gtk-key-theme",
            MATE_INTERFACE, "gtk-im-module", GNOME_INTERFACE, "gtk-im-module",
            MATE_INTERFACE, "gtk-im-preedit-style", GNOME_INTERFACE, "gtk-im-preedit-style",
            MATE_INTERFACE, "gtk-im-status-style", GNOME_INTERFACE, "gtk-im-status-style",
            MATE_INTERFACE, "gtk-enable-primary-paste", GNOME_INTERFACE, "gtk-enable-primary-paste",
            MATE_INTERFACE, "gtk-enable-animations", GNOME_INTERFACE, "enable-animations",
            MATE_INTERFACE, "gtk-overlay-scrolling", GNOME_INTERFACE, "overlay-scrolling",
            MATE_INTERFACE, "gtk-color-scheme", GNOME_INTERFACE, "gtk-color-scheme",
            MATE_INTERFACE, "gtk-color-palette", GNOME_INTERFACE, "gtk-color-palette",
            MATE_INTERFACE, "toolbar-style", GNOME_INTERFACE, "toolbar-style",
            MATE_INTERFACE, "toolbar-icons-size", GNOME_INTERFACE, "toolbar-icons-size",
            MATE_INTERFACE, "menubar-accel", GNOME_INTERFACE, "menubar-accel",
            MATE_INTERFACE, "gtk-decoration-layout", GNOME_WM, "button-layout",
            MATE_MOUSE, "cursor-theme", GNOME_INTERFACE, "cursor-theme",
            MATE_MOUSE, "cursor-size", GNOME_INTERFACE, "cursor-size",
            MATE_MOUSE, "double-click", GNOME_MOUSE, "double-click",
            MATE_MOUSE, "drag-threshold", GNOME_MOUSE, "drag-threshold",
            MATE_SOUND, "theme-name", GNOME_SOUND, "theme-name",
            MATE_SOUND, "event-sounds", GNOME_SOUND, "event-sounds",
            MATE_SOUND, "input-feedback-sounds", GNOME_SOUND, "input-feedback-sounds",
            MATE_FONTS, "antialiasing", GNOME_INTERFACE, "font-antialiasing",
            MATE_FONTS, "hinting", GNOME_INTERFACE, "font-hinting",
            MATE_FONTS, "rgba-order", GNOME_INTERFACE, "font-rgba-order",
            MATE_FONTS, "dpi", GNOME_INTERFACE, "text-scaling-factor",
        };

        private GLib.Settings? general;
        private GLib.HashTable<string, GLib.Settings> schemas =
            new GLib.HashTable<string, GLib.Settings> (str_hash, str_equal);
        private GLib.GenericArray<GLib.Settings> watched = new GLib.GenericArray<GLib.Settings> ();
        private bool enabled = false;

        public InterfaceMirror (GLib.Settings? general) {
            this.general = general;
            for (int i = 0; i < TABLE.length; i += 4) {
                lookup (TABLE[i]);
                lookup (TABLE[i + 2]);
            }
            if (general != null) {
                general.changed[ENABLED_KEY].connect (update_enabled);
            }
            update_enabled ();
        }

        public void shutdown () {
            if (general != null) {
                general.changed[ENABLED_KEY].disconnect (update_enabled);
            }
            set_enabled (false);
        }

        private GLib.Settings? lookup (string id) {
            var found = schemas.lookup (id);
            if (found != null) {
                return found;
            }
            var source = GLib.SettingsSchemaSource.get_default ();
            var schema = source != null ? source.lookup (id, true) : null;
            if (schema == null) {
                return null;
            }
            var settings = new GLib.Settings.full (schema, null, null);
            schemas.insert (id, settings);
            return settings;
        }

        private void update_enabled () {
            set_enabled (general == null || !general.settings_schema.has_key (ENABLED_KEY) ||
                         general.get_boolean (ENABLED_KEY));
        }

        private void set_enabled (bool on) {
            if (on == enabled) {
                return;
            }
            enabled = on;
            if (!on) {
                foreach (var settings in watched) {
                    settings.changed.disconnect (on_changed);
                }
                watched.length = 0;
                return;
            }
            for (int i = 0; i < TABLE.length; i += 4) {
                var settings = schemas.lookup (TABLE[i]);
                if (settings != null && !watched.find (settings)) {
                    watched.add (settings);
                    settings.changed.connect (on_changed);
                }
            }
            for (int i = 0; i < TABLE.length; i += 4) {
                copy (i);
            }
        }

        private void on_changed (GLib.Settings settings, string key) {
            for (int i = 0; i < TABLE.length; i += 4) {
                if (TABLE[i + 1] == key && schemas.lookup (TABLE[i]) == settings) {
                    copy (i);
                }
            }
        }

        private void copy (int i) {
            var from = schemas.lookup (TABLE[i]);
            var to = schemas.lookup (TABLE[i + 2]);
            unowned string from_key = TABLE[i + 1];
            unowned string to_key = TABLE[i + 3];
            if (from == null || to == null || !from.settings_schema.has_key (from_key) ||
                !to.settings_schema.has_key (to_key)) {
                return;
            }
            var target = to.settings_schema.get_key (to_key);
            var value = translate (to_key, from.get_value (from_key));
            if (value == null || !value.get_type ().equal (target.get_value_type ()) ||
                !target.range_check (value)) {
                return;
            }
            if (!to.get_value (to_key).equal (value)) {
                to.set_value (to_key, value);
            }
        }

        private static GLib.Variant? translate (string to_key, GLib.Variant value) {
            switch (to_key) {
                case "toolbar-style":
                    return value.get_string () == "both_horiz" ? new GLib.Variant.string ("both-horiz") : value;
                case "toolbar-icons-size":
                    switch (value.get_string ()) {
                        case "small-toolbar":
                            return new GLib.Variant.string ("small");
                        case "large-toolbar":
                            return new GLib.Variant.string ("large");
                        default:
                            return value;
                    }
                case "text-scaling-factor":
                    double dpi = value.get_double ();
                    if (dpi == 0) {
                        dpi = DPI_FALLBACK;
                    }
                    dpi = dpi.clamp (DPI_LOW_REASONABLE_VALUE, DPI_HIGH_REASONABLE_VALUE);
                    return new GLib.Variant.double ((dpi / DPI_FALLBACK).clamp (0.5, 3.0));
                default:
                    return value;
            }
        }
    }
}
