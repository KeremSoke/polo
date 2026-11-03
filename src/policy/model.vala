/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Polo.Policy.Model: outputs, windows, the stack, focus and
 * workspaces, and every window-management decision about them. Decisions
 * depend only on (model, MarcoPrefs snapshot, event), so the replayer in
 * tests/policy can play Marco oracle scenarios against it with no
 * compositor at all.
 *
 * Placement, constraints, stacking and focus are Marco's place.c,
 * constraints.c, stack.c and focus code (lib/marco, Stack, FocusPolicy);
 * every geometry change goes through move_resize_internal ().
 */

namespace Polo.Policy {
    /* Marco MetaClientType: who sent an activation (_NET_ACTIVE_WINDOW). */
    public enum ActivationSource {
        UNKNOWN,
        APPLICATION,
        PAGER
    }

    public class Output : GLib.Object {
        public string name { get; construct; }
        public bool primary { get; set; }
        public Marco.Rect rect;
        /* The work area on the current workspace (Model.work_areas ()). */
        public Marco.Rect work_area;

        public Output (string name, Marco.Rect rect) {
            Object (name: name);
            this.rect = rect;
            this.work_area = rect;
        }

        public bool contains (double x, double y) {
            return x >= rect.x && x < rect.x + rect.width && y >= rect.y && y < rect.y + rect.height;
        }
    }

    public class Model : GLib.Object {
        /*
         * Struts that apply on every workspace besides the windows' own:
         * layer-shell exclusive zones, as Marco per-output struts
         * (WorkAreas.strut_from_area ()).
         */
        public delegate GLib.SList<Marco.Strut?> LayerStruts ();

        /* A plain field: delegates cannot be GObject properties. */
        public LayerStruts? layer_struts = null;

        public MarcoPrefs prefs { get; set; }
        public Window? focused { get; private set; }
        public int current_workspace { get; private set; default = 0; }
        public int previous_workspace { get; private set; default = 0; }
        /* Marco display->last_user_time: the newest user interaction. */
        public int64 last_user_time { get; private set; default = 0; }
        public int n_workspaces { get; private set; default = 1; }
        /* MetaScreen's layout; rows and columns <= 0 mean "as many as it takes". */
        public int workspace_rows { get; private set; default = 1; }
        public int workspace_columns { get; private set; default = -1; }
        public bool workspaces_vertical { get; private set; default = false; }
        public Corner workspaces_corner { get; private set; default = Corner.TOPLEFT; }
        /* The window of a MOVING or KEYBOARD_MOVING grab op. */
        public Window? moving_window { get; set; default = null; }

        private GLib.GenericArray<Output> outputs = new GLib.GenericArray<Output> ();
        /* Managed windows, in the order they became known. */
        private GLib.GenericArray<Window> windows = new GLib.GenericArray<Window> ();
        /* Mapped windows, bottom to top, sorted by layer. */
        private Stack stack;
        /* workspace->mru_list per workspace, most recent first. */
        private GLib.HashTable<int, GLib.GenericArray<Window>> mru_lists =
            new GLib.HashTable<int, GLib.GenericArray<Window>> (GLib.direct_hash, GLib.direct_equal);
        public double pointer_x { get; private set; default = 0; }
        public double pointer_y { get; private set; default = 0; }
        public FocusPolicy focus_policy { get; private set; }
        /* Per workspace, built on demand; cleared by invalidate_work_areas (). */
        private GLib.GenericArray<WorkAreas?> work_area_cache = new GLib.GenericArray<WorkAreas?> ();

        /* The stacking order or a window's visibility changed. */
        public signal void stack_changed ();
        /* Keyboard focus moved; focused may be null. */
        public signal void focus_changed ();
        public signal void work_areas_changed ();
        /* Before the first focus of a mapped window. */
        public signal void window_mapped (Window window);

        public Model (MarcoPrefs prefs) {
            Object (prefs: prefs);
            stack = new Stack (this);
            focus_policy = new FocusPolicy (this);
            n_workspaces = prefs.num_workspaces.clamp (1, MAX_REASONABLE_WORKSPACES);
            notify["prefs"].connect (update_num_workspaces);
        }

        /* ---- Outputs -------------------------------------------------- */

        public void add_output (Output output) {
            outputs.add (output);
            invalidate_work_areas ();
            if (outputs.length == 1) {
                foreach (var window in windows) {
                    relocate (window, output.rect, output);
                }
            }
        }

        public void remove_output (Output output) {
            var affected = new GLib.GenericArray<Window> ();
            foreach (var window in windows) {
                if (output_for_window (window) == output) {
                    affected.add (window);
                }
            }
            outputs.remove (output);
            invalidate_work_areas ();
            Output? destination = null;
            foreach (var candidate in outputs) {
                if (destination == null || candidate.primary) {
                    destination = candidate;
                }
            }
            if (destination != null) {
                foreach (var window in affected) {
                    relocate (window, output.rect, destination);
                }
            }
        }

        public void reconfigure_output (Output output, Marco.Rect rect) {
            var old = output.rect;
            var affected = new GLib.GenericArray<Window> ();
            foreach (var window in windows) {
                if (output_for_window (window) == output) {
                    affected.add (window);
                }
            }
            output.rect = rect;
            invalidate_work_areas ();
            foreach (var window in affected) {
                relocate (window, old, output);
            }
        }

        private static Marco.Rect translated (Marco.Rect rect, Marco.Rect old, Marco.Rect area) {
            rect.x = int.max (area.x, int.min (area.x + rect.x - old.x,
                                              area.x + area.width - rect.width));
            rect.y = int.max (area.y, int.min (area.y + rect.y - old.y,
                                              area.y + area.height - rect.height));
            return rect;
        }

        /*
         * Carry a window to another output, keeping its offset, then let
         * the constraints refit it (as Marco does for every window when
         * the xineramas change).
         */
        private void relocate (Window window, Marco.Rect old, Output destination) {
            var area = work_area_for (destination, window.workspace);
            window.user_rect = translated (window.user_rect, old, area);
            window.saved_rect = translated (window.saved_rect, old, area);
            var client = translated (window.client_rect (), old, area);
            window.set_client_rect (client);
            if (window.has_state (WindowState.MAPPED)) {
                move_resize_now (window);
            }
        }

        /* Move a window to another output, keeping its relative position
         * (as on hotplug); task bars ask for it with set_fullscreen. */
        public void move_to_output (Window window, Output destination) {
            var current = output_for_window (window);
            if (current == null || current == destination) {
                return;
            }
            relocate (window, current.rect, destination);
        }

        public unowned GLib.GenericArray<Output> get_outputs () {
            return outputs;
        }

        public Output? output_at (double x, double y) {
            foreach (var output in outputs) {
                if (output.contains (x, y)) {
                    return output;
                }
            }
            return outputs.length > 0 ? outputs[0] : null;
        }

        /* The output holding the centre of the window's frame. */
        public Output? output_for_window (Window window) {
            return output_at (window.frame.x + window.frame.width / 2,
                              window.frame.y + window.frame.height / 2);
        }

        /* ---- Work areas------------------------------------------------ */

        /*
         * The work areas of a workspace (-1: the current one), computed
         * as Marco's ensure_work_areas_validated () does from the struts
         * of the windows on it, sticky ones included, and of layer-shell
         * surfaces.
         */
        public WorkAreas work_areas (int workspace = -1) {
            int index = workspace < 0 ? current_workspace : workspace;
            while (work_area_cache.length <= index) {
                work_area_cache.add (null);
            }
            var areas = work_area_cache[index];
            if (areas == null) {
                areas = new WorkAreas (outputs, struts_on (index));
                work_area_cache[index] = areas;
            }
            return areas;
        }

        /* Marco meta_window_get_work_area_for_xinerama (). */
        public Marco.Rect work_area_for (Output output, int workspace = -1) {
            return work_areas (workspace).area_for (output);
        }

        /* Collect the struts that build this workspace's work areas. */
        private GLib.SList<Marco.Strut?> struts_on (int workspace) {
            GLib.SList<Marco.Strut?> struts;
            if (layer_struts != null) {
                struts = layer_struts ();
            } else {
                struts = new GLib.SList<Marco.Strut?> ();
            }
            foreach (var window in windows) {
                if (window.workspace != -1 && window.workspace != workspace) {
                    continue;
                }
                foreach (unowned Marco.Strut strut in window.struts) {
                    struts.prepend (strut);
                }
            }
            return struts;
        }

        /* Drop cached work areas and refit mapped windows after a change. */
        public void invalidate_work_areas () {
            work_area_cache.remove_range (0, work_area_cache.length);
            refresh_output_work_areas ();
            foreach (var window in windows) {
                if (window.has_state (WindowState.MAPPED)) {
                    move_resize_now (window);
                }
            }
        }

        private void refresh_output_work_areas () {
            if (outputs.length == 0) {
                return;
            }
            var areas = work_areas ();
            foreach (var output in outputs) {
                output.work_area = areas.area_for (output);
            }
            work_areas_changed ();
        }

        /* ---- Windows -------------------------------------------------- */

