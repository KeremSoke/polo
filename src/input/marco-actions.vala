/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Marco's keybinding handlers (keybindings.c). Window bindings act on the
 * focus window and do nothing without one. Bindings without a handler leave
 * their keys for clients.
 */

namespace Polo.Input {
    public delegate void CommandLauncher (string command_line) throws GLib.Error;
    public delegate void ErrorDialog (string message);

    public class MarcoActions : GLib.Object {
        private Policy.Model model;
        private MoveResize move_resize;
        private CommandLauncher? launcher;
        private ErrorDialog? error_dialog;
        private GLib.Settings? terminal_settings;

        [CCode (cname = "g_dgettext", cheader_filename = "glib.h")]
        private static extern unowned string dgettext (string domain, string msgid);

        private static unowned string marco (string msgid) {
            return dgettext ("marco", msgid);
        }

        public MarcoActions (Policy.Model model, Bindings bindings, MoveResize move_resize) {
            this.model = model;
            this.move_resize = move_resize;
            var source = GLib.SettingsSchemaSource.get_default ();
            var schema = source != null ? source.lookup ("org.mate.applications-terminal", true) : null;
            if (schema != null) {
                terminal_settings = new GLib.Settings.full (schema, null, null);
            }

            for (int i = 1; i <= 12; i++) {
                bindings.register ("switch-to-workspace-%d".printf (i), handle_switch_to_workspace);
                bindings.register ("move-to-workspace-%d".printf (i), per_window (handle_move_to_workspace));
                bindings.register ("run-command-%d".printf (i), handle_run_command);
            }
            bindings.register ("show-desktop", (binding, seat, event) => handle_show_desktop ());
            bindings.register ("switch-to-workspace-prev", handle_switch_to_workspace);
            bindings.register ("run-command-screenshot", handle_run_command);
            bindings.register ("run-command-window-screenshot", handle_run_command);
            bindings.register ("run-command-terminal", handle_run_terminal);
            bindings.register ("panel-run-dialog", (binding, seat, event) => {
                launch ("mate-panel --run-dialog");
            });

            bindings.register ("toggle-fullscreen", per_window (handle_toggle_fullscreen));
            bindings.register ("toggle-maximized", per_window (handle_toggle_maximized));
            bindings.register ("toggle-above", per_window (handle_toggle_above));
            bindings.register ("toggle-shaded", per_window (handle_toggle_shaded));
            bindings.register ("maximize", per_window (handle_maximize));
            bindings.register ("unmaximize", per_window (handle_unmaximize));
            bindings.register ("minimize", per_window (handle_minimize));
            bindings.register ("close", per_window (handle_close));
            bindings.register ("begin-move", per_window (handle_begin_move));
            bindings.register ("begin-resize", per_window (handle_begin_resize));
            bindings.register ("toggle-on-all-workspaces", per_window (handle_toggle_on_all_workspaces));
            bindings.register ("raise-or-lower", per_window (handle_raise_or_lower));
            bindings.register ("raise", per_window ((w) => model.raise (w)));
            bindings.register ("lower", per_window ((w) => model.lower (w)));
            bindings.register ("maximize-vertically", per_window (handle_maximize_vertically));
            bindings.register ("maximize-horizontally", per_window (handle_maximize_horizontally));
            bindings.register ("move-to-corner-nw", per_window ((w) => move_to_corner (w, true, true, false, false)));
            bindings.register ("move-to-corner-ne", per_window ((w) => move_to_corner (w, true, true, true, false)));
            bindings.register ("move-to-corner-sw", per_window ((w) => move_to_corner (w, true, true, false, true)));
            bindings.register ("move-to-corner-se", per_window ((w) => move_to_corner (w, true, true, true, true)));
            bindings.register ("move-to-side-n", per_window ((w) => move_to_corner (w, false, true, false, false)));
            bindings.register ("move-to-side-s", per_window ((w) => move_to_corner (w, false, true, false, true)));
            bindings.register ("move-to-side-e", per_window ((w) => move_to_corner (w, true, false, true, false)));
            bindings.register ("move-to-side-w", per_window ((w) => move_to_corner (w, true, false, false, false)));
            bindings.register ("move-to-center", per_window (handle_move_to_center));
            bindings.register ("tile-to-side-e", per_window ((w) => model.toggle_tiled (w, Marco.TileMode.RIGHT)));
            bindings.register ("tile-to-side-w", per_window ((w) => model.toggle_tiled (w, Marco.TileMode.LEFT)));
            bindings.register ("tile-to-corner-nw",
                               per_window ((w) => model.toggle_tiled (w, Marco.TileMode.TOP_LEFT)));
            bindings.register ("tile-to-corner-ne",
                               per_window ((w) => model.toggle_tiled (w, Marco.TileMode.TOP_RIGHT)));
            bindings.register ("tile-to-corner-sw",
                               per_window ((w) => model.toggle_tiled (w, Marco.TileMode.BOTTOM_LEFT)));
            bindings.register ("tile-to-corner-se",
                               per_window ((w) => model.toggle_tiled (w, Marco.TileMode.BOTTOM_RIGHT)));
            bindings.register ("move-to-monitor-n", per_window ((w) => move_to_monitor (w, Direction.UP)));
            bindings.register ("move-to-monitor-s", per_window ((w) => move_to_monitor (w, Direction.DOWN)));
            bindings.register ("move-to-monitor-e", per_window ((w) => move_to_monitor (w, Direction.RIGHT)));
            bindings.register ("move-to-monitor-w", per_window ((w) => move_to_monitor (w, Direction.LEFT)));
        }

