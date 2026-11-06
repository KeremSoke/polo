/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The compositor-neutral window model, part of libpolo-policy.
 * Value types (rectangles, hints, borders, struts) come from libpolo-marco,
 * so the C ports of Marco's algorithms read exactly these fields.
 */

namespace Polo {
    public enum WindowKind {
        XDG,
        X11,
        LAYER,
        INTERNAL
    }

    /* Bit values are part of the polo_test_v1 window_info event. */
    [Flags]
    public enum WindowState {
        MAPPED,
        MINIMIZED,
        MAXIMIZED_H,
        MAXIMIZED_V,
        FULLSCREEN,
        SHADED,
        ABOVE,
        BELOW,
        STICKY,
        URGENT,
        DEMANDS_ATTENTION,
        MODAL,
        ACTIVE,
        SKIP_TASKBAR,
        SKIP_PAGER,
        TILED_LEFT,
        TILED_RIGHT,
        TILED_TOP,
        TILED_BOTTOM,
        MINIMIZING;

        public const WindowState MAXIMIZED = MAXIMIZED_H | MAXIMIZED_V;
    }

    /*
     * What the window manager allows on a window: Marco's has_*_func,
     * decorated, border_only and always_sticky.
     */
    [Flags]
    public enum WindowFeatures {
        CLOSE,
        MINIMIZE,
        MAXIMIZE,
        MOVE,
        RESIZE,
        SHADE,
        FULLSCREEN,
        DECORATED,
        BORDER_ONLY,
        ALWAYS_STICKY;

        public const WindowFeatures MWM_FUNCTIONS = CLOSE | MINIMIZE | MAXIMIZE | MOVE | RESIZE;
    }

    public class Window : GLib.Object {
        private static uint64 next_id = 1;

        /* No _NET_WM_USER_TIME (or xdg activation) information. */
        public const int64 USER_TIME_UNSET = -1;

        public uint64 id { get; construct; }
        public WindowKind kind { get; construct; }
        public WindowAdapter adapter { get; construct; }

        /* A stable name for logs and tests (scenario ids). */
        public string? label { get; set; }
        public string? title { get; set; }
        public string? app_id { get; set; }
        /* WM_CLASS class, for Marco's terminal test; null for xdg windows. */
        public string? res_class = null;
        public WindowState state { get; set; }

        public Marco.WindowType window_type { get; set; default = Marco.WindowType.NORMAL; }
        /* _NET_WM_WINDOW_TYPE; null when the client names none (all xdg toplevels). */
        public Marco.WindowType? type_hint = null;
        public WindowFeatures features { get; set; default = WindowFeatures.MWM_FUNCTIONS |
                                                             WindowFeatures.DECORATED; }
        /* _MOTIF_WM_HINTS: the functions and decorations the client asks for. */
        public WindowFeatures mwm_functions = WindowFeatures.MWM_FUNCTIONS;
        public bool mwm_decorated = true;
        public bool mwm_border_only = false;
        /* Client decoration request from _MOTIF_WM_HINTS. */
        public bool mwm_wants_frame = true;
        /* _NET_WM_STATE_SKIP_TASKBAR and _SKIP_PAGER as the client asked. */
        public bool wm_state_skip_taskbar = false;
        public bool wm_state_skip_pager = false;
        public Marco.StackLayer layer { get; set; default = Marco.StackLayer.NORMAL; }
        public weak Window? transient_for { get; set; }
        /* WM_TRANSIENT_FOR names the root window: transient for its whole group. */
        public bool transient_parent_is_root_window = false;
        /* Marco stack_position; -1 when not in the stack. */
        public int stack_position = -1;
        /* placement-mode manual: start a keyboard move once focused. */
        public bool move_after_placement = false;
        /* An interactive resize is under way (xdg_toplevel resizing state). */
        public bool resizing { get; set; default = false; }
        /* Anchor preserved during an interactive resize. */
        public Marco.Gravity resize_gravity = Marco.Gravity.NORTH_WEST;
        /* No pong within the ping timeout after a close request. */
        public bool not_responding { get; set; default = false; }
        /* Marco's MetaGroup (the client leader); 0 for none. */
        public uint64 group = 0;
        /* The keyboard layout group libxklavier keeps for the window; -1 for none yet. */
        public int xkb_group = -1;
        /* Unminimized only while the switcher shows it (window->tab_unminimized). */
        public bool tab_unminimized = false;

