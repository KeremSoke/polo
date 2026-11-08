/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The four layouts of the display-mode popup, as GNOME's monitor switcher
 * and mate-settings-daemon's xrandr plugin cycle them: the primary screen
 * alone, every screen showing the same picture, the screens side by side,
 * or the other screens without the primary. The primary is the laptop's
 * panel if there is one, else the primary output. Mirroring needs a
 * resolution every screen has; the largest one is used.
 */

namespace Polo.Outputs {
    public enum DisplayMode {
        PRIMARY_ONLY,
        MIRROR,
        EXTEND,
        SECONDARY_ONLY;

        public const int COUNT = 4;

        public unowned string label () {
            switch (this) {
                case PRIMARY_ONLY:
                    return "Primary only";
                case MIRROR:
                    return "Mirror";
                case EXTEND:
                    return "Extend";
                default:
                    return "Secondary only";
            }
        }

        public unowned string icon_name () {
            switch (this) {
                case PRIMARY_ONLY:
                    return "polo-display-primary-only-symbolic";
                case MIRROR:
                    return "polo-display-mirror-symbolic";
                case EXTEND:
                    return "polo-display-extend-symbolic";
                default:
                    return "polo-display-secondary-only-symbolic";
            }
        }
    }

    public class DisplayModes : GLib.Object {
        public static Polo.Output? primary_of (GLib.GenericArray<Polo.Output> outputs) {
            foreach (var output in outputs) {
                if (output.internal_panel) {
                    return output;
                }
            }
            foreach (var output in outputs) {
                if (output.enabled && output.primary) {
                    return output;
                }
            }
            return outputs.length > 0 ? sorted (outputs)[0] : null;
        }

        private static GLib.GenericArray<Polo.Output> sorted (GLib.GenericArray<Polo.Output> outputs) {
            var copy = new GLib.GenericArray<Polo.Output> ();
            foreach (var output in outputs) {
                copy.add (output);
            }
            copy.sort ((a, b) => a.name.collate (b.name));
            return copy;
        }

        public static bool common_size (GLib.GenericArray<Polo.Output> outputs, out int width, out int height) {
            width = height = 0;
            if (outputs.length == 0) {
                return false;
            }
            foreach (var mode in outputs[0].modes) {
                bool everywhere = true;
                foreach (var output in outputs) {
                    everywhere &= best_rate (output, mode.width, mode.height) != null;
                }
                if (everywhere && mode.width * mode.height > width * height) {
                    width = mode.width;
                    height = mode.height;
                }
            }
            return width > 0;
        }

        private static OutputMode? best_rate (Polo.Output output, int width, int height) {
            OutputMode? best = null;
            foreach (var mode in output.modes) {
                if (mode.width == width && mode.height == height && (best == null || mode.rate > best.rate)) {
                    best = mode;
                }
            }
            return best;
        }

        public static bool available (GLib.GenericArray<Polo.Output> outputs, DisplayMode mode) {
            if (outputs.length < 2) {
                return false;
            }
            int width, height;
            return mode != DisplayMode.MIRROR || common_size (outputs, out width, out height);
        }

        public static GLib.GenericArray<Polo.Output>? configure (GLib.GenericArray<Polo.Output> outputs,
                                                                 DisplayMode mode, int scaling) {
            if (!available (outputs, mode)) {
                return null;
            }
            var primary = primary_of (outputs);
            var result = new GLib.GenericArray<Polo.Output> ();
            Polo.Output? new_primary = null;
            var others = new GLib.GenericArray<Polo.Output> ();
            foreach (var output in sorted (outputs)) {
                var copy = new Polo.Output ();
                copy.name = output.name;
                copy.vendor = output.vendor;
                copy.product = output.product;
                copy.serial = output.serial;
                copy.model = output.model;
                copy.internal_panel = output.internal_panel;
                copy.mm_width = output.mm_width;
                copy.mm_height = output.mm_height;
                copy.modes = output.modes;
                copy.copy_configuration (output);
                copy.primary = false;
                result.add (copy);
                if (output == primary) {
                    new_primary = copy;
                } else {
                    others.add (copy);
                }
            }
            if (new_primary == null || others.length == 0) {
                return null;
            }
            switch (mode) {
                case DisplayMode.PRIMARY_ONLY:
                    Layout.use_preferred (new_primary, scaling);
                    place (new_primary, 0);
                    new_primary.primary = true;
                    foreach (var output in others) {
                        output.enabled = false;
                    }
                    break;
                case DisplayMode.SECONDARY_ONLY:
                    new_primary.enabled = false;
                    int right = 0;
                    foreach (var output in others) {
                        Layout.use_preferred (output, scaling);
                        place (output, right);
                        right += output.logical_width ();
                    }
                    others[0].primary = true;
                    break;
                case DisplayMode.EXTEND:
                    Layout.use_preferred (new_primary, scaling);
                    place (new_primary, 0);
                    new_primary.primary = true;
                    int right = new_primary.logical_width ();
                    foreach (var output in others) {
                        Layout.use_preferred (output, scaling);
                        place (output, right);
                        right += output.logical_width ();
                    }
                    break;
                default:
                    int width, height;
                    common_size (result, out width, out height);
                    foreach (var output in result) {
                        var chosen = best_rate (output, width, height);
                        output.width = width;
                        output.height = height;
                        output.rate = chosen.rate;
                        output.scale = scaling > 0 ? int.min (scaling, 8) : 1;
                        output.transform = 0;
                        place (output, 0);
                    }
                    new_primary.primary = true;
                    break;
            }
            return result;
        }

        private static void place (Polo.Output output, int x) {
            output.x = x;
            output.y = 0;
            output.enabled = true;
        }

        public static int current (GLib.GenericArray<Polo.Output> outputs) {
            if (outputs.length < 2) {
                return -1;
            }
            var primary = primary_of (outputs);
            int enabled = 0;
            bool same_place = true;
            Polo.Output? first = null;
            foreach (var output in outputs) {
                if (!output.enabled) {
                    continue;
                }
                enabled++;
                if (first == null) {
                    first = output;
                } else {
                    same_place &= output.x == first.x && output.y == first.y &&
                                  output.logical_width () == first.logical_width () &&
                                  output.height / int.max (output.scale, 1) == first.height / int.max (first.scale, 1);
                }
            }
            if (enabled == 1) {
                return primary.enabled ? DisplayMode.PRIMARY_ONLY : DisplayMode.SECONDARY_ONLY;
            }
            if (!primary.enabled) {
                return DisplayMode.SECONDARY_ONLY;
            }
            if (enabled == outputs.length) {
                return same_place ? DisplayMode.MIRROR : DisplayMode.EXTEND;
            }
            return -1;
        }
    }
}
