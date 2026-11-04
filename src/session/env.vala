/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

namespace Polo {
    public class SessionEnv : GLib.Object {
        private const int CALL_TIMEOUT_MS = 2000;
#if POLO_HAVE_SYSTEMD
        private const string SYSTEMD = "org.freedesktop.systemd1";
        private const string SYSTEMD_PATH = "/org/freedesktop/systemd1";
        private const string SYSTEMD_MANAGER = "org.freedesktop.systemd1.Manager";
        private const string SESSION_TARGET = "polo-session.target";
#endif

        private const string[] PUBLISHED = {
            "XDG_CURRENT_DESKTOP", "XDG_SESSION_DESKTOP", "XDG_SESSION_TYPE", "DESKTOP_SESSION",
            "XDG_MENU_PREFIX", "MATE_DESKTOP_SESSION_ID", "DCONF_PROFILE", "QT_QPA_PLATFORM",
            "MOZ_ENABLE_WAYLAND", "WAYLAND_DISPLAY", "DISPLAY", "XAUTHORITY", "XCURSOR_THEME",
            "XCURSOR_SIZE", "GTK_MODULES",
        };

#if POLO_HAVE_SYSTEMD
        private string[] sent_to_systemd = {};
        private bool target_started = false;
#endif

        public static void set_fixed () {
            GLib.Environment.set_variable ("XDG_CURRENT_DESKTOP", "MATE", true);
            GLib.Environment.set_variable ("XDG_SESSION_DESKTOP", "mate", true);
            GLib.Environment.set_variable ("XDG_SESSION_TYPE", "wayland", true);
            GLib.Environment.set_variable ("DESKTOP_SESSION", "mate", true);
            GLib.Environment.set_variable ("XDG_MENU_PREFIX", "mate-", true);
            GLib.Environment.set_variable ("MATE_DESKTOP_SESSION_ID", "this-is-deprecated", true);
            GLib.Environment.set_variable ("QT_QPA_PLATFORM", "wayland;xcb", true);
            GLib.Environment.set_variable ("MOZ_ENABLE_WAYLAND", "1", true);
            GLib.Environment.unset_variable ("GDK_BACKEND");
        }

        public void publish (string socket_name) {
            GLib.Environment.set_variable ("WAYLAND_DISPLAY", socket_name, true);
            add_accessibility_module ();

            var activation = new GLib.VariantBuilder (new GLib.VariantType ("a{ss}"));
            string[] assignments = {};
            string[] names = {};
            foreach (unowned string name in PUBLISHED) {
                unowned string? value = GLib.Environment.get_variable (name);
                if (value != null) {
                    activation.add ("{ss}", name, value);
                    assignments += "%s=%s".printf (name, value);
                    names += name;
                }
            }

            try {
                var bus = GLib.Bus.get_sync (GLib.BusType.SESSION);
                bus.call_sync ("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
                               "UpdateActivationEnvironment", new GLib.Variant ("(a{ss})", activation),
                               null, GLib.DBusCallFlags.NONE, CALL_TIMEOUT_MS);
#if POLO_HAVE_SYSTEMD
                bus.call_sync (SYSTEMD, SYSTEMD_PATH, SYSTEMD_MANAGER, "SetEnvironment",
                               new GLib.Variant ("(^as)", assignments), null,
                               GLib.DBusCallFlags.NO_AUTO_START, CALL_TIMEOUT_MS);
                sent_to_systemd = names;
                unit_call (bus, "StartUnit");
                target_started = true;
#endif
            } catch (GLib.Error e) {
                Logging.scope (LogDomain.POLO).info ("session environment: %s", e.message);
            }
        }

        public void withdraw () {
#if POLO_HAVE_SYSTEMD
            if (sent_to_systemd.length == 0) {
                return;
            }
            try {
                var bus = GLib.Bus.get_sync (GLib.BusType.SESSION);
                if (target_started) {
                    unit_call (bus, "StopUnit");
                }
                bus.call_sync (SYSTEMD, SYSTEMD_PATH, SYSTEMD_MANAGER, "UnsetEnvironment",
                               new GLib.Variant ("(^as)", sent_to_systemd), null,
                               GLib.DBusCallFlags.NO_AUTO_START, CALL_TIMEOUT_MS);
            } catch (GLib.Error e) {
                Logging.scope (LogDomain.POLO).info ("session environment: %s", e.message);
            }
            sent_to_systemd = {};
            target_started = false;
#endif
        }

#if POLO_HAVE_SYSTEMD
        private static void unit_call (GLib.DBusConnection bus, string method) throws GLib.Error {
            bus.call_sync (SYSTEMD, SYSTEMD_PATH, SYSTEMD_MANAGER, method,
                           new GLib.Variant ("(ss)", SESSION_TARGET, "replace"), null,
                           GLib.DBusCallFlags.NO_AUTO_START, CALL_TIMEOUT_MS);
        }

        /* gsm_util_setenv (): for Polo and what it starts, D-Bus activation and
         * systemd's user manager. */
        public static void setenv (string name, string value) {
            GLib.Environment.set_variable (name, value, true);
            try {
                var bus = GLib.Bus.get_sync (GLib.BusType.SESSION);
                var activation = new GLib.VariantBuilder (new GLib.VariantType ("a{ss}"));
                activation.add ("{ss}", name, value);
                bus.call.begin ("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
                                "UpdateActivationEnvironment", new GLib.Variant ("(a{ss})", activation), null,
                                GLib.DBusCallFlags.NONE, CALL_TIMEOUT_MS, null);
#if POLO_HAVE_SYSTEMD
                bus.call.begin (SYSTEMD, SYSTEMD_PATH, SYSTEMD_MANAGER, "SetEnvironment",
                                new GLib.Variant ("(^as)", new string[] { "%s=%s".printf (name, value) }), null,
                                GLib.DBusCallFlags.NO_AUTO_START, CALL_TIMEOUT_MS, null);
#endif
            } catch (GLib.Error e) {
                Logging.scope (LogDomain.POLO).info ("Setenv %s: %s", name, e.message);
            }
        }
#endif

        public static void register_with_display_manager () {
            try {
                var bus = GLib.Bus.get_sync (GLib.BusType.SYSTEM);
                bus.call_sync ("org.gnome.DisplayManager", "/org/gnome/DisplayManager/Manager",
                               "org.gnome.DisplayManager.Manager", "RegisterSession",
                               new GLib.Variant ("(@a{sv})", new GLib.Variant ("a{sv}", null)), null,
                               GLib.DBusCallFlags.NO_AUTO_START, CALL_TIMEOUT_MS);
            } catch (GLib.Error e) {
                Logging.scope (LogDomain.POLO).info ("display manager: %s", e.message);
            }
        }

        private static void add_accessibility_module () {
            const string MODULE = "gail:atk-bridge";
            var source = GLib.SettingsSchemaSource.get_default ();
            var schema = source != null ? source.lookup ("org.mate.interface", true) : null;
            if (schema == null || !new GLib.Settings.full (schema, null, null).get_boolean ("accessibility")) {
                return;
            }
            unowned string? modules = GLib.Environment.get_variable ("GTK_MODULES");
            if (modules == null || modules == "") {
                GLib.Environment.set_variable ("GTK_MODULES", MODULE, true);
            } else if (!(MODULE in modules.split (":"))) {
                GLib.Environment.set_variable ("GTK_MODULES", modules + ":" + MODULE, true);
            }
        }
    }
}
