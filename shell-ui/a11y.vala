/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The popups never take the keyboard, so a screen reader has no focus to
 * follow there: what they show is announced (ATK's notification, AT-SPI's
 * object:announcement, which Orca speaks).
 */

namespace PoloShellUi {
    namespace A11y {
        private const uint OSD_VOLUME = 1;
        private const uint OSD_BRIGHTNESS = 2;
        private const int LIVE_POLITE = 1;

        public void announce (Gtk.Widget widget, string? text) {
            if (text == null || text.strip () == "") {
                return;
            }
            var accessible = widget.get_accessible ();
            if (accessible != null) {
                GLib.Signal.emit_by_name (accessible, "notification", text.strip (), LIVE_POLITE);
            }
        }

        private static string? message_for (string icon) {
            switch (icon) {
                case "input-touchpad":
                    return "Touchpad on";
                case "touchpad-disabled":
                    return "Touchpad off";
                case "media-eject":
                    return "Eject";
                case "airplane-mode-symbolic":
                    return "Airplane mode";
                case "bluetooth-disabled-symbolic":
                    return "Bluetooth off";
                case "bluetooth-active-symbolic":
                    return "Bluetooth on";
                case "video-joined-displays-symbolic":
                    return "Mirrored screens";
                case "video-single-display-symbolic":
                    return "One screen";
                default:
                    return null;
            }
        }

        public string? osd_words (uint kind, string? icon, string? text, int value) {
            if (text != null && text.strip () != "") {
                return value >= 0 ? "%s %d%%".printf (text.strip (), value) : text.strip ();
            }
            string first = icon != null ? icon.split (",")[0] : "";
            if (kind == OSD_VOLUME) {
                string what = first.has_prefix ("microphone") ? "Microphone" : "Volume";
                return first.has_suffix ("muted") ? "%s muted".printf (what) : "%s %d%%".printf (what, value);
            }
            if (kind == OSD_BRIGHTNESS) {
                string what = first.contains ("kbd") ? "Keyboard brightness" : "Brightness";
                return value >= 0 ? "%s %d%%".printf (what, value) : what;
            }
            return message_for (first);
        }
    }
}
