/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Marco window.c meta_window_show_menu () and menu_callback (), and
 * keybindings.c handle_activate_window_menu (). polo-shell-ui draws the
 * menu (menu.c) and answers with the chosen op.
 */

namespace Polo.Shell {
    public class WindowMenu : GLib.Object {
        private const uint ON_ALL_WORKSPACES = uint.MAX;

        private Backend.Server server;
        private Policy.Model model;
        private Desktop desktop;
        private bool connected = false;
        private uint next_id = 1;
        private uint menu_id = 0;
        private Window? menu_window = null;

        public WindowMenu (Backend.Server server, Policy.Model model, Desktop desktop, Input.Bindings bindings) {
            this.server = server;
            this.model = model;
            this.desktop = desktop;
            bindings.register ("activate-window-menu", (binding, seat, event) => {
                handle_activate_window_menu ();
            });
        }

        public void shutdown () {
            if (connected) {
                server.get_shell_ui ().menu_action.disconnect (on_menu_action);
                connected = false;
            }
            forget ();
        }

        private void handle_activate_window_menu () {
            var window = model.focused;
            if (window == null) {
                return;
            }
            var client = window.client_rect ();
            int x = client.x;
            if (!Policy.WorkspaceLayout.ltr ()) {
                x += client.width;
            }
            show (window, x, client.y);
        }

        private bool titlebar_is_onscreen (Window window) {
            if (!window.has_frame ()) {
                return true;
            }
            var titlebar = window.frame;
            titlebar.height = window.visible_borders ().top;
            foreach (var spanning in model.work_areas ().screen_region) {
                Marco.Rect overlap;
                titlebar.intersect (spanning, out overlap);
                if (overlap.height > int.min (titlebar.height, 8) &&
                    overlap.width > int.min ((int) (0.5 * titlebar.width), 50)) {
                    return true;
                }
            }
            return false;
        }

        private void shove_titlebar_onscreen (Window window) {
            if (!window.has_frame ()) {
                return;
            }
            var outer = window.frame;
            var region = new GLib.List<Marco.Rect?> ();
            foreach (var r in model.work_areas ().screen_region) {
                region.append (r);
            }
            region = Marco.Region.expand ((owned) region, outer.width, outer.width, 0, outer.height);
            Marco.Region.shove (region, Marco.FixedDirections.X, ref outer);
            var borders = window.visible_borders ();
            model.move (window, false, outer.x + borders.left, outer.y + borders.top);
        }

        private static bool allows_resize (Window window) {
            return window.has_feature (WindowFeatures.RESIZE) &&
                   !window.has_state (WindowState.MAXIMIZED) &&
                   !window.has_state (WindowState.FULLSCREEN) && !window.has_state (WindowState.SHADED) &&
                   (window.hints.min_width < window.hints.max_width ||
                    window.hints.min_height < window.hints.max_height);
        }

        /* root_x and root_y are global coordinates. */
        public void show (Window window, int root_x, int root_y) {
            var shell_ui = server.get_shell_ui ();
            if (!shell_ui.is_bound ()) {
                Logging.scope (LogDomain.POLICY).debug ("no polo-shell-ui for the window menu of %s",
                                                        window.describe ());
                return;
            }
            if (!connected) {
                shell_ui.menu_action.connect (on_menu_action);
                connected = true;
            }
            if (menu_window != null) {
                shell_ui.hide_window_menu (menu_id);
                forget ();
            }

            var ops = Backend.MenuOp.DELETE | Backend.MenuOp.MINIMIZE | Backend.MenuOp.MOVE |
                      Backend.MenuOp.RESIZE;
            Backend.MenuOp insensitive = Backend.MenuOp.NONE;
            if (!titlebar_is_onscreen (window) && window.window_type != Marco.WindowType.DOCK &&
                window.window_type != Marco.WindowType.DESKTOP) {
                ops |= Backend.MenuOp.RECOVER;
            }
            int n_workspaces = model.n_workspaces;
            if (n_workspaces > 1) {
                ops |= Backend.MenuOp.WORKSPACES;
            }
            if (window.workspace != -1) {
                var layout = model.workspace_layout ();
                bool ltr = Policy.WorkspaceLayout.ltr ();
                if (layout.current_col > 0) {
                    ops |= ltr ? Backend.MenuOp.MOVE_LEFT : Backend.MenuOp.MOVE_RIGHT;
                }
                if (layout.current_col < layout.cols - 1 &&
                    layout.current_row * layout.cols + (layout.current_col + 1) < n_workspaces) {
                    ops |= ltr ? Backend.MenuOp.MOVE_RIGHT : Backend.MenuOp.MOVE_LEFT;
                }
                if (layout.current_row > 0) {
                    ops |= Backend.MenuOp.MOVE_UP;
                }
                if (layout.current_row < layout.rows - 1 &&
                    (layout.current_row + 1) * layout.cols + layout.current_col < n_workspaces) {
                    ops |= Backend.MenuOp.MOVE_DOWN;
                }
            }
            ops |= window.has_state (WindowState.MAXIMIZED) ? Backend.MenuOp.UNMAXIMIZE : Backend.MenuOp.MAXIMIZE;
            ops |= Backend.MenuOp.UNSTICK | Backend.MenuOp.STICK;
            ops |= window.has_state (WindowState.ABOVE) ? Backend.MenuOp.UNABOVE : Backend.MenuOp.ABOVE;

            if (!window.has_feature (WindowFeatures.MAXIMIZE)) {
                insensitive |= Backend.MenuOp.UNMAXIMIZE | Backend.MenuOp.MAXIMIZE;
            }
            if (!window.has_feature (WindowFeatures.MINIMIZE)) {
                insensitive |= Backend.MenuOp.MINIMIZE;
            }
            if (!window.has_feature (WindowFeatures.SHADE)) {
                insensitive |= Backend.MenuOp.SHADE | Backend.MenuOp.UNSHADE;
            }
            if (!window.has_feature (WindowFeatures.MOVE) || window.has_state (WindowState.FULLSCREEN)) {
                insensitive |= Backend.MenuOp.MOVE;
            }
            if (!allows_resize (window)) {
                insensitive |= Backend.MenuOp.RESIZE;
            }
            if (window.has_feature (WindowFeatures.ALWAYS_STICKY)) {
                insensitive |= Backend.MenuOp.STICK | Backend.MenuOp.UNSTICK | Backend.MenuOp.WORKSPACES;
            }
            if (window.window_type == Marco.WindowType.DESKTOP || window.window_type == Marco.WindowType.DOCK ||
                window.window_type == Marco.WindowType.SPLASHSCREEN) {
                insensitive |= Backend.MenuOp.ABOVE | Backend.MenuOp.UNABOVE;
            }
            if ((ops & ~insensitive) == 0) {
                return;
            }

            var output = model.output_at (root_x, root_y) ?? model.output_for_window (window);
            int x = output != null ? root_x - output.rect.x : root_x;
            int y = output != null ? root_y - output.rect.y : root_y;
            for (int i = 0; i < n_workspaces; i++) {
                shell_ui.window_menu_workspace (i, model.workspace_name (i));
            }
            menu_id = next_id++;
            menu_window = window;
            window.unmanaged.connect (on_unmanaged);
            shell_ui.show_window_menu (menu_id, output != null ? desktop.backend_output (output) : null, x, y, ops,
                                       insensitive,
                                       window.workspace == -1 ? ON_ALL_WORKSPACES : (uint) window.workspace,
                                       n_workspaces);
        }

