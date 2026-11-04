/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

namespace Polo.Dbus {
    public delegate void MethodHandler (string sender, string method, GLib.Variant parameters,
                                        owned GLib.DBusMethodInvocation invocation);

    public delegate GLib.Variant? PropertyHandler (string property);

    public class Registration : GLib.Object {
        private GLib.DBusConnection connection;
        private uint id = 0;
        private MethodHandler on_method;
        private PropertyHandler? on_property;

        public Registration (GLib.DBusConnection connection, string path, GLib.DBusInterfaceInfo info,
                             owned MethodHandler on_method, owned PropertyHandler? on_property = null)
            throws GLib.Error {
            this.connection = connection;
            this.on_method = (owned) on_method;
            this.on_property = (owned) on_property;
            var method_closure = closure (marshal_method);
            GLib.Closure? property_closure = this.on_property != null ? closure (marshal_property) : null;
            id = connection.register_object_with_closures (path, info, method_closure, property_closure, null);
        }

        public void unregister () {
            if (id != 0) {
                connection.unregister_object (id);
                id = 0;
            }
        }

        private GLib.Closure closure (GLib.ClosureMarshal marshal) {
            var made = new GLib.Closure.object ((uint) GLib.Closure.SIZE, this);
            made.set_meta_marshal (this, marshal);
            made.ref ();
            made.sink ();
            return made;
        }

        private static void marshal_method (GLib.Closure closure, out GLib.Value return_value,
                                            GLib.Value[] param_values, void* hint, void* data) {
            return_value = GLib.Value (typeof (bool));
            unowned Registration self = (Registration) data;
            var invocation = (GLib.DBusMethodInvocation) param_values[6].dup_object ();
            invocation.unref ();
            self.on_method (param_values[1].get_string (), param_values[4].get_string (),
                            param_values[5].get_variant (), (owned) invocation);
        }

        private static void marshal_property (GLib.Closure closure, out GLib.Value return_value,
                                              GLib.Value[] param_values, void* hint, void* data) {
            unowned Registration self = (Registration) data;
            return_value = GLib.Value (typeof (GLib.Variant));
            return_value.set_variant (self.on_property (param_values[4].get_string ()));
        }

        public static GLib.DBusInterfaceInfo load (string resource, string name) throws GLib.Error {
            var bytes = GLib.resources_lookup_data (resource, GLib.ResourceLookupFlags.NONE);
            var node = new GLib.DBusNodeInfo.for_xml ((string) bytes.get_data ());
            var info = node.lookup_interface (name);
            if (info == null) {
                throw new GLib.IOError.NOT_FOUND ("%s has no interface %s", resource, name);
            }
            return info;
        }
    }
}
