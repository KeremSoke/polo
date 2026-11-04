/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

namespace Polo.Decor {
    /* MetaFrameControl */
    public enum FrameControl {
        NONE,
        TITLE,
        DELETE,
        MENU,
        APPMENU,
        MINIMIZE,
        MAXIMIZE,
        UNMAXIMIZE,
        SHADE,
        UNSHADE,
        ABOVE,
        UNABOVE,
        STICK,
        UNSTICK,
        RESIZE_SE,
        RESIZE_S,
        RESIZE_SW,
        RESIZE_N,
        RESIZE_NE,
        RESIZE_NW,
        RESIZE_W,
        RESIZE_E,
        CLIENT_AREA;

        public bool is_resize () {
            return this >= RESIZE_SE && this <= RESIZE_E;
        }
    }

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
        private int drawn_scale;
        private unowned WindowIcon? drawn_icon = null;
        private uint drawn_icon_serial;

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

        /* Marco frames.c get_control(); coordinates are relative to the outer frame corner. */
        public FrameControl control_at (int x, int y, Marco.FrameFlags flags) {
            unowned Marco.Border total = geometry.borders.total;
            Marco.Rect client = { total.left, total.top, geometry.width - total.left - total.right,
                                  geometry.height - total.top - total.bottom };
            if (inside (x, y, client)) {
                return FrameControl.CLIENT_AREA;
            }
            if (inside (x, y, geometry.close_rect.clickable)) {
                return FrameControl.DELETE;
            }
            if (inside (x, y, geometry.min_rect.clickable)) {
                return FrameControl.MINIMIZE;
            }
            if (inside (x, y, geometry.menu_rect.clickable)) {
                return FrameControl.MENU;
            }
            if (inside (x, y, geometry.appmenu_rect.clickable)) {
                return FrameControl.APPMENU;
            }

            bool has_vert = Marco.FrameFlags.ALLOWS_VERTICAL_RESIZE in flags;
            bool has_horiz = Marco.FrameFlags.ALLOWS_HORIZONTAL_RESIZE in flags;

            if (inside (x, y, geometry.title_rect)) {
                return has_vert && y <= TOP_RESIZE_HEIGHT ? FrameControl.RESIZE_N : FrameControl.TITLE;
            }
            if (inside (x, y, geometry.max_rect.clickable)) {
                return Marco.FrameFlags.MAXIMIZED in flags ? FrameControl.UNMAXIMIZE : FrameControl.MAXIMIZE;
            }
            if (inside (x, y, geometry.shade_rect.clickable)) {
                return FrameControl.SHADE;
            }
            if (inside (x, y, geometry.unshade_rect.clickable)) {
                return FrameControl.UNSHADE;
            }
            if (inside (x, y, geometry.above_rect.clickable)) {
                return FrameControl.ABOVE;
            }
            if (inside (x, y, geometry.unabove_rect.clickable)) {
                return FrameControl.UNABOVE;
            }
            if (inside (x, y, geometry.stick_rect.clickable)) {
                return FrameControl.STICK;
            }
            if (inside (x, y, geometry.unstick_rect.clickable)) {
                return FrameControl.UNSTICK;
            }

            int bottom = geometry.height - total.bottom - RESIZE_EXTENDS;
            int right = geometry.width - total.right - RESIZE_EXTENDS;
            int left = total.left + RESIZE_EXTENDS;
            int top = geometry.borders.invisible.top + RESIZE_EXTENDS;
            if (y >= bottom && x >= right) {
                if (has_vert && has_horiz) {
                    return FrameControl.RESIZE_SE;
                } else if (has_vert) {
                    return FrameControl.RESIZE_S;
                } else if (has_horiz) {
                    return FrameControl.RESIZE_E;
                }
            } else if (y >= bottom && x <= left) {
                if (has_vert && has_horiz) {
                    return FrameControl.RESIZE_SW;
                } else if (has_vert) {
                    return FrameControl.RESIZE_S;
                } else if (has_horiz) {
                    return FrameControl.RESIZE_W;
                }
            } else if (y < top && x <= left) {
                if (has_vert && has_horiz) {
                    return FrameControl.RESIZE_NW;
                } else if (has_vert) {
                    return FrameControl.RESIZE_N;
                } else if (has_horiz) {
                    return FrameControl.RESIZE_W;
                }
            } else if (y < top && x >= right) {
                if (has_vert && has_horiz) {
                    return FrameControl.RESIZE_NE;
                } else if (has_vert) {
                    return FrameControl.RESIZE_N;
                } else if (has_horiz) {
                    return FrameControl.RESIZE_E;
                }
            } else if (y < geometry.borders.invisible.top + TOP_RESIZE_HEIGHT) {
                if (has_vert) {
                    return FrameControl.RESIZE_N;
                }
            } else if (y >= bottom) {
                if (has_vert) {
                    return FrameControl.RESIZE_S;
                }
            } else if (x <= left) {
                if (has_horiz) {
                    return FrameControl.RESIZE_W;
                }
            } else if (x >= right) {
                if (has_horiz) {
                    return FrameControl.RESIZE_E;
                }
            }

            return y >= total.top ? FrameControl.NONE : FrameControl.TITLE;
        }

