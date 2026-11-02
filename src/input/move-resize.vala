/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Marco's window grab operations: display.c meta_display_begin_grab_op ()
 * and meta_display_end_grab_op (), window.c update_move (), update_resize ()
 * and warp_grab_pointer (), and keybindings.c process_mouse_move_resize_grab
 * (), process_keyboard_move_grab () and process_keyboard_resize_grab (),
 * with edge resistance and snapping from edge-resistance.c.
 */

namespace Polo.Input {
    public enum GrabOp {
        NONE,
        MOVING,
        RESIZING_SE,
        RESIZING_S,
        RESIZING_SW,
        RESIZING_N,
        RESIZING_NE,
        RESIZING_NW,
        RESIZING_W,
        RESIZING_E,
        KEYBOARD_MOVING,
        KEYBOARD_RESIZING_UNKNOWN,
        KEYBOARD_RESIZING_S,
        KEYBOARD_RESIZING_N,
        KEYBOARD_RESIZING_W,
        KEYBOARD_RESIZING_E,
        KEYBOARD_RESIZING_SE,
        KEYBOARD_RESIZING_NE,
        KEYBOARD_RESIZING_SW,
        KEYBOARD_RESIZING_NW;

        public bool is_moving () {
            return this == MOVING || this == KEYBOARD_MOVING;
        }

        public bool is_resizing () {
            return (this >= RESIZING_SE && this <= RESIZING_E) ||
                   (this >= KEYBOARD_RESIZING_UNKNOWN && this <= KEYBOARD_RESIZING_NW);
        }

        public bool is_keyboard () {
            return this >= KEYBOARD_MOVING;
        }

        public bool is_mouse () {
            return this >= MOVING && this <= RESIZING_E;
        }

        public Marco.Gravity resize_gravity () {
            switch (this) {
                case RESIZING_SE:
                case KEYBOARD_RESIZING_SE:
                    return Marco.Gravity.NORTH_WEST;
                case RESIZING_S:
                case KEYBOARD_RESIZING_S:
                    return Marco.Gravity.NORTH;
                case RESIZING_SW:
                case KEYBOARD_RESIZING_SW:
                    return Marco.Gravity.NORTH_EAST;
                case RESIZING_N:
                case KEYBOARD_RESIZING_N:
                    return Marco.Gravity.SOUTH;
                case RESIZING_NE:
                case KEYBOARD_RESIZING_NE:
                    return Marco.Gravity.SOUTH_WEST;
                case RESIZING_NW:
                case KEYBOARD_RESIZING_NW:
                    return Marco.Gravity.SOUTH_EAST;
                case RESIZING_E:
                case KEYBOARD_RESIZING_E:
                    return Marco.Gravity.WEST;
                case RESIZING_W:
                case KEYBOARD_RESIZING_W:
                    return Marco.Gravity.EAST;
                default:
                    return Marco.Gravity.NORTH_WEST;
            }
        }
    }

    public class MoveResize : GLib.Object {
        /* meta_ui_get_drag_threshold (): GTK's default gtk-dnd-drag-threshold. */
        private const int DRAG_THRESHOLD = 8;
        private const int SHAKE_THRESHOLD = DRAG_THRESHOLD * 2;
        private const uint TILE_PREVIEW_TIMEOUT_MS = 200;
        private const int SMALL_INCREMENT = 1;
        private const int NORMAL_INCREMENT = 10;

        private const uint32 XK_Escape = 0xff1b;
        private const uint32 XK_Left = 0xff51;
        private const uint32 XK_Up = 0xff52;
        private const uint32 XK_Right = 0xff53;
        private const uint32 XK_Down = 0xff54;
        private const uint32 XK_KP_Home = 0xff95;
        private const uint32 XK_KP_Left = 0xff96;
        private const uint32 XK_KP_Up = 0xff97;
        private const uint32 XK_KP_Right = 0xff98;
        private const uint32 XK_KP_Down = 0xff99;
        private const uint32 XK_KP_Prior = 0xff9a;
        private const uint32 XK_KP_Next = 0xff9b;
        private const uint32 XK_KP_End = 0xff9c;
        private const uint32 XK_h = 0x68;
        private const uint32 XK_j = 0x6a;
        private const uint32 XK_k = 0x6b;
        private const uint32 XK_l = 0x6c;

        private Policy.Model model;

