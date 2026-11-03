/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Polo.Policy.Stack: Marco's MetaStack (src/core/stack.c). Windows have a
 * stack position and a layer; stack_ensure_sorted () recomputes the
 * layers, applies the transient constraints (lib/marco/stack-constraints.c)
 * and sorts by layer, then position. Every change is applied at once, as
 * Marco does when the stack is not frozen.
 */

namespace Polo.Policy {
    public class Stack : GLib.Object {
        private unowned Model model;

        /* stack->sorted: topmost first. */
        private GLib.GenericArray<Window> sorted = new GLib.GenericArray<Window> ();
        /* Most recent first, as stack->added. */
        private GLib.GenericArray<Window> added = new GLib.GenericArray<Window> ();
        private int n_positions = 0;
        private bool need_resort = false;
        private bool need_constrain = false;
        private bool need_relayer = false;

        /* sorted, bottom to top. */
        private GLib.GenericArray<Window> bottom_to_top = new GLib.GenericArray<Window> ();

        public Stack (Model model) {
            this.model = model;
        }

        public void add (Window window) {
            if (window.stack_position >= 0) {
                return;
            }
            added.insert (0, window);
            window.stack_position = n_positions;
            n_positions += 1;
            sync ();
        }

        public void remove (Window window) {
            if (window.stack_position < 0) {
                return;
            }
            set_stack_position_no_sync (window, n_positions - 1);
            window.stack_position = -1;
            n_positions -= 1;
            added.remove (window);
            sorted.remove (window);
            sync ();
        }

        public bool contains (Window window) {
            return window.stack_position >= 0;
        }

        public void update_layer () {
            need_relayer = true;
            sync ();
        }

        public void update_transient () {
            need_constrain = true;
            sync ();
        }

        private int workspace_of (Window window) {
            return window.workspace == -1 ? model.current_workspace : window.workspace;
        }

        private static bool located_on_workspace (Window window, int workspace) {
            return window.workspace == -1 || window.workspace == workspace;
        }

        /* meta_stack_raise (): raise within the layer. */
        public void raise (Window window) {
            int max_stack_position = window.stack_position;
            int workspace = workspace_of (window);
            foreach (var w in sorted) {
                if (located_on_workspace (w, workspace) && w.stack_position > max_stack_position) {
                    max_stack_position = w.stack_position;
                }
            }
            if (max_stack_position == window.stack_position) {
                return;
            }
            set_stack_position_no_sync (window, max_stack_position);
            sync ();
        }

        public void lower (Window window) {
            int min_stack_position = window.stack_position;
            int workspace = workspace_of (window);
            foreach (var w in sorted) {
                if (located_on_workspace (w, workspace) && w.stack_position < min_stack_position) {
                    min_stack_position = w.stack_position;
                }
            }
            if (min_stack_position == window.stack_position) {
                return;
            }
            set_stack_position_no_sync (window, min_stack_position);
            sync ();
        }

        public void set_stack_position (Window window, int position) {
            set_stack_position_no_sync (window, position);
            sync ();
        }

        private void set_stack_position_no_sync (Window window, int position) {
            if (window.stack_position < 0 || position < 0 || position >= n_positions ||
                position == window.stack_position) {
                return;
            }
            need_resort = true;
            need_constrain = true;

            int low, high, delta;
            if (position < window.stack_position) {
                low = position;
                high = window.stack_position - 1;
                delta = 1;
            } else {
                low = window.stack_position + 1;
                high = position;
                delta = -1;
            }
            foreach (var w in sorted) {
                if (w.stack_position >= low && w.stack_position <= high) {
                    w.stack_position += delta;
                }
            }
            window.stack_position = position;
        }

        private bool windows_on_different_xinerama (Window a, Window b) {
            var outputs = model.get_outputs ();
            return Placement.xinerama_for_rect (outputs, a.frame) !=
                   Placement.xinerama_for_rect (outputs, b.frame);
        }

        private bool has_focused_transient (Window window) {
            var focused = model.focused;
            if (focused == null) {
                return false;
            }
            foreach (var w in model.get_windows ()) {
                if (w == focused && Model.is_ancestor_of_transient (window, w)) {
                    return true;
                }
            }
            return false;
        }

