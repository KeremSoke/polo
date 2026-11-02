/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * org.mate.peripherals-keyboard-xkb.kbd as libmatekbd 1.28 makes an XKB
 * configuration of it: matekbd_keyboard_config_load_from_gsettings (), where
 * the system configuration fills each empty key, then
 * matekbd_keyboard_config_copy_to_xkl_config (). msd's filter_xkb_config ()
 * drops what the XKB registry does not know.
 */

namespace Polo.Input {
    public class XkbConfig {
        public string? model = null;
        public string[] layouts = {};
        public string[] variants = {};
        public string[] options = {};

        /* The X server's form, comma-separated lists, as localed and XKB_DEFAULT_* give it. */
        public static XkbConfig from_rmlvo (string? model, string? layout, string? variant, string? options) {
            var config = new XkbConfig ();
            config.model = model != null && model != "" ? model : null;
            if (layout != null && layout != "") {
                config.layouts = layout.split (",");
                var given = variant != null ? variant.split (",") : new string[0];
                config.variants = new string[config.layouts.length];
                for (int i = 0; i < config.layouts.length; i++) {
                    config.variants[i] = i < given.length ? given[i] : "";
                }
            }
            if (options != null && options != "") {
                config.options = options.split (",");
            }
            return config;
        }

        public static XkbConfig from_settings (string model, string[] layouts, string[] options,
                                               XkbConfig system) {
            var config = new XkbConfig ();
            config.model = model != "" ? model : system.model;
            if (layouts.length == 0) {
                config.layouts = system.layouts;
                config.variants = system.variants;
            } else {
                config.layouts = new string[layouts.length];
                config.variants = new string[layouts.length];
                for (int i = 0; i < layouts.length; i++) {
                    int tab = layouts[i].index_of_char ('\t');
                    config.layouts[i] = tab < 0 ? layouts[i] : layouts[i].substring (0, tab);
                    config.variants[i] = tab < 0 ? "" : layouts[i].substring (tab + 1);
                }
            }
            if (options.length == 0) {
                config.options = system.options;
            } else {
                config.options = new string[options.length];
                for (int i = 0; i < options.length; i++) {
                    int tab = options[i].index_of_char ('\t');
                    config.options[i] = tab < 0 ? "" : options[i].substring (tab + 1);
                }
            }
            return config;
        }

        /* filter_xkb_config (): whether a layout was dropped. */
        public bool filter () {
            string[] kept_layouts = {};
            string[] kept_variants = {};
            for (int i = 0; i < layouts.length; i++) {
                bool known = Backend.Keymap.has_layout (layouts[i], null) &&
                             (variants[i] == "" || Backend.Keymap.has_layout (layouts[i], variants[i]));
                if (known) {
                    kept_layouts += layouts[i];
                    kept_variants += variants[i];
                }
            }
            bool any_change = kept_layouts.length != layouts.length;
            layouts = kept_layouts;
            variants = kept_variants;
            return any_change;
        }

        public string? layout_names () {
            return layouts.length > 0 ? string.joinv (",", layouts) : null;
        }

        public string? variant_names () {
            return layouts.length > 0 ? string.joinv (",", variants) : null;
        }

        public string? option_names () {
            string[] given = {};
            foreach (unowned string option in options) {
                if (option != "") {
                    given += option;
                }
            }
            return given.length > 0 ? string.joinv (",", given) : null;
        }

        /* matekbd_keyboard_config_equals (): options in any order. */
        public bool equals (XkbConfig other) {
            return model == other.model && layout_names () == other.layout_names () &&
                   variant_names () == other.variant_names () && sorted_options () == other.sorted_options ();
        }

        private string sorted_options () {
            var sorted = new GLib.GenericArray<string> ();
            foreach (unowned string option in options) {
                if (option != "") {
                    sorted.add (option);
                }
            }
            sorted.sort (GLib.strcmp);
            return string.joinv (",", sorted.data);
        }

        public string to_string () {
            return "model %s, layouts %s, variants %s, options %s".printf (
                model ?? "(default)", layout_names () ?? "(default)", variant_names () ?? "(default)",
                option_names () ?? "(none)");
        }
    }
}