        public GrabOp op { get; private set; default = GrabOp.NONE; }
        public Window? window { get; private set; default = null; }
        private Backend.Seat? seat;
        private Backend.PointerGrab? grab;
        private bool frame_action;
        private uint button;
        private int anchor_root_x;
        private int anchor_root_y;
        private Marco.Rect anchor_window_pos;
        private Marco.Rect initial_window_pos;
        private int initial_x;
        private int initial_y;
        private bool threshold_movement_reached;
        private bool shaken_loose;
        private Marco.TileMode grab_tile_mode;
        private int grab_tile_monitor_number;
        private bool eat_buttons = false;
        private Marco.EdgeResistance? edge_resistance = null;
        private bool last_user_action_was_snap = false;
        private uint tile_preview_timeout = 0;

        public signal void tile_preview_shown (Marco.Rect area);
        public signal void tile_preview_hidden ();

        public MoveResize (Policy.Model model) {
            this.model = model;
            model.window_unmanaged.connect ((w) => {
                if (w == window) {
                    end_grab_op ();
                }
            });
            model.work_areas_changed.connect (cleanup_edges);
            model.notify["current-workspace"].connect (cleanup_edges);
        }

        public bool active { get { return op != GrabOp.NONE; } }

        /*
         * meta_display_begin_grab_op (). grab is the pointer grab the caller
         * started (the press of a mouse operation); keyboard operations take
         * their own after warping the pointer.
         */
        public bool begin (Backend.Seat seat, Window window, GrabOp new_op, Backend.PointerGrab? grab,
                           bool frame_action, uint button, int root_x, int root_y, int64 timestamp) {
            if (op != GrabOp.NONE) {
                if (grab != null) {
                    grab.end ();
                }
                return false;
            }
            if (new_op.is_moving () || new_op.is_resizing ()) {
                if (model.prefs.raise_on_click) {
                    model.raise (window);
                } else {
                    initial_x = root_x;
                    initial_y = root_y;
                    threshold_movement_reached = false;
                }
            }

            model.focus_policy.focus_window (window, timestamp);

            this.seat = seat;
            this.window = window;
            this.grab = grab;
            this.frame_action = frame_action;
            this.button = button;
            op = new_op;
            model.moving_window = new_op == GrabOp.MOVING || new_op == GrabOp.KEYBOARD_MOVING ? window : null;
            grab_tile_mode = window.tile_mode;
            grab_tile_monitor_number = window.tile_monitor_number;
            anchor_root_x = root_x;
            anchor_root_y = root_y;
            initial_window_pos = window.client_rect ();
            anchor_window_pos = initial_window_pos;
            shaken_loose = false;
            last_user_action_was_snap = false;
            if (op.is_resizing ()) {
                window.resize_gravity = op.resize_gravity ();
                window.resizing = true;
            }
            if (grab != null) {
                connect_grab ();
            }
            return true;
        }

        public bool begin_keyboard (Backend.Seat seat, Window window, GrabOp new_op, int64 timestamp) {
            if (op != GrabOp.NONE) {
                return false;
            }
            var pointer_grab = seat.begin_pointer_grab ();
            if (pointer_grab == null) {
                return false;
            }
            int x, y;
            warp_point (window, new_op, out x, out y);
            seat.warp_pointer (x, y);
            model.pointer_moved (x, y);
            if (!begin (seat, window, new_op, pointer_grab, false, 0, x, y, timestamp)) {
                return false;
            }
            anchor_window_pos = window.client_rect ();
            return true;
        }

        private void connect_grab () {
            grab.motion.connect (on_motion);
            grab.button.connect (on_button);
            grab.ended.connect (on_grab_ended);
        }

        private void disconnect_grab () {
            if (grab == null) {
                return;
            }
            grab.motion.disconnect (on_motion);
            grab.button.disconnect (on_button);
            grab.ended.disconnect (on_grab_ended);
        }

        public void end_grab_op () {
            if (op == GrabOp.NONE) {
                return;
            }
            var ending = window;
            var ending_op = op;
            op = GrabOp.NONE;
            model.moving_window = null;
            window = null;
            shaken_loose = false;
            tile_preview_hide ();
            if (ending != null && !model.prefs.raise_on_click &&
                (ending_op.is_moving () || ending_op.is_resizing ()) &&
                !threshold_movement_reached) {
                model.raise (ending);
            }
            if (ending != null) {
                ending.resizing = false;
            }
            if (ending_op.is_moving () || ending_op.is_resizing ()) {
                cleanup_edges ();
            }
            if (grab != null && !eat_buttons) {
                disconnect_grab ();
                var g = grab;
                grab = null;
                g.end ();
            }
        }

        private void on_grab_ended (bool cancelled) {
            disconnect_grab ();
            grab = null;
            eat_buttons = false;
            if (op != GrabOp.NONE) {
                end_grab_op ();
            }
        }

