/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * mate-settings-daemon 1.28's OSD (msd-osd-window.c and
 * msd-media-keys-window.c, composited): a square of 110 pixels on a 640x480
 * screen, scaled up with the monitor, in the middle of its lower half. The
 * icon takes 55% of it, the level a bar under it, the text a strip at the
 * bottom. It stays for two seconds, fading after one and a half, and never
 * takes input.
 */

namespace PoloShellUi {
    public class Osd : GLib.Object {
        private const uint DIALOG_TIMEOUT = 2000;
        private const uint DIALOG_FADE_TIMEOUT = 1500;
        private const uint FADE_TIMEOUT = 10;
        private const double ICON_SCALE = 0.55;
        private const double BG_ALPHA = 0.75;

        private Client client;
        private Gtk.Window? window = null;
        private Gtk.DrawingArea area;
        private string? icon = null;
        private string? text = null;
        private int value = -1;
        private double alpha = 1.0;
        private uint hide_id = 0;
        private uint fade_id = 0;

        public Osd (Client client) {
            this.client = client;
            client.show_osd.connect (on_show_osd);
        }

        private void on_show_osd (uint kind, Gdk.Monitor? monitor, string? icon, string? text, int value) {
            this.icon = icon;
            this.text = text;
            this.value = value;
            var target = monitor ?? Gdk.Display.get_default ().get_monitor (0);
            if (target == null) {
                return;
            }
            if (window == null) {
                build ();
            }
            place (target);
            alpha = 1.0;
            window.opacity = 1.0;
            area.queue_draw ();
            window.show_all ();
            restart_timers ();
        }

        private void build () {
            window = new Gtk.Window (Gtk.WindowType.TOPLEVEL);
            GtkLayerShell.init_for_window (window);
            GtkLayerShell.set_layer (window, GtkLayerShell.Layer.OVERLAY);
            GtkLayerShell.set_namespace (window, "polo-osd");
            GtkLayerShell.set_keyboard_mode (window, GtkLayerShell.KeyboardMode.NONE);
            GtkLayerShell.set_exclusive_zone (window, -1);
            GtkLayerShell.set_anchor (window, GtkLayerShell.Edge.TOP, true);
            GtkLayerShell.set_anchor (window, GtkLayerShell.Edge.LEFT, true);
            window.app_paintable = true;
            window.get_style_context ().add_class (Gtk.STYLE_CLASS_OSD);
            window.get_style_context ().add_class ("window-frame");
            var visual = window.get_screen ().get_rgba_visual ();
            window.set_visual (visual ?? window.get_screen ().get_system_visual ());
            area = new Gtk.DrawingArea ();
            area.draw.connect (draw);
            window.add (area);
            window.realize.connect (() => {
                window.input_shape_combine_region (new Cairo.Region ());
            });
        }

        private void place (Gdk.Monitor monitor) {
            var geometry = monitor.get_geometry ();
            double scale = double.min (geometry.width / 640.0, geometry.height / 480.0);
            int size = (int) (110 * double.max (1, scale));
            GtkLayerShell.set_monitor (window, monitor);
            GtkLayerShell.set_margin (window, GtkLayerShell.Edge.LEFT, (geometry.width - size) / 2);
            GtkLayerShell.set_margin (window, GtkLayerShell.Edge.TOP,
                                      geometry.height / 2 + (geometry.height / 2 - size) / 2);
            area.set_size_request (size, size);
            window.resize (size, size);
        }

        private void restart_timers () {
            if (hide_id != 0) {
                GLib.Source.remove (hide_id);
            }
            if (fade_id != 0) {
                GLib.Source.remove (fade_id);
                fade_id = 0;
            }
            hide_id = GLib.Timeout.add (DIALOG_FADE_TIMEOUT, () => {
                hide_id = 0;
                fade_id = GLib.Timeout.add (FADE_TIMEOUT, fade);
                return GLib.Source.REMOVE;
            });
        }

        private bool fade () {
            alpha -= (double) FADE_TIMEOUT / (DIALOG_TIMEOUT - DIALOG_FADE_TIMEOUT);
            if (alpha <= 0) {
                fade_id = 0;
                window.hide ();
                return GLib.Source.REMOVE;
            }
            window.opacity = alpha;
            return GLib.Source.CONTINUE;
        }

