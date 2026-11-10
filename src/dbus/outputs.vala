/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

namespace Polo.Dbus {
    /* mate-settings-daemon 1.28's xrandr plugin confirms "Does the display look
     * OK?", reverting after 30 seconds, saved only when kept */
    public class Outputs : GLib.Object {
        public const string NAME = "org.mate.Polo";
        public const string PATH = "/org/mate/Polo";
        public const string INTERFACE = "org.mate.Polo.Outputs1";
        private const string RESOURCE = "/org/mate/polo/dbus/org.mate.Polo.Outputs1.xml";
        private const int CONFIRMATION_SECONDS = 30;
        private const int MAX_COORDINATE = 32767;
        private const uint FIRST_DIALOG = 0x50000000;
        private const int RESPONSE_OK = -5;
        private const int RESPONSE_ACCEPT = -3;

        private Shell.Desktop desktop;
        private Backend.ShellUi shell_ui;
        private GLib.DBusConnection connection;
        private Registration? exported = null;
        private uint owner = 0;
        private uint serial = 1;
        private GLib.GenericArray<Polo.Output>? previous = null;
        private uint dialog = 0;
        private uint next_dialog = FIRST_DIALOG;

        public Outputs (GLib.DBusConnection connection, Shell.Desktop desktop, Backend.ShellUi shell_ui) {
            this.connection = connection;
            this.desktop = desktop;
            this.shell_ui = shell_ui;
        }

        public void export () throws GLib.Error {
            exported = new Registration (connection, PATH, Registration.load (RESOURCE, INTERFACE), on_method,
                                         on_property);
            owner = GLib.Bus.own_name_on_connection (connection, NAME, GLib.BusNameOwnerFlags.DO_NOT_QUEUE);
            desktop.layout_changed.connect (on_layout_changed);
            shell_ui.dialog_response.connect (on_dialog_response);
            shell_ui.unbound.connect (on_unbound);
        }

        public void unexport () {
            desktop.layout_changed.disconnect (on_layout_changed);
            shell_ui.dialog_response.disconnect (on_dialog_response);
            shell_ui.unbound.disconnect (on_unbound);
            if (dialog != 0) {
                shell_ui.hide_dialog (dialog);
                dialog = 0;
            }
            if (owner != 0) {
                GLib.Bus.unown_name (owner);
                owner = 0;
            }
            if (exported != null) {
                exported.unregister ();
                exported = null;
            }
        }

        private void on_layout_changed () {
            serial++;
            try {
                connection.emit_signal (null, PATH, INTERFACE, "Changed", new GLib.Variant ("(u)", serial));
                var changed = new GLib.VariantBuilder (new GLib.VariantType ("a{sv}"));
                changed.add ("{sv}", "Serial", new GLib.Variant.uint32 (serial));
                connection.emit_signal (null, PATH, "org.freedesktop.DBus.Properties", "PropertiesChanged",
                                        new GLib.Variant ("(sa{sv}as)", INTERFACE, changed, null));
            } catch (GLib.Error e) {
                Logging.scope (LogDomain.POLO).info ("Outputs1 Changed: %s", e.message);
            }
        }

        private GLib.Variant? on_property (string property) {
            return property == "Serial" ? new GLib.Variant.uint32 (serial) : null;
        }

        private static GLib.Variant describe (Polo.Output o) {
            var modes = new GLib.VariantBuilder (new GLib.VariantType ("a(iiib)"));
            foreach (var mode in o.modes) {
                modes.add ("(iiib)", mode.width, mode.height, mode.rate, mode.preferred);
            }
            var output = new GLib.VariantBuilder (new GLib.VariantType ("a{sv}"));
            output.add ("{sv}", "name", new GLib.Variant.string (o.name));
            output.add ("{sv}", "display-name", new GLib.Variant.string (o.model));
            output.add ("{sv}", "vendor", new GLib.Variant.string (o.vendor));
            output.add ("{sv}", "product", new GLib.Variant.string (o.product));
            output.add ("{sv}", "serial", new GLib.Variant.string (o.serial));
            output.add ("{sv}", "internal", new GLib.Variant.boolean (o.internal_panel));
            output.add ("{sv}", "physical-width", new GLib.Variant.int32 (o.mm_width));
            output.add ("{sv}", "physical-height", new GLib.Variant.int32 (o.mm_height));
            output.add ("{sv}", "modes", modes.end ());
            output.add ("{sv}", "enabled", new GLib.Variant.boolean (o.enabled));
            output.add ("{sv}", "primary", new GLib.Variant.boolean (o.primary));
            output.add ("{sv}", "x", new GLib.Variant.int32 (o.x));
            output.add ("{sv}", "y", new GLib.Variant.int32 (o.y));
            output.add ("{sv}", "width", new GLib.Variant.int32 (o.width));
            output.add ("{sv}", "height", new GLib.Variant.int32 (o.height));
            output.add ("{sv}", "refresh", new GLib.Variant.int32 (o.rate));
            output.add ("{sv}", "scale", new GLib.Variant.int32 (o.scale));
            output.add ("{sv}", "transform", new GLib.Variant.uint32 ((uint) o.transform));
            return output.end ();
        }