        private void check_threshold_reached (int x, int y) {
            if (model.prefs.raise_on_click || threshold_movement_reached) {
                return;
            }
            if ((initial_x - x).abs () >= 8 || (initial_y - y).abs () >= 8) {
                threshold_movement_reached = true;
            }
        }

        private void on_motion (double x, double y, uint time_msec) {
            model.pointer_moved (x, y);
            if (op == GrabOp.NONE) {
                return;
            }
            check_threshold_reached ((int) x, (int) y);
            bool snap = Backend.Modifiers.SHIFT in seat.get_modifiers ();
            if (op.is_moving ()) {
                update_move (snap, (int) x, (int) y);
            } else if (op.is_resizing ()) {
                update_resize (snap, (int) x, (int) y);
            }
        }

        /*
         * display.c event_callback (): a press ends a keyboard operation
         * (its press and release stay away from clients), a press of
         * another button ends a mouse one, and any release ends a mouse one
         * after a last update.
         */
        private void on_button (uint pressed_button, bool pressed, uint buttons_held) {
            if (eat_buttons) {
                if (!pressed && buttons_held == 0) {
                    eat_buttons = false;
                    var g = grab;
                    disconnect_grab ();
                    grab = null;
                    g.end ();
                }
                return;
            }
            if (op == GrabOp.NONE) {
                return;
            }
            if (op.is_keyboard ()) {
                if (pressed) {
                    eat_buttons = true;
                    end_grab_op ();
                }
                return;
            }
            if (pressed) {
                if (pressed_button != button) {
                    end_grab_op ();
                }
                return;
            }
            double x, y;
            seat.get_pointer_position (out x, out y);
            check_threshold_reached ((int) x, (int) y);
            if (!last_user_action_was_snap) {
                bool snap = Backend.Modifiers.SHIFT in seat.get_modifiers ();
                if (op.is_moving ()) {
                    if (window.tile_mode == Marco.TileMode.MAXIMIZED) {
                        model.maximize (window, WindowState.MAXIMIZED);
                        window.tile_mode = Marco.TileMode.NONE;
                    } else if (window.tile_mode != Marco.TileMode.NONE) {
                        model.tile (window);
                    } else {
                        update_move (snap, (int) x, (int) y);
                    }
                } else if (op.is_resizing ()) {
                    update_resize (snap, (int) x, (int) y);
                    update_tile_mode (window);
                }
            }
            end_grab_op ();
        }

        /* ---- Edge resistance (edge-resistance.c) -------------------------- */

        /* Cache edge resistance for this operation. */
        private unowned Marco.EdgeResistance edges () {
            if (edge_resistance == null) {
                var areas = model.work_areas ();
                var stacked = new GLib.Array<Marco.EdgeWindow> ();
                foreach (var w in model.get_stack_object ().get_bottom_to_top ()) {
                    if (w.workspace != -1 && w.workspace != model.current_workspace) {
                        continue;
                    }
                    var type = w.window_type;
                    stacked.append_val (Marco.EdgeWindow () {
                        outer = w.frame,
                        relevant = model.is_showing (w) && w != window && type != Marco.WindowType.DESKTOP &&
                                   type != Marco.WindowType.MENU && type != Marco.WindowType.SPLASHSCREEN,
                        is_dock = type == Marco.WindowType.DOCK
                    });
                }
                edge_resistance = new Marco.EdgeResistance (areas.screen, stacked.data, areas.xinerama_edges,
                                                            areas.screen_edges);
            }
            return edge_resistance;
        }

        private void cleanup_edges () {
            edge_resistance = null;
        }

        /* ---- Moving ------------------------------------------------------ */

        private Marco.FrameBorders total_borders (Window w) {
            var visible = w.visible_borders ();
            var invisible = Policy.Placement.invisible_borders (w);
            return Marco.FrameBorders () {
                left = visible.left + invisible.left,
                right = visible.right + invisible.right,
                top = visible.top + invisible.top,
                bottom = visible.bottom + invisible.bottom
            };
        }

        private static bool is_tiled (Window w) {
            return w.tiled && w.tile_mode != Marco.TileMode.NONE &&
                   w.tile_mode != Marco.TileMode.MAXIMIZED;
        }

        private static bool is_corner_tiled (Window w) {
            return w.tiled && (w.tile_mode == Marco.TileMode.TOP_LEFT || w.tile_mode == Marco.TileMode.TOP_RIGHT ||
                               w.tile_mode == Marco.TileMode.BOTTOM_LEFT ||
                               w.tile_mode == Marco.TileMode.BOTTOM_RIGHT);
        }