        private static void rounded (Cairo.Context cr, double x, double y, double width, double height,
                                     double radius) {
            cr.new_sub_path ();
            cr.arc (x + width - radius, y + radius, radius, -Math.PI / 2, 0);
            cr.arc (x + width - radius, y + height - radius, radius, 0, Math.PI / 2);
            cr.arc (x + radius, y + height - radius, radius, Math.PI / 2, Math.PI);
            cr.arc (x + radius, y + radius, radius, Math.PI, 3 * Math.PI / 2);
            cr.close_path ();
        }

        private bool draw (Gtk.Widget widget, Cairo.Context cr) {
            int width = widget.get_allocated_width ();
            int height = widget.get_allocated_height ();
            var style = window.get_style_context ();
            var background = (Gdk.RGBA) style.get_property ("background-color", style.get_state ());
            var foreground = style.get_color (style.get_state ());

            cr.set_operator (Cairo.Operator.SOURCE);
            cr.set_source_rgba (0, 0, 0, 0);
            cr.paint ();
            cr.set_operator (Cairo.Operator.OVER);
            rounded (cr, 0.5, 0.5, width - 1, height - 1, height / 10.0);
            cr.set_source_rgba (background.red, background.green, background.blue, BG_ALPHA);
            cr.fill_preserve ();
            cr.set_source_rgba (foreground.red, foreground.green, foreground.blue, BG_ALPHA / 2);
            cr.set_line_width (1);
            cr.stroke ();

            double icon_box = Math.round (width * ICON_SCALE);
            double icon_x = Math.round ((width - icon_box) / 2);
            double icon_y = Math.round ((height - icon_box) / 2);
            if (text != null && text != "") {
                icon_y = Math.round (icon_y - height * 0.175 / 2);
            }
            draw_icon (widget, cr, icon_x, icon_y, (int) icon_box);

            if (value >= 0) {
                double bar_height = Math.round (height * 0.05);
                double bar_y = Math.round (height - icon_y / 2 - bar_height);
                cr.set_source_rgba (foreground.red, foreground.green, foreground.blue, 0.25);
                cr.rectangle (icon_x, bar_y, icon_box, bar_height);
                cr.fill ();
                cr.set_source_rgba (foreground.red, foreground.green, foreground.blue, 1.0);
                cr.rectangle (icon_x, bar_y, icon_box * int.min (value, 100) / 100.0, bar_height);
                cr.fill ();
            }
            if (text != null && text != "") {
                var layout = widget.create_pango_layout (text);
                layout.set_width ((int) (width * 0.9 * Pango.SCALE));
                layout.set_alignment (Pango.Alignment.CENTER);
                layout.set_ellipsize (Pango.EllipsizeMode.END);
                int text_width, text_height;
                layout.get_pixel_size (out text_width, out text_height);
                double label_box = Math.round (height * 0.175);
                double label_y = height - label_box / 2 - label_box;
                cr.set_source_rgba (foreground.red, foreground.green, foreground.blue, 1.0);
                cr.move_to (width * 0.05, label_y + (label_box - text_height) / 2);
                Pango.cairo_show_layout (cr, layout);
            }
            return true;
        }

        private void draw_icon (Gtk.Widget widget, Cairo.Context cr, double x, double y, int size) {
            if (icon == null) {
                return;
            }
            var theme = Gtk.IconTheme.get_for_screen (widget.get_screen ());
            int scale = widget.get_scale_factor ();
            var info = theme.lookup_by_gicon_for_scale (new GLib.ThemedIcon.from_names (icon.split (",")), size,
                                                        scale, Gtk.IconLookupFlags.FORCE_SIZE);
            if (info == null) {
                return;
            }
            try {
                var pixbuf = info.load_icon ();
                var surface = Gdk.cairo_surface_create_from_pixbuf (pixbuf, scale, widget.get_window ());
                cr.set_source_surface (surface, x, y);
                cr.paint ();
            } catch (GLib.Error e) {
                debug ("OSD icon %s: %s", icon, e.message);
            }
        }
    }
}
