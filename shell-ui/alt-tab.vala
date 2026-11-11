/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * GTK implementation of Marco's Alt-Tab popup. It uses a centred layer-shell
 * overlay for the output Polo selects. Polo owns the selection; this process
 * draws it and reports entry clicks. The popup uses OSD styling because Polo
 * always composites.
 */

namespace PoloShellUi {
    private const int OUTSIDE_SELECT_RECT = 8;
    private const int INSIDE_SELECT_RECT = 8;
    private const int ICON_OFFSET = 6;
    private const int MINI_ICON_SIZE = 16;
    private const string DEFAULT_ICON_NAME = "preferences-desktop-theme";
    private const uint FLAG_BLANK = 1;
    private const uint FLAG_HIDDEN = 2;
    private const uint FLAG_DEMANDS_ATTENTION = 4;

    /* Marco's theme.c HLS shading helpers. */
    namespace Style {
        private const double LIGHTNESS_MULT = 1.3;
        private const double DARKNESS_MULT = 0.7;

        private void rgb_to_hls (ref double r, ref double g, ref double b) {
            double red = r, green = g, blue = b;
            double max, min;
            if (red > green) {
                max = red > blue ? red : blue;
                min = green < blue ? green : blue;
            } else {
                max = green > blue ? green : blue;
                min = red < blue ? red : blue;
            }
            double l = (max + min) / 2;
            double s = 0;
            double h = 0;
            if (max != min) {
                s = l <= 0.5 ? (max - min) / (max + min) : (max - min) / (2 - max - min);
                double delta = max - min;
                if (red == max) {
                    h = (green - blue) / delta;
                } else if (green == max) {
                    h = 2 + (blue - red) / delta;
                } else if (blue == max) {
                    h = 4 + (red - green) / delta;
                }
                h *= 60;
                if (h < 0.0) {
                    h += 360;
                }
            }
            r = h;
            g = l;
            b = s;
        }

        private double hue_value (double m1, double m2, double hue) {
            while (hue > 360) {
                hue -= 360;
            }
            while (hue < 0) {
                hue += 360;
            }
            if (hue < 60) {
                return m1 + (m2 - m1) * hue / 60;
            } else if (hue < 180) {
                return m2;
            } else if (hue < 240) {
                return m1 + (m2 - m1) * (240 - hue) / 60;
            }
            return m1;
        }

        private void hls_to_rgb (ref double h, ref double l, ref double s) {
            double lightness = l, saturation = s;
            double m2 = lightness <= 0.5 ? lightness * (1 + saturation)
                                         : lightness + saturation - lightness * saturation;
            double m1 = 2 * lightness - m2;
            if (saturation == 0) {
                h = lightness;
                l = lightness;
                s = lightness;
                return;
            }
            double r = hue_value (m1, m2, h + 120);
            double g = hue_value (m1, m2, h);
            double b = hue_value (m1, m2, h - 120);
            h = r;
            l = g;
            s = b;
        }

        private Gdk.RGBA shade (Gdk.RGBA a, double k) {
            double red = a.red, green = a.green, blue = a.blue;
            rgb_to_hls (ref red, ref green, ref blue);
            green = (green * k).clamp (0.0, 1.0);
            blue = (blue * k).clamp (0.0, 1.0);
            hls_to_rgb (ref red, ref green, ref blue);
            return { red, green, blue, a.alpha };
        }

        public Gdk.RGBA background (Gtk.StyleContext context, Gtk.StateFlags state) {
            Gdk.RGBA? color = null;
            context.get (state, "background-color", out color);
            if (color.red == 0 && color.green == 0 && color.blue == 0 && color.alpha == 0) {
                var toplevel = new Gtk.Window (Gtk.WindowType.TOPLEVEL);
                toplevel.get_style_context ().get (state, "background-color", out color);
                toplevel.destroy ();
            }
            return color;
        }

        public Gdk.RGBA light (Gtk.StyleContext context, Gtk.StateFlags state) {
            return shade (background (context, state), LIGHTNESS_MULT);
        }

        public Gdk.RGBA dark (Gtk.StyleContext context, Gtk.StateFlags state) {
            return shade (background (context, state), DARKNESS_MULT);
        }
    }

    private class SelectImage : Gtk.Image {
        public bool selected { get; set; default = false; }