        private static void update_tile_mode (Window w) {
            if ((w.tile_mode == Marco.TileMode.LEFT || w.tile_mode == Marco.TileMode.RIGHT) && !w.tiled) {
                w.tile_mode = Marco.TileMode.NONE;
            }
        }

        private Marco.TileMode calculate_tiling_mode (int x, int y, Window w, Marco.Rect monitor, Marco.Rect work_area) {
            bool top = y >= monitor.y && y < work_area.y + SHAKE_THRESHOLD;
            bool bottom = y < monitor.y + monitor.height && y > work_area.y + work_area.height - SHAKE_THRESHOLD;
            if (model.can_tile (w) && x >= monitor.x && x < work_area.x + SHAKE_THRESHOLD) {
                return top ? Marco.TileMode.TOP_LEFT : bottom ? Marco.TileMode.BOTTOM_LEFT : Marco.TileMode.LEFT;
            } else if (model.can_tile (w) && x >= work_area.x + work_area.width - SHAKE_THRESHOLD &&
                       x < monitor.x + monitor.width) {
                return top ? Marco.TileMode.TOP_RIGHT : bottom ? Marco.TileMode.BOTTOM_RIGHT : Marco.TileMode.RIGHT;
            } else if (model.can_tile_maximized (w) && y >= monitor.y && y <= work_area.y &&
                       model.prefs.allow_top_tiling) {
                return Marco.TileMode.MAXIMIZED;
            }
            return Marco.TileMode.NONE;
        }

        private void tile_preview_refresh () {
            var w = window;
            bool needs_preview = false;
            if (w != null) {
                switch (w.tile_mode) {
                    case Marco.TileMode.MAXIMIZED:
                        needs_preview = !w.has_state (WindowState.MAXIMIZED);
                        break;
                    case Marco.TileMode.NONE:
                        break;
                    default:
                        needs_preview = !(w.tiled && (w.tile_mode == Marco.TileMode.LEFT ||
                                                      w.tile_mode == Marco.TileMode.RIGHT));
                        break;
                }
            }
            if (needs_preview) {
                tile_preview_shown (Policy.Constraints.tile_area (model, w));
            } else {
                tile_preview_hidden ();
            }
        }

        private void tile_preview_update (bool delay) {
            if (delay) {
                if (tile_preview_timeout > 0) {
                    return;
                }
                tile_preview_timeout = GLib.Timeout.add (TILE_PREVIEW_TIMEOUT_MS, () => {
                    tile_preview_timeout = 0;
                    tile_preview_refresh ();
                    return GLib.Source.REMOVE;
                });
            } else {
                if (tile_preview_timeout > 0) {
                    GLib.Source.remove (tile_preview_timeout);
                    tile_preview_timeout = 0;
                }
                tile_preview_refresh ();
            }
        }

        private void tile_preview_hide () {
            if (tile_preview_timeout > 0) {
                GLib.Source.remove (tile_preview_timeout);
                tile_preview_timeout = 0;
            }
            tile_preview_hidden ();
        }