        public void manage (Window window) {
            if (!is_managed (window)) {
                windows.add (window);
            }
        }

        public bool is_managed (Window window) {
            uint index;
            return windows.find (window, out index);
        }

        /* The window is ready to be shown: place, stack and maybe focus it. */
        public void map (Window window) {
            if (!is_managed (window)) {
                manage (window);
            }
            take_initial_state (window);
            if (window.initially_iconic && !window.placed) {
                window.set_state_flags (WindowState.MINIMIZED, true);
            }
            recalc_window_type (window);
            recalc_window_features (window);
            if (window.has_feature (WindowFeatures.ALWAYS_STICKY)) {
                window.set_state_flags (WindowState.STICKY, true);
            }
            note_user_time (window.user_time);
            int home = current_workspace;
            var parent = window.transient_for;
            if (parent != null && parent.has_state (WindowState.MAPPED)) {
                home = parent.home_workspace;
                if (parent.has_state (WindowState.STICKY)) {
                    window.set_state_flags (WindowState.STICKY, true);
                }
            }
            if (window.initial_workspace >= 0 && window.initial_workspace < n_workspaces) {
                home = window.initial_workspace;
            }
            window.set_state_flags (WindowState.MAPPED, true);
            workspace_add_window (window, home, false);

            var requested = window.client_rect ();
            requested.x = window.frame.x;
            requested.y = window.frame.y;
            window.user_rect = requested;
            window.saved_rect = requested;

            stack.add (window);
            if (window.struts.length > 0) {
                invalidate_work_areas ();
            }
            window_mapped (window);
            maybe_leave_show_desktop_mode (window);

            move_resize_internal (window,
                                  Marco.MoveResizeFlags.IS_CONFIGURE_REQUEST |
                                  Marco.MoveResizeFlags.IS_MOVE_ACTION |
                                  Marco.MoveResizeFlags.IS_RESIZE_ACTION,
                                  window.hints.win_gravity, requested);

            bool takes_focus = show (window);

            stack_changed ();
            window.initial_timestamp = Window.USER_TIME_UNSET;
            if (takes_focus) {
                focus_policy.focus_window (window, window.user_time > 0 ? window.user_time : last_user_time);
                if (window.move_after_placement) {
                    begin_keyboard_move (window);
                    window.move_after_placement = false;
                }
            }
        }

        private void take_initial_state (Window window) {
            if (window.placed) {
                return;
            }
            if (window.has_state (WindowState.MAXIMIZED_H)) {
                window.maximize_horizontally_after_placement = true;
            }
            if (window.has_state (WindowState.MAXIMIZED_V)) {
                window.maximize_vertically_after_placement = true;
            }
            if (window.has_state (WindowState.FULLSCREEN)) {
                window.fullscreen_after_placement = true;
            }
            if (window.has_state (WindowState.MINIMIZED)) {
                window.minimize_after_placement = true;
            }
            window.set_state_flags (WindowState.MAXIMIZED | WindowState.FULLSCREEN |
                                    WindowState.MINIMIZED, false);
        }

        private static bool windows_overlap (Window w1, Window w2) {
            if (w1.has_state (WindowState.MINIMIZED) || w2.has_state (WindowState.MINIMIZED)) {
                return false;
            }
            return w1.frame.overlap (w2.frame);
        }

        /* Marco window_would_be_covered (): an "above" window overlaps the newcomer. */
        private bool window_would_be_covered (Window newbie) {
            foreach (var w in windows) {
                if (w != newbie && w.has_state (WindowState.ABOVE) && is_on_workspace (w, newbie.workspace) &&
                    windows_overlap (w, newbie)) {
                    return true;
                }
            }
            return false;
        }

        private static bool is_on_workspace (Window w, int workspace) {
            return w.workspace == -1 || workspace == -1 || w.workspace == workspace;
        }

        /* ---- MRU lists (Marco workspace.c, window.c) ------------------- */

        /* A workspace's list; a new one starts with the sticky windows (meta_workspace_new ()). */
        private unowned GLib.GenericArray<Window> mru_list (int workspace) {
            unowned GLib.GenericArray<Window>? list = mru_lists.lookup (workspace);
            if (list == null) {
                var created = new GLib.GenericArray<Window> ();
                foreach (var w in windows) {
                    if (w.workspace == -1 && w.has_state (WindowState.MAPPED)) {
                        created.insert (0, w);
                    }
                }
                mru_lists.insert (workspace, created);
                list = mru_lists.lookup (workspace);
            }
            return list;
        }

        private const int MAX_REASONABLE_WORKSPACES = 36;
        private uint workspace_serial = 0;

        /* Most recently used first; workspace -1 means the current one. */
        public unowned GLib.GenericArray<Window> get_mru (int workspace = -1) {
            return mru_list (workspace < 0 ? current_workspace : workspace);
        }

        private void mru_add (Window window) {
            if (window.workspace == -1) {
                for (int i = 0; i < int.max (n_workspaces, current_workspace + 1); i++) {
                    var list = mru_list (i);
                    uint index;
                    if (!list.find (window, out index)) {
                        list.insert (0, window);
                    }
                }
                foreach (var list in mru_lists.get_values ()) {
                    uint index;
                    if (!list.find (window, out index)) {
                        list.insert (0, window);
                    }
                }
            } else {
                var list = mru_list (window.workspace);
                list.remove (window);
                list.insert (0, window);
            }
        }

        private void mru_remove (Window window) {
            foreach (var list in mru_lists.get_values ()) {
                list.remove (window);
            }
        }

        /* FocusIn moves the window to the front of the active workspace's list. */
        private void mru_focused (Window window) {
            if (!is_on_current_workspace (window)) {
                return;
            }
            var list = mru_list (current_workspace);
            list.remove (window);
            list.insert (0, window);
        }

        /* meta_core_minimize (): to the back of the active workspace's list. */
        public void mru_to_back (Window window) {
            if (!is_on_current_workspace (window)) {
                return;
            }
            var list = mru_list (current_workspace);
            list.remove (window);
            list.add (window);
        }

        private void ensure_mru_position_after (Window window, Window after) {
            var mru = mru_list (current_workspace);
            uint after_index;
            if (!mru.find (after, out after_index)) {
                return;
            }
            uint window_index;
            if (mru.find (window, out window_index)) {
                if (window_index > after_index) {
                    return;
                }
                mru.remove_index (window_index);
                after_index--;
            }
            mru.insert ((int) after_index + 1, window);
        }

        private void stack_just_below (Window window, Window below) {
            if (window.stack_position > below.stack_position) {
                stack.set_stack_position (window, below.stack_position);
            }
        }

        /*
         * The first-show part of meta_window_show (): placement, then what
         * a window that does not take focus gets (stacking just below the
         * focus window, MRU position, a demand for attention). Returns
         * whether the window takes focus.
         */
        private bool show (Window window) {
            bool takes_focus;
            bool places_on_top;
            focus_policy.state_on_map (window, out takes_focus, out places_on_top);
            takes_focus = takes_focus && is_showing (window);

            bool will_be_covered = window_would_be_covered (window);
            bool needs_stacking_adjustment = focused != null && focused != window &&
                ((!places_on_top && !takes_focus) || will_be_covered) &&
                !is_ancestor_of_transient (focused, window);

            if (!window.placed) {
                move_resize_internal (window,
                                      Marco.MoveResizeFlags.IS_MOVE_ACTION |
                                      Marco.MoveResizeFlags.IS_RESIZE_ACTION,
                                      Marco.Gravity.NORTH_WEST, window.user_rect, true,
                                      needs_stacking_adjustment);
                window.placed = true;
            }

            if (needs_stacking_adjustment) {
                takes_focus = false;
                bool overlap = windows_overlap (window, focused);
                ensure_mru_position_after (window, focused);
                if (!will_be_covered && (overlap || (prefs.focus_mode == FocusMode.CLICK && prefs.raise_on_click))) {
                    stack_just_below (window, focused);
                }
                if (overlap || will_be_covered) {
                    window.set_state_flags (WindowState.DEMANDS_ATTENTION, true);
                }
            }
            return takes_focus;
        }

        /* ---- Interactive move started by placement-mode manual ----------- */

        private Window? grab_window = null;
        private double grab_anchor_x;
        private double grab_anchor_y;
        private Marco.Rect grab_anchor_rect;

        public bool is_moving { get { return grab_window != null; } }

        /* The pointer should be moved to this point (Marco warp_grab_pointer ()). */
        public signal void pointer_warped (int x, int y);

        public delegate bool KeyboardMoveHandler (Window window);

        /* Starts a keyboard move for manual placement when available. */
        public KeyboardMoveHandler? keyboard_move_handler = null;

