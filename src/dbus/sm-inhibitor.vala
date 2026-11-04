/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

namespace Polo.Dbus {
    [Flags]
    public enum InhibitorFlags {
        LOGOUT = 1,
        SWITCH_USER = 2,
        SUSPEND = 4,
        IDLE = 8,
        AUTOMOUNT = 16,
    }

    public class Inhibitor : GLib.Object {
        public const string INTERFACE = "org.gnome.SessionManager.Inhibitor";
        private const string RESOURCE = "/org/mate/polo/dbus/org.gnome.SessionManager.Inhibitor.xml";

        private static uint serial = 1;
        private Registration? exported = null;

        public string path { get; private set; }
        public string app_id { get; construct; }
        public uint toplevel_xid { get; construct; }
        public string reason { get; construct; }
        public uint flags { get; construct; }
        public string bus_name { get; construct; }
        public uint cookie { get; construct; }
        public string? client_id { get; construct; }

        public Inhibitor (string app_id, uint toplevel_xid, string reason, uint flags, string bus_name,
                          uint cookie) {
            Object (app_id: app_id, toplevel_xid: toplevel_xid, reason: reason, flags: flags,
                    bus_name: bus_name, cookie: cookie);
            path = "%s/Inhibitor%u".printf (Manager.PATH, serial++);
        }

        public Inhibitor.for_client (string client_id, string app_id, uint flags, string reason,
                                     string bus_name, uint cookie) {
            Object (app_id: app_id, toplevel_xid: 0, reason: reason, flags: flags, bus_name: bus_name,
                    cookie: cookie, client_id: client_id);
            path = "%s/Inhibitor%u".printf (Manager.PATH, serial++);
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
                    invocation.return_value (new GLib.Variant ("(s)", app_id));
                    break;
                case "GetClientId":
                    if (client_id == null) {
                        invocation.return_dbus_error (INTERFACE + ".NotSet", "Value is not set");
                    } else {
                        invocation.return_value (new GLib.Variant ("(o)", client_id));
                    }
                    break;
                case "GetReason":
                    invocation.return_value (new GLib.Variant ("(s)", reason));
                    break;
                case "GetFlags":
                    invocation.return_value (new GLib.Variant ("(u)", flags));
                    break;
                case "GetToplevelXid":
                    invocation.return_value (new GLib.Variant ("(u)", toplevel_xid));
                    break;
                default:
                    invocation.return_dbus_error ("org.freedesktop.DBus.Error.UnknownMethod",
                                                  "No such method %s".printf (method));
                    break;
            }
        }
    }
}