        public void set_launcher (owned CommandLauncher? launcher) {
            this.launcher = (owned) launcher;
        }

        public void set_error_dialog (owned ErrorDialog? dialog) {
            this.error_dialog = (owned) dialog;
        }

        private delegate void WindowHandler (Window window);

        private unowned Backend.Seat? current_seat = null;
        private Backend.KeyEvent current_event;
        private string current_binding_name = "";

        private BindingAction per_window (owned WindowHandler handler) {
            return (binding, seat, event) => {
                var window = model.focused;
                if (window != null) {
                    current_seat = seat;
                    current_event = event;
                    current_binding_name = binding.name;
                    handler (window);
                    current_seat = null;
                }
            };
        }

        /* ---- Global ------------------------------------------------------ */

        /* Numbered workspaces plus "prev", which switches without a popup. */
        private void handle_switch_to_workspace (Binding binding, Backend.Seat seat, Backend.KeyEvent event) {
            string suffix = binding.name.substring ("switch-to-workspace-".length);
            if (suffix == "prev") {
                model.switch_workspace (model.previous_workspace);
                return;
            }
            int which = int.parse (suffix) - 1;
            if (which >= 0 && which < model.n_workspaces) {
                model.switch_workspace (which);
            }
        }

        private void handle_show_desktop () {
            if (model.showing_desktop) {
                model.unshow_desktop ();
                model.focus_default ();
            } else {
                model.show_desktop ();
            }
        }

        private void handle_run_command (Binding binding, Backend.Seat seat, Backend.KeyEvent event) {
            string suffix = binding.name.substring ("run-command-".length);
            int which = int.parse (suffix);
            var value = model.prefs.get_value ("keybinding-commands/command-" + suffix);
            if (value == null) {
                show_error (marco ("No command %d has been defined.\n").printf (which));
                return;
            }
            run (value.get_string ());
        }

        private void handle_run_terminal (Binding binding, Backend.Seat seat, Backend.KeyEvent event) {
            if (terminal_settings == null) {
                show_error (marco ("No terminal command has been defined.\n"));
                return;
            }
            run (terminal_settings.get_string ("exec"));
        }

        private void run (string command) {
            if (launcher == null) {
                Logging.scope (LogDomain.INPUT).warning ("cannot run \"%s\": no launcher yet", command);
                return;
            }
            try {
                launcher (command);
            } catch (GLib.Error e) {
                Logging.scope (LogDomain.INPUT).warning ("Error on command \"%s\": %s", command, e.message);
                if (command != "") {
                    show_error (marco ("There was an error running <tt>%s</tt>:\n\n%s").printf (
                        GLib.Markup.escape_text (command), GLib.Markup.escape_text (e.message)));
                } else {
                    show_error (GLib.Markup.escape_text (e.message));
                }
            }
        }

        private void launch (string command) {
            if (launcher == null) {
                return;
            }
            try {
                launcher (command);
            } catch (GLib.Error e) {
                Logging.scope (LogDomain.INPUT).warning ("cannot run \"%s\": %s", command, e.message);
            }
        }

        private void show_error (string message) {
            if (error_dialog != null) {
                error_dialog (message);
            }
        }

        /* ---- Per window -------------------------------------------------- */

        private void handle_toggle_fullscreen (Window window) {
            if (window.has_state (WindowState.FULLSCREEN)) {
                model.set_state (window, WindowState.FULLSCREEN, false);
            } else if (window.has_feature (WindowFeatures.FULLSCREEN)) {
                model.set_state (window, WindowState.FULLSCREEN, true);
            }
        }