        private void begin_keyboard_move (Window window) {
            if (keyboard_move_handler != null && keyboard_move_handler (window)) {
                return;
            }
            var outer = window.frame;
            var screen = Marco.Rect ();
            bool first = true;
            foreach (var output in outputs) {
                if (first) {
                    screen = output.rect;
                    first = false;
                } else {
                    Marco.Rect merged;
                    screen.union (output.rect, out merged);
                    screen = merged;
                }
            }
            int x = (outer.x + outer.width / 2).clamp (0, int.max (screen.x + screen.width - 1, 0));
            int y = (outer.y + outer.height / 2).clamp (0, int.max (screen.y + screen.height - 1, 0));

            grab_window = window;
            grab_anchor_x = x;
            grab_anchor_y = y;
            grab_anchor_rect = window.client_rect ();
            pointer_x = x;
            pointer_y = y;
            pointer_warped (x, y);
        }

        public void end_move () {
            grab_window = null;
        }

        public void recalc_window_type (Window window) {
            Marco.WindowType type;
            if (window.type_hint != null) {
                type = window.type_hint;
            } else if (window.transient_for != null) {
                type = Marco.WindowType.DIALOG;
            } else {
                type = Marco.WindowType.NORMAL;
            }
            if (type == Marco.WindowType.DIALOG && window.has_state (WindowState.MODAL)) {
                type = Marco.WindowType.MODAL_DIALOG;
            }
            if (type != window.window_type) {
                window.window_type = type;
                recalc_window_features (window);
                if (window.has_state (WindowState.MAPPED)) {
                    stack.update_layer ();
                }
            }
        }

        private Marco.Rect screen_rect () {
            var screen = Marco.Rect ();
            bool first = true;
            foreach (var output in outputs) {
                if (first) {
                    screen = output.rect;
                    first = false;
                } else {
                    Marco.Rect merged;
                    screen.union (output.rect, out merged);
                    screen = merged;
                }
            }
            return screen;
        }

        /* Modal dialogs are never attached to their parent. */
        public void recalc_window_features (Window window) {
            var f = window.mwm_functions & WindowFeatures.MWM_FUNCTIONS;
            f |= WindowFeatures.RESIZE;
            if (window.mwm_decorated) {
                f |= WindowFeatures.DECORATED;
            }
            if (window.mwm_border_only) {
                f |= WindowFeatures.BORDER_ONLY;
            }
            var hints = window.hints;
            if (hints.min_width == hints.max_width && hints.min_height == hints.max_height) {
                f &= ~WindowFeatures.RESIZE;
            }
            f |= WindowFeatures.SHADE | WindowFeatures.FULLSCREEN;

            var type = window.window_type;
            if (type == Marco.WindowType.TOOLBAR) {
                f &= ~WindowFeatures.DECORATED;
            }
            if (type == Marco.WindowType.DESKTOP || type == Marco.WindowType.DOCK) {
                f |= WindowFeatures.ALWAYS_STICKY;
            }
            if (type == Marco.WindowType.DESKTOP || type == Marco.WindowType.DOCK ||
                type == Marco.WindowType.SPLASHSCREEN) {
                f &= ~(WindowFeatures.DECORATED | WindowFeatures.CLOSE | WindowFeatures.SHADE |
                       WindowFeatures.MOVE | WindowFeatures.RESIZE);
            }
            if (type != Marco.WindowType.NORMAL && type != Marco.WindowType.DIALOG) {
                f &= ~(WindowFeatures.MINIMIZE | WindowFeatures.MAXIMIZE | WindowFeatures.FULLSCREEN);
            }
            if (!(WindowFeatures.RESIZE in f)) {
                f &= ~WindowFeatures.MAXIMIZE;
                var screen = screen_rect ();
                if (!(hints.min_width == screen.width && hints.min_height == screen.height)) {
                    f &= ~WindowFeatures.FULLSCREEN;
                }
            }
            if (window.has_state (WindowState.FULLSCREEN)) {
                f &= ~(WindowFeatures.SHADE | WindowFeatures.MOVE | WindowFeatures.RESIZE |
                       WindowFeatures.MAXIMIZE);
            }
            if (!(WindowFeatures.DECORATED in f) || WindowFeatures.BORDER_ONLY in f) {
                f &= ~WindowFeatures.SHADE;
            }
            if (f != window.features) {
                window.features = f;
            }

            bool skip_taskbar = window.wm_state_skip_taskbar;
            bool skip_pager = window.wm_state_skip_pager;
            switch (type) {
                case Marco.WindowType.DESKTOP:
                case Marco.WindowType.DOCK:
                case Marco.WindowType.TOOLBAR:
                case Marco.WindowType.MENU:
                case Marco.WindowType.UTILITY:
                case Marco.WindowType.SPLASHSCREEN:
                    skip_taskbar = true;
                    skip_pager = true;
                    break;
                case Marco.WindowType.DIALOG:
                case Marco.WindowType.MODAL_DIALOG:
                    if (window.transient_for != null) {
                        skip_taskbar = true;
                    }
                    break;
                default:
                    break;
            }
            var skip = (skip_taskbar ? WindowState.SKIP_TASKBAR : 0) |
                       (skip_pager ? WindowState.SKIP_PAGER : 0);
            var state = (window.state & ~(WindowState.SKIP_TASKBAR | WindowState.SKIP_PAGER)) | skip;
            if (state != window.state) {
                window.state = state;
            }
        }

        /* WM_NORMAL_HINTS or xdg min/max size changed (meta_set_normal_hints ()). */
        public void set_size_hints (Window window, Marco.SizeHints hints) {
            var normalized = hints;
            normalized.x = window.hints.x;
            normalized.y = window.hints.y;
            normalized.width = window.hints.width;
            normalized.height = window.hints.height;
            normalized.normalize ();
            window.hints = normalized;
            recalc_window_features (window);
            if (window.has_state (WindowState.MAPPED)) {
                move_resize_now (window);
            }
        }

        /* WM_TRANSIENT_FOR or xdg_toplevel.set_parent. */
        public void set_transient_for (Window window, Window? parent) {
            for (var p = parent; p != null; p = p.transient_for) {
                if (p == window) {
                    parent = null;
                    break;
                }
            }
            if (window.transient_for == parent) {
                return;
            }
            window.transient_for = parent;
            recalc_window_type (window);
            recalc_window_features (window);
            if (window.has_state (WindowState.MAPPED)) {
                stack.update_transient ();
                stack.update_layer ();
                stack_changed ();
            }
        }

        /*
         * A client or pager asks for a state (_NET_WM_STATE, xdg-shell,
         * foreign-toplevel): Marco honours shade, fullscreen and maximize
         * only when the window has the function.
         */
        public void request_state (Window window, WindowState flags, bool on) {
            /* Before the first map, features are not worked out yet: as for
             * _NET_WM_STATE in meta_window_new (), the state is simply taken, and
             * recalc_window_features () undoes what the window cannot have. */
            if (on && window.has_state (WindowState.MAPPED)) {
                if ((flags & WindowState.SHADED) != 0 && !window.has_feature (WindowFeatures.SHADE)) {
                    return;
                }
                if ((flags & WindowState.FULLSCREEN) != 0 &&
                    !window.has_feature (WindowFeatures.FULLSCREEN)) {
                    return;
                }
                if ((flags & WindowState.MAXIMIZED) != 0 &&
                    !window.has_feature (WindowFeatures.MAXIMIZE)) {
                    return;
                }
            }
            set_state (window, flags, on);
        }

        /* Emitted after a window is withdrawn or destroyed, before focus is replaced. */
        public signal void window_unmanaged (Window window);

        public void unmanage (Window window) {
            window_unmanaged (window);
            bool had_focus = focused == window;
            window.set_state_flags (WindowState.MAPPED, false);
            if (had_focus) {
                focus_policy.focus_default_window (window, last_user_time);
            }
            focus_policy.forget (window);
            uint ping_id = pings.lookup (window);
            if (ping_id != 0) {
                GLib.Source.remove (ping_id);
                pings.remove (window);
            }
            if (grab_window == window) {
                end_move ();
            }
            var orphans = new GLib.GenericArray<Window> ();
            foreach (var w in windows) {
                if (w != window && w.transient_for == window) {
                    orphans.add (w);
                }
            }
            windows.remove (window);
            stack.remove (window);
            mru_remove (window);
            if (window.struts.length > 0) {
                invalidate_work_areas ();
            }
            foreach (var orphan in orphans) {
                set_transient_for (orphan, window.transient_for);
            }
            if (focused == window) {
                set_input_focus (null);
                focus_policy.focus_default_window (null, last_user_time);
            }
            stack_changed ();
        }

        /*
         * Marco meta_window_should_be_showing (): on the current workspace,
         * not minimized and with no minimized ancestor.
         */
        public bool is_showing (Window window) {
            return window.has_state (WindowState.MAPPED) &&
                   (showing_on_its_workspace (window) || (in_show_desktop_skip_list (window) &&
                                                          !window.has_state (WindowState.MINIMIZED) &&
                                                          !ancestor_is_minimized (window))) &&
                   (window.workspace == -1 || window.workspace == current_workspace);
        }

        public bool showing_on_its_workspace (Window window) {
            if (window.has_state (WindowState.MINIMIZED) || ancestor_is_minimized (window)) {
                return false;
            }
            int workspace = window.workspace == -1 ? current_workspace : window.workspace;
            return !workspace_showing_desktop.contains (workspace) || is_desktop_or_dock (window);
        }