        public SelectImage (Cairo.Surface surface) {
            Object (surface: surface);
            notify["selected"].connect (() => queue_draw ());
        }

        public override void get_preferred_width (out int minimum, out int natural) {
            base.get_preferred_width (out minimum, out natural);
            minimum += (INSIDE_SELECT_RECT + OUTSIDE_SELECT_RECT) * 2;
            natural += (INSIDE_SELECT_RECT + OUTSIDE_SELECT_RECT) * 2;
        }

        public override void get_preferred_height (out int minimum, out int natural) {
            base.get_preferred_height (out minimum, out natural);
            minimum += (INSIDE_SELECT_RECT + OUTSIDE_SELECT_RECT) * 2;
            natural += (INSIDE_SELECT_RECT + OUTSIDE_SELECT_RECT) * 2;
        }

        public override bool draw (Cairo.Context cr) {
            var context = get_style_context ();
            if (selected) {
                Gtk.Requisition requisition;
                get_preferred_size (out requisition, null);
                int x = INSIDE_SELECT_RECT;
                int y = INSIDE_SELECT_RECT;
                int w = requisition.width - OUTSIDE_SELECT_RECT * 2;
                int h = requisition.height - OUTSIDE_SELECT_RECT * 2;
                context.set_state (Gtk.StateFlags.SELECTED);
                var color = Style.light (context, Gtk.StateFlags.SELECTED);
                cr.set_line_width (256.0);
                cr.set_source_rgb (color.red, color.green, color.blue);
                cr.rectangle (x, y, w, h);
                cr.stroke ();
                cr.set_line_width (1.0);
            } else {
                context.set_state (Gtk.StateFlags.NORMAL);
            }
            return base.draw (cr);
        }
    }

    private class TabEntry {
        public uint id;
        public string title;
        public string? app_id;
        public uint flags;
        public Cairo.ImageSurface? thumbnail = null;
        public string? icon_name = null;
        public Cairo.ImageSurface? icon_pixels = null;
        public string? markup = null;
        public SelectImage? widget = null;
        public SelectWorkspace? workspace = null;
        public GLib.GenericArray<WorkspaceWindow> windows = new GLib.GenericArray<WorkspaceWindow> ();
    }

    private class WorkspaceWindow {
        public int x;
        public int y;
        public int width;
        public int height;
        public bool focused;
        public string? app_id;
        private Gdk.Pixbuf? icon = null;
        private Gdk.Pixbuf? mini_icon = null;

        public Gdk.Pixbuf? get_icon (int size) {
            if (icon == null) {
                icon = TabPopup.load_icon (app_id, size, 1);
            }
            return icon;
        }

        public Gdk.Pixbuf? get_mini_icon () {
            if (mini_icon == null) {
                mini_icon = TabPopup.load_icon (app_id, MINI_ICON_SIZE, 1);
            }
            return mini_icon;
        }
    }

    /* Extra state needed to render workspace previews. */
    private class WorkspacePopupInfo {
        public uint active;
        public int screen_width;
        public int screen_height;
        public int output_width;
        public int output_height;
        public int icon_size;
    }

    /* Workspace preview, following Marco's draw-workspace.c. */
    private class SelectWorkspace : Gtk.DrawingArea {
        private const int SELECT_OUTLINE_WIDTH = 2;
        private const double MINI_WORKSPACE_SCREEN_FRACTION = 0.33;

        public bool selected = false;
        private TabEntry entry;
        private WorkspacePopupInfo info;

        public SelectWorkspace (TabEntry entry, WorkspacePopupInfo info, int entry_count) {
            this.entry = entry;
            this.info = info;
            int width, height;
            if (info.screen_width < info.screen_height) {
                double ratio = (double) info.screen_width / (double) info.screen_height;
                height = (int) ((double) info.output_height * MINI_WORKSPACE_SCREEN_FRACTION / entry_count);
                width = (int) ((double) height * ratio);
            } else {
                double ratio = (double) info.screen_height / (double) info.screen_width;
                width = (int) ((double) info.output_width * MINI_WORKSPACE_SCREEN_FRACTION / entry_count);
                height = (int) ((double) width * ratio);
            }
            set_size_request (width + SELECT_OUTLINE_WIDTH * 2, height + SELECT_OUTLINE_WIDTH * 2);
        }

