/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Where a minimising window's Magic Lamp goes (D13), the geometry part:
 * KWin's pointer fallback (magiclamp.cpp), which keeps the target off the
 * window by moving it to the nearest border, and the funnel's edge from a
 * panel's anchor or the output edge nearest the target.
 */

namespace Polo.Policy {
    public enum LampEdge {
        BOTTOM,
        TOP,
        LEFT,
        RIGHT
    }

    namespace LampGeometry {
        /* zwlr_layer_surface_v1.anchor bits */
        public const uint ANCHOR_TOP = 1;
        public const uint ANCHOR_BOTTOM = 2;
        public const uint ANCHOR_LEFT = 4;
        public const uint ANCHOR_RIGHT = 8;

        public static Marco.Rect pointer_target (Marco.Rect window, int x, int y, out bool on_border,
                                                 out LampEdge border) {
            on_border = false;
            border = LampEdge.BOTTOM;
            int right = window.x + window.width - 1;
            int bottom = window.y + window.height - 1;
            if (x >= window.x && x <= right && y >= window.y && y <= bottom) {
                on_border = true;
                int distance = y - window.y;
                border = LampEdge.TOP;
                if (x - window.x < distance) {
                    distance = x - window.x;
                    border = LampEdge.LEFT;
                }
                if (bottom - y < distance) {
                    distance = bottom - y;
                    border = LampEdge.BOTTOM;
                }
                if (right - x < distance) {
                    border = LampEdge.RIGHT;
                }
                switch (border) {
                    case LampEdge.TOP:
                        y = window.y;
                        break;
                    case LampEdge.LEFT:
                        x = window.x;
                        break;
                    case LampEdge.BOTTOM:
                        y = bottom;
                        break;
                    case LampEdge.RIGHT:
                        x = right;
                        break;
                }
            }
            return Marco.Rect () { x = x, y = y, width = 0, height = 0 };
        }

        public static bool edge_for_anchor (uint anchor, out LampEdge edge) {
            edge = LampEdge.BOTTOM;
            bool top = (anchor & ANCHOR_TOP) != 0, bottom = (anchor & ANCHOR_BOTTOM) != 0;
            bool left = (anchor & ANCHOR_LEFT) != 0, right = (anchor & ANCHOR_RIGHT) != 0;
            if (top != bottom && (left == right)) {
                edge = top ? LampEdge.TOP : LampEdge.BOTTOM;
                return true;
            }
            if (left != right && (top == bottom)) {
                edge = left ? LampEdge.LEFT : LampEdge.RIGHT;
                return true;
            }
            return false;
        }

        public static LampEdge nearest_edge (Marco.Rect target, Marco.Rect output) {
            int cx = target.x + target.width / 2;
            int cy = target.y + target.height / 2;
            int distance = output.y + output.height - cy;
            var edge = LampEdge.BOTTOM;
            if (cy - output.y < distance) {
                distance = cy - output.y;
                edge = LampEdge.TOP;
            }
            if (cx - output.x < distance) {
                distance = cx - output.x;
                edge = LampEdge.LEFT;
            }
            if (output.x + output.width - cx < distance) {
                edge = LampEdge.RIGHT;
            }
            return edge;
        }
    }
}