        private static bool is_desktop_or_dock (Window window) {
            bool result = window.window_type == Marco.WindowType.DESKTOP ||
                          window.window_type == Marco.WindowType.DOCK;
            for (var parent = window.transient_for; parent != null; parent = parent.transient_for) {
                result = parent.window_type == Marco.WindowType.DESKTOP ||
                         parent.window_type == Marco.WindowType.DOCK;
                if (result) {
                    break;
                }
            }
            return result;
        }

        public static bool ancestor_is_minimized (Window window) {
            for (var parent = window.transient_for; parent != null; parent = parent.transient_for) {
                if (parent.has_state (WindowState.MINIMIZED)) {
                    return true;
                }
            }
            return false;
        }

        /* Mapped windows bottom to top. */
        public unowned GLib.GenericArray<Window> get_stack () {
            return stack.get_bottom_to_top ();
        }

        public Stack get_stack_object () {
            return stack;
        }

        public unowned GLib.GenericArray<Window> get_windows () {
            return windows;
        }

        /* ---- Placement ------------------------------------------------- */

        public static bool is_ancestor_of_transient (Window ancestor, Window window) {
            for (var w = window.transient_for; w != null; w = w.transient_for) {
                if (w == ancestor) {
                    return true;
                }
            }
            return false;
        }

        /*
         * Marco place_window_if_needed (): place a window not yet placed,
         * unless it is maximized, minimized or fullscreen, then apply what
         * _NET_WM_STATE asked for at map time.
         */
        private void place_window_if_needed (Window window, ref Marco.Rect current,
                                             bool calc_placement, bool denied_focus) {
            bool did_placement = false;
            if (!window.placed && calc_placement &&
                !(window.has_state (WindowState.MAXIMIZED_H) || window.has_state (WindowState.MAXIMIZED_V)) &&
                !window.has_state (WindowState.MINIMIZED) &&
                !window.has_state (WindowState.FULLSCREEN)) {
                var saved_frame = window.frame;
                window.set_client_rect (current);
                var result = Placement.place (this, window, denied_focus, pointer_x, pointer_y);
                window.frame = saved_frame;
                window.move_after_placement = result.move_after_placement;
                if (result.maximize_after_placement) {
                    window.maximize_horizontally_after_placement = true;
                    window.maximize_vertically_after_placement = true;
                }
                current.x = result.x;
                current.y = result.y;
                did_placement = true;
            }

            if (window.placed || did_placement) {
                if (window.maximize_horizontally_after_placement ||
                    window.maximize_vertically_after_placement ||
                    window.fullscreen_after_placement) {
                    var output = outputs[Placement.xinerama_for_rect (outputs, current)];
                    var area = work_area_for (output, window.workspace);
                    if (current.width >= area.width) {
                        current.width = (int) (0.75 * (double) area.width);
                        current.x = area.x + (int) (0.125 * (double) area.width);
                    }
                    if (current.height >= area.height) {
                        current.height = (int) (0.75 * (double) area.height);
                        current.y = area.y + (int) (0.083 * (double) area.height);
                    }

                    if (window.maximize_horizontally_after_placement ||
                        window.maximize_vertically_after_placement) {
                        WindowState directions = 0;
                        if (window.maximize_horizontally_after_placement) {
                            directions |= WindowState.MAXIMIZED_H;
                        }
                        if (window.maximize_vertically_after_placement) {
                            directions |= WindowState.MAXIMIZED_V;
                        }
                        maximize_internal (window, directions, current);
                    }

                    if (window.fullscreen_after_placement) {
                        window.saved_rect = current;
                        window.set_state_flags (WindowState.FULLSCREEN, true);
                        window.adapter.set_fullscreen (true);
                        recalc_window_features (window);
                        window.fullscreen_after_placement = false;
                    }

                    window.maximize_horizontally_after_placement = false;
                    window.maximize_vertically_after_placement = false;
                }
                window.user_rect = current;
                if (window.minimize_after_placement) {
                    window.set_state_flags (WindowState.MINIMIZED, true);
                }
            }

            window.minimize_after_placement = false;
        }

        /*
         * Marco meta_window_move_resize_internal (): requested is a client
         * area whose meaning depends on flags (resize only: size only; a
         * configure request or gravity adjust: the reference point of
         * gravity; otherwise: the client's north-west corner).
         */
        public void move_resize_internal (Window window, Marco.MoveResizeFlags flags,
                                          Marco.Gravity gravity, Marco.Rect requested,
                                          bool calc_placement = false, bool denied_focus = false,
                                          bool grab_frame_action = false) {
            if (outputs.length == 0) {
                window.adapter.calc_borders ();
                window.set_client_rect (requested);
                window.adapter.configure (window.frame);
                window.geometry_changed ();
                return;
            }

            var old_rect = window.client_rect ();
            window.adapter.calc_borders ();
            var new_rect = requested;
            if ((flags & (Marco.MoveResizeFlags.IS_MOVE_ACTION | Marco.MoveResizeFlags.IS_RESIZE_ACTION)) ==
                Marco.MoveResizeFlags.IS_RESIZE_ACTION) {
                Marco.Rect.resize_with_gravity (old_rect, ref new_rect, gravity,
                                                new_rect.width, new_rect.height);
            } else if ((flags & (Marco.MoveResizeFlags.IS_CONFIGURE_REQUEST |
                                 Marco.MoveResizeFlags.DO_GRAVITY_ADJUST)) != 0) {
                Marco.FrameBorders? borders = null;
                if (window.has_frame ()) {
                    borders = window.borders;
                }
                Marco.adjust_for_gravity (borders, 0, gravity, ref new_rect);
            }

            place_window_if_needed (window, ref new_rect, calc_placement, denied_focus);
            Constraints.constrain (this, window, flags, gravity, old_rect, ref new_rect,
                                   grab_frame_action);

            window.set_client_rect (new_rect);

            if (!window.placed && window.force_save_user_rect &&
                !window.has_state (WindowState.FULLSCREEN)) {
                window.user_rect = window.client_rect ();
            } else if (Marco.MoveResizeFlags.IS_USER_ACTION in flags) {
                save_user_window_placement (window);
            }

            window.adapter.configure (window.frame);
            window.geometry_changed ();
        }

        /* meta_window_move_resize_now (): back to where the user had it. */
        public void move_resize_now (Window window) {
            move_resize_internal (window,
                                  Marco.MoveResizeFlags.IS_MOVE_ACTION | Marco.MoveResizeFlags.IS_RESIZE_ACTION,
                                  Marco.Gravity.NORTH_WEST, window.user_rect);
        }

        /* meta_window_move (): x, y are the client's north-west corner. */
        public void move (Window window, bool user_op, int x, int y, bool grab_frame_action = false) {
            var flags = Marco.MoveResizeFlags.IS_MOVE_ACTION;
            if (user_op) {
                flags |= Marco.MoveResizeFlags.IS_USER_ACTION;
            }
            var client = window.client_rect ();
            move_resize_internal (window, flags, Marco.Gravity.NORTH_WEST,
                                  Marco.Rect () { x = x, y = y, width = client.width, height = client.height },
                                  false, false, grab_frame_action);
        }

        /* meta_window_resize_with_gravity (): a client size. */
        public void resize_with_gravity (Window window, bool user_op, int width, int height,
                                         Marco.Gravity gravity) {
            var flags = Marco.MoveResizeFlags.IS_RESIZE_ACTION;
            if (user_op) {
                flags |= Marco.MoveResizeFlags.IS_USER_ACTION;
            }
            var client = window.client_rect ();
            move_resize_internal (window, flags, gravity,
                                  Marco.Rect () { x = client.x, y = client.y, width = width, height = height });
        }

        private void save_user_window_placement (Window window) {
            if (!(window.has_state (WindowState.MAXIMIZED) || is_side_tiled (window) ||
                  window.has_state (WindowState.FULLSCREEN))) {
                var client = window.client_rect ();
                if (!window.has_state (WindowState.MAXIMIZED_H)) {
                    window.user_rect.x = client.x;
                    window.user_rect.width = client.width;
                }
                if (!window.has_state (WindowState.MAXIMIZED_V)) {
                    window.user_rect.y = client.y;
                    window.user_rect.height = client.height;
                }
            }
        }

        private static bool is_side_tiled (Window window) {
            return window.tiled && (window.tile_mode == Marco.TileMode.LEFT ||
                                    window.tile_mode == Marco.TileMode.RIGHT);
        }

        public static bool is_tiled (Window window) {
            return window.tiled && window.tile_mode != Marco.TileMode.NONE &&
                   window.tile_mode != Marco.TileMode.MAXIMIZED;
        }

