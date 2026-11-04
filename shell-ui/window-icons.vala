/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

namespace PoloShellUi {
    /* The icons Polo's frames draw, loaded as Marco's ui.c loads them. */
    public class WindowIcons : GLib.Object {
        /* META_DEFAULT_ICON_NAME */
        private const string MARCO_DEFAULT_ICON = "preferences-desktop-theme";

        private Client client;

        public WindowIcons (Client client) {
            this.client = client;
            client.lookup_icon.connect (on_lookup_icon);
            Gtk.IconTheme.get_default ().changed.connect (on_theme_changed);
        }

        private void on_theme_changed () {
            client.icon_theme_changed ();
        }

        private void on_lookup_icon (uint serial, string? name, string? desktop_id, int mini_size, int size,
                                     int scale, bool fallback) {
            var theme = Gtk.IconTheme.get_default ();
            Gdk.Pixbuf? mini = null;
            Gdk.Pixbuf? full = null;
            GLib.Icon? gicon = null;
            if (desktop_id != null && desktop_id.has_suffix (".desktop")) {
                var info = new GLib.DesktopAppInfo (desktop_id);
                gicon = info != null ? info.get_icon () : null;
            }
            try {
                if (name != null && theme.has_icon (name)) {
                    mini = theme.load_icon_for_scale (name, mini_size, scale, Gtk.IconLookupFlags.FORCE_SIZE);
                    full = theme.load_icon_for_scale (name, size, scale, Gtk.IconLookupFlags.FORCE_SIZE);
                } else if (gicon != null) {
                    mini = from_gicon (theme, gicon, mini_size, scale);
                    full = from_gicon (theme, gicon, size, scale);
                }
                if ((mini == null || full == null) && fallback) {
                    string fallback_name = theme.has_icon (MARCO_DEFAULT_ICON) ? MARCO_DEFAULT_ICON : "image-missing";
                    mini = theme.load_icon_for_scale (fallback_name, mini_size, scale, 0);
                    full = theme.load_icon_for_scale (fallback_name, size, scale, 0);
                }
            } catch (GLib.Error e) {
                mini = null;
            }
            if (mini == null || full == null) {
                client.no_icon (serial);
                return;
            }
            client.icon (serial, surface_of (mini), surface_of (full));
        }

        private static Gdk.Pixbuf? from_gicon (Gtk.IconTheme theme, GLib.Icon gicon, int size, int scale)
            throws GLib.Error {
            var info = theme.lookup_by_gicon_for_scale (gicon, size, scale, Gtk.IconLookupFlags.FORCE_SIZE);
            return info != null ? info.load_icon () : null;
        }

        private static Cairo.ImageSurface surface_of (Gdk.Pixbuf pixbuf) {
            var surface = new Cairo.ImageSurface (Cairo.Format.ARGB32, pixbuf.width, pixbuf.height);
            var cr = new Cairo.Context (surface);
            Gdk.cairo_set_source_pixbuf (cr, pixbuf, 0, 0);
            cr.paint ();
            return surface;
        }
    }
}
