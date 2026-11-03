/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

namespace Polo.Decor {
    /* Renders a window frame as four edges so memory scales with perimeter, not area. */
    public class Frame : GLib.Object {
        public enum Edge {
            TOP,
            BOTTOM,
            LEFT,
            RIGHT;

            public const int COUNT = 4;
        }

        public Marco.FrameGeometry geometry;

        private Backend.Server server;
        private Decorations decorations;
        private Backend.PixelBuffer?[] edges = new Backend.PixelBuffer?[Edge.COUNT];
        private Marco.Rect[] edge_rects = new Marco.Rect[Edge.COUNT];
        private Marco.ButtonState[] button_states = new Marco.ButtonState[Marco.ButtonType.LAST];
        private bool drawn = false;
        private uint drawn_serial;
        private Marco.FrameType drawn_type;
        private Marco.FrameFlags drawn_flags;
        private int drawn_width;
        private int drawn_height;
        private string? drawn_title;

        /* Emitted when an edge view is ready to place. */
        public signal void ready ();

        public Frame (Backend.Server server, Decorations decorations) {
            this.server = server;
            this.decorations = decorations;
        }

        /* Returns an edge buffer, or null when empty, and its frame-relative bounds. */
        public unowned Backend.PixelBuffer? get_edge (Edge edge, out Marco.Rect rect) {
            rect = edge_rects[edge];
            return edges[edge];
        }

        public void set_button_state (Marco.ButtonType button, Marco.ButtonState state) {
            if (button_states[button] != state) {
                button_states[button] = state;
                drawn = false;
            }
        }

        /* Redraws only when an input changed; returns true if it drew. */
        public bool update (Marco.FrameType type, Marco.FrameFlags flags, int client_width, int client_height,
                            string? title) {
            unowned Marco.Theme? theme = decorations.get_theme ();
            if (theme == null || theme.get_frame_style (type, flags) == null) {
                clear ();
                return false;
            }
            if (drawn && drawn_serial == decorations.serial && drawn_type == type && drawn_flags == flags &&
                drawn_width == client_width && drawn_height == client_height && drawn_title == title) {
                return false;
            }
            int text_height = theme.text_height (type, flags, decorations.system_font, decorations.titlebar_font,
                                                 Decorations.DPI);
            theme.calc_geometry (type, text_height, flags, client_width, client_height,
                                 decorations.button_layout, out geometry);
            unowned Marco.Border total = geometry.borders.total;
            int width = geometry.width;
            int height = geometry.height;
            int middle = height - total.top - total.bottom;
            edge_rects[Edge.TOP] = { 0, 0, width, total.top };
            edge_rects[Edge.BOTTOM] = { 0, height - total.bottom, width, total.bottom };
            edge_rects[Edge.LEFT] = { 0, total.top, total.left, middle };
            edge_rects[Edge.RIGHT] = { width - total.right, total.top, total.right, middle };
            for (int i = 0; i < Edge.COUNT; i++) {
                draw_edge (theme, (Edge) i, type, flags, client_width, client_height, title);
            }
            drawn = true;
            drawn_serial = decorations.serial;
            drawn_type = type;
            drawn_flags = flags;
            drawn_width = client_width;
            drawn_height = client_height;
            drawn_title = title;
            return true;
        }

        private void draw_edge (Marco.Theme theme, Edge edge, Marco.FrameType type, Marco.FrameFlags flags,
                                int client_width, int client_height, string? title) {
            var rect = edge_rects[edge];
            if (rect.width <= 0 || rect.height <= 0) {
                edges[edge] = null;
                return;
            }
            int width, height, scale;
            if (edges[edge] != null) {
                edges[edge].get_size (out width, out height, out scale);
                if (width != rect.width || height != rect.height) {
                    edges[edge] = null;
                }
            }
            if (edges[edge] == null) {
                edges[edge] = new Backend.PixelBuffer (server, rect.width, rect.height, 1);
                edges[edge].ready.connect (on_edge_ready);
            }
            int stride;
            unowned uint8[] pixels = edges[edge].begin (out stride);
            if (pixels.length == 0) {
                return;
            }
            theme.render (decorations.get_colors (), type, flags, client_width, client_height, title,
                          decorations.system_font, decorations.titlebar_font, Decorations.DPI,
                          decorations.button_layout, button_states, rect.x, rect.y, pixels,
                          rect.width, rect.height, stride);
            edges[edge].commit ();
        }

        private void on_edge_ready () {
            ready ();
        }

        public void clear () {
            for (int i = 0; i < Edge.COUNT; i++) {
                edges[i] = null;
            }
            drawn = false;
        }
    }
}