        private Marco.Rect gravity_position (Window window, Marco.Gravity gravity) {
            var client = window.client_rect ();
            Marco.Rect frame_extents;
            if (gravity == Marco.Gravity.STATIC || !window.has_frame ()) {
                frame_extents = client;
            } else {
                frame_extents = Placement.frame_rect (window);
            }
            int x = frame_extents.x;
            int y = frame_extents.y;
            switch (gravity) {
                case Marco.Gravity.NORTH:
                case Marco.Gravity.CENTER:
                case Marco.Gravity.SOUTH:
                    x += frame_extents.width / 2;
                    x -= client.width / 2;
                    break;
                case Marco.Gravity.SOUTH_EAST:
                case Marco.Gravity.EAST:
                case Marco.Gravity.NORTH_EAST:
                    x += frame_extents.width;
                    x -= client.width;
                    break;
                default:
                    break;
            }
            switch (gravity) {
                case Marco.Gravity.WEST:
                case Marco.Gravity.CENTER:
                case Marco.Gravity.EAST:
                    y += frame_extents.height / 2;
                    y -= client.height / 2;
                    break;
                case Marco.Gravity.SOUTH_WEST:
                case Marco.Gravity.SOUTH:
                case Marco.Gravity.SOUTH_EAST:
                    y += frame_extents.height;
                    y -= client.height;
                    break;
                default:
                    break;
            }
            return Marco.Rect () { x = x, y = y, width = client.width, height = client.height };
        }

        /*
         * Marco meta_window_move_resize_request () for a ConfigureRequest:
         * x, y are relative to the window's gravity, width and height a
         * client size.
         */
        public void configure_request (Window window, bool move, int x, int y,
                                       bool resize, int width, int height,
                                       Marco.Gravity? requested_gravity = null) {
            var gravity = requested_gravity ?? window.hints.win_gravity;
            var position = gravity_position (window, gravity);
            int new_x = position.x;
            int new_y = position.y;

            bool allow_position_change;
            if (prefs.disable_workarounds) {
                allow_position_change = false;
                if (window.window_type == Marco.WindowType.DIALOG ||
                    window.window_type == Marco.WindowType.MODAL_DIALOG ||
                    window.window_type == Marco.WindowType.SPLASHSCREEN) {
                } else if ((window.hints.flags & Marco.SizeHintFlags.P_POSITION) != 0 ||
                           ((window.hints.flags & Marco.SizeHintFlags.US_POSITION) != 0 && !window.placed)) {
                    allow_position_change = true;
                }
            } else {
                allow_position_change = true;
            }

            if (allow_position_change && move) {
                new_x = x;
                new_y = y;
                window.placed = true;
            }

            var client = window.client_rect ();
            int new_width = resize ? width : client.width;
            int new_height = resize ? height : client.height;

            window.hints.x = new_x;
            window.hints.y = new_y;
            window.hints.width = new_width;
            window.hints.height = new_height;

            var flags = Marco.MoveResizeFlags.IS_CONFIGURE_REQUEST;
            if (move) {
                flags |= Marco.MoveResizeFlags.IS_MOVE_ACTION;
            }
            if (resize) {
                flags |= Marco.MoveResizeFlags.IS_RESIZE_ACTION;
            }

            if (move || resize) {
                var rect = Marco.Rect () { x = new_x, y = new_y, width = new_width, height = new_height };
                var output = outputs.length > 0 ? outputs[Placement.xinerama_for_rect (outputs, rect)] : null;
                if (output != null && rect.equal (output.rect) &&
                    window.has_feature (WindowFeatures.FULLSCREEN) &&
                    !window.has_state (WindowState.FULLSCREEN)) {
                    make_fullscreen_internal (window);
                }
                move_resize_internal (window, flags, gravity, rect);
            }

            save_user_window_placement (window);
        }

        /* ---- Maximize and fullscreen (Marco window.c) ------------------- */

        private void save_rect (Window window) {
            if (!(window.has_state (WindowState.MAXIMIZED) || is_tiled (window) ||
                  window.has_state (WindowState.FULLSCREEN))) {
                var client = window.client_rect ();
                if (!window.has_state (WindowState.MAXIMIZED_H)) {
                    window.saved_rect.x = client.x;
                    window.saved_rect.width = client.width;
                }
                if (!window.has_state (WindowState.MAXIMIZED_V)) {
                    window.saved_rect.y = client.y;
                    window.saved_rect.height = client.height;
                }
            }
        }

        private void maximize_internal (Window window, WindowState directions, Marco.Rect? saved) {
            if (saved != null) {
                window.saved_rect = saved;
            } else {
                save_rect (window);
            }
            window.set_state_flags (directions, true);
            recalc_window_features (window);
            window.adapter.set_maximized (true);
        }

        public void maximize (Window window, WindowState directions) {
            bool h = (directions & WindowState.MAXIMIZED_H) != 0;
            bool v = (directions & WindowState.MAXIMIZED_V) != 0;
            if (!((h && !window.has_state (WindowState.MAXIMIZED_H)) ||
                  (v && !window.has_state (WindowState.MAXIMIZED_V)))) {
                return;
            }
            if (window.has_state (WindowState.SHADED) && v) {
                unshade (window, last_user_time);
            }
            if (!window.placed) {
                window.maximize_horizontally_after_placement |= h;
                window.maximize_vertically_after_placement |= v;
                return;
            }
            Marco.Rect? saved = null;
            if (window.tile_mode != Marco.TileMode.NONE) {
                saved = window.saved_rect;
                window.set_state_flags (WindowState.MAXIMIZED_V, false);
                window.tile_mode = Marco.TileMode.NONE;
            }
            maximize_internal (window, directions & WindowState.MAXIMIZED, saved);
            update_tile_state (window);
            move_resize_now (window);
        }

        public void unmaximize (Window window, WindowState directions) {
            bool h = (directions & WindowState.MAXIMIZED_H) != 0;
            bool v = (directions & WindowState.MAXIMIZED_V) != 0;
            window.tile_mode = Marco.TileMode.NONE;
            window.tiled = false;
            update_tile_state (window);
            if (!((h && window.has_state (WindowState.MAXIMIZED_H)) ||
                  (v && window.has_state (WindowState.MAXIMIZED_V)))) {
                return;
            }
            if (h) {
                window.set_state_flags (WindowState.MAXIMIZED_H, false);
            }
            if (v) {
                window.set_state_flags (WindowState.MAXIMIZED_V, false);
            }
            var target = window.client_rect ();
            if (h) {
                target.x = window.saved_rect.x;
                target.width = window.saved_rect.width;
            }
            if (v) {
                target.y = window.saved_rect.y;
                target.height = window.saved_rect.height;
            }
            Marco.ensure_size_hints_satisfied (ref target, window.hints);
            move_resize_internal (window,
                                  Marco.MoveResizeFlags.IS_MOVE_ACTION | Marco.MoveResizeFlags.IS_RESIZE_ACTION,
                                  Marco.Gravity.NORTH_WEST, target);
            window.user_rect = window.client_rect ();
            recalc_window_features (window);
            window.adapter.set_maximized (window.has_state (WindowState.MAXIMIZED_H) ||
                                          window.has_state (WindowState.MAXIMIZED_V));
        }

        private void update_tile_state (Window window) {
            WindowState edges = 0;
            if (is_tiled (window)) {
                switch (window.tile_mode) {
                    case Marco.TileMode.LEFT:
                        edges = WindowState.TILED_LEFT | WindowState.TILED_TOP | WindowState.TILED_BOTTOM;
                        break;
                    case Marco.TileMode.RIGHT:
                        edges = WindowState.TILED_RIGHT | WindowState.TILED_TOP | WindowState.TILED_BOTTOM;
                        break;
                    case Marco.TileMode.TOP_LEFT:
                        edges = WindowState.TILED_LEFT | WindowState.TILED_TOP;
                        break;
                    case Marco.TileMode.TOP_RIGHT:
                        edges = WindowState.TILED_RIGHT | WindowState.TILED_TOP;
                        break;
                    case Marco.TileMode.BOTTOM_LEFT:
                        edges = WindowState.TILED_LEFT | WindowState.TILED_BOTTOM;
                        break;
                    case Marco.TileMode.BOTTOM_RIGHT:
                        edges = WindowState.TILED_RIGHT | WindowState.TILED_BOTTOM;
                        break;
                    default:
                        break;
                }
            }
            window.set_state_flags (WindowState.TILED_LEFT,
                                    is_side_tiled (window) && window.tile_mode == Marco.TileMode.LEFT);
            window.set_state_flags (WindowState.TILED_RIGHT,
                                    is_side_tiled (window) && window.tile_mode == Marco.TileMode.RIGHT);
            window.adapter.set_tiled (edges);
            window.adapter.set_maximized (!is_tiled (window) &&
                                          (window.has_state (WindowState.MAXIMIZED_H) ||
                                           window.has_state (WindowState.MAXIMIZED_V)));
        }

        public void tile (Window window) {
            switch (window.tile_mode) {
                case Marco.TileMode.NONE:
                    return;
                case Marco.TileMode.LEFT:
                case Marco.TileMode.RIGHT:
                    maximize_internal (window, WindowState.MAXIMIZED_V, window.saved_rect);
                    break;
                case Marco.TileMode.MAXIMIZED:
                    save_rect (window);
                    break;
                default:
                    window.set_state_flags (WindowState.MAXIMIZED, false);
                    break;
            }
            window.tiled = true;
            update_tile_state (window);
            recalc_window_features (window);
            move_resize_now (window);
        }

