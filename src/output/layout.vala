/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
namespace Polo.Outputs {
    public class Layout : GLib.Object {
        public static bool restore (GLib.GenericArray<Polo.Output> outputs, MonitorsXml? saved) {
            if (saved == null) {
                return false;
            }
            foreach (var config in saved.configurations) {
                if (config.outputs.length != outputs.length) {
                    continue;
                }
                var matches = new GLib.GenericArray<Polo.Output> ();
                bool valid = true;
                bool any_enabled = false;
                foreach (var output in outputs) {
                    Polo.Output? match = null;
                    foreach (var candidate in config.outputs) {
                        uint index;
                        if (output.same_monitor (candidate) && !matches.find (candidate, out index)) {
                            match = candidate;
                            break;
                        }
                    }
                    if (match == null || !output.supports (match)) {
                        valid = false;
                        break;
                    }
                    matches.add (match);
                    any_enabled |= match.enabled;
                }
                if (!valid || !any_enabled) {
                    continue;
                }
                for (uint i = 0; i < outputs.length; i++) {
                    outputs[i].copy_configuration (matches[i]);
                }
                ensure_primary (outputs);
                return true;
            }
            return false;
        }

        public static void ensure_primary (GLib.GenericArray<Polo.Output> outputs) {
            Polo.Output? primary = null;
            foreach (var output in outputs) {
                if (output.enabled && output.primary && primary == null) {
                    primary = output;
                } else {
                    output.primary = false;
                }
            }
            if (primary == null) {
                foreach (var output in outputs) {
                    if (output.enabled) {
                        output.primary = true;
                        break;
                    }
                }
            }
        }

        public static void arrange (GLib.GenericArray<Polo.Output> outputs, MonitorsXml? saved,
                                    MonitorsXml? imported, int scaling, bool laptop, bool external) {
            outputs.sort ((a, b) => {
                if (a.internal_panel != b.internal_panel) {
                    return a.internal_panel ? -1 : 1;
                }
                return a.name.collate (b.name);
            });
            if (restore (outputs, saved) || restore (outputs, imported)) {
                return;
            }
            int x = 0;
            bool enabled = false;
            foreach (var output in outputs) {
                foreach (var mode in output.modes) {
                    if (mode.preferred || output.width == 0) {
                        output.width = mode.width;
                        output.height = mode.height;
                        output.rate = mode.rate;
                        if (mode.preferred) {
                            break;
                        }
                    }
                }
                output.scale = scaling > 0 ? int.min (scaling, 8) :
                    (output.mm_width > 0 && output.width * 25.4 / output.mm_width > 192 ? 2 : 1);
                output.transform = 0;
                output.x = x;
                output.y = 0;
                output.enabled = output.internal_panel ? laptop : external;
                output.primary = output.enabled && !enabled;
                if (output.enabled) {
                    x += output.logical_width ();
                    enabled = true;
                }
            }
            /* Never strand the session with every connected monitor disabled. */
            if (!enabled && outputs.length > 0) {
                outputs[0].enabled = outputs[0].primary = true;
                outputs[0].x = 0;
            }
        }
    }
}
