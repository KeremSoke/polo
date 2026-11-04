/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

namespace Polo.Decor {
    /* Holds the theme, colours, fonts, layout, and a serial that changes with them. */
    public class Decorations : GLib.Object {
        /* Marco's default Xft DPI, used until Polo owns XSETTINGS. */
        public const double DPI = 96;

        private Marco.Theme? theme = null;
        private Marco.ThemeColors colors = new Marco.ThemeColors ();
        public string? system_font { get; private set; default = null; }
        public string? titlebar_font { get; private set; default = null; }
        public Marco.ButtonLayout button_layout;
        public uint serial { get; private set; default = 0; }

        public signal void changed ();
        public signal void icons_changed ();

        public unowned Marco.Theme? get_theme () {
            return theme;
        }

        public unowned Marco.ThemeColors get_colors () {
            return colors;
        }

        private Backend.ShellUi? shell_ui;

        public Decorations () {
            Marco.parse_button_layout ("menu:minimize,maximize,close", false, out button_layout);
        }

        public void attach (Backend.ShellUi shell_ui) {
            this.shell_ui = shell_ui;
            shell_ui.color_table.connect (on_color_table);
            shell_ui.custom_color.connect (on_custom_color);
            shell_ui.bound.connect (ask_custom_colors);
            shell_ui.icon.connect (on_icon);
            shell_ui.icon_theme_changed.connect (on_icon_theme_changed);
            shell_ui.bound.connect (on_icon_theme_changed);
            ask_custom_colors ();
        }

        public void shutdown () {
            if (shell_ui != null) {
                shell_ui.color_table.disconnect (on_color_table);
                shell_ui.custom_color.disconnect (on_custom_color);
                shell_ui.bound.disconnect (ask_custom_colors);
                shell_ui.icon.disconnect (on_icon);
                shell_ui.icon_theme_changed.disconnect (on_icon_theme_changed);
                shell_ui.bound.disconnect (on_icon_theme_changed);
            }
            icon_requests.remove_all ();
        }

        private string? theme_name = null;
        private int _image_scale = 1;
        public int image_scale { get { return _image_scale; } }
        /* Marco's icon-size preference, for icons bigger than the mini icon. */
        private int _icon_size = 48;
        public int icon_size { get { return _icon_size; } }

        /* Marco loads theme images at the root-window scale; use the highest output scale. */
        public void set_image_scale (int scale) {
            if (scale == image_scale) {
                return;
            }
            _image_scale = scale;
            if (theme_name != null) {
                load_theme (theme_name);
            }
            icons_changed ();
        }

        public void set_icon_size (int size) {
            if (size > 0 && size != icon_size) {
                _icon_size = size;
                icons_changed ();
            }
        }

        private GLib.HashTable<uint, unowned WindowIcon> icon_requests =
            new GLib.HashTable<uint, unowned WindowIcon> (GLib.direct_hash, GLib.direct_equal);
        private uint icon_serial = 0;

        internal uint request_icon (WindowIcon icon, string? name, string? desktop_id, bool fallback) {
            if (shell_ui == null) {
                return 0;
            }
            if (++icon_serial == 0) {
                icon_serial = 1;
            }
            if (!shell_ui.lookup_icon (icon_serial, name, desktop_id, WindowIcon.MINI_SIZE, icon_size,
                                       image_scale, fallback)) {
                return 0;
            }
            icon_requests[icon_serial] = icon;
            return icon_serial;
        }

        internal void forget_icon (uint serial) {
            icon_requests.remove (serial);
        }

        private void on_icon (uint serial, GLib.Bytes? mini_pixels, int mini_width, int mini_height,
                              GLib.Bytes? pixels, int width, int height) {
            unowned WindowIcon? icon = icon_requests[serial];
            if (icon == null) {
                return;
            }
            icon_requests.remove (serial);
            Marco.Icon? mini = null;
            Marco.Icon? full = null;
            if (mini_pixels != null && pixels != null) {
                mini = Marco.Icon.from_argb32 (mini_pixels.get_data (), mini_width, mini_height, mini_width * 4, 0);
                full = Marco.Icon.from_argb32 (pixels.get_data (), width, height, width * 4, 0);
            }
            icon.answer (serial, (owned) mini, (owned) full);
        }

