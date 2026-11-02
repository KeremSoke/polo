/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
namespace Polo {
    /* Compositor-neutral monitor description and requested configuration. */
    public class Output : GLib.Object {
        public string name = "";
        public string vendor = "";
        public string product = "";
        public string serial = "";
        public string model = "";
        public int mm_width;
        public int mm_height;
        public bool internal_panel;
        public int width;
        public int height;
        public int rate;
        public int x;
        public int y;
        public int scale = 1;
        public int transform;
        public bool primary;
        public bool enabled = true;
        public GLib.GenericArray<OutputMode> modes = new GLib.GenericArray<OutputMode> ();

        public bool same_monitor (Output other) {
            return vendor != "" && product != "" && vendor == other.vendor &&
                product == other.product && serial == other.serial;
        }

        public void copy_configuration (Output other) {
            width = other.width;
            height = other.height;
            rate = other.rate;
            x = other.x;
            y = other.y;
            scale = other.scale;
            transform = other.transform;
            primary = other.primary;
            enabled = other.enabled;
        }

        public bool supports (Output config) {
            if (!config.enabled) {
                return true;
            }
            foreach (var mode in modes) {
                if (mode.width == config.width && mode.height == config.height &&
                    (config.rate == 0 || (mode.rate - config.rate).abs () <= 1000)) {
                    return true;
                }
            }
            return false;
        }

        public int logical_width () {
            return ((transform & 1) != 0 ? height : width) / scale;
        }
    }

    public class OutputMode : GLib.Object {
        public int width;
        public int height;
        public int rate;
        public bool preferred;
        public bool current;
    }
}