        /* Workspace index; -1 for sticky (all workspaces). */
        public int workspace { get; set; default = 0; }
        /* window->workspace, which a sticky window keeps while workspace is -1. */
        public int home_workspace = 0;
        /* Orders a workspace's windows as workspace->windows does. */
        public uint workspace_serial = 0;
        /* Whether the client accepts keyboard focus (ICCCM input hint). */
        public bool accepts_focus { get; set; default = true; }
        public int64 user_time { get; set; default = USER_TIME_UNSET; }
        /* Startup notification (an xdg activation token) before the first show. */
        public int64 initial_timestamp = USER_TIME_UNSET;

        /* The outer frame (decorations included), global logical coordinates. */
        public Marco.Rect frame;
        /* Client area to return to when leaving maximized or fullscreen (Marco saved_rect). */
        public Marco.Rect saved_rect;
        /* Client area the user or client last chose, to snap back to (Marco user_rect). */
        public Marco.Rect user_rect;
        public bool placed = false;
        /* _NET_WM_STATE asked for at map time, applied once placed. */
        public bool maximize_horizontally_after_placement = false;
        public bool maximize_vertically_after_placement = false;
        public bool fullscreen_after_placement = false;
        public bool minimize_after_placement = false;
        /* WM_HINTS initial state: mapped minimized (window->initially_iconic). */
        public bool initially_iconic = false;
        /* _NET_WM_DESKTOP at map (window->initial_workspace); -1 for none. */
        public int initial_workspace = -1;
        public bool force_save_user_rect = true;
        public bool require_fully_onscreen = true;
        public bool require_on_single_xinerama = true;
        public bool require_titlebar_visible = true;
        public bool tiled = false;
        /* Marco's frame->is_flashing: the visual bell inverts frame's focus style */
        public bool frame_flashing = false;
        public Marco.TileMode tile_mode = Marco.TileMode.NONE;
        public Marco.TileCycle tile_cycle = Marco.TileCycle.NONE;
        public int tile_monitor_number = -1;
        public bool tile_resized = false;
        /* _NET_WM_FULLSCREEN_MONITORS; -1 for the window's own output. */
        public int fullscreen_monitors[4] = { -1, -1, -1, -1 };
        /* Decorations around the client area; zero for client-side ones. */
        public Marco.FrameBorders borders;
        public Marco.SizeHints hints;
        /* Space this window reserves at screen edges (panels, docks). */
        public Marco.Strut[] struts = {};

        public signal void geometry_changed ();
        public signal void unmanaged ();

        public Window (WindowKind kind, WindowAdapter adapter) {
            Object (id: next_id++, kind: kind, adapter: adapter);
            hints.normalize ();
        }

        public bool has_feature (WindowFeatures flags) {
            return (features & flags) == flags;
        }

        public bool has_state (WindowState flags) {
            return (state & flags) == flags;
        }

        public void set_state_flags (WindowState flags, bool on) {
            state = on ? state | flags : state & ~flags;
        }

        public bool has_frame () {
            return has_feature (WindowFeatures.DECORATED);
        }

        public Marco.FrameBorders visible_borders () {
            if (!has_frame () || has_state (WindowState.FULLSCREEN)) {
                return Marco.FrameBorders ();
            }
            return borders;
        }

        /* Returns the outer frame; shaded windows expose only the titlebar. */
        public Marco.Rect outer_rect () {
            var outer = frame;
            if (has_frame () && has_state (WindowState.SHADED)) {
                outer.height = visible_borders ().top;
            }
            return outer;
        }

        public Marco.Rect client_rect () {
            var b = visible_borders ();
            return Marco.Rect () {
                x = frame.x + b.left,
                y = frame.y + b.top,
                width = frame.width - b.left - b.right,
                height = frame.height - b.top - b.bottom
            };
        }

        public void set_client_rect (Marco.Rect client) {
            var b = visible_borders ();
            frame = Marco.Rect () {
                x = client.x - b.left,
                y = client.y - b.top,
                width = client.width + b.left + b.right,
                height = client.height + b.top + b.bottom
            };
        }

        public bool has_user_position () {
            return (hints.flags & (Marco.SizeHintFlags.US_POSITION |
                                   Marco.SizeHintFlags.P_POSITION)) != 0;
        }

        public string describe () {
            return "%s#%llu (%s)".printf (kind.to_string (), id, label ?? app_id ?? title ?? "?");
        }
    }
}
