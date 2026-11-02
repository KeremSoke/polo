/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Polo.MarcoPrefsTracker turns raw org.mate.Marco.* values into MarcoPrefs
 * snapshots with the acceptance rules of Marco's prefs.c handlers: an
 * unparsable mouse-button-modifier, titlebar-font or keybinding keeps the
 * previous value, and a change that parses to the same thing informs no one.
 */

namespace Polo {
    [Flags]
    public enum PrefsChange {
        NONE = 0,
        FOCUS,
        RAISE,
        TITLEBAR_ACTIONS,
        BUTTON_LAYOUT,
        TITLEBAR_FONT,
        MOUSE_BUTTONS,
        THEME,
        WORKSPACES,
        KEYBINDINGS,
        PLACEMENT,
        TILING,
        BELL,
        COMPOSITING,
        ALT_TAB,
        OTHER
    }

    public class MarcoPrefsTracker : GLib.Object {
        private GLib.HashTable<string, GLib.Variant> accepted =
            new GLib.HashTable<string, GLib.Variant> (GLib.str_hash, GLib.str_equal);

        public bool rtl { get; private set; }
        public MarcoPrefs current { get; private set; }

        public signal void rejected (string key, string reason);

        public MarcoPrefsTracker (bool rtl) {
            this.rtl = rtl;
            current = MarcoPrefs.defaults ();
        }

        public static PrefsChange category (string key) {
            if (key.has_prefix ("window-keybindings/") || key.has_prefix ("global-keybindings/") ||
                key.has_prefix ("keybinding-commands/")) {
                return PrefsChange.KEYBINDINGS;
            }
            if (key.has_prefix ("workspace-names/")) {
                return PrefsChange.WORKSPACES;
            }
            switch (key) {
                case "general/focus-mode":
                case "general/focus-new-windows":
                    return PrefsChange.FOCUS;
                case "general/raise-on-click":
                case "general/auto-raise":
                case "general/auto-raise-delay":
                    return PrefsChange.RAISE;
                case "general/action-double-click-titlebar":
                case "general/action-middle-click-titlebar":
                case "general/action-right-click-titlebar":
                    return PrefsChange.TITLEBAR_ACTIONS;
                case "general/button-layout":
                    return PrefsChange.BUTTON_LAYOUT;
                case "general/titlebar-font":
                case "general/titlebar-uses-system-font":
                    return PrefsChange.TITLEBAR_FONT;
                case "general/mouse-button-modifier":
                case "general/resize-with-right-button":
                    return PrefsChange.MOUSE_BUTTONS;
                case "general/theme":
                    return PrefsChange.THEME;
                case "general/num-workspaces":
                case "general/wrap-style":
                    return PrefsChange.WORKSPACES;
                case "general/placement-mode":
                case "general/center-new-windows":
                    return PrefsChange.PLACEMENT;
                case "general/allow-tiling":
                case "general/allow-top-tiling":
                case "general/allow-tile-cycling":
                    return PrefsChange.TILING;
                case "general/visual-bell":
                case "general/audible-bell":
                case "general/visual-bell-type":
                    return PrefsChange.BELL;
                case "general/compositing-manager":
                case "general/compositing-fast-alt-tab":
                case "general/reduced-resources":
                    return PrefsChange.COMPOSITING;
                case "general/show-tab-border":
                case "general/alt-tab-max-columns":
                case "general/alt-tab-expand-to-fit-title":
                case "general/alt-tab-raise-windows":
                    return PrefsChange.ALT_TAB;
                default:
                    return PrefsChange.OTHER;
            }
        }

        private static bool same_layout (string a, string b, bool rtl) {
            Marco.ButtonLayout la, lb;
            Marco.parse_button_layout (a, rtl, out la);
            Marco.parse_button_layout (b, rtl, out lb);
            return la.equal (lb);
        }

        private static bool same_font (string a, string b) {
            return Marco.parse_font (a) == Marco.parse_font (b);
        }

        private static bool valid (string key, GLib.Variant value) {
            if (key == "general/num-workspaces") {
                return value.get_int32 () >= 1 && value.get_int32 () <= 36;
            }
            if (!value.is_of_type (GLib.VariantType.STRING)) {
                return true;
            }
            unowned string text = value.get_string ();
            if (key == "general/mouse-button-modifier") {
                Marco.VirtualModifier mask;
                return Marco.parse_modifier (text, out mask);
            }
            if (key == "general/titlebar-font") {
                return Marco.parse_font (text) != null;
            }
            if (key.has_prefix ("window-keybindings/") || key.has_prefix ("global-keybindings/")) {
                string? name;
                uint keycode;
                Marco.VirtualModifier mask;
                return Marco.parse_accelerator (text, out name, out keycode, out mask);
            }
            return true;
        }

        /* Applies new raw values; returns what changed for listeners. */
        public PrefsChange update (GLib.HashTable<string, GLib.Variant> changes) {
            var mask = PrefsChange.NONE;
            foreach (unowned string key in changes.get_keys ()) {
                var value = changes[key];
                var old = current.get_value (key);
                if (old == null) {
                    rejected (key, "unknown key");
                    continue;
                }
                if (!value.get_type ().equal (old.get_type ())) {
                    rejected (key, "wants %s, got %s".printf (old.get_type_string (),
                                                              value.get_type_string ()));
                    continue;
                }
                if (!valid (key, value)) {
                    rejected (key, "%s is not a valid value".printf (value.print (false)));
                    continue;
                }
                var previous = accepted.lookup (key);
                accepted.insert (key, value);
                if (value.equal (old)) {
                    continue;
                }
                if (key == "general/button-layout" &&
                    same_layout (value.get_string (), old.get_string (), rtl)) {
                    continue;
                }
                if (key == "general/titlebar-font" &&
                    same_font (value.get_string (), old.get_string ())) {
                    continue;
                }
                try {
                    current = MarcoPrefs.with_overrides (accepted);
                } catch (PrefsError e) {
                    rejected (key, e.message);
                    if (previous != null) {
                        accepted.insert (key, previous);
                    } else {
                        accepted.remove (key);
                    }
                    continue;
                }
                mask |= category (key);
            }
            return mask;
        }
    }
}