        private void update_move (bool snap, int x, int y) {
            var w = window;
            int dx = x - anchor_root_x;
            int dy = y - anchor_root_y;
            int new_x = anchor_window_pos.x + dx;
            int new_y = anchor_window_pos.y + dy;
            if (dx == 0 && dy == 0) {
                return;
            }

            if (snap) {
                w.tile_mode = Marco.TileMode.NONE;
                w.tile_monitor_number = -1;
                w.tile_cycle = Marco.TileCycle.NONE;
            } else if (model.prefs.allow_tiling && !w.has_state (WindowState.MAXIMIZED) && !is_tiled (w)) {
                var outputs = model.get_outputs ();
                int monitor = -1;
                var pointer_output = model.output_at (x, y);
                for (int i = 0; i < outputs.length; i++) {
                    if (outputs[i] == pointer_output) {
                        monitor = i;
                    }
                }
                if (monitor >= 0) {
                    var old_tile_mode = w.tile_mode;
                    w.tile_mode = calculate_tiling_mode (x, y, w, pointer_output.rect,
                                                         model.work_area_for (pointer_output, w.workspace));
                    if (w.tile_mode != Marco.TileMode.NONE) {
                        w.tile_monitor_number = monitor;
                    }
                    if (old_tile_mode != w.tile_mode) {
                        w.tile_resized = false;
                        w.tile_cycle = Marco.TileCycle.NONE;
                    }
                }
            }

            bool maximized = w.has_state (WindowState.MAXIMIZED);
            if ((maximized && dy.abs () >= SHAKE_THRESHOLD) ||
                (is_tiled (w) && int.max (dx.abs (), dy.abs ()) >= SHAKE_THRESHOLD)) {
                shaken_loose = maximized;
                w.tile_mode = Marco.TileMode.NONE;
                w.tile_cycle = Marco.TileCycle.NONE;
                w.tiled = false;

                double prop = ((double) (x - initial_window_pos.x)) / ((double) initial_window_pos.width);
                initial_window_pos.x = x - (int) (prop * (double) w.saved_rect.width);
                initial_window_pos.y = y;
                if (w.has_frame ()) {
                    initial_window_pos.y += total_borders (w).top / 2;
                }
                w.saved_rect.x = initial_window_pos.x;
                w.saved_rect.y = initial_window_pos.y;
                anchor_root_x = x;
                anchor_root_y = y;
                model.unmaximize (w, WindowState.MAXIMIZED);
                anchor_window_pos = w.user_rect;
                return;
            } else if (shaken_loose || maximized) {
                var outputs = model.get_outputs ();
                int window_output = Policy.Placement.xinerama_for_rect (outputs, w.frame);
                for (int monitor = 0; monitor < outputs.length; monitor++) {
                    var work_area = model.work_area_for (outputs[monitor], w.workspace);
                    if (x >= work_area.x + SHAKE_THRESHOLD &&
                        x < work_area.x + work_area.width - SHAKE_THRESHOLD &&
                        y >= work_area.y && y < work_area.y + SHAKE_THRESHOLD) {
                        if (window_output != monitor) {
                            w.saved_rect.x = work_area.x;
                            w.saved_rect.y = work_area.y;
                            if (w.has_frame ()) {
                                var b = total_borders (w);
                                w.saved_rect.x += b.left;
                                w.saved_rect.y += b.top;
                            }
                            w.user_rect.x = w.saved_rect.x;
                            w.user_rect.y = w.saved_rect.y;
                            model.unmaximize (w, WindowState.MAXIMIZED);
                        }
                        initial_window_pos = work_area;
                        anchor_root_x = x;
                        anchor_root_y = y;
                        shaken_loose = false;
                        w.tile_mode = Marco.TileMode.NONE;
                        w.tile_cycle = Marco.TileCycle.NONE;
                        model.maximize (w, WindowState.MAXIMIZED);
                        return;
                    }
                }
            }

            tile_preview_update (w.tile_mode != Marco.TileMode.NONE);

            var old = w.client_rect ();
            if (w.has_state (WindowState.MAXIMIZED_H) || is_tiled (w)) {
                new_x = old.x;
            }
            if (w.has_state (WindowState.MAXIMIZED_V) || is_corner_tiled (w)) {
                new_y = old.y;
            }
            last_user_action_was_snap = snap;
            edges ().for_move (w.frame, old.x, old.y, ref new_x, ref new_y, snap, false);
            model.move (w, true, new_x, new_y, frame_action);
        }

        /* ---- Resizing ---------------------------------------------------- */