        private Marco.StackLayer get_standalone_layer (Window window) {
            var focused = model.focused;
            switch (window.window_type) {
                case Marco.WindowType.DESKTOP:
                    return Marco.StackLayer.DESKTOP;
                case Marco.WindowType.DOCK:
                    return window.has_state (WindowState.BELOW) ? Marco.StackLayer.BOTTOM
                                                                : Marco.StackLayer.DOCK;
                default:
                    if (window.has_state (WindowState.BELOW)) {
                        return Marco.StackLayer.BOTTOM;
                    } else if (window.has_state (WindowState.FULLSCREEN) &&
                               (has_focused_transient (window) || window == focused || focused == null ||
                                windows_on_different_xinerama (window, focused))) {
                        return Marco.StackLayer.FULLSCREEN;
                    } else if (window.has_state (WindowState.ABOVE)) {
                        return Marco.StackLayer.TOP;
                    }
                    return Marco.StackLayer.NORMAL;
            }
        }

        public static bool has_transient_type (Window w) {
            switch (w.window_type) {
                case Marco.WindowType.DIALOG:
                case Marco.WindowType.MODAL_DIALOG:
                case Marco.WindowType.TOOLBAR:
                case Marco.WindowType.MENU:
                case Marco.WindowType.UTILITY:
                    return true;
                default:
                    return false;
            }
        }

        /* meta_group_list_windows (); a window without a group is its own group. */
        private GLib.GenericArray<Window> group_windows (Window window) {
            var members = new GLib.GenericArray<Window> ();
            if (window.group == 0) {
                members.add (window);
                return members;
            }
            var all = model.get_windows ();
            for (int i = (int) all.length - 1; i >= 0; i--) {
                if (all[i].group == window.group) {
                    members.add (all[i]);
                }
            }
            return members;
        }

        private Marco.StackLayer get_maximum_layer_in_group (Window window) {
            var max = Marco.StackLayer.DESKTOP;
            foreach (var w in group_windows (window)) {
                var layer = get_standalone_layer (w);
                if (layer > max) {
                    max = layer;
                }
            }
            return max;
        }

        private void compute_layer (Window window) {
            window.layer = get_standalone_layer (window);
            if (has_transient_type (window) &&
                (window.transient_for == null || window.transient_parent_is_root_window)) {
                var group_max = get_maximum_layer_in_group (window);
                if (group_max > window.layer) {
                    window.layer = group_max;
                }
            }
        }

        private void do_window_additions () {
            if (added.length > 0) {
                for (int i = (int) added.length - 1; i >= 0; i--) {
                    sorted.insert (0, added[i]);
                }
                need_resort = true;
                need_constrain = true;
                need_relayer = true;
            }
            added.remove_range (0, added.length);
        }

        private void do_relayer () {
            if (!need_relayer) {
                return;
            }
            foreach (var w in sorted) {
                var old_layer = w.layer;
                compute_layer (w);
                if (w.layer != old_layer) {
                    need_resort = true;
                    need_constrain = true;
                }
            }
            need_relayer = false;
        }

        private void do_constrain () {
            if (!need_constrain) {
                return;
            }
            int n = (int) sorted.length;
            if (n == 0) {
                need_constrain = false;
                return;
            }
            var infos = new Marco.StackWindow[n];
            var index = new GLib.HashTable<Window, int> (GLib.direct_hash, GLib.direct_equal);
            for (int i = 0; i < n; i++) {
                index.insert (sorted[i], i);
            }
            for (int i = 0; i < n; i++) {
                var w = sorted[i];
                int parent = -1;
                if (w.transient_for != null && index.contains (w.transient_for)) {
                    parent = index[w.transient_for];
                }
                infos[i] = Marco.StackWindow () {
                    stack_position = w.stack_position,
                    layer = w.layer,
                    type = w.window_type,
                    transient_for = parent,
                    transient_parent_is_root_window = w.transient_parent_is_root_window,
                    group = w.group
                };
            }
            var group_order = new int[n];
            int k = 0;
            var all = model.get_windows ();
            for (int i = (int) all.length - 1; i >= 0; i--) {
                if (index.contains (all[i])) {
                    group_order[k++] = index[all[i]];
                }
            }
            for (; k < n; k++) {
                group_order[k] = k;
            }
            Marco.stack_constrain (infos, group_order, n_positions);
            for (int i = 0; i < n; i++) {
                if (sorted[i].stack_position != infos[i].stack_position) {
                    need_resort = true;
                }
                sorted[i].stack_position = infos[i].stack_position;
                sorted[i].layer = infos[i].layer;
            }
            need_constrain = false;
        }

        /* compare_window_position (): by layer, then position, topmost first. */
        private static int compare_window_position (Window a, Window b) {
            if (a.layer < b.layer) {
                return 1;
            } else if (a.layer > b.layer) {
                return -1;
            } else if (a.stack_position < b.stack_position) {
                return 1;
            } else if (a.stack_position > b.stack_position) {
                return -1;
            }
            return 0;
        }

        private void do_resort () {
            if (!need_resort) {
                return;
            }
            sorted.sort (compare_window_position);
            need_resort = false;
        }

