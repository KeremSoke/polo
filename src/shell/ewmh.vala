/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The EWMH root window properties, computed from the model as Marco's
 * screen.c and stack.c compute them; the XWM writes them. Native windows cannot
 * be represented, so the client lists hold X windows only.
 */

namespace Polo.Shell {
    public delegate void NumWorkspacesWriter (int count);

    public class Ewmh : GLib.Object {
        /* Marco's _NET_SUPPORTED: atomnames.h under EWMH_ATOMS_ONLY and the _GTK ones. */
        private const string[] SUPPORTED = {
            "_NET_WM_NAME", "_NET_CLOSE_WINDOW", "_NET_WM_STATE", "_NET_WM_STATE_SHADED",
            "_NET_WM_STATE_MAXIMIZED_HORZ", "_NET_WM_STATE_MAXIMIZED_VERT", "_NET_WM_DESKTOP",
            "_NET_NUMBER_OF_DESKTOPS", "_NET_CURRENT_DESKTOP", "_NET_WM_WINDOW_TYPE",
            "_NET_WM_WINDOW_TYPE_DESKTOP", "_NET_WM_WINDOW_TYPE_DOCK", "_NET_WM_WINDOW_TYPE_TOOLBAR",
            "_NET_WM_WINDOW_TYPE_MENU", "_NET_WM_WINDOW_TYPE_DIALOG", "_NET_WM_WINDOW_TYPE_NORMAL",
            "_NET_WM_STATE_MODAL", "_NET_CLIENT_LIST", "_NET_CLIENT_LIST_STACKING",
            "_NET_WM_STATE_SKIP_TASKBAR", "_NET_WM_STATE_SKIP_PAGER", "_NET_WM_ICON_NAME", "_NET_WM_ICON",
            "_NET_WM_ICON_GEOMETRY", "_NET_WM_MOVERESIZE", "_NET_ACTIVE_WINDOW", "_NET_WM_STRUT",
            "_NET_WM_STATE_HIDDEN", "_NET_WM_WINDOW_TYPE_UTILITY", "_NET_WM_WINDOW_TYPE_SPLASH",
            "_NET_WM_STATE_FULLSCREEN", "_NET_WM_PING", "_NET_WM_PID", "_NET_WORKAREA",
            "_NET_SHOWING_DESKTOP", "_NET_DESKTOP_LAYOUT", "_NET_DESKTOP_NAMES", "_NET_WM_ALLOWED_ACTIONS",
            "_NET_WM_ACTION_MOVE", "_NET_WM_ACTION_RESIZE", "_NET_WM_ACTION_SHADE", "_NET_WM_ACTION_STICK",
            "_NET_WM_ACTION_MAXIMIZE_HORZ", "_NET_WM_ACTION_MAXIMIZE_VERT", "_NET_WM_ACTION_CHANGE_DESKTOP",
            "_NET_WM_ACTION_CLOSE", "_NET_WM_STATE_ABOVE", "_NET_WM_STATE_BELOW", "_NET_STARTUP_ID",
            "_NET_WM_STRUT_PARTIAL", "_NET_WM_ACTION_FULLSCREEN", "_NET_WM_ACTION_MINIMIZE",
            "_NET_FRAME_EXTENTS", "_NET_REQUEST_FRAME_EXTENTS", "_NET_WM_USER_TIME",
            "_NET_WM_STATE_DEMANDS_ATTENTION", "_NET_MOVERESIZE_WINDOW", "_NET_DESKTOP_GEOMETRY",
            "_NET_DESKTOP_VIEWPORT", "_NET_WM_USER_TIME_WINDOW", "_NET_WM_ACTION_ABOVE", "_NET_WM_ACTION_BELOW",
            "_NET_WM_STATE_STICKY", "_NET_WM_FULLSCREEN_MONITORS", "_NET_WM_STATE_FOCUSED",
            "_NET_RESTACK_WINDOW", "_GTK_FRAME_EXTENTS", "_GTK_SHOW_WINDOW_MENU", "_GTK_WORKAREAS"
        };

        private Policy.Model model;
        private Xwm.Manager xwm;
        private Decor.Decorations decorations;
        private uint idle = 0;
        public Shell.NumWorkspacesWriter? write_num_workspaces_func = null;

        private void write_num_workspaces (int count) {
            if (write_num_workspaces_func != null) {
                write_num_workspaces_func (count);
            }
        }

        /* Marco reports normal-window borders unless MWM hints disable decoration. */
        private void request_frame_extents (Xwm.XWindow window) {
            var hints = window.get_cardinals ("_MOTIF_WM_HINTS");
            var borders = Marco.FrameBorders ();
            if (hints.length < 3 || hints[2] != 0) {
                borders = decorations.normal_borders ();
            }
            window.set_frame_extents (borders.left, borders.right, borders.top, borders.bottom);
        }

        public Ewmh (Policy.Model model, Xwm.Manager xwm, Decor.Decorations decorations) {
            this.model = model;
            this.xwm = xwm;
            this.decorations = decorations;
            xwm.started.connect (sync_all);
            xwm.root_property_changed.connect (on_root_property_changed);
            xwm.root_message.connect (on_root_message);
            xwm.focus_lost.connect (on_focus_lost);
            xwm.window_created.connect ((window) => {
                window.client_message.connect ((type) => {
                    if (type == "_NET_REQUEST_FRAME_EXTENTS") {
                        request_frame_extents (window);
                    }
                });
            });
            model.stack_changed.connect (queue_sync);
            model.focus_changed.connect (queue_sync);
            model.work_areas_changed.connect (queue_sync);
            model.notify["n-workspaces"].connect (queue_sync);
            model.notify["current-workspace"].connect (queue_sync);
            model.notify["prefs"].connect (queue_sync);
        }