        private void update_resize (bool snap, int x, int y) {
            var w = window;
            int dx = x - anchor_root_x;
            int dy = y - anchor_root_y;
            int new_w = anchor_window_pos.width;
            int new_h = anchor_window_pos.height;
            if (dx == 0 && dy == 0) {
                return;
            }

            if (op == GrabOp.KEYBOARD_RESIZING_UNKNOWN) {
                if (dx > 0 && dy > 0) {
                    op = GrabOp.KEYBOARD_RESIZING_SE;
                } else if (dx < 0 && dy > 0) {
                    op = GrabOp.KEYBOARD_RESIZING_SW;
                } else if (dx > 0 && dy < 0) {
                    op = GrabOp.KEYBOARD_RESIZING_NE;
                } else if (dx < 0 && dy < 0) {
                    op = GrabOp.KEYBOARD_RESIZING_NW;
                } else if (dx < 0) {
                    op = GrabOp.KEYBOARD_RESIZING_W;
                } else if (dx > 0) {
                    op = GrabOp.KEYBOARD_RESIZING_E;
                } else if (dy > 0) {
                    op = GrabOp.KEYBOARD_RESIZING_S;
                } else {
                    op = GrabOp.KEYBOARD_RESIZING_N;
                }
                update_keyboard_resize ();
            }

            switch (op) {
                case GrabOp.RESIZING_SE:
                case GrabOp.RESIZING_NE:
                case GrabOp.RESIZING_E:
                case GrabOp.KEYBOARD_RESIZING_SE:
                case GrabOp.KEYBOARD_RESIZING_NE:
                case GrabOp.KEYBOARD_RESIZING_E:
                    new_w += dx;
                    break;
                case GrabOp.RESIZING_NW:
                case GrabOp.RESIZING_SW:
                case GrabOp.RESIZING_W:
                case GrabOp.KEYBOARD_RESIZING_NW:
                case GrabOp.KEYBOARD_RESIZING_SW:
                case GrabOp.KEYBOARD_RESIZING_W:
                    new_w -= dx;
                    break;
                default:
                    break;
            }
            switch (op) {
                case GrabOp.RESIZING_SE:
                case GrabOp.RESIZING_S:
                case GrabOp.RESIZING_SW:
                case GrabOp.KEYBOARD_RESIZING_SE:
                case GrabOp.KEYBOARD_RESIZING_S:
                case GrabOp.KEYBOARD_RESIZING_SW:
                    new_h += dy;
                    break;
                case GrabOp.RESIZING_N:
                case GrabOp.RESIZING_NE:
                case GrabOp.RESIZING_NW:
                case GrabOp.KEYBOARD_RESIZING_N:
                case GrabOp.KEYBOARD_RESIZING_NE:
                case GrabOp.KEYBOARD_RESIZING_NW:
                    new_h -= dy;
                    break;
                default:
                    break;
            }

            var old = w.client_rect ();
            switch (op) {
                case GrabOp.RESIZING_S:
                case GrabOp.RESIZING_N:
                    new_w = old.width;
                    break;
                case GrabOp.RESIZING_E:
                case GrabOp.RESIZING_W:
                    new_h = old.height;
                    break;
                default:
                    break;
            }

            var gravity = op.resize_gravity ();
            last_user_action_was_snap = snap;
            edges ().for_resize (w.frame, w.hints.width_inc, w.hints.height_inc, old.width, old.height,
                                 ref new_w, ref new_h, gravity, snap, false);
            w.resize_gravity = gravity;
            if (old.width != new_w || old.height != new_h) {
                model.resize_with_gravity (w, true, new_w, new_h, gravity);
            }
        }

        /* ---- Pointer warping (window.c warp_grab_pointer ()) ------------- */

        private void warp_point (Window w, GrabOp warp_op, out int x, out int y) {
            var rect = w.frame;
            switch (warp_op) {
                case GrabOp.KEYBOARD_RESIZING_S:
                    x = rect.width / 2;
                    y = rect.height - 1;
                    break;
                case GrabOp.KEYBOARD_RESIZING_N:
                    x = rect.width / 2;
                    y = 0;
                    break;
                case GrabOp.KEYBOARD_RESIZING_W:
                    x = 0;
                    y = rect.height / 2;
                    break;
                case GrabOp.KEYBOARD_RESIZING_E:
                    x = rect.width - 1;
                    y = rect.height / 2;
                    break;
                case GrabOp.KEYBOARD_RESIZING_SE:
                    x = rect.width - 1;
                    y = rect.height - 1;
                    break;
                case GrabOp.KEYBOARD_RESIZING_NE:
                    x = rect.width - 1;
                    y = 0;
                    break;
                case GrabOp.KEYBOARD_RESIZING_SW:
                    x = 0;
                    y = rect.height - 1;
                    break;
                case GrabOp.KEYBOARD_RESIZING_NW:
                    x = 0;
                    y = 0;
                    break;
                default:
                    x = rect.width / 2;
                    y = rect.height / 2;
                    break;
            }
            x += rect.x;
            y += rect.y;

            var outputs = model.get_outputs ();
            if (outputs.length > 0) {
                var screen = outputs[0].rect;
                foreach (var output in outputs) {
                    Marco.Rect merged;
                    screen.union (output.rect, out merged);
                    screen = merged;
                }
                x = x.clamp (0, screen.x + screen.width - 1);
                y = y.clamp (0, screen.y + screen.height - 1);
            }
        }

        private void warp_grab_pointer () {
            int x, y;
            warp_point (window, op, out x, out y);
            anchor_root_x = x;
            anchor_root_y = y;
            anchor_window_pos = window.client_rect ();
            seat.warp_pointer (x, y);
            model.pointer_moved (x, y);
        }

        private void update_keyboard_resize () {
            if (window.resizing) {
                window.resize_gravity = op.resize_gravity ();
            }
            warp_grab_pointer ();
        }

        /* ---- Keys (keybindings.c) ---------------------------------------- */

