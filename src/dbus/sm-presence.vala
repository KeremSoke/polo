/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

namespace Polo.Dbus {
    public enum PresenceStatus {
        AVAILABLE = 0,
        INVISIBLE = 1,
        BUSY = 2,
        IDLE = 3,
    }

    public class Presence : GLib.Object {
        public const string INTERFACE = "org.gnome.SessionManager.Presence";
        public const string PATH = Manager.PATH + "/Presence";
        private const string RESOURCE = "/org/mate/polo/dbus/org.gnome.SessionManager.Presence.xml";
        private const int MAX_STATUS_TEXT = 140;
        private const string SESSION_SCHEMA = "org.mate.session";
        private const string SCREENSAVER = "org.mate.ScreenSaver";

        private uint _status = PresenceStatus.AVAILABLE;
        public uint status { get { return _status; } }
        private string _status_text = "";
        public string status_text { get { return _status_text; } }
        public uint idle_timeout_ms { get; private set; default = 0; }
        public bool idle_enabled { get; set; default = true; }
        public bool screensaver_active { get; private set; default = false; }

        private uint saved_status = PresenceStatus.AVAILABLE;
        private GLib.DBusConnection connection;
        private Registration? exported = null;
        private GLib.Settings? settings = null;
        private uint screensaver_subscription = 0;

        public Presence (GLib.DBusConnection connection) {
            this.connection = connection;
            var source = GLib.SettingsSchemaSource.get_default ();
            var schema = source != null ? source.lookup (SESSION_SCHEMA, true) : null;
            if (schema != null && schema.has_key ("idle-delay")) {
                settings = new GLib.Settings.full (schema, null, null);
                settings.changed["idle-delay"].connect (load_idle_delay);
                load_idle_delay ();
            }
        }

        public void export () throws GLib.Error {
            exported = new Registration (connection, PATH, Registration.load (RESOURCE, INTERFACE), on_method,
                                         on_get, on_set);
            screensaver_subscription = connection.signal_subscribe (SCREENSAVER, SCREENSAVER, "ActiveChanged",
                                                                    "/org/mate/ScreenSaver", null,
                                                                    GLib.DBusSignalFlags.NONE,
                                                                    on_screensaver_active_changed);
        }

        public void unexport () {
            if (screensaver_subscription != 0) {
                connection.signal_unsubscribe (screensaver_subscription);
                screensaver_subscription = 0;
            }
            if (exported != null) {
                exported.unregister ();
                exported = null;
            }
            if (settings != null) {
                settings.changed["idle-delay"].disconnect (load_idle_delay);
            }
        }

        private void load_idle_delay () {
            idle_timeout_ms = (uint) int.max (0, settings.get_int ("idle-delay")) * 60000;
        }

        public void set_status (uint next) {
            if (next == _status) {
                return;
            }
            _status = next;
            notify_property ("status");
            emit ("StatusChanged", new GLib.Variant ("(u)", next));
        }

        public bool set_status_text (string text) {
            if (text.length > MAX_STATUS_TEXT) {
                return false;
            }
            _status_text = text;
            notify_property ("status-text");
            emit ("StatusTextChanged", new GLib.Variant ("(s)", text));
            return true;
        }

        public void set_session_idle (bool idle) {
            if (idle) {
                if (_status == PresenceStatus.IDLE) {
                    return;
                }
                saved_status = _status;
                set_status (PresenceStatus.IDLE);
            } else {
                if (_status != PresenceStatus.IDLE) {
                    return;
                }
                set_status (saved_status);
                saved_status = PresenceStatus.AVAILABLE;
            }
        }

        private void on_screensaver_active_changed (GLib.DBusConnection connection, string? sender, string path,
                                                    string interface_name, string signal_name,
                                                    GLib.Variant parameters) {
            if (!parameters.is_of_type (new GLib.VariantType ("(b)"))) {
                return;
            }
            bool active;
            parameters.get ("(b)", out active);
            if (screensaver_active != active) {
                screensaver_active = active;
                set_session_idle (active);
            }
        }

        private void emit (string signal_name, GLib.Variant parameters) {
            try {
                connection.emit_signal (null, PATH, INTERFACE, signal_name, parameters);
            } catch (GLib.Error e) {
                Logging.scope (LogDomain.POLO).warning ("cannot emit %s: %s", signal_name, e.message);
            }
        }

        private GLib.Variant? on_get (string property) {
            switch (property) {
                case "status":
                    return new GLib.Variant.uint32 (_status);
                case "status-text":
                    return new GLib.Variant.string (_status_text);
                default:
                    return null;
            }
        }

        private bool on_set (string property, GLib.Variant value) {
            switch (property) {
                case "status":
                    if (value.get_uint32 () > PresenceStatus.IDLE) {
                        return false;
                    }
                    set_status (value.get_uint32 ());
                    return true;
                case "status-text":
                    return set_status_text (value.get_string ());
                default:
                    return false;
            }
        }

        private void on_method (string sender, string method, GLib.Variant parameters,
                                owned GLib.DBusMethodInvocation invocation) {
            switch (method) {
                case "SetStatus":
                    uint next;
                    parameters.get ("(u)", out next);
                    if (next > PresenceStatus.IDLE) {
                        invocation.return_dbus_error ("org.freedesktop.DBus.Error.InvalidArgs",
                                                      "Unknown status %u".printf (next));
                        return;
                    }
                    set_status (next);
                    invocation.return_value (null);
                    break;
                case "SetStatusText":
                    string text;
                    parameters.get ("(s)", out text);
                    if (!set_status_text (text)) {
                        invocation.return_dbus_error (INTERFACE + ".GeneralError", "Status text too long");
                        return;
                    }
                    invocation.return_value (null);
                    break;
                default:
                    invocation.return_dbus_error ("org.freedesktop.DBus.Error.UnknownMethod",
                                                  "No such method %s".printf (method));
                    break;
            }
        }
    }
}