        private void draw_window (Cairo.Context cr, WorkspaceWindow win, int rx, int ry, int rw, int rh,
                                  Gtk.StateFlags state) {
            var style = get_style_context ();
            cr.save ();
            cr.rectangle (rx, ry, rw, rh);
            cr.clip ();
            Gdk.cairo_set_source_rgba (cr, win.focused ? Style.light (style, state) : Style.background (style, state));
            cr.rectangle (rx + 1, ry + 1, int.max (0, rw - 2), int.max (0, rh - 2));
            cr.fill ();

            int scale = get_scale_factor ();
            Gdk.Pixbuf? pixbuf = win.get_icon (info.icon_size);
            if (pixbuf != null && (pixbuf.width / scale > rw - 2 || pixbuf.height / scale > rh - 2)) {
                pixbuf = win.get_mini_icon ();
                if (pixbuf != null && (pixbuf.width / scale > rw - 2 || pixbuf.height / scale > rh - 2)) {
                    pixbuf = null;
                }
            }
            if (pixbuf != null) {
                var icon = Gdk.cairo_surface_create_from_pixbuf (pixbuf, scale, null);
                int icon_w = pixbuf.width / scale;
                int icon_h = pixbuf.height / scale;
                int icon_x = rx + (rw - icon_w) / 2;
                int icon_y = ry + (rh - icon_h) / 2;
                cr.save ();
                cr.set_source_surface (icon, icon_x, icon_y);
                cr.rectangle (icon_x, icon_y, icon_w, icon_h);
                cr.clip ();
                cr.paint ();
                cr.restore ();
            }

            Gdk.cairo_set_source_rgba (cr, style.get_color (state));
            cr.set_line_width (1.0);
            cr.rectangle (rx + 0.5, ry + 0.5, int.max (0, rw - 1), int.max (0, rh - 1));
            cr.stroke ();
            cr.restore ();
        }

        public override bool draw (Cairo.Context cr) {
            Gtk.Allocation allocation;
            get_allocation (out allocation);
            int x = SELECT_OUTLINE_WIDTH;
            int y = SELECT_OUTLINE_WIDTH;
            int width = allocation.width - SELECT_OUTLINE_WIDTH * 2;
            int height = allocation.height - SELECT_OUTLINE_WIDTH * 2;
            var state = entry.id == info.active ? Gtk.StateFlags.SELECTED : Gtk.StateFlags.NORMAL;
            var style = get_style_context ();

            cr.save ();
            var color = Style.dark (style, state);
            color.alpha = 0.25;
            Gdk.cairo_set_source_rgba (cr, color);
            cr.rectangle (x, y, width, height);
            cr.fill ();

            double width_ratio = (double) width / (double) info.screen_width;
            double height_ratio = (double) height / (double) info.screen_height;
            foreach (var win in entry.windows) {
                int rx = (int) (width_ratio * (double) win.x) + x;
                int ry = (int) (height_ratio * (double) win.y) + y;
                int rw = int.max ((int) (width_ratio * (double) win.width), 3);
                int rh = int.max ((int) (height_ratio * (double) win.height), 3);
                draw_window (cr, win, rx, ry, rw, rh, state);
            }
            cr.restore ();

            if (selected) {
                style.set_state (get_state_flags ());
                var outline = Style.light (style, Gtk.StateFlags.SELECTED);
                cr.set_line_width (SELECT_OUTLINE_WIDTH);
                cr.set_source_rgb (outline.red, outline.green, outline.blue);
                cr.rectangle (SELECT_OUTLINE_WIDTH / 2.0, SELECT_OUTLINE_WIDTH / 2.0,
                              allocation.width - SELECT_OUTLINE_WIDTH, allocation.height - SELECT_OUTLINE_WIDTH);
                cr.stroke ();
            }
            return true;
        }
    }

    private class TabPopup : GLib.Object {
        private Client client;
        private Gtk.Window window;
        private Gtk.Label label;
        private GLib.GenericArray<TabEntry> entries;
        private TabEntry? current = null;
        private bool mouse_pressed = false;
        private WorkspacePopupInfo? workspace_info;

