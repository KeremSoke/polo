/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Marco display.c: the tab chains and meta_display_get_tab_list (),
 * _get_tab_next () and _get_tab_current (), over the workspaces' MRU lists.
 */

namespace Polo.Policy {
    public enum TabList {
        NORMAL,
        DOCKS,
        GROUP,
        NORMAL_ALL_WORKSPACES
    }

    namespace TabChain {
        private static bool in_normal_chain_type (Window w) {
            return w.window_type != Marco.WindowType.DOCK && w.window_type != Marco.WindowType.DESKTOP;
        }

        private static uint64 group_of (Window w) {
            return w.group != 0 ? w.group : (1ULL << 63) | w.id;
        }

        /* IN_TAB_CHAIN (); group is get_focussed_group (), 0 for none. */
        public bool contains (Window w, TabList type, uint64 group) {
            if (!w.accepts_focus) {
                return false;
            }
            switch (type) {
                case TabList.NORMAL:
                case TabList.NORMAL_ALL_WORKSPACES:
                    return in_normal_chain_type (w) && !w.wm_state_skip_taskbar;
                case TabList.DOCKS:
                    return !in_normal_chain_type (w) || w.wm_state_skip_taskbar;
                default:
                    return group == 0 || group_of (w) == group;
            }
        }

        private static uint64 focused_group (Model model) {
            return model.focused != null ? group_of (model.focused) : 0;
        }

        private static bool located_on_workspace (Window w, int workspace) {
            return w.workspace == -1 || w.workspace == workspace;
        }

        public GLib.GenericArray<Window> get_tab_list (Model model, TabList type, int active_workspace) {
            uint64 group = focused_group (model);
            int[] workspaces;
            if (type == TabList.NORMAL_ALL_WORKSPACES) {
                workspaces = new int[model.n_workspaces];
                for (int i = 0; i < model.n_workspaces; i++) {
                    workspaces[i] = i;
                }
            } else {
                workspaces = { active_workspace };
            }

            var tab_list = new GLib.GenericArray<Window> ();
            foreach (bool minimized in new bool[] { false, true }) {
                foreach (int workspace in workspaces) {
                    foreach (var w in model.get_mru (workspace)) {
                        if (w.has_state (WindowState.MINIMIZED) == minimized && contains (w, type, group)) {
                            tab_list.add (w);
                        }
                    }
                }
            }

            foreach (var w in model.get_windows ()) {
                if (w.has_state (WindowState.DEMANDS_ATTENTION) &&
                    w.home_workspace != active_workspace && contains (w, type, group)) {
                    tab_list.insert (0, w);
                }
            }
            return tab_list;
        }

        private static Window? find_tab_forward (Model model, TabList type, int workspace,
                                                 GLib.GenericArray<Window> tab_list, int start,
                                                 bool skip_first, uint64 group) {
            for (int i = skip_first ? start + 1 : start; i < tab_list.length; i++) {
                if (contains (tab_list[i], type, group)) {
                    return tab_list[i];
                }
            }
            foreach (var w in model.get_mru (workspace)) {
                if (contains (w, type, group)) {
                    return w;
                }
            }
            return null;
        }

        private static Window? find_tab_backward (Model model, TabList type, int workspace,
                                                  GLib.GenericArray<Window> tab_list, int start,
                                                  bool skip_last, uint64 group) {
            for (int i = skip_last ? start - 1 : start; i >= 0; i--) {
                if (contains (tab_list[i], type, group)) {
                    return tab_list[i];
                }
            }
            var mru = model.get_mru (workspace);
            for (int i = (int) mru.length - 1; i >= 0; i--) {
                if (contains (mru[i], type, group)) {
                    return mru[i];
                }
            }
            return null;
        }

        public Window? get_tab_next (Model model, TabList type, int workspace, Window? window, bool backward) {
            var tab_list = get_tab_list (model, type, workspace);
            if (tab_list.length == 0) {
                return null;
            }
            uint64 group = focused_group (model);
            int start = 0;
            bool skip;
            if (window != null) {
                uint index;
                if (!tab_list.find (window, out index)) {
                    return null;
                }
                start = (int) index;
                skip = true;
            } else {
                skip = model.focused != null && tab_list[0] == model.focused;
            }
            return backward ? find_tab_backward (model, type, workspace, tab_list, start, skip, group)
                            : find_tab_forward (model, type, workspace, tab_list, start, skip, group);
        }

        public Window? get_tab_current (Model model, TabList type, int workspace) {
            var window = model.focused;
            if (window != null && contains (window, type, focused_group (model)) &&
                (workspace < 0 || located_on_workspace (window, workspace))) {
                return window;
            }
            return null;
        }
    }
}
