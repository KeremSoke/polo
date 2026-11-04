/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

namespace Polo.Dbus {
    public class App : GLib.Object {
        public const string INTERFACE = "org.gnome.SessionManager.App";
        private const string RESOURCE = "/org/mate/polo/dbus/org.gnome.SessionManager.App.xml";

        private static uint serial = 1;

        public string path { get; private set; }
        public AutostartApp app { get; construct; }
        private Registration? exported = null;

        public App (AutostartApp app) {
            Object (app: app);
            path = "%s/App%u".printf (Manager.PATH, serial++);
        }

        public void export (GLib.DBusConnection connection) throws GLib.Error {
            exported = new Registration (connection, path, Registration.load (RESOURCE, INTERFACE), on_method);
        }

        public void unexport () {
            if (exported != null) {
                exported.unregister ();
                exported = null;
            }
        }

        private void on_method (string sender, string method, GLib.Variant parameters,
                                owned GLib.DBusMethodInvocation invocation) {
            switch (method) {
                case "GetAppId":
                    invocation.return_value (new GLib.Variant ("(s)", app.id));
                    break;
                case "GetStartupId":
                    invocation.return_value (new GLib.Variant ("(s)", app.startup_id));
                    break;
                case "GetPhase":
                    invocation.return_value (new GLib.Variant ("(u)", (uint) ManagerPhase.from_autostart (app.phase)));
                    break;
                default:
                    invocation.return_dbus_error ("org.freedesktop.DBus.Error.UnknownMethod",
                                                  "No such method %s".printf (method));
                    break;
            }
        }
    }
}