        public bool can_tile_maximized (Window window) {
            return window.has_feature (WindowFeatures.MAXIMIZE);
        }

        /* meta_window_can_tile (): the pointer's output. */
        public bool can_tile (Window window) {
            if (!can_tile_maximized (window) || window.has_state (WindowState.SHADED)) {
                return false;
            }
            var output = output_at (pointer_x, pointer_y) ?? output_for_window (window);
            if (output == null) {
                return false;
            }
            var area = work_area_for (output, window.workspace);
            var borders = window.visible_borders ();
            return area.width / 2 - borders.left - borders.right >= window.hints.min_width &&
                   area.height - borders.top - borders.bottom >= window.hints.min_height;
        }

        public void toggle_tiled (Window window, Marco.TileMode mode) {
            Marco.TileCycle next_cycle;
            if (prefs.allow_tile_cycling) {
                if (window.tile_mode != mode) {
                    next_cycle = Marco.TileCycle.@50;
                } else {
                    switch (window.tile_cycle) {
                        case Marco.TileCycle.NONE:
                            next_cycle = Marco.TileCycle.@50;
                            break;
                        case Marco.TileCycle.@50:
                            next_cycle = Marco.TileCycle.@33;
                            break;
                        case Marco.TileCycle.@33:
                            next_cycle = Marco.TileCycle.@25;
                            break;
                        case Marco.TileCycle.@25:
                            next_cycle = Marco.TileCycle.@100;
                            break;
                        case Marco.TileCycle.@100:
                            next_cycle = Marco.TileCycle.@75;
                            break;
                        case Marco.TileCycle.@75:
                            next_cycle = Marco.TileCycle.@66;
                            break;
                        default:
                            next_cycle = Marco.TileCycle.NONE;
                            break;
                    }
                }
            } else {
                next_cycle = is_tiled (window) ? Marco.TileCycle.NONE : Marco.TileCycle.@50;
            }

            if (mode == window.tile_mode && next_cycle == Marco.TileCycle.NONE) {
                window.tile_mode = Marco.TileMode.NONE;
                window.tile_monitor_number = -1;
                window.tile_cycle = Marco.TileCycle.NONE;
                window.tiled = false;
                unmaximize (window, WindowState.MAXIMIZED);
            } else if (can_tile (window)) {
                window.tile_cycle = next_cycle;
                window.tile_mode = mode;
                window.tile_resized = false;
                window.tile_monitor_number = Placement.xinerama_for_rect (get_outputs (), window.frame);
                window.set_state_flags (WindowState.MAXIMIZED, false);
                tile (window);
            }
        }

        private void make_fullscreen_internal (Window window) {
            if (window.has_state (WindowState.FULLSCREEN)) {
                return;
            }
            if (window.has_state (WindowState.SHADED)) {
                unshade (window, last_user_time);
            }
            save_rect (window);
            window.set_state_flags (WindowState.FULLSCREEN, true);
            window.tile_resized = false;
            window.force_save_user_rect = false;
            stack.update_layer ();
            raise (window);
            recalc_window_features (window);
            window.adapter.set_fullscreen (true);
        }

        public void make_fullscreen (Window window) {
            if (window.has_state (WindowState.FULLSCREEN)) {
                return;
            }
            if (!window.placed) {
                window.fullscreen_after_placement = true;
                return;
            }
            make_fullscreen_internal (window);
            move_resize_now (window);
        }

        public void unmake_fullscreen (Window window) {
            if (!window.has_state (WindowState.FULLSCREEN)) {
                return;
            }
            window.set_state_flags (WindowState.FULLSCREEN, false);
            Marco.Rect target;
            if (!is_tiled (window)) {
                target = window.saved_rect;
            } else {
                var b = window.visible_borders ();
                target = Constraints.tile_area (this, window);
                target.x += b.left;
                target.y += b.top;
                target.width -= b.left + b.right;
                target.height -= b.top + b.bottom;
            }
            Marco.ensure_size_hints_satisfied (ref target, window.hints);
            recalc_window_features (window);
            window.adapter.set_fullscreen (false);
            move_resize_internal (window,
                                  Marco.MoveResizeFlags.IS_MOVE_ACTION | Marco.MoveResizeFlags.IS_RESIZE_ACTION,
                                  Marco.Gravity.NORTH_WEST, target);
            window.user_rect = window.client_rect ();
            stack.update_layer ();
        }

        /* ---- Stacking (Marco window.c over Stack) ----------------------- */

        private static Window find_root_ancestor (Window window) {
            var ancestor = window;
            for (var w = window.transient_for; w != null; w = w.transient_for) {
                ancestor = w;
                if (w == window) {
                    break;
                }
            }
            return ancestor;
        }

        /*
         * meta_window_raise (): raise the root ancestor, and the window too
         * so the right one of several children ends on top.
         */
        public void raise (Window window) {
            if (!stack.contains (window)) {
                return;
            }
            var ancestor = find_root_ancestor (window);
            if (stack.contains (ancestor)) {
                stack.raise (ancestor);
            }
            if (window != ancestor) {
                stack.raise (window);
            }
            stack_changed ();
        }

        public GLib.GenericArray<Window> get_stack_positions () {
            return stack.get_positions ();
        }

        public void set_stack_positions (GLib.GenericArray<Window> windows) {
            if (!stack.set_positions (windows)) {
                GLib.warning ("This list of windows has somehow changed; not resetting positions of the windows.");
                return;
            }
            stack_changed ();
        }

        public void lower (Window window) {
            if (!stack.contains (window)) {
                return;
            }
            stack.lower (window);
            stack_changed ();
        }

        /* Position within the whole stack, 0 = bottom; -1 if not mapped. */
        public int stack_index (Window window) {
            uint index;
            return stack.get_bottom_to_top ().find (window, out index) ? (int) index : -1;
        }

        /* ---- Focus (the policy is FocusPolicy) ------------------------- */

        public bool is_on_current_workspace (Window window) {
            return window.workspace == -1 || window.workspace == current_workspace;
        }

        public void focus (Window? window) {
            set_input_focus (window);
        }

        internal void set_input_focus (Window? window) {
            if (focused == window) {
                return;
            }

            var old = focused;
            if (old != null) {
                old.set_state_flags (WindowState.ACTIVE, false);
                old.adapter.set_activated (false);
            }
            focused = window;
            if (window != null) {
                mru_focused (window);
                window.set_state_flags (WindowState.ACTIVE, true);
                window.adapter.set_activated (true);
                window.adapter.take_focus ();
            }

            stack.update_layer ();
            stack_changed ();
            focus_changed ();
        }

        public void focus_default () {
            focus_policy.focus_default_window (null, last_user_time);
        }

        /* ---- Input ------------------------------------------------------ */

        public void pointer_moved (double x, double y) {
            pointer_x = x;
            pointer_y = y;
            if (grab_window != null) {
                move (grab_window, true, grab_anchor_rect.x + (int) (x - grab_anchor_x),
                      grab_anchor_rect.y + (int) (y - grab_anchor_y));
            }
        }

        /* A button press on a window; on_frame is a press on its decorations. */
        public void button_pressed (Window window, bool on_frame, int64 time = 0) {
            if (grab_window != null) {
                end_move ();
                return;
            }
            if (!is_showing (window)) {
                return;
            }
            focus_policy.button_press (window, on_frame, time);
        }

        /* The pointer now is over window (null: no managed window). */
        public void pointer_entered (Window? window, int64 time = 0) {
            focus_policy.pointer_entered (window, time);
        }

        /* Keep the newest user-input timestamp. */
        internal void note_user_time (int64 time) {
            if (time != Window.USER_TIME_UNSET && time > last_user_time) {
                last_user_time = time;
            }
        }

        /*
         * Marco window_activate () (_NET_ACTIVE_WINDOW): an activation
         * with an outdated timestamp, or of a window on another workspace,
         * only marks the window as demanding attention; otherwise the
         * window is unminimized with its transient parents, raised (always
         * for pagers) and focused. time 0 means "now", as Marco treats
         * pagers that send no timestamp.
         */
        public void activate (Window window, ActivationSource source = ActivationSource.PAGER,
                              int64 time = 0) {
            if (time != 0 && last_user_time != 0 && time < last_user_time) {
                focus_policy.set_demands_attention (window);
                return;
            }
            if (time == 0) {
                time = last_user_time;
            }
            if (time != 0) {
                focus_policy.set_user_time (window, time);
            }

            maybe_leave_show_desktop_mode (window);

            if (window.transient_for == null && !is_on_current_workspace (window)) {
                focus_policy.set_demands_attention (window);
                return;
            } else if (window.transient_for != null) {
                move_to_workspace (window, current_workspace);
            }

            if (window.has_state (WindowState.SHADED)) {
                set_state (window, WindowState.SHADED, false);
            }

            for (var w = window; w != null; w = w.transient_for) {
                unminimize (w);
            }

            if (prefs.raise_on_click || source == ActivationSource.PAGER) {
                raise (window);
            }
            focus_policy.focus_window (window, time);
        }