        private static bool read_int (GLib.VariantDict dict, string key, ref int value) {
            var v = dict.lookup_value (key, null);
            if (v == null) {
                return true;
            }
            if (v.is_of_type (GLib.VariantType.INT32)) {
                value = v.get_int32 ();
            } else if (v.is_of_type (GLib.VariantType.UINT32)) {
                value = (int) v.get_uint32 ();
            } else {
                return false;
            }
            return true;
        }

        private static bool read_bool (GLib.VariantDict dict, string key, ref bool value) {
            var v = dict.lookup_value (key, null);
            if (v == null) {
                return true;
            }
            if (!v.is_of_type (GLib.VariantType.BOOLEAN)) {
                return false;
            }
            value = v.get_boolean ();
            return true;
        }

        private GLib.GenericArray<Polo.Output>? wanted (GLib.Variant requests) {
            var outputs = desktop.current_outputs ();
            if (requests.n_children () > outputs.length) {
                return null;
            }
            foreach (var request in requests) {
                var dict = new GLib.VariantDict (request);
                string name;
                if (!dict.lookup ("name", "s", out name)) {
                    return null;
                }
                Polo.Output? target = null;
                foreach (var o in outputs) {
                    if (o.name == name) {
                        target = o;
                    }
                }
                if (target == null || !read_bool (dict, "enabled", ref target.enabled) ||
                    !read_bool (dict, "primary", ref target.primary) || !read_int (dict, "x", ref target.x) ||
                    !read_int (dict, "y", ref target.y) || !read_int (dict, "width", ref target.width) ||
                    !read_int (dict, "height", ref target.height) || !read_int (dict, "refresh", ref target.rate) ||
                    !read_int (dict, "scale", ref target.scale) ||
                    !read_int (dict, "transform", ref target.transform)) {
                    return null;
                }
                if (target.x < -MAX_COORDINATE || target.x > MAX_COORDINATE || target.y < -MAX_COORDINATE ||
                    target.y > MAX_COORDINATE) {
                    return null;
                }
                if (target.primary) {
                    foreach (var o in outputs) {
                        o.primary = o == target;
                    }
                }
            }
            return outputs;
        }

        private void fail (GLib.DBusMethodInvocation invocation, string error, string message) {
            invocation.return_dbus_error (INTERFACE + ".Error." + error, message);
        }

        private void on_method (string sender, string method, GLib.Variant parameters,
                                owned GLib.DBusMethodInvocation invocation) {
            switch (method) {
                case "GetOutputs":
                    var list = new GLib.VariantBuilder (new GLib.VariantType ("aa{sv}"));
                    foreach (var o in desktop.current_outputs ()) {
                        list.add_value (describe (o));
                    }
                    invocation.return_value (new GLib.Variant ("(aa{sv})", list));
                    break;
                case "ApplyConfiguration":
                    apply (parameters, invocation);
                    break;
                default:
                    invocation.return_dbus_error ("org.freedesktop.DBus.Error.UnknownMethod",
                                                  "No such method %s".printf (method));
                    break;
            }
        }

        private void apply (GLib.Variant parameters, GLib.DBusMethodInvocation invocation) {
            uint request_serial;
            GLib.Variant requests;
            bool persistent;
            parameters.get ("(u@aa{sv}b)", out request_serial, out requests, out persistent);
            if (dialog != 0) {
                fail (invocation, "Busy", "the previous configuration is still waiting for confirmation");
                return;
            }
            if (request_serial != serial) {
                fail (invocation, "Stale", "the outputs changed since serial %u".printf (request_serial));
                return;
            }
            var outputs = wanted (requests);
            if (outputs == null) {
                fail (invocation, "Invalid", "an output is unknown or a value has the wrong type");
                return;
            }
            var before = desktop.current_outputs ();
            if (!desktop.apply_monitors (outputs, false)) {
                fail (invocation, "Failed", "the configuration could not be applied");
                return;
            }
            invocation.return_value (null);
            if (!persistent) {
                return;
            }
            if (!shell_ui.is_bound ()) {
                Logging.scope (LogDomain.POLO).info ("no polo-shell-ui to confirm the display; not saving it");
                return;
            }
            previous = before;
            dialog = next_dialog++;
            shell_ui.show_dialog (dialog, Backend.DialogKind.DISPLAY_CONFIRMATION, null,
                                  CONFIRMATION_SECONDS.to_string ());
        }

        private void confirmed (bool keep) {
            dialog = 0;
            var restore = (owned) previous;
            if (keep) {
                desktop.remember_current ();
            } else if (restore != null) {
                desktop.apply_monitors (restore, false);
            }
        }

        private void on_dialog_response (uint id, int response, string? text) {
            if (id == dialog) {
                confirmed (response == RESPONSE_ACCEPT || response == RESPONSE_OK);
            }
        }

        private void on_unbound () {
            if (dialog != 0) {
                confirmed (false);
            }
        }
    }
}