        private void handle_toggle_maximized (Window window) {
            if (window.has_state (WindowState.MAXIMIZED)) {
                model.set_state (window, WindowState.MAXIMIZED, false);
            } else if (window.has_feature (WindowFeatures.MAXIMIZE)) {
                model.set_state (window, WindowState.MAXIMIZED, true);
            }
        }

        /* handle_toggle_above (): meta_window_make_above () / unmake_above () raise too. */
        private void handle_toggle_above (Window window) {
            model.set_state (window, WindowState.ABOVE, !window.has_state (WindowState.ABOVE));
            model.raise (window);
        }

        private void handle_toggle_shaded (Window window) {
            if (window.has_state (WindowState.SHADED)) {
                model.unshade (window, current_event.time_msec);
            } else if (window.has_feature (WindowFeatures.SHADE)) {
                model.shade (window, current_event.time_msec);
            }
        }

        private void handle_maximize (Window window) {
            if (window.has_feature (WindowFeatures.MAXIMIZE)) {
                model.set_state (window, WindowState.MAXIMIZED, true);
            }
        }

        private void handle_unmaximize (Window window) {
            bool corner_tiled = window.tiled &&
                                (window.tile_mode == Marco.TileMode.TOP_LEFT ||
                                 window.tile_mode == Marco.TileMode.TOP_RIGHT ||
                                 window.tile_mode == Marco.TileMode.BOTTOM_LEFT ||
                                 window.tile_mode == Marco.TileMode.BOTTOM_RIGHT);
            if (window.has_state (WindowState.MAXIMIZED_V) || window.has_state (WindowState.MAXIMIZED_H) ||
                corner_tiled) {
                model.set_state (window, WindowState.MAXIMIZED, false);
            }
        }

        private void handle_minimize (Window window) {
            if (window.has_feature (WindowFeatures.MINIMIZE)) {
                model.minimize (window);
            }
        }

        private void handle_close (Window window) {
            if (window.has_feature (WindowFeatures.CLOSE)) {
                model.close (window);
            }
        }

        private void handle_begin_move (Window window) {
            if (window.has_feature (WindowFeatures.MOVE)) {
                move_resize.begin_keyboard (current_seat, window, GrabOp.KEYBOARD_MOVING,
                                            current_event.time_msec);
            }
        }

        private void handle_begin_resize (Window window) {
            if (window.has_feature (WindowFeatures.RESIZE)) {
                move_resize.begin_keyboard (current_seat, window, GrabOp.KEYBOARD_RESIZING_UNKNOWN,
                                            current_event.time_msec);
            }
        }

        private void handle_toggle_on_all_workspaces (Window window) {
            model.set_state (window, WindowState.STICKY, !window.has_state (WindowState.STICKY));
        }

        private void handle_move_to_workspace (Window window) {
            if (window.has_feature (WindowFeatures.ALWAYS_STICKY)) {
                return;
            }
            int which = int.parse (current_binding_name.substring ("move-to-workspace-".length)) - 1;
            if (which >= 0 && which < model.n_workspaces) {
                model.move_to_workspace (window, which);
            }
        }

        private void handle_raise_or_lower (Window window) {
            var stack = model.get_stack_object ();
            if (stack.get_top () == window) {
                model.lower (window);
                return;
            }
            for (var above = stack.get_above (window, true); above != null;
                 above = stack.get_above (above, true)) {
                if (model.is_showing (above)) {
                    Marco.Rect overlap;
                    if (window.outer_rect ().intersect (above.outer_rect (), out overlap)) {
                        model.raise (window);
                        return;
                    }
                }
            }
            model.lower (window);
        }

        private void handle_maximize_vertically (Window window) {
            if (window.has_feature (WindowFeatures.RESIZE)) {
                model.set_state (window, WindowState.MAXIMIZED_V, !window.has_state (WindowState.MAXIMIZED_V));
            }
        }

        private void handle_maximize_horizontally (Window window) {
            if (window.has_feature (WindowFeatures.RESIZE)) {
                model.set_state (window, WindowState.MAXIMIZED_H, !window.has_state (WindowState.MAXIMIZED_H));
            }
        }