        /*
         * xdg_activation_v1.activate. A shown window activates as with
         * _NET_ACTIVE_WINDOW from an application at the token's time; one
         * not shown yet takes the time as its startup timestamp. A worthless
         * token only asks for attention.
         */
        public void activate_with_token (Window window, bool valid, int64 user_time) {
            if (!valid) {
                if (window.placed) {
                    focus_policy.set_demands_attention (window);
                }
                return;
            }
            if (!window.placed) {
                window.initial_timestamp = user_time;
                return;
            }
            activate (window, ActivationSource.APPLICATION, user_time);
        }

        /* ---- Minimizing (Marco meta_window_minimize/unminimize) ---------- */

        public void minimize (Window window) {
            if (window.has_state (WindowState.MINIMIZED)) {
                return;
            }
            window.set_state_flags (WindowState.MINIMIZED, true);
            stack_changed ();
            if (focused == window || (focused != null && !is_showing (focused))) {
                focus_policy.focus_default_window (focused, last_user_time);
                if (focused != null && !is_showing (focused)) {
                    set_input_focus (null);
                }
            }
        }

        public void unminimize (Window window) {
            if (!window.has_state (WindowState.MINIMIZED)) {
                return;
            }
            window.set_state_flags (WindowState.MINIMIZED, false);
            stack_changed ();
        }

        /* Ask the client to close, then start a responsiveness ping. */
        public void close (Window window) {
            window.adapter.close ();
            ping (window);
        }

        /* ---- Pings (Marco display.c meta_display_ping_window ()) -------- */

        public const uint PING_TIMEOUT_DELAY = 5000;

        private GLib.HashTable<Window, uint> pings =
            new GLib.HashTable<Window, uint> (GLib.direct_hash, GLib.direct_equal);

        /* delete_ping_timeout_func (): ask whether to force the client to quit. */
        public signal void not_responding (Window window);

        private void ping (Window window) {
            if (pings.contains (window)) {
                return;
            }
            window.adapter.ping ();
            uint id = 0;
            id = GLib.Timeout.add (PING_TIMEOUT_DELAY, () => {
                if (pings.lookup (window) == id) {
                    pings.remove (window);
                }
                window.not_responding = true;
                not_responding (window);
                return GLib.Source.REMOVE;
            });
            pings.insert (window, id);
        }

        /* The client answered a ping. */
        public void pong (Window window) {
            uint id = pings.lookup (window);
            if (id != 0) {
                GLib.Source.remove (id);
                pings.remove (window);
            }
            window.not_responding = false;
        }

        /* Run a pending ping timeout now (tests). */
        public void expire_ping (Window window) {
            uint id = pings.lookup (window);
            if (id != 0) {
                GLib.Source.remove (id);
                pings.remove (window);
                window.not_responding = true;
                not_responding (window);
            }
        }

        public bool ping_pending (Window window) {
            return pings.contains (window);
        }

        /* ---- Window states --------------------------------------------- */

        /* Turn state flags on or off; maximize and fullscreen go through Marco's functions. */
        /* Marco shade: roll up to the titlebar without losing focus. */
        public void shade (Window window, int64 time) {
            if (window.has_state (WindowState.SHADED)) {
                return;
            }
            window.set_state_flags (WindowState.SHADED, true);
            shade_changed (window, time);
        }

        /* meta_window_unshade () */
        public void unshade (Window window, int64 time) {
            if (!window.has_state (WindowState.SHADED)) {
                return;
            }
            window.set_state_flags (WindowState.SHADED, false);
            shade_changed (window, time);
        }

        private void shade_changed (Window window, int64 time) {
            if (window.placed && window.has_state (WindowState.MAPPED)) {
                move_resize_now (window);
            }
            stack_changed ();
            if (window.has_state (WindowState.MAPPED)) {
                focus_policy.focus_window (window, time);
            }
        }

        public void set_state (Window window, WindowState flags, bool on) {
            var before = window.state;
            var size_flags = flags & (WindowState.MAXIMIZED | WindowState.FULLSCREEN);
            var other = flags & ~(WindowState.MAXIMIZED | WindowState.FULLSCREEN);

            if ((size_flags & WindowState.MAXIMIZED) != 0) {
                if (on) {
                    maximize (window, size_flags & WindowState.MAXIMIZED);
                } else {
                    unmaximize (window, size_flags & WindowState.MAXIMIZED);
                }
            }
            if ((size_flags & WindowState.FULLSCREEN) != 0) {
                if (on) {
                    make_fullscreen (window);
                } else {
                    unmake_fullscreen (window);
                }
            }
            if ((other & WindowState.SHADED) != 0) {
                other &= ~WindowState.SHADED;
                if (on) {
                    shade (window, last_user_time);
                } else {
                    unshade (window, last_user_time);
                }
            }
            if ((other & WindowState.STICKY) != 0) {
                other &= ~WindowState.STICKY;
                if (on) {
                    stick (window);
                } else {
                    unstick (window);
                }
                if (window.struts.length > 0) {
                    invalidate_work_areas ();
                }
            }
            window.set_state_flags (other, on);
            if (window.state == before) {
                return;
            }

            stack.update_layer ();
            stack_changed ();
            if (on && (flags & WindowState.FULLSCREEN) != 0) {
                focus (window);
            }
        }

        /* ---- Show desktop (Marco screen.c) ------------------------------ */

        private GLib.HashTable<int, bool> workspace_showing_desktop =
            new GLib.HashTable<int, bool> (GLib.direct_hash, GLib.direct_equal);

        public bool showing_desktop { get { return workspace_showing_desktop.contains (current_workspace); } }

        private bool in_show_desktop_skip_list (Window window) {
            string? class_name = window.res_class ?? window.app_id;
            if (class_name == null) {
                return false;
            }
            foreach (unowned string item in prefs.show_desktop_skip_list.split (",")) {
                if (class_name.ascii_casecmp (item.strip ()) == 0) {
                    return true;
                }
            }
            return false;
        }

        public void show_desktop () {
            if (showing_desktop) {
                return;
            }
            workspace_showing_desktop.insert (current_workspace, true);
            stack_changed ();
            foreach (var window in get_mru ()) {
                if (window.window_type == Marco.WindowType.DESKTOP && !in_show_desktop_skip_list (window)) {
                    focus_policy.focus_window (window, last_user_time);
                    break;
                }
            }
            if (focused != null && !is_showing (focused)) {
                focus_policy.focus_default_window (focused, last_user_time);
                if (focused != null && !is_showing (focused)) {
                    set_input_focus (null);
                }
            }
        }

        public void unshow_desktop () {
            if (workspace_showing_desktop.remove (current_workspace)) {
                stack_changed ();
            }
        }

        private void maybe_leave_show_desktop_mode (Window window) {
            if (!showing_desktop || is_desktop_or_dock (window)) {
                return;
            }
            foreach (var other in windows) {
                if (other.has_state (WindowState.MAPPED) && other.home_workspace == current_workspace &&
                    other.has_feature (WindowFeatures.MINIMIZE) && other != window) {
                    minimize (other);
                }
            }
            unshow_desktop ();
        }

        /* ---- Workspaces (Marco workspace.c, screen.c) ------------------ */

        [CCode (cname = "g_dgettext", cheader_filename = "glib.h")]
        private static extern unowned string dgettext (string domain, string msgid);

        public string workspace_name (int index) {
            var value = prefs.get_value ("workspace-names/name-%d".printf (index + 1));
            if (value != null && value.get_string () != "") {
                return value.get_string ();
            }
            return dgettext ("marco", "Workspace %d").printf (index + 1);
        }

        public WorkspaceLayout workspace_layout () {
            return new WorkspaceLayout (n_workspaces, current_workspace, workspace_rows, workspace_columns,
                                        workspaces_vertical, workspaces_corner);
        }

        /* meta_workspace_get_neighbor () of the active workspace. */
        public int workspace_neighbor (Motion direction, int from = -1) {
            int start = from < 0 ? current_workspace : from;
            var layout = new WorkspaceLayout (n_workspaces, start, workspace_rows, workspace_columns,
                                              workspaces_vertical, workspaces_corner);
            return layout.neighbor (n_workspaces, start, direction, prefs.wrap_style, WorkspaceLayout.ltr ());
        }

        /* Apply a workspace layout supplied by a pager. */
        public void set_workspace_layout (int columns, int rows, bool vertical, Corner corner) {
            workspaces_vertical = vertical;
            if (rows <= 0 && columns <= 0) {
                GLib.warning ("Columns = %d rows = %d in the workspace layout makes no sense", rows, columns);
            } else {
                workspace_rows = rows > 0 ? rows : -1;
                workspace_columns = columns > 0 ? columns : -1;
            }
            workspaces_corner = corner;
        }