        private void sync () {
            do_window_additions ();
            do_relayer ();
            do_constrain ();
            do_resort ();
            bottom_to_top.remove_range (0, bottom_to_top.length);
            for (int i = (int) sorted.length - 1; i >= 0; i--) {
                bottom_to_top.add (sorted[i]);
            }
        }

        /* meta_stack_get_positions (): every window, by stack_position. */
        public GLib.GenericArray<Window> get_positions () {
            sync ();
            var positions = new GLib.GenericArray<Window> ();
            foreach (var w in sorted) {
                positions.add (w);
            }
            positions.sort ((a, b) => a.stack_position - b.stack_position);
            return positions;
        }

        /* meta_stack_set_positions (): only for the same set of windows. */
        public bool set_positions (GLib.GenericArray<Window> windows) {
            sync ();
            if (windows.length != sorted.length) {
                return false;
            }
            foreach (var w in windows) {
                uint index;
                if (!sorted.find (w, out index)) {
                    return false;
                }
            }
            sorted.remove_range (0, sorted.length);
            for (int i = 0; i < windows.length; i++) {
                sorted.add (windows[i]);
                windows[i].stack_position = i;
            }
            need_resort = true;
            need_constrain = true;
            sync ();
            return true;
        }

        public unowned GLib.GenericArray<Window> get_bottom_to_top () {
            return bottom_to_top;
        }

        public Window? get_top () {
            return sorted.length > 0 ? sorted[0] : null;
        }

        public Window? get_bottom () {
            return sorted.length > 0 ? sorted[sorted.length - 1] : null;
        }

        public Window? get_above (Window window, bool only_within_layer) {
            uint i;
            if (!sorted.find (window, out i) || i == 0) {
                return null;
            }
            var above = sorted[i - 1];
            return only_within_layer && above.layer != window.layer ? null : above;
        }

        public Window? get_below (Window window, bool only_within_layer) {
            uint i;
            if (!sorted.find (window, out i) || i + 1 >= sorted.length) {
                return null;
            }
            var below = sorted[i + 1];
            return only_within_layer && below.layer != window.layer ? null : below;
        }

        /* meta_stack_windows_cmp (): negative when a is below b. */
        public int windows_cmp (Window a, Window b) {
            if (a.layer < b.layer) {
                return -1;
            } else if (a.layer > b.layer) {
                return 1;
            } else if (a.stack_position < b.stack_position) {
                return -1;
            } else if (a.stack_position > b.stack_position) {
                return 1;
            }
            return 0;
        }

        private static bool window_contains_point (Window window, int x, int y) {
            var r = window.outer_rect ();
            return x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height;
        }

        /*
         * get_default_focus_window (): the topmost focusable window,
         * preferring the transient parent of not_this_one, then the top
         * window of its group, then anything but a dock, then a dock.
         * workspace -1 means any workspace.
         */
        public Window? get_default_focus_window (int workspace, Window? not_this_one,
                                                 bool must_be_at_point = false,
                                                 int root_x = 0, int root_y = 0) {
            Window? topmost_dock = null;
            Window? transient_parent = null;
            Window? topmost_in_group = null;
            Window? topmost_overall = null;
            uint64 not_this_one_group = not_this_one != null ? group_id (not_this_one) : 0;

            foreach (var window in sorted) {
                if (window == not_this_one || window.has_state (WindowState.MINIMIZED) ||
                    !window.accepts_focus ||
                    !(workspace == -1 || located_on_workspace (window, workspace))) {
                    continue;
                }
                if (topmost_dock == null && window.window_type == Marco.WindowType.DOCK) {
                    topmost_dock = window;
                }
                if (not_this_one != null) {
                    if (transient_parent == null && not_this_one.transient_for == window &&
                        (!must_be_at_point || window_contains_point (window, root_x, root_y))) {
                        transient_parent = window;
                    }
                    if (topmost_in_group == null && not_this_one_group == group_id (window) &&
                        (!must_be_at_point || window_contains_point (window, root_x, root_y))) {
                        topmost_in_group = window;
                    }
                }
                if (topmost_overall == null && window.window_type != Marco.WindowType.DOCK &&
                    (!must_be_at_point || window_contains_point (window, root_x, root_y))) {
                    topmost_overall = window;
                }
            }

            if (transient_parent != null) {
                return transient_parent;
            } else if (topmost_in_group != null) {
                return topmost_in_group;
            } else if (topmost_overall != null) {
                return topmost_overall;
            }
            return topmost_dock;
        }

        /* A window without a client leader is a group of its own. */
        private static uint64 group_id (Window w) {
            return w.group != 0 ? w.group : (1ULL << 63) | w.id;
        }
    }
}