        public TabPopup (Client client, GLib.GenericArray<TabEntry> entries, Gdk.Monitor? monitor,
                         uint selected, int width, bool expand_for_titles, int icon_size,
                         WorkspacePopupInfo? workspace_info = null, string? heading = null) {
            this.client = client;
            this.workspace_info = workspace_info;
            this.entries = entries;
            window = new Gtk.Window (Gtk.WindowType.TOPLEVEL);
            GtkLayerShell.init_for_window (window);
            GtkLayerShell.set_layer (window, GtkLayerShell.Layer.OVERLAY);
            GtkLayerShell.set_namespace (window, "polo-switcher");
            GtkLayerShell.set_keyboard_mode (window, GtkLayerShell.KeyboardMode.NONE);
            if (monitor != null) {
                GtkLayerShell.set_monitor (window, monitor);
            }
            window.resizable = true;
            window.get_style_context ().add_class (Gtk.STYLE_CLASS_OSD);
            var visual = window.get_screen ().get_rgba_visual ();
            window.set_visual (visual ?? window.get_screen ().get_system_visual ());
            window.add_events (Gdk.EventMask.BUTTON_PRESS_MASK | Gdk.EventMask.BUTTON_RELEASE_MASK |
                               Gdk.EventMask.POINTER_MOTION_MASK);
            window.button_press_event.connect (on_button_press);
            window.button_release_event.connect (on_button_release);
            window.motion_notify_event.connect (on_motion);

            int scale = window.get_scale_factor ();
            var foreground = window.get_style_context ().get_color (Gtk.StateFlags.NORMAL);
            foreach (var entry in entries) {
                entry.markup = title_markup (entry);
            }

            width = int.max (width, 1);
            int height = (int) entries.length / width;
            if (entries.length % width != 0) {
                height += 1;
            }

            var vbox = new Gtk.Box (Gtk.Orientation.VERTICAL, 0);
            var grid = new Gtk.Grid ();
            var frame = new Gtk.Frame (null);
            frame.shadow_type = Gtk.ShadowType.NONE;
            grid.border_width = 16;
            window.add (frame);
            frame.add (vbox);
            if (heading != null) {
                var title = new Gtk.Label (null);
                title.set_markup ("<b>%s</b>".printf (GLib.Markup.escape_text (heading)));
                title.margin_start = 16;
                title.margin_end = 16;
                title.margin_top = 16;
                title.wrap = true;
                vbox.pack_start (title, false, false, 0);
            }
            vbox.pack_start (grid, true, true, 0);

            label = new Gtk.Label ("");
            label.margin_start = 16;
            label.margin_end = 16;
            label.margin_top = 0;
            label.margin_bottom = 16;
            vbox.pack_end (label, false, false, 0);

            int max_label_width = 0;
            int top = 0;
            int left = 0;
            int index = 0;
            label.show ();
            while (index < entries.length && top < height) {
                left = 0;
                while (index < entries.length && left < width) {
                    var entry = entries[index];
                    Gtk.Widget image;
                    if ((entry.flags & FLAG_BLANK) != 0) {
                        image = new Gtk.Label ("");
                    } else if (workspace_info != null) {
                        entry.workspace = new SelectWorkspace (entry, workspace_info, (int) entries.length);
                        image = entry.workspace;
                    } else {
                        var select = new SelectImage (picture (entry, icon_size, scale, foreground));
                        select.halign = Gtk.Align.CENTER;
                        select.valign = Gtk.Align.CENTER;
                        entry.widget = select;
                        image = select;
                    }
                    grid.attach (image, left, top, 1, 1);
                    label.set_markup (entry.markup);
                    Gtk.Requisition req;
                    label.get_preferred_size (out req, null);
                    max_label_width = int.max (max_label_width, req.width);
                    index++;
                    left++;
                }
                top++;
            }

            label.set_text ("");
            label.ellipsize = Pango.EllipsizeMode.END;
            label.get_accessible ().set_role (Atk.Role.STATUSBAR);

            int default_window_width = 0;
            if (expand_for_titles && top <= 1 && left < width) {
                var dummies = new Gtk.Widget[width - left];
                for (int j = 0; j < width - left; j++) {
                    dummies[j] = new Gtk.Label ("");
                    grid.attach (dummies[j], left + j, top, 1, 1);
                }
                grid.column_homogeneous = true;
                grid.halign = Gtk.Align.CENTER;
                grid.show_all ();
                Gtk.Requisition req;
                grid.get_preferred_size (out req, null);
                default_window_width = req.width;
                foreach (var dummy in dummies) {
                    grid.remove (dummy);
                }
                if (max_label_width < default_window_width) {
                    default_window_width = max_label_width;
                }
            }
            window.set_default_size (default_window_width, -1);

            select (selected);
            window.show_all ();
        }