        private void forget () {
            if (menu_window != null) {
                menu_window.unmanaged.disconnect (on_unmanaged);
                menu_window = null;
            }
        }

        private void on_unmanaged () {
            server.get_shell_ui ().hide_window_menu (menu_id);
            forget ();
        }

        private void on_menu_action (uint id, Backend.MenuOp op, int workspace_index) {
            if (id != menu_id || menu_window == null) {
                return;
            }
            var window = menu_window;
            forget ();

            int workspace = -1;
            switch (op) {
                case Backend.MenuOp.DELETE:
                    model.close (window);
                    break;
                case Backend.MenuOp.MINIMIZE:
                    model.minimize (window);
                    break;
                case Backend.MenuOp.UNMAXIMIZE:
                    model.unmaximize (window, WindowState.MAXIMIZED);
                    break;
                case Backend.MenuOp.MAXIMIZE:
                    model.maximize (window, WindowState.MAXIMIZED);
                    break;
                case Backend.MenuOp.UNSHADE:
                    model.set_state (window, WindowState.SHADED, false);
                    break;
                case Backend.MenuOp.SHADE:
                    model.set_state (window, WindowState.SHADED, true);
                    break;
                case Backend.MenuOp.MOVE_LEFT:
                    workspace = model.workspace_neighbor (Policy.Motion.LEFT);
                    break;
                case Backend.MenuOp.MOVE_RIGHT:
                    workspace = model.workspace_neighbor (Policy.Motion.RIGHT);
                    break;
                case Backend.MenuOp.MOVE_UP:
                    workspace = model.workspace_neighbor (Policy.Motion.UP);
                    break;
                case Backend.MenuOp.MOVE_DOWN:
                    workspace = model.workspace_neighbor (Policy.Motion.DOWN);
                    break;
                case Backend.MenuOp.WORKSPACES:
                    if (workspace_index >= 0 && workspace_index < model.n_workspaces) {
                        workspace = workspace_index;
                    }
                    break;
                case Backend.MenuOp.STICK:
                    model.set_state (window, WindowState.STICKY, true);
                    break;
                case Backend.MenuOp.UNSTICK:
                    model.set_state (window, WindowState.STICKY, false);
                    break;
                case Backend.MenuOp.ABOVE:
                case Backend.MenuOp.UNABOVE:
                    model.set_state (window, WindowState.ABOVE, !window.has_state (WindowState.ABOVE));
                    model.raise (window);
                    break;
                case Backend.MenuOp.MOVE:
                    begin_keyboard (window, Input.GrabOp.KEYBOARD_MOVING);
                    break;
                case Backend.MenuOp.RESIZE:
                    begin_keyboard (window, Input.GrabOp.KEYBOARD_RESIZING_UNKNOWN);
                    break;
                case Backend.MenuOp.RECOVER:
                    shove_titlebar_onscreen (window);
                    break;
                default:
                    break;
            }
            if (workspace >= 0) {
                model.move_to_workspace (window, workspace);
            }
        }

        private void begin_keyboard (Window window, Input.GrabOp op) {
            var seat = desktop.default_seat ();
            if (seat != null) {
                desktop.move_resize.begin_keyboard (seat, window, op, model.last_user_time);
            }
        }
    }
}