        public void shutdown () {
            xwm.started.disconnect (sync_all);
            xwm.root_property_changed.disconnect (on_root_property_changed);
            xwm.root_message.disconnect (on_root_message);
            xwm.focus_lost.disconnect (on_focus_lost);
            model.stack_changed.disconnect (queue_sync);
            model.focus_changed.disconnect (queue_sync);
            model.work_areas_changed.disconnect (queue_sync);
            model.notify["n-workspaces"].disconnect (queue_sync);
            model.notify["current-workspace"].disconnect (queue_sync);
            model.notify["prefs"].disconnect (queue_sync);
            if (idle != 0) {
                GLib.Source.remove (idle);
                idle = 0;
            }
        }

        private void queue_sync () {
            if (idle == 0) {
                idle = GLib.Idle.add (() => {
                    idle = 0;
                    sync ();
                    return GLib.Source.REMOVE;
                });
            }
        }

        private void sync_all () {
            xwm.set_supported (SUPPORTED);
            sync ();
        }

        private void sync () {
            if (!xwm.is_running ()) {
                return;
            }
            sync_workspaces ();
            sync_work_areas ();
            sync_windows ();
            xwm.set_showing_desktop (model.showing_desktop);
        }

        private void sync_workspaces () {
            xwm.set_number_of_desktops (model.n_workspaces);
            xwm.set_current_desktop (model.current_workspace);
            string[] names = {};
            for (int i = 0; i < model.n_workspaces; i++) {
                names += model.workspace_name (i);
            }
            xwm.set_desktop_names (names);
            var screen = Marco.Rect ();
            var outputs = model.get_outputs ();
            foreach (var output in outputs) {
                Marco.Rect merged;
                screen.union (output.rect, out merged);
                screen = merged;
            }
            xwm.set_desktop_geometry (screen.width, screen.height);
            xwm.set_desktop_viewport (new uint32[model.n_workspaces * 2]);
        }

        private void sync_work_areas () {
            var outputs = model.get_outputs ();
            uint32[] all = {};
            for (int workspace = 0; workspace < model.n_workspaces; workspace++) {
                var areas = model.work_areas (workspace);
                var screen = areas.screen_area;
                all += screen.x;
                all += screen.y;
                all += screen.width;
                all += screen.height;
                uint32[] per_output = {};
                foreach (var output in outputs) {
                    var area = areas.area_for (output);
                    per_output += area.x;
                    per_output += area.y;
                    per_output += area.width;
                    per_output += area.height;
                }
                xwm.set_gtk_workareas (workspace, per_output);
            }
            xwm.set_workarea (all);
        }

        /* Publish mapped windows in mapping and bottom-to-top order. */
        private void sync_windows () {
            uint32[] mapped = {};
            foreach (var window in model.get_windows ()) {
                uint32 id = managed_id (window);
                if (id != 0) {
                    mapped += id;
                }
            }
            uint32[] stacked = {};
            foreach (var window in model.get_stack ()) {
                uint32 id = managed_id (window);
                if (id != 0) {
                    stacked += id;
                }
            }
            xwm.set_client_list (mapped);
            xwm.set_client_list_stacking (stacked);
            xwm.set_active_window (model.focused != null ? managed_id (model.focused) : 0);
        }

        private static uint32 managed_id (Window window) {
            var x_window = window.adapter as X11Window;
            return x_window != null && x_window.mapped ? x_window.xwindow.get_id () : 0;
        }

        private void on_root_message (string type, uint data) {
            switch (type) {
                case "_NET_CURRENT_DESKTOP":
                    if (data < model.n_workspaces) {
                        model.switch_workspace ((int) data);
                    }
                    break;
                case "_NET_NUMBER_OF_DESKTOPS":
                    write_num_workspaces ((int) data);
                    break;
                case "_NET_SHOWING_DESKTOP":
                    if (data != 0) {
                        model.show_desktop ();
                    } else {
                        model.unshow_desktop ();
                        model.focus_default ();
                    }
                    break;
                default:
                    break;
            }
        }

        /* Root focus means no client currently owns focus. */
        private void on_focus_lost () {
            model.focus_policy.focus_default_window (null, model.last_user_time);
        }

        private void on_root_property_changed (string atom) {
            if (atom != "_NET_DESKTOP_LAYOUT") {
                return;
            }
            uint32[] list = xwm.get_desktop_layout ();
            if (list.length != 3 && list.length != 4) {
                return;
            }
            var corner = Policy.Corner.TOPLEFT;
            if (list.length == 4) {
                switch (list[3]) {
                    case 1: corner = Policy.Corner.TOPRIGHT; break;
                    case 2: corner = Policy.Corner.BOTTOMRIGHT; break;
                    case 3: corner = Policy.Corner.BOTTOMLEFT; break;
                    default: break;
                }
            }
            model.set_workspace_layout ((int) list[1], (int) list[2], list[0] == 1, corner);
        }
    }
}