        public void destroy () {
            foreach (var entry in entries) {
                entry.workspace = null;
                entry.widget = null;
            }
            window.destroy ();
        }

        public TabEntry? entry_with_id (uint id) {
            foreach (var entry in entries) {
                if (entry.id == id) {
                    return entry;
                }
            }
            return null;
        }

        public void update_workspaces (uint selected, uint active) {
            workspace_info.active = active;
            select (selected);
            foreach (var entry in entries) {
                if (entry.workspace != null) {
                    entry.workspace.queue_draw ();
                }
            }
        }

        private static string title_markup (TabEntry entry) {
            string title = entry.title.char_count () > 4096 ? entry.title.substring (0, entry.title.index_of_nth_char (4096))
                                                             : entry.title;
            string markup = GLib.Markup.printf_escaped ((entry.flags & FLAG_HIDDEN) != 0 ? "[%s]" : "%s", title);
            if ((entry.flags & FLAG_DEMANDS_ATTENTION) != 0) {
                markup = "<b>%s</b>".printf (markup);
            }
            return markup;
        }

        private static Gdk.Pixbuf? entry_icon (TabEntry entry, int size, int scale, Gdk.RGBA foreground) {
            var theme = Gtk.IconTheme.get_default ();
            if (entry.icon_name != null && theme.has_icon (entry.icon_name)) {
                try {
                    if (entry.icon_name.has_suffix ("-symbolic")) {
                        var info = theme.lookup_icon_for_scale (entry.icon_name, size, scale,
                                                                Gtk.IconLookupFlags.FORCE_SIZE);
                        bool was_symbolic;
                        if (info != null) {
                            return info.load_symbolic (foreground, null, null, null, out was_symbolic);
                        }
                    }
                    return theme.load_icon_for_scale (entry.icon_name, size, scale,
                                                      Gtk.IconLookupFlags.FORCE_SIZE);
                } catch (GLib.Error e) {
                }
            }
            if (entry.icon_pixels != null) {
                int side = entry.icon_pixels.get_width ();
                var pixels = Gdk.pixbuf_get_from_surface (entry.icon_pixels, 0, 0, side, side);
                if (pixels != null) {
                    return pixels.scale_simple (size * scale, size * scale,
                                                Gdk.InterpType.BILINEAR);
                }
            }
            return load_icon (entry.app_id, size, scale);
        }

        public static Gdk.Pixbuf? load_icon (string? app_id, int size, int scale) {
            var theme = Gtk.IconTheme.get_default ();
            var flags = Gtk.IconLookupFlags.FORCE_SIZE;
            try {
                if (app_id != null) {
                    var info = new GLib.DesktopAppInfo (app_id + ".desktop");
                    var gicon = info != null ? info.get_icon () : null;
                    if (gicon != null) {
                        var found = theme.lookup_by_gicon_for_scale (gicon, size, scale, flags);
                        if (found != null) {
                            return found.load_icon ();
                        }
                    }
                    if (theme.has_icon (app_id)) {
                        return theme.load_icon_for_scale (app_id, size, scale, flags);
                    }
                }
                string name = theme.has_icon (DEFAULT_ICON_NAME) ? DEFAULT_ICON_NAME : "image-missing";
                return theme.load_icon_for_scale (name, size, scale, flags);
            } catch (GLib.Error e) {
                return null;
            }
        }

        private static Gdk.Pixbuf dim (Gdk.Pixbuf pixbuf) {
            var dimmed = pixbuf.has_alpha ? pixbuf.copy () : pixbuf.add_alpha (false, 0, 0, 0);
            unowned uint8[] pixels = dimmed.get_pixels_with_length ();
            int rowstride = dimmed.rowstride;
            for (int y = 0; y < dimmed.height; y++) {
                for (int x = 0; x < dimmed.width; x++) {
                    pixels[y * rowstride + x * 4 + 3] /= 2;
                }
            }
            return dimmed;
        }

