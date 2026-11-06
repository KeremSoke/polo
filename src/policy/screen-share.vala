/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The sources xdg-desktop-portal-wlr 0.8 offers its chooser, one per line:
 * "Monitor: <output name>[ <description>]" and "Window: <title>
 * (<ext-foreign-toplevel identifier>)", or "Window: <identifier>" for an
 * untitled window. The chooser answers with the line it was given.
 */

namespace Polo.Policy {
    public class ShareSource : GLib.Object {
        public enum Kind {
            MONITOR,
            WINDOW
        }

        private const string MONITOR_PREFIX = "Monitor: ";
        private const string WINDOW_PREFIX = "Window: ";

        public string line { get; construct; }
        public Kind kind { get; construct; }
        public string key { get; construct; }
        public string label { get; construct; }

        private ShareSource (string line, Kind kind, string key, string label) {
            Object (line: line, kind: kind, key: key, label: label);
        }

        public static ShareSource? parse (string line) {
            if (line.has_prefix (MONITOR_PREFIX)) {
                var parts = line.substring (MONITOR_PREFIX.length).strip ().split (" ", 2);
                if (parts.length == 0 || parts[0] == "") {
                    return null;
                }
                string description = parts.length > 1 ? parts[1].strip () : "";
                return new ShareSource (line, Kind.MONITOR, parts[0], description != "" ? description : parts[0]);
            }
            if (!line.has_prefix (WINDOW_PREFIX)) {
                return null;
            }
            string rest = line.substring (WINDOW_PREFIX.length).strip ();
            int open = rest.last_index_of (" (");
            if (rest.has_suffix (")") && open > 0) {
                string identifier = rest.substring (open + 2, rest.length - open - 3);
                if (identifier != "") {
                    return new ShareSource (line, Kind.WINDOW, identifier, rest.substring (0, open));
                }
            }
            if (rest == "") {
                return null;
            }
            return new ShareSource (line, Kind.WINDOW, rest, rest);
        }
    }
}
