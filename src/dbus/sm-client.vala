/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

namespace Polo.Dbus {
    public enum ClientStatus {
        UNREGISTERED = 0,
        REGISTERED = 1,
        FINISHED = 2,
        FAILED = 3,
    }

    public class Client : GLib.Object {
        public const string INTERFACE = "org.gnome.SessionManager.Client";
        public const string PRIVATE_INTERFACE = "org.gnome.SessionManager.ClientPrivate";
        private const string RESOURCE = "/org/mate/polo/dbus/org.gnome.SessionManager.Client.xml";
        private const string PRIVATE_RESOURCE = "/org/mate/polo/dbus/org.gnome.SessionManager.ClientPrivate.xml";

        private static uint serial = 1;

        public string path { get; private set; }
        public string startup_id { get; construct; }
        public string bus_name { get; construct; }
        public string app_id { get; set; default = ""; }
        public ClientStatus status { get; set; default = ClientStatus.UNREGISTERED; }
        public uint pid { get; set; default = 0; }
        public uint restart_style_hint { get; set; default = 0; }

        public signal void end_session_response (bool is_ok, string reason);

        private GLib.DBusConnection? connection = null;
        private Registration? exported = null;
        private Registration? exported_private = null;

        public Client (string startup_id, string bus_name) {
            Object (startup_id: startup_id, bus_name: bus_name);
            path = "%s/Client%u".printf (Manager.PATH, serial++);
        }

        public void export (GLib.DBusConnection connection) throws GLib.Error {
            this.connection = connection;
            exported = new Registration (connection, path, Registration.load (RESOURCE, INTERFACE),
                                         on_method);
            exported_private = new Registration (connection, path,
                                                 Registration.load (PRIVATE_RESOURCE, PRIVATE_INTERFACE),
                                                 on_private_method);
        }

        public void unexport () {
            if (exported != null) {
                exported.unregister ();
                exported = null;
            }
            if (exported_private != null) {
                exported_private.unregister ();
                exported_private = null;
            }
            connection = null;
        }

        public void stop () {
            send ("Stop", null);
        }

        public void query_end_session (uint flags) {
            send ("QueryEndSession", new GLib.Variant ("(u)", flags));
        }

        public void end_session (uint flags) {
            send ("EndSession", new GLib.Variant ("(u)", flags));
        }

        public void cancel_end_session () {
            send ("CancelEndSession", null);
        }

        /* gsm unicasts ClientPrivate signals to the registered bus name only. */
        private void send (string signal_name, GLib.Variant? parameters) {
            if (connection == null) {
                return;
            }
            try {
                connection.emit_signal (bus_name, path, PRIVATE_INTERFACE, signal_name, parameters);
            } catch (GLib.Error e) {
                Logging.scope (LogDomain.POLO).info ("cannot send %s to %s: %s", signal_name, bus_name,
                                                     e.message);
            }
        }

        private void on_method (string sender, string method, GLib.Variant parameters,
                                owned GLib.DBusMethodInvocation invocation) {
            switch (method) {
                case "GetAppId":
                    invocation.return_value (new GLib.Variant ("(s)", app_id));
                    break;
                case "GetStartupId":
                    invocation.return_value (new GLib.Variant ("(s)", startup_id));
                    break;
                case "GetRestartStyleHint":
                    invocation.return_value (new GLib.Variant ("(u)", restart_style_hint));
                    break;
                case "GetUnixProcessId":
                    invocation.return_value (new GLib.Variant ("(u)", pid));
                    break;
                case "GetStatus":
                    invocation.return_value (new GLib.Variant ("(u)", (uint) status));
                    break;
                case "Stop":
                    invocation.return_value (null);
                    stop ();
                    break;
                default:
                    invocation.return_dbus_error ("org.freedesktop.DBus.Error.UnknownMethod",
                                                  "No such method %s".printf (method));
                    break;
            }
        }

        private void on_private_method (string sender, string method, GLib.Variant parameters,
                                        owned GLib.DBusMethodInvocation invocation) {
            if (method != "EndSessionResponse") {
                invocation.return_dbus_error ("org.freedesktop.DBus.Error.UnknownMethod",
                                              "No such method %s".printf (method));
                return;
            }
            if (sender != bus_name) {
                invocation.return_dbus_error ("org.freedesktop.DBus.Error.Failed",
                                              "Caller not recognized as the client");
                return;
            }
            bool is_ok;
            string reason;
            parameters.get ("(bs)", out is_ok, out reason);
            invocation.return_value (null);
            end_session_response (is_ok, reason);
        }
    }
}