        private const int RESIZE_EXTENDS = 15;
        private const int TOP_RESIZE_HEIGHT = 4;

        private static bool inside (int x, int y, Marco.Rect rect) {
            return x >= rect.x && x < rect.x + rect.width && y >= rect.y && y < rect.y + rect.height;
        }

        public void set_button_state (Marco.ButtonType button, Marco.ButtonState state) {
            if (button_states[button] != state) {
                button_states[button] = state;
                drawn = false;
            }
        }

        public Marco.ButtonState get_button_state (Marco.ButtonType button) {
            return button_states[button];
        }

        /* Buffers use the highest scale of the outputs under the frame. */
        public bool update (Marco.FrameType type, Marco.FrameFlags flags, int client_width, int client_height,
                            string? title, int scale = 1, WindowIcon? icon = null) {
            unowned Marco.Theme? theme = decorations.get_theme ();
            if (theme == null || theme.get_frame_style (type, flags) == null) {
                clear ();
                return false;
            }
            if (drawn && drawn_serial == decorations.serial && drawn_type == type && drawn_flags == flags &&
                drawn_width == client_width && drawn_height == client_height && drawn_title == title &&
                drawn_scale == scale && drawn_icon == icon &&
                (icon == null || drawn_icon_serial == icon.serial)) {
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
                /* A shaded frame has only its top edge. */
                if (Marco.FrameFlags.SHADED in flags && i != Edge.TOP) {
                    edges[i] = null;
                    continue;
                }
                draw_edge (theme, (Edge) i, type, flags, client_width, client_height, title, scale, icon);
            }
            drawn = true;
            drawn_serial = decorations.serial;
            drawn_type = type;
            drawn_flags = flags;
            drawn_width = client_width;
            drawn_height = client_height;
            drawn_title = title;
            drawn_scale = scale;
            drawn_icon = icon;
            drawn_icon_serial = icon != null ? icon.serial : 0;
            return true;
        }

        private void draw_edge (Marco.Theme theme, Edge edge, Marco.FrameType type, Marco.FrameFlags flags,
                                int client_width, int client_height, string? title, int scale,
                                WindowIcon? icon) {
            var rect = edge_rects[edge];
            if (rect.width <= 0 || rect.height <= 0) {
                edges[edge] = null;
                return;
            }
            int width, height, buffer_scale;
            if (edges[edge] != null) {
                edges[edge].get_size (out width, out height, out buffer_scale);
                if (width != rect.width * scale || height != rect.height * scale || buffer_scale != scale) {
                    edges[edge] = null;
                }
            }
            if (edges[edge] == null) {
                edges[edge] = new Backend.PixelBuffer (server, rect.width * scale, rect.height * scale, scale);
                edges[edge].set_accepts_input (true);
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
                          rect.width * scale, rect.height * scale, stride, scale,
                          icon != null ? icon.mini_icon : null, icon != null ? icon.icon : null);
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