        private static Cairo.Surface picture (TabEntry entry, int icon_size, int scale, Gdk.RGBA foreground) {
            var pixbuf = entry_icon (entry, icon_size, scale, foreground);
            if (pixbuf == null) {
                return new Cairo.ImageSurface (Cairo.Format.ARGB32, icon_size, icon_size);
            }
            if ((entry.flags & FLAG_HIDDEN) != 0) {
                return Gdk.cairo_surface_create_from_pixbuf (dim (pixbuf), scale, null);
            }
            var icon = Gdk.cairo_surface_create_from_pixbuf (pixbuf, scale, null);
            if (entry.thumbnail == null) {
                return icon;
            }
            int width = entry.thumbnail.get_width ();
            int height = entry.thumbnail.get_height ();
            var surface = new Cairo.ImageSurface (Cairo.Format.ARGB32, width, height);
            var cr = new Cairo.Context (surface);
            cr.set_source_surface (entry.thumbnail, 0, 0);
            cr.paint ();
            int icon_width = pixbuf.width / scale;
            int icon_height = pixbuf.height / scale;
            cr.set_operator (Cairo.Operator.OVER);
            cr.set_source_surface (icon, width - icon_width - ICON_OFFSET, height - icon_height - ICON_OFFSET);
            cr.paint ();
            return surface;
        }

        public void select (uint id) {
            foreach (var entry in entries) {
                if (entry.id == id) {
                    if (current != null && current.widget != null) {
                        current.widget.selected = false;
                    }
                    if (current != null && current.workspace != null) {
                        current.workspace.selected = false;
                        current.workspace.queue_draw ();
                    }
                    label.set_markup (entry.markup);
                    A11y.announce (label, label.get_text ());
                    if (entry.widget != null) {
                        entry.widget.selected = true;
                    }
                    if (entry.workspace != null) {
                        entry.workspace.selected = true;
                        entry.workspace.queue_draw ();
                    }
                    current = entry;
                    return;
                }
            }
        }

        private void mouse_press (double x, double y) {
            foreach (var entry in entries) {
                Gtk.Widget? cell = entry.widget;
                if (cell == null) {
                    cell = entry.workspace;
                }
                if (cell == null) {
                    continue;
                }
                int wx, wy;
                if (window.translate_coordinates (cell, (int) x, (int) y, out wx, out wy)) {
                    Gtk.Allocation alloc;
                    cell.get_allocation (out alloc);
                    if (0 <= wx && wx < alloc.width && 0 <= wy && wy < alloc.height) {
                        client.switcher_select (entry.id);
                        return;
                    }
                }
            }
        }

        private bool on_button_press (Gdk.EventButton event) {
            if (event.button == 1) {
                mouse_pressed = true;
                mouse_press (event.x, event.y);
            }
            return true;
        }

        private bool on_button_release (Gdk.EventButton event) {
            if (event.button == 1) {
                mouse_pressed = false;
            }
            return true;
        }

        private bool on_motion (Gdk.EventMotion event) {
            if (mouse_pressed) {
                mouse_press (event.x, event.y);
            }
            return true;
        }
    }

    public class AltTab : GLib.Object {
        private Client client;
        private GLib.GenericArray<TabEntry> pending = new GLib.GenericArray<TabEntry> ();
        private TabPopup? popup = null;
        /* Constructing a popup dispatches Wayland events, so hide or show can arrive before
         * it is assigned. Each request advances the generation and discards stale popups. */
        private uint generation = 0;
        private uint selected_while_creating = 0;
        private string? heading = null;

        public AltTab (Client client) {
            this.client = client;
            client.switcher_entry.connect (on_entry);
            client.switcher_thumbnail.connect (on_thumbnail);
            client.switcher_icon_name.connect (on_icon_name);
            client.switcher_icon_pixels.connect (on_icon_pixels);
            client.show_switcher.connect (on_show);
            client.update_selected.connect (on_update_selected);
            client.hide_switcher.connect (on_hide);
            client.switcher_heading.connect ((text) => heading = text);
            client.workspace_switcher_entry.connect (on_workspace_entry);
            client.workspace_switcher_window.connect (on_workspace_window);
            client.show_workspace_switcher.connect (on_show_workspaces);
            client.update_workspace_switcher.connect (on_update_workspaces);
        }

