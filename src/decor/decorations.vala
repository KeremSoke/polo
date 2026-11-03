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
            ask_custom_colors ();
        }

        public void shutdown () {
            if (shell_ui != null) {
                shell_ui.color_table.disconnect (on_color_table);
                shell_ui.custom_color.disconnect (on_custom_color);
                shell_ui.bound.disconnect (ask_custom_colors);
            }
        }

        private string? theme_name = null;
        private int image_scale = 1;

        /* Marco loads theme images at the root-window scale; use the highest output scale. */
        public void set_image_scale (int scale) {
            if (scale == image_scale) {
                return;
            }
            image_scale = scale;
            if (theme_name != null) {
                load_theme (theme_name);
            }
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