        private static bool is_modifier (uint32 keysym) {
            return (keysym >= 0xffe1 && keysym <= 0xffee) || (keysym >= 0xfe01 && keysym <= 0xfe13) ||
                   keysym == 0xff7e || keysym == 0xff7f;
        }

        /*
         * Every key goes to the operation while it lasts (all keys are
         * grabbed); a key that does not keep it ends it.
         */
        public Backend.FilterResult key (Backend.Seat seat, Backend.KeyEvent event) {
            if (op == GrabOp.NONE || seat != this.seat) {
                return Backend.FilterResult.PASS;
            }
            if (!event.pressed) {
                return Backend.FilterResult.CONSUME;
            }
            bool keep;
            if (op.is_mouse ()) {
                keep = process_mouse_move_resize_grab (event);
            } else if (op == GrabOp.KEYBOARD_MOVING) {
                keep = process_keyboard_move_grab (event);
            } else {
                keep = process_keyboard_resize_grab (event);
            }
            if (!keep) {
                end_grab_op ();
            }
            return Backend.FilterResult.CONSUME;
        }

        private void restore_initial () {
            model.move_resize_internal (window,
                                        Marco.MoveResizeFlags.IS_MOVE_ACTION |
                                        Marco.MoveResizeFlags.IS_RESIZE_ACTION |
                                        Marco.MoveResizeFlags.IS_USER_ACTION,
                                        Marco.Gravity.NORTH_WEST, initial_window_pos);
        }

        private bool process_mouse_move_resize_grab (Backend.KeyEvent event) {
            if (event.base_keysym != XK_Escape) {
                return true;
            }
            window.tile_mode = grab_tile_mode;
            window.tile_monitor_number = grab_tile_monitor_number;
            if (shaken_loose) {
                model.maximize (window, WindowState.MAXIMIZED);
            } else if (window.tile_mode != Marco.TileMode.MAXIMIZED && window.tile_mode != Marco.TileMode.NONE) {
                model.tile (window);
            } else {
                restore_initial ();
            }
            return false;
        }

        private bool process_keyboard_move_grab (Backend.KeyEvent event) {
            uint32 keysym = event.base_keysym;
            if (is_modifier (keysym)) {
                return true;
            }
            var position = window.client_rect ();
            int x = position.x;
            int y = position.y;
            bool smart_snap = Backend.Modifiers.SHIFT in event.modifiers;
            int incr;
            if (smart_snap) {
                incr = 1;
            } else if (Backend.Modifiers.CONTROL in event.modifiers) {
                incr = SMALL_INCREMENT;
            } else {
                incr = NORMAL_INCREMENT;
            }

            if (keysym == XK_Escape) {
                if (shaken_loose) {
                    model.maximize (window, WindowState.MAXIMIZED);
                } else {
                    restore_initial ();
                }
            }

            bool handled = false;
            switch (keysym) {
                case XK_KP_Home:
                case XK_KP_Prior:
                case XK_Up:
                case XK_KP_Up:
                case XK_k:
                    y -= incr;
                    handled = true;
                    break;
                case XK_KP_End:
                case XK_KP_Next:
                case XK_Down:
                case XK_KP_Down:
                case XK_j:
                    y += incr;
                    handled = true;
                    break;
                default:
                    break;
            }
            switch (keysym) {
                case XK_KP_Home:
                case XK_KP_End:
                case XK_Left:
                case XK_KP_Left:
                case XK_h:
                    x -= incr;
                    handled = true;
                    break;
                case XK_KP_Prior:
                case XK_KP_Next:
                case XK_Right:
                case XK_KP_Right:
                case XK_l:
                    x += incr;
                    handled = true;
                    break;
                default:
                    break;
            }

            if (handled) {
                last_user_action_was_snap = smart_snap;
                edges ().for_move (window.frame, position.x, position.y, ref x, ref y, smart_snap, true);
                model.move (window, true, x, y, frame_action);
                warp_grab_pointer ();
            }
            return handled;
        }