        private void on_icon_theme_changed () {
            icons_changed ();
        }

        /* Keep the current theme if loading the new one fails. */
        public bool load_theme (string name) {
            try {
                Marco.set_image_scale (image_scale);
                var loaded = Marco.Theme.load (name);
                theme = (owned) loaded;
                theme_name = name;
            } catch (GLib.Error e) {
                Logging.scope (LogDomain.POLO).warning ("cannot load the window theme %s: %s", name, e.message);
                return false;
            }
            ask_custom_colors ();
            bump ();
            return true;
        }


        /* meta_theme_set_current (): the theme in use is not loaded again. */
        public bool set_theme (string name) {
            if (theme != null && name == theme_name) {
                return true;
            }
            return load_theme (name);
        }

        /* main.c: with no theme yet, Simple, then any installed theme that loads. */
        public void ensure_theme () {
            if (theme != null || load_theme ("Simple")) {
                return;
            }
            var names = new GLib.GenericArray<string> ();
            foreach (unowned string dir in GLib.Environment.get_system_data_dirs ()) {
                var themes = GLib.Path.build_filename (dir, "themes");
                try {
                    var listing = GLib.Dir.open (themes);
                    for (unowned string? name = listing.read_name (); name != null; name = listing.read_name ()) {
                        if (GLib.FileUtils.test (GLib.Path.build_filename (themes, name, "metacity-1"),
                                                 GLib.FileTest.IS_DIR)) {
                            names.add (name);
                        }
                    }
                } catch (GLib.FileError e) {
                    continue;
                }
            }
            names.sort (GLib.strcmp);
            foreach (unowned string name in names) {
                if (load_theme (name)) {
                    return;
                }
            }
            Logging.scope (LogDomain.POLO).warning ("no window theme loads; windows stay undecorated");
        }

        /* Marco's normal-window borders with no state flags. */
        public Marco.FrameBorders normal_borders () {
            if (theme == null || theme.get_frame_style (Marco.FrameType.NORMAL, 0) == null) {
                return Marco.FrameBorders ();
            }
            int text_height = theme.text_height (Marco.FrameType.NORMAL, 0, system_font, titlebar_font, DPI);
            Marco.ThemeBorders borders;
            theme.get_frame_borders (Marco.FrameType.NORMAL, text_height, 0, out borders);
            return Marco.FrameBorders () {
                left = borders.visible.left, right = borders.visible.right,
                top = borders.visible.top, bottom = borders.visible.bottom
            };
        }

        public void set_fonts (string? system_font, string? titlebar_font) {
            if (system_font == this.system_font && titlebar_font == this.titlebar_font) {
                return;
            }
            this.system_font = system_font;
            this.titlebar_font = titlebar_font;
            bump ();
        }

        public void set_button_layout (string layout) {
            Marco.ButtonLayout parsed;
            Marco.parse_button_layout (layout, false, out parsed);
            if (!parsed.equal (button_layout)) {
                Marco.parse_button_layout (layout, false, out button_layout);
                bump ();
            }
        }

        private void ask_custom_colors () {
            if (shell_ui == null || theme == null) {
                return;
            }
            foreach (unowned string name in theme.get_custom_color_names ()) {
                shell_ui.lookup_color (name);
            }
        }

        private void on_color_table (GLib.Bytes table) {
            var values = new float[table.get_size () / sizeof (float)];
            GLib.Memory.copy (values, table.get_data (), table.get_size ());
            if (colors.set_table (values)) {
                bump ();
            }
        }

        private void on_custom_color (string name, GLib.Bytes? rgba) {
            if (rgba == null || rgba.get_size () != 4 * sizeof (float)) {
                colors.set_custom (name, null);
            } else {
                var value = new float[4];
                GLib.Memory.copy (value, rgba.get_data (), rgba.get_size ());
                colors.set_custom (name, Marco.RGBA () {
                    red = value[0], green = value[1], blue = value[2], alpha = value[3]
                });
            }
            bump ();
        }

        private void bump () {
            serial++;
            changed ();
        }
    }
}