        private void move_resize_to (Window window, int x, int y) {
            var client = window.client_rect ();
            model.move_resize_internal (window,
                                        Marco.MoveResizeFlags.IS_MOVE_ACTION |
                                        Marco.MoveResizeFlags.IS_RESIZE_ACTION |
                                        Marco.MoveResizeFlags.IS_USER_ACTION,
                                        Marco.Gravity.NORTH_WEST,
                                        Marco.Rect () { x = x, y = y, width = client.width, height = client.height });
        }

        /* handle_move_to_corner_backend (): against the work area of all xineramas. */
        private void move_to_corner (Window window, bool xchange, bool ychange, bool to_right, bool to_bottom) {
            var work_area = model.work_areas (window.workspace).screen_area;
            var outer = window.outer_rect ();
            var orig = window.client_rect ();
            int new_x = orig.x;
            int new_y = orig.y;
            if (xchange) {
                int delta_x = to_right ? (work_area.x + work_area.width) - (outer.x + outer.width)
                                       : work_area.x - outer.x;
                new_x = orig.x + delta_x;
            }
            if (ychange) {
                int delta_y = to_bottom ? (work_area.y + work_area.height) - (outer.y + outer.height)
                                        : work_area.y - outer.y;
                new_y = orig.y + delta_y;
            }
            move_resize_to (window, new_x, new_y);
        }

        private void handle_move_to_center (Window window) {
            var outputs = model.get_outputs ();
            if (outputs.length == 0) {
                return;
            }
            var current = outputs[Policy.Placement.xinerama_for_rect (outputs, window.frame)];
            var work_area = model.work_area_for (current, window.workspace);
            var outer = window.frame;
            outer.x = work_area.x + (work_area.width - outer.width) / 2;
            outer.y = work_area.y + (work_area.height - outer.height) / 2;
            var borders = window.visible_borders ();
            move_resize_to (window, outer.x + borders.left, outer.y + borders.top);
        }

        private enum Direction {
            UP,
            DOWN,
            LEFT,
            RIGHT
        }

        private static Policy.Output? neighbor (GLib.GenericArray<Policy.Output> outputs, Policy.Output input,
                                                Direction direction) {
            var r = input.rect;
            foreach (var current in outputs) {
                var c = current.rect;
                bool vert_overlap = c.y < r.y + r.height && r.y < c.y + c.height;
                bool horiz_overlap = c.x < r.x + r.width && r.x < c.x + c.width;
                if ((direction == Direction.RIGHT && c.x == r.x + r.width && vert_overlap) ||
                    (direction == Direction.LEFT && r.x == c.x + c.width && vert_overlap) ||
                    (direction == Direction.UP && r.y == c.y + c.height && horiz_overlap) ||
                    (direction == Direction.DOWN && c.y == r.y + r.height && horiz_overlap)) {
                    return current;
                }
            }
            return null;
        }

        private static Marco.Rect transform_to_monitor (Marco.Rect rect, Marco.Rect from, Marco.Rect to) {
            double horizontal_ratio = (double) to.width / from.width;
            double vertical_ratio = (double) to.height / from.height;
            rect.x -= from.x;
            rect.y -= from.y;
            rect.width = (int) (horizontal_ratio * (double) rect.width);
            rect.height = (int) (vertical_ratio * (double) rect.height);
            rect.x = (int) (horizontal_ratio * (double) rect.x);
            rect.y = (int) (vertical_ratio * (double) rect.y);
            rect.x += to.x;
            rect.y += to.y;
            return rect;
        }

        private void move_to_monitor (Window window, Direction direction) {
            var outputs = model.get_outputs ();
            if (outputs.length == 0) {
                return;
            }
            var current = outputs[Policy.Placement.xinerama_for_rect (outputs, window.frame)];
            var next = neighbor (outputs, current, direction);
            if (next == null || next == current) {
                return;
            }
            var from = current.rect;
            var to = next.rect;
            if (Policy.Model.is_tiled (window)) {
                for (int i = 0; i < outputs.length; i++) {
                    if (outputs[i] == next) {
                        window.tile_monitor_number = i;
                    }
                }
                window.saved_rect = transform_to_monitor (window.saved_rect, from, to);
                window.user_rect = transform_to_monitor (window.user_rect, from, to);
                model.tile (window);
                return;
            }
            var target = transform_to_monitor (window.client_rect (), from, to);
            model.move_resize_internal (window,
                                        Marco.MoveResizeFlags.IS_MOVE_ACTION |
                                        Marco.MoveResizeFlags.IS_RESIZE_ACTION |
                                        Marco.MoveResizeFlags.IS_USER_ACTION,
                                        Marco.Gravity.NORTH_WEST, target);
        }
    }
}