        private bool process_keyboard_resize_grab_op_change (uint32 keysym) {
            bool up = keysym == XK_Up || keysym == XK_KP_Up || keysym == XK_k;
            bool down = keysym == XK_Down || keysym == XK_KP_Down || keysym == XK_j;
            bool left = keysym == XK_Left || keysym == XK_KP_Left || keysym == XK_h;
            bool right = keysym == XK_Right || keysym == XK_KP_Right || keysym == XK_l;
            GrabOp changed = GrabOp.NONE;
            switch (op) {
                case GrabOp.KEYBOARD_RESIZING_UNKNOWN:
                    changed = up ? GrabOp.KEYBOARD_RESIZING_N : down ? GrabOp.KEYBOARD_RESIZING_S :
                              left ? GrabOp.KEYBOARD_RESIZING_W : right ? GrabOp.KEYBOARD_RESIZING_E :
                              GrabOp.NONE;
                    break;
                case GrabOp.KEYBOARD_RESIZING_S:
                case GrabOp.KEYBOARD_RESIZING_N:
                    changed = left ? GrabOp.KEYBOARD_RESIZING_W : right ? GrabOp.KEYBOARD_RESIZING_E :
                              GrabOp.NONE;
                    break;
                case GrabOp.KEYBOARD_RESIZING_W:
                case GrabOp.KEYBOARD_RESIZING_E:
                    changed = up ? GrabOp.KEYBOARD_RESIZING_N : down ? GrabOp.KEYBOARD_RESIZING_S :
                              GrabOp.NONE;
                    break;
                default:
                    break;
            }
            if (changed == GrabOp.NONE) {
                return false;
            }
            op = changed;
            update_keyboard_resize ();
            return true;
        }

        private bool process_keyboard_resize_grab (Backend.KeyEvent event) {
            uint32 keysym = event.base_keysym;
            if (is_modifier (keysym)) {
                return true;
            }
            if (keysym == XK_Escape) {
                restore_initial ();
                return false;
            }
            if (process_keyboard_resize_grab_op_change (keysym)) {
                return true;
            }

            var rect = window.client_rect ();
            int width = rect.width;
            int height = rect.height;
            var gravity = op.resize_gravity ();
            bool smart_snap = Backend.Modifiers.SHIFT in event.modifiers;
            int width_inc, height_inc;
            if (smart_snap) {
                width_inc = 1;
                height_inc = 1;
            } else if (Backend.Modifiers.CONTROL in event.modifiers) {
                width_inc = SMALL_INCREMENT;
                height_inc = SMALL_INCREMENT;
            } else {
                width_inc = NORMAL_INCREMENT;
                height_inc = NORMAL_INCREMENT;
            }
            if (window.hints.width_inc > 1) {
                width_inc = window.hints.width_inc;
            }
            if (window.hints.height_inc > 1) {
                height_inc = window.hints.height_inc;
            }

            bool north = gravity == Marco.Gravity.NORTH || gravity == Marco.Gravity.NORTH_WEST ||
                         gravity == Marco.Gravity.NORTH_EAST;
            bool south = gravity == Marco.Gravity.SOUTH || gravity == Marco.Gravity.SOUTH_WEST ||
                         gravity == Marco.Gravity.SOUTH_EAST;
            bool east = gravity == Marco.Gravity.EAST || gravity == Marco.Gravity.SOUTH_EAST ||
                        gravity == Marco.Gravity.NORTH_EAST;
            bool west = gravity == Marco.Gravity.WEST || gravity == Marco.Gravity.SOUTH_WEST ||
                        gravity == Marco.Gravity.NORTH_WEST;

            bool handled = false;
            switch (keysym) {
                case XK_Up:
                case XK_KP_Up:
                case XK_k:
                    if (north) {
                        height -= height_inc;
                    } else if (south) {
                        height += height_inc;
                    }
                    handled = true;
                    break;
                case XK_Down:
                case XK_KP_Down:
                case XK_j:
                    if (north) {
                        height += height_inc;
                    } else if (south) {
                        height -= height_inc;
                    }
                    handled = true;
                    break;
                case XK_Left:
                case XK_KP_Left:
                case XK_h:
                    if (east) {
                        width += width_inc;
                    } else if (west) {
                        width -= width_inc;
                    }
                    handled = true;
                    break;
                case XK_Right:
                case XK_KP_Right:
                case XK_l:
                    if (east) {
                        width -= width_inc;
                    } else if (west) {
                        width += width_inc;
                    }
                    handled = true;
                    break;
                default:
                    break;
            }
            if (height < 1) {
                height = 1;
            }
            if (width < 1) {
                width = 1;
            }
            if (handled) {
                last_user_action_was_snap = smart_snap;
                edges ().for_resize (window.frame, window.hints.width_inc, window.hints.height_inc, rect.width,
                                     rect.height, ref width, ref height, gravity, smart_snap, true);
                if (rect.width != width || rect.height != height) {
                    window.resize_gravity = gravity;
                    model.resize_with_gravity (window, true, width, height, gravity);
                }
                update_keyboard_resize ();
            }
            return handled;
        }
    }
}