        private void update_num_workspaces () {
            int new_num = prefs.num_workspaces.clamp (1, MAX_REASONABLE_WORKSPACES);
            if (new_num == n_workspaces) {
                return;
            }
            int last_remaining = new_num - 1;
            for (int extra = n_workspaces - 1; extra >= new_num; extra--) {
                relocate_windows (extra, last_remaining);
            }
            if (current_workspace >= new_num) {
                switch_workspace (last_remaining);
            }
            foreach (var key in mru_lists.get_keys ()) {
                if (key >= new_num) {
                    mru_lists.remove (key);
                    workspace_showing_desktop.remove (key);
                }
            }
            n_workspaces = new_num;
            foreach (var window in windows) {
                if (window.has_state (WindowState.MAPPED) && window.workspace == -1) {
                    mru_add (window);
                }
            }
            invalidate_work_areas ();
            stack_changed ();
        }

        /* meta_workspace_relocate_windows (): workspace->windows, newest first. */
        private void relocate_windows (int workspace, int new_home) {
            var moving = new GLib.GenericArray<Window> ();
            foreach (var window in windows) {
                if (window.has_state (WindowState.MAPPED) && window.home_workspace == workspace) {
                    moving.add (window);
                }
            }
            moving.sort ((a, b) => a.workspace_serial < b.workspace_serial ? 1 : -1);
            foreach (var window in moving) {
                workspace_remove_window (window);
                workspace_add_window (window, new_home);
            }
        }

        private void workspace_add_window (Window window, int workspace, bool has_struts = true) {
            window.home_workspace = workspace;
            window.workspace = window.has_state (WindowState.STICKY) ? -1 : workspace;
            window.workspace_serial = ++workspace_serial;
            mru_add (window);
            if (has_struts && window.struts.length > 0) {
                invalidate_work_areas ();
            }
        }

        private void workspace_remove_window (Window window) {
            mru_remove (window);
            if (window.struts.length > 0) {
                invalidate_work_areas ();
            }
        }

        /* window_stick_impl (): the window keeps its workspace to return to. */
        private void stick_impl (Window window) {
            if (window.has_state (WindowState.STICKY)) {
                return;
            }
            window.set_state_flags (WindowState.STICKY, true);
            window.workspace = -1;
            if (window.has_state (WindowState.MAPPED)) {
                mru_add (window);
            }
        }

        private void unstick_impl (Window window) {
            if (!window.has_state (WindowState.STICKY)) {
                return;
            }
            window.set_state_flags (WindowState.STICKY, false);
            window.workspace = window.home_workspace;
            foreach (var key in mru_lists.get_keys ()) {
                if (key != window.home_workspace) {
                    mru_lists[key].remove (window);
                }
            }
            if (current_workspace != window.home_workspace) {
                change_workspace (window, current_workspace);
            }
        }

        private GLib.GenericArray<Window> transients_of (Window window) {
            var transients = new GLib.GenericArray<Window> ();
            foreach (var candidate in windows) {
                if (is_ancestor_of_transient (window, candidate)) {
                    transients.add (candidate);
                }
            }
            return transients;
        }

        private void stick (Window window) {
            stick_impl (window);
            foreach (var transient in transients_of (window)) {
                stick_impl (transient);
            }
        }

        private void unstick (Window window) {
            unstick_impl (window);
            foreach (var transient in transients_of (window)) {
                unstick_impl (transient);
            }
        }

        private void change_workspace_without_transients (Window window, int workspace) {
            if (window.has_state (WindowState.STICKY)) {
                unstick (window);
            }
            if (window.home_workspace != workspace) {
                workspace_remove_window (window);
                workspace_add_window (window, workspace);
            }
        }

        private void change_workspace (Window window, int workspace) {
            change_workspace_without_transients (window, workspace);
            foreach (var transient in transients_of (window)) {
                change_workspace_without_transients (transient, workspace);
            }
            for (var ancestor = window.transient_for; ancestor != null; ancestor = ancestor.transient_for) {
                change_workspace_without_transients (ancestor, workspace);
            }
        }

        /* meta_workspace_activate_with_focus (); focus_this may be null. */
        public void switch_workspace (int workspace, Window? focus_this = null) {
            if (workspace < 0 || workspace >= n_workspaces || workspace == current_workspace) {
                return;
            }
            previous_workspace = current_workspace;
            current_workspace = workspace;
            var move_window = moving_window;
            if (move_window != null && move_window.has_state (WindowState.STICKY)) {
                move_window = null;
            }
            if (move_window != null) {
                change_workspace (move_window, workspace);
            }
            refresh_output_work_areas ();
            stack_changed ();
            if (focus_this != null) {
                focus_policy.focus_window (focus_this, last_user_time);
                raise (focus_this);
            } else if (move_window != null) {
                raise (move_window);
            } else {
                focus_policy.focus_default_window (null, last_user_time);
            }
            if (focused != null && !is_showing (focused)) {
                set_input_focus (null);
            }
        }

        public void move_window_and_activate (Window window, int workspace) {
            change_workspace (window, workspace);
            focus_policy.leave_mouse_mode ();
            switch_workspace (workspace, window);
        }

        public void move_to_workspace (Window window, int workspace) {
            if (workspace < 0 || workspace >= n_workspaces) {
                return;
            }
            change_workspace (window, workspace);
            stack_changed ();
            if (focused != null && !is_showing (focused)) {
                focus_policy.focus_default_window (focused, last_user_time);
                if (focused != null && !is_showing (focused)) {
                    set_input_focus (null);
                }
            }
        }

        /* ---- Snapshots -------------------------------------------------- */

        private static void append_rect (GLib.StringBuilder json, Marco.Rect r) {
            json.append_printf ("[%d,%d,%d,%d]", r.x, r.y, r.width, r.height);
        }

        private static void append_string (GLib.StringBuilder json, string s) {
            json.append_c ('"');
            json.append (s.replace ("\\", "\\\\").replace ("\"", "\\\""));
            json.append_c ('"');
        }

        /* _NET_WM_STATE names, as the Marco oracle records them. */
        public static string[] state_names (Window window) {
            string[] names = {};
            if (window.has_state (WindowState.ABOVE)) names += "above";
            if (window.has_state (WindowState.BELOW)) names += "below";
            if (window.has_state (WindowState.DEMANDS_ATTENTION)) names += "demands_attention";
            /* meta_window_appears_focused (): docks and splash screens
             * always look focused. */
            if (window.has_state (WindowState.ACTIVE) ||
                window.window_type == Marco.WindowType.DOCK ||
                window.window_type == Marco.WindowType.SPLASHSCREEN) names += "focused";
            if (window.has_state (WindowState.FULLSCREEN)) names += "fullscreen";
            if (window.has_state (WindowState.MINIMIZED)) names += "hidden";
            if (window.has_state (WindowState.MAXIMIZED_H)) names += "maximized_horz";
            if (window.has_state (WindowState.MAXIMIZED_V)) names += "maximized_vert";
            if (window.has_state (WindowState.MODAL)) names += "modal";
            if (window.has_state (WindowState.SHADED)) names += "shaded";
            if (window.has_state (WindowState.SKIP_PAGER)) names += "skip_pager";
            if (window.has_state (WindowState.SKIP_TASKBAR)) names += "skip_taskbar";
            if (window.has_state (WindowState.STICKY)) names += "sticky";
            return names;
        }

        /*
         * The model as JSON, in the shape of the oracle's snapshots
         * (tools/marco-oracle/README.md). Hand-written, so the compositor
         * does not depend on json-glib.
         */
        public string dump_json () {
            var json = new GLib.StringBuilder ("{\"active\":");
            if (focused != null) {
                append_string (json, focused.label ?? focused.id.to_string ());
            } else {
                json.append ("null");
            }
            json.append_printf (",\"current_workspace\":%d,\"n_workspaces\":%d,\"stacking\":[",
                                current_workspace, n_workspaces);
            var order = stack.get_bottom_to_top ();
            for (int i = 0; i < order.length; i++) {
                if (i > 0) {
                    json.append_c (',');
                }
                append_string (json, order[i].label ?? order[i].id.to_string ());
            }
            json.append ("],\"windows\":{");
            for (int i = 0; i < windows.length; i++) {
                var window = windows[i];
                if (i > 0) {
                    json.append_c (',');
                }
                append_string (json, window.label ?? window.id.to_string ());
                json.append (":{\"client\":");
                append_rect (json, window.client_rect ());
                json.append_printf (",\"extents\":[%d,%d,%d,%d],\"frame\":",
                                    window.borders.left, window.borders.right,
                                    window.borders.top, window.borders.bottom);
                append_rect (json, window.frame);
                json.append_printf (",\"mapped\":%s,\"state\":[",
                                    window.has_state (WindowState.MAPPED) ? "true" : "false");
                var names = state_names (window);
                for (int j = 0; j < names.length; j++) {
                    if (j > 0) {
                        json.append_c (',');
                    }
                    append_string (json, names[j]);
                }
                json.append_printf ("],\"visible\":%s,\"workspace\":%d}",
                                    is_showing (window) ? "true" : "false", window.workspace);
            }
            json.append ("}}");
            return json.str;
        }
    }
}
