/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

namespace Polo.Dbus {
    public class InputDevices : GLib.Object {
        public const string NAME = "org.mate.InputDevices";
        public const string PATH = "/org/mate/InputDevices";
        public const string INTERFACE = "org.mate.InputDevices1";
        private const string RESOURCE = "/org/mate/polo/dbus/org.mate.InputDevices1.xml";

        private GLib.DBusConnection connection;
        private Input.DeviceList devices;
        private Registration? exported = null;
        private uint owner = 0;

        public InputDevices (GLib.DBusConnection connection, Input.DeviceList devices) {
            this.connection = connection;
            this.devices = devices;
        }

        public void export () throws GLib.Error {
            exported = new Registration (connection, PATH, Registration.load (RESOURCE, INTERFACE), on_method);
            owner = GLib.Bus.own_name_on_connection (connection, NAME, GLib.BusNameOwnerFlags.DO_NOT_QUEUE);
        }

        public void unexport () {
            if (owner != 0) {
                GLib.Bus.unown_name (owner);
                owner = 0;
            }
            if (exported != null) {
                exported.unregister ();
                exported = null;
            }
        }

        private void on_method (string sender, string method, GLib.Variant parameters,
                                owned GLib.DBusMethodInvocation invocation) {
            if (method != "GetDevices") {
                invocation.return_gerror (new GLib.DBusError.UNKNOWN_METHOD ("no method %s", method));
                return;
            }
            invocation.return_value (new GLib.Variant.tuple ({ devices.to_variant () }));
        }
    }
}