        private bool showing_workspaces = false;

        private TabEntry? workspace_entry (uint id) {
            if (showing_workspaces) {
                return popup.entry_with_id (id);
            }
            foreach (var entry in pending) {
                if (entry.id == id) {
                    return entry;
                }
            }
            return null;
        }

        private void on_workspace_entry (uint id, string title, bool blank) {
            if (showing_workspaces) {
                var entry = workspace_entry (id);
                if (entry != null) {
                    entry.windows = new GLib.GenericArray<WorkspaceWindow> ();
                }
                return;
            }
            pending.add (new TabEntry () { id = id, title = title, flags = blank ? FLAG_BLANK : 0 });
        }

        private void on_workspace_window (uint id, int x, int y, int width, int height, bool focused, string? app_id) {
            var entry = workspace_entry (id);
            if (entry != null) {
                entry.windows.add (new WorkspaceWindow () {
                    x = x, y = y, width = width, height = height, focused = focused, app_id = app_id
                });
            }
        }

        private void on_show_workspaces (Gdk.Monitor? monitor, uint selected, uint columns, uint active,
                                         int screen_width, int screen_height, int output_width, int output_height,
                                         uint icon_size) {
            var entries = pending;
            pending = new GLib.GenericArray<TabEntry> ();
            if (popup != null) {
                popup.destroy ();
                popup = null;
            }
            selected_while_creating = 0;
            heading = null;
            var info = new WorkspacePopupInfo () {
                active = active, screen_width = screen_width, screen_height = screen_height,
                output_width = output_width, output_height = output_height, icon_size = (int) icon_size
            };
            uint mine = ++generation;
            var created = new TabPopup (client, entries, monitor, selected, (int) columns, false, (int) icon_size,
                                        info);
            if (mine != generation) {
                created.destroy ();
                return;
            }
            if (selected_while_creating != 0) {
                created.select (selected_while_creating);
            }
            popup = created;
            showing_workspaces = true;
        }

        private void on_update_workspaces (uint selected, uint active) {
            if (showing_workspaces) {
                popup.update_workspaces (selected, active);
            }
        }

        private void on_entry (uint id, string title, string? app_id, uint flags) {
            pending.add (new TabEntry () { id = id, title = title, app_id = app_id, flags = flags });
        }

        private void on_icon_name (uint id, string name) {
            foreach (var entry in pending) {
                if (entry.id == id) {
                    entry.icon_name = name;
                }
            }
        }

        private void on_icon_pixels (uint id, Cairo.ImageSurface pixels) {
            foreach (var entry in pending) {
                if (entry.id == id) {
                    entry.icon_pixels = pixels;
                }
            }
        }

        private void on_thumbnail (uint id, Cairo.ImageSurface thumbnail) {
            foreach (var entry in pending) {
                if (entry.id == id) {
                    entry.thumbnail = thumbnail;
                }
            }
        }

        private void on_show (Gdk.Monitor? monitor, uint selected, uint columns, bool expand_to_fit,
                              uint icon_size) {
            var entries = pending;
            pending = new GLib.GenericArray<TabEntry> ();
            if (popup != null) {
                popup.destroy ();
                popup = null;
            }
            selected_while_creating = 0;
            showing_workspaces = false;
            uint mine = ++generation;
            string? shown_heading = heading;
            heading = null;
            var created = new TabPopup (client, entries, monitor, selected, (int) columns, expand_to_fit,
                                        (int) icon_size, null, shown_heading);
            if (mine != generation) {
                created.destroy ();
                return;
            }
            if (selected_while_creating != 0) {
                created.select (selected_while_creating);
            }
            popup = created;
        }

        private void on_update_selected (uint selected) {
            if (popup != null) {
                popup.select (selected);
            } else {
                selected_while_creating = selected;
            }
        }

        private void on_hide () {
            heading = null;
            generation++;
            if (popup != null) {
                popup.destroy ();
                popup = null;
            }
            showing_workspaces = false;
            pending = new GLib.GenericArray<TabEntry> ();
        }
    }
}
