/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Magic Lamp targets (D13), in priority order: the task-bar button a panel
 * gave with zwlr_foreign_toplevel_handle_v1.set_rectangle (already in
 * global coordinates), an X dock's _NET_WM_ICON_GEOMETRY on an X window,
 * then KWin's pointer fallback. The funnel leaves from the edge of the
 * panel the target is on, else the output edge nearest it; a pointer moved
 * to a window border leaves from that border, as in KWin.
 */

namespace Polo.Effects {
    public enum LampSource {
        PANEL,
        ICON_GEOMETRY,
        POINTER
    }

    public struct LampTarget {
        public Marco.Rect rect;
        public Policy.LampEdge edge;
        public LampSource source;
    }

    public delegate bool PanelButton (Window window, out Marco.Rect rect);

    public class LampTargets : GLib.Object {
        private Policy.Model model;
        private Shell.LayerShell layer_shell;
        private PanelButton? panel_button = null;

        public LampTargets (Policy.Model model, Shell.LayerShell layer_shell) {
            this.model = model;
            this.layer_shell = layer_shell;
        }

        public void set_panel_button (owned PanelButton? lookup) {
            panel_button = (owned) lookup;
        }

        private static bool icon_geometry (Window window, out Marco.Rect rect) {
            rect = Marco.Rect ();
#if POLO_HAVE_XWAYLAND
            var x11 = window.adapter as Shell.X11Window;
            if (x11 == null) {
                return false;
            }
            uint32[] values = x11.xwindow.get_cardinals ("_NET_WM_ICON_GEOMETRY");
            if (values.length != 4 || values[2] == 0 || values[3] == 0) {
                return false;
            }
            rect = Marco.Rect () {
                x = (int) values[0], y = (int) values[1], width = (int) values[2], height = (int) values[3]
            };
            return true;
#else
            return false;
#endif
        }

        private Policy.LampEdge edge_at (Marco.Rect target) {
            int cx = target.x + target.width / 2;
            int cy = target.y + target.height / 2;
            uint anchor;
            if (layer_shell.panel_at (cx, cy, out anchor)) {
                Policy.LampEdge edge;
                bool known = Policy.LampGeometry.edge_for_anchor (anchor, out edge);
                if (known) {
                    return edge;
                }
            }
            var output = model.output_at (cx, cy) ?? nearest_output (cx, cy);
            return output != null ? Policy.LampGeometry.nearest_edge (target, output.rect) : Policy.LampEdge.BOTTOM;
        }

        private Policy.Output? nearest_output (int x, int y) {
            Policy.Output? best = null;
            int64 best_distance = int64.MAX;
            foreach (var output in model.get_outputs ()) {
                var r = output.rect;
                int dx = x < r.x ? r.x - x : x >= r.x + r.width ? x - (r.x + r.width - 1) : 0;
                int dy = y < r.y ? r.y - y : y >= r.y + r.height ? y - (r.y + r.height - 1) : 0;
                int64 distance = (int64) dx * dx + (int64) dy * dy;
                if (distance < best_distance) {
                    best_distance = distance;
                    best = output;
                }
            }
            return best;
        }

        public LampTarget resolve (Window window) {
            var target = LampTarget ();
            var rect = Marco.Rect ();
            bool from_panel = panel_button != null && panel_button (window, out rect);
            if (from_panel && rect.width > 0 && rect.height > 0) {
                target.rect = rect;
                target.source = LampSource.PANEL;
                target.edge = edge_at (rect);
                return target;
            }
            if (icon_geometry (window, out rect)) {
                target.rect = rect;
                target.source = LampSource.ICON_GEOMETRY;
                target.edge = edge_at (rect);
                return target;
            }
            bool on_border;
            Policy.LampEdge border;
            target.rect = Policy.LampGeometry.pointer_target (window.outer_rect (), (int) model.pointer_x,
                                                              (int) model.pointer_y, out on_border, out border);
            target.source = LampSource.POINTER;
            target.edge = on_border ? border : edge_at (target.rect);
            return target;
        }
    }
}
