/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * polo --session: the session-mode setup required before the compositor
 * starts.
 */

namespace Polo {
    public errordomain SessionError {
        NO_BUS,
        ANOTHER_SESSION
    }

    /* Minimal SessionManager interface used by session mode. */
    [DBus (name = "org.gnome.SessionManager")]
    public class SessionManagerStub : GLib.Object {
        [DBus (visible = false)]
        public signal void logout_requested ();

        public void logout (uint mode) throws GLib.DBusError, GLib.IOError {
            GLib.Idle.add (() => {
                logout_requested ();
                return GLib.Source.REMOVE;
            });
        }
    }

    public class Session : GLib.Object {
        public const string MANAGER_NAME = "org.gnome.SessionManager";
        private const string PROFILE = "polo";
        private const string PROFILE_PATH = "/etc/dconf/profile/polo";

        private const string MANAGER_PATH = "/org/gnome/SessionManager";

        public signal void logout_requested ();

        private GLib.DBusConnection? manager_bus = null;
        private SessionManagerStub? manager = null;
        private uint manager_object = 0;
        private uint manager_name = 0;

        public void own_session_manager () {
            try {
                manager_bus = GLib.Bus.get_sync (GLib.BusType.SESSION);
                manager = new SessionManagerStub ();
                manager.logout_requested.connect (() => logout_requested ());
                manager_object = manager_bus.register_object (MANAGER_PATH, manager);
                manager_name = GLib.Bus.own_name_on_connection (manager_bus, MANAGER_NAME,
                                                                GLib.BusNameOwnerFlags.DO_NOT_QUEUE);
            } catch (GLib.Error e) {
                Logging.scope (LogDomain.POLO).warning ("cannot provide %s: %s", MANAGER_NAME, e.message);
            }
        }

        public void release_session_manager () {
            if (manager_name != 0) {
                GLib.Bus.unown_name (manager_name);
                manager_name = 0;
            }
            if (manager_object != 0) {
                manager_bus.unregister_object (manager_object);
                manager_object = 0;
            }
            manager = null;
            manager_bus = null;
        }

        public static void check_no_session_manager () throws SessionError {
            try {
                var bus = GLib.Bus.get_sync (GLib.BusType.SESSION);
                var reply = bus.call_sync ("org.freedesktop.DBus", "/org/freedesktop/DBus",
                                           "org.freedesktop.DBus", "NameHasOwner",
                                           new GLib.Variant ("(s)", MANAGER_NAME),
                                           new GLib.VariantType ("(b)"),
                                           GLib.DBusCallFlags.NONE, -1);
                bool owned;
                reply.get ("(b)", out owned);
                if (owned) {
                    throw new SessionError.ANOTHER_SESSION (
                        "another session manager is running (%s is owned)", MANAGER_NAME);
                }
            } catch (SessionError e) {
                throw e;
            } catch (GLib.Error e) {
                throw new SessionError.NO_BUS ("no D-Bus session bus: %s", e.message);
            }
        }

        /*
         * Before anything reads GSettings. dconf falls back to a null configuration,
         * which saves nothing, for a named profile that does not exist.
         */
        public static void use_dconf_profile () {
            string? test_profile = CommandLine.test_mode
                ? GLib.Environment.get_variable ("POLO_TEST_DCONF_PROFILE") : null;
            if (test_profile != null) {
                GLib.Environment.set_variable ("DCONF_PROFILE", test_profile, true);
            } else if (GLib.FileUtils.test (PROFILE_PATH, GLib.FileTest.EXISTS)) {
                GLib.Environment.set_variable ("DCONF_PROFILE", PROFILE, true);
            } else {
                Logging.scope (LogDomain.POLO).warning ("%s is not installed; using the default dconf profile",
                                                        PROFILE_PATH);
            }
        }
    }
}
