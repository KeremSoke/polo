/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The id MATE stores per-monitor settings under. A monitor whose make and
 * model no other connected monitor shares is "make-model", exactly as
 * libmate-desktop names it from a GdkMonitor; twins are told apart by
 * their EDID serial, or else by their connector, never by position.
 */
namespace Polo.Outputs {
    public class MonitorId : GLib.Object {
        public static string sanitize (string text) {
            var id = new GLib.StringBuilder ();
            for (int i = 0; i < text.length; i++) {
                char c = text[i];
                id.append_c (c.isalnum () || c == '_' || c == '.' || c == '-' ? c : '_');
            }
            return id.str;
        }

        public static string base_id (Polo.Output output) {
            var id = new GLib.StringBuilder (sanitize (output.make));
            if (output.make != "" && output.model != "") {
                id.append_c ('-');
            }
            id.append (sanitize (output.model));
            return id.len == 0 ? "monitor" : id.str;
        }

        public static string serial_key (Polo.Output output) {
            if (output.serial.has_prefix ("0x") && output.serial != "0x00000000") {
                return output.serial.substring (2);
            }
            return sanitize (output.serial_text);
        }

        public static string for_output (Polo.Output output, GLib.GenericArray<Polo.Output> connected) {
            string base_name = base_id (output);
            string key = serial_key (output);
            bool twin = false;
            bool same_key = false;
            foreach (var other in connected) {
                if (other == output || base_id (other) != base_name) {
                    continue;
                }
                twin = true;
                if (key == "" || serial_key (other) == key) {
                    same_key = true;
                }
            }
            if (!twin) {
                return base_name;
            }
            if (!same_key) {
                return "%s-s%s".printf (base_name, key);
            }
            return "%s@%s".printf (base_name, sanitize (output.name));
        }
    }
}
