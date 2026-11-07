/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Marco's directional workspace keys: keybindings.c
 * handle_workspace_switch_or_move () and process_workspace_switch_grab (),
 * and screen.c meta_screen_ensure_workspace_popup (). Each press switches at
 * once; the popup shows while the primary modifier is held.
 */

namespace Polo.Shell {
    public class WorkspaceSwitcher : GLib.Object {
        private class Grab {
            public unowned Backend.Seat seat;
            public Backend.Modifiers primary;
            public Marco.VirtualModifier mask;
            public Window? window;
            public int selected;
            public bool showing = false;
        }

        private Backend.Server server;
        private Policy.Model model;
        private Desktop desktop;
        private Input.Bindings bindings;
        private Grab? grab = null;

        public bool active { get { return grab != null; } }

        public WorkspaceSwitcher (Backend.Server server, Policy.Model model, Desktop desktop,
                                  Input.Bindings bindings) {
            this.server = server;
            this.model = model;
            this.desktop = desktop;
            this.bindings = bindings;
            add ("left", Policy.Motion.LEFT);
            add ("right", Policy.Motion.RIGHT);
            add ("up", Policy.Motion.UP);
            add ("down", Policy.Motion.DOWN);
        }

        public void shutdown () {
            if (grab != null) {
                end_grab ();
            }
        }

        private void add (string direction, Policy.Motion motion) {
            bindings.register ("switch-to-workspace-" + direction, (binding, seat, event) => {
                switch_or_move (seat, event, motion, null);
            });
            bindings.register ("move-to-workspace-" + direction, (binding, seat, event) => {
                var window = model.focused;
                if (window != null && !window.has_feature (WindowFeatures.ALWAYS_STICKY)) {
                    switch_or_move (seat, event, motion, window);
                }
            });
        }

        private void switch_or_move (Backend.Seat seat, Backend.KeyEvent event, Policy.Motion motion,
                                     Window? window) {
            if (model.n_workspaces == 1 || desktop.grab_active) {
                return;
            }
            var mask = Input.Accelerators.from_real (event.modifiers);
            var primary = Switcher.primary_modifier (mask);
            int next = next_workspace (motion, model.current_workspace);
            bool grabbed_before_release = primary != 0 && primary in seat.get_modifiers ();
            activate (next, window, motion);
            if (grabbed_before_release) {
                grab = new Grab () {
                    seat = seat,
                    primary = primary,
                    mask = mask,
                    window = window,
                    selected = next
                };
                seat.modifiers_changed.connect (on_modifiers_changed);
                server.get_shell_ui ().switcher_select.connect (on_switcher_select);
            }
            if (grabbed_before_release && !desktop.cube.active) {
                show_popup ();
            }
        }

        private int next_workspace (Policy.Motion motion, int from) {
            if (desktop.cube.available () && (motion == Policy.Motion.LEFT || motion == Policy.Motion.RIGHT)) {
                var layout = model.workspace_layout ();
                int[] row = {};
                int position = -1;
                for (int column = 0; column < layout.cols; column++) {
                    int workspace = layout.grid[layout.current_row * layout.cols + column];
                    if (workspace < 0) {
                        continue;
                    }
                    if (workspace == from) {
                        position = row.length;
                    }
                    row += workspace;
                }
                if (row.length > 1 && position >= 0) {
                    int step = motion == Policy.Motion.RIGHT ? 1 : -1;
                    return row[(position + step + row.length) % row.length];
                }
            }
            return model.workspace_neighbor (motion, from);
        }

        private void activate (int workspace, Window? window, Policy.Motion motion) {
            int direction = motion == Policy.Motion.RIGHT ? 1 : motion == Policy.Motion.LEFT ? -1 : 0;
            if (window != null) {
                model.move_window_and_activate (window, workspace, direction);
            } else {
                model.switch_workspace (workspace, null, true, direction);
            }
        }

        private void send_entries () {
            var shell_ui = server.get_shell_ui ();
            var layout = model.workspace_layout ();
            for (int i = 0; i < layout.grid.length; i++) {
                int workspace = layout.grid[i];
                if (workspace < 0) {
                    shell_ui.workspace_switcher_entry (0, "", true);
                    continue;
                }
                shell_ui.workspace_switcher_entry (workspace + 1, model.workspace_name (workspace), false);
                send_windows (workspace);
            }
        }

        /* Send windows from bottom to top. */
        private void send_windows (int workspace) {
            var shell_ui = server.get_shell_ui ();
            foreach (var w in model.get_stack ()) {
                bool sticky = w.workspace == -1;
                if ((!sticky && w.workspace != workspace) || w.wm_state_skip_pager ||
                    !model.showing_on_its_workspace (w) ||
                    (sticky && workspace != model.current_workspace)) {
                    continue;
                }
                shell_ui.workspace_switcher_window (workspace + 1, w.frame.x, w.frame.y, w.frame.width,
                                                    w.frame.height, w == model.focused, w.app_id);
            }
        }

        private void show_popup () {
            var shell_ui = server.get_shell_ui ();
            if (!shell_ui.is_bound ()) {
                return;
            }
            var output = model.output_at (model.pointer_x, model.pointer_y);
            if (output == null && model.get_outputs ().length > 0) {
                output = model.get_outputs ()[0];
            }
            int screen_width = 0;
            int screen_height = 0;
            foreach (var o in model.get_outputs ()) {
                screen_width = int.max (screen_width, o.rect.x + o.rect.width);
                screen_height = int.max (screen_height, o.rect.y + o.rect.height);
            }
            send_entries ();
            var layout = model.workspace_layout ();
            shell_ui.show_workspace_switcher (output != null ? desktop.backend_output (output) : null,
                                              grab.selected + 1, layout.cols, model.current_workspace + 1,
                                              screen_width, screen_height,
                                              output != null ? output.rect.width : screen_width,
                                              output != null ? output.rect.height : screen_height,
                                              (uint) model.prefs.icon_size);
            grab.showing = true;
        }

        public void hide_popup () {
            if (grab != null && grab.showing) {
                server.get_shell_ui ().hide_switcher ();
                grab.showing = false;
            }
        }

        private void update_popup () {
            if (!grab.showing) {
                return;
            }
            send_entries ();
            server.get_shell_ui ().update_workspace_switcher (grab.selected + 1, model.current_workspace + 1);
        }

        public Backend.FilterResult key (Backend.Seat seat, Backend.KeyEvent event) {
            if (grab == null || seat != grab.seat) {
                return Backend.FilterResult.PASS;
            }
            if (!event.pressed || Switcher.is_modifier (event.keysym) || Switcher.is_modifier (event.base_keysym)) {
                return Backend.FilterResult.CONSUME;
            }
            unowned Input.Binding? binding = bindings.match_mask (event, grab.mask);
            Policy.Motion? motion = null;
            if (binding != null) {
                unowned string name = binding.name;
                if (name.has_suffix ("-workspace-up")) {
                    motion = Policy.Motion.UP;
                } else if (name.has_suffix ("-workspace-down")) {
                    motion = Policy.Motion.DOWN;
                } else if (name.has_suffix ("-workspace-left")) {
                    motion = Policy.Motion.LEFT;
                } else if (name.has_suffix ("-workspace-right")) {
                    motion = Policy.Motion.RIGHT;
                }
            }
            if (motion != null) {
                grab.selected = next_workspace (motion, grab.selected);
                activate (grab.selected, grab.window, motion);
                if (!grab.showing && !desktop.cube.active) {
                    show_popup ();
                } else {
                    update_popup ();
                }
                return Backend.FilterResult.CONSUME;
            }
            bool switching = grab.window == null;
            end_grab ();
            if (switching) {
                model.focus_default ();
            }
            return Backend.FilterResult.CONSUME;
        }

        /* The KeyRelease branch: the switch has already happened. */
        private void on_modifiers_changed (Backend.Seat seat, Backend.Modifiers modifiers) {
            if (grab != null && seat == grab.seat && !(grab.primary in modifiers)) {
                if (grab.selected != model.current_workspace) {
                    Logging.scope (LogDomain.POLICY).warning (
                        "target_workspace != active_workspace.  Some other event must have occurred.");
                }
                end_grab ();
            }
        }

        /* A popup click changes selection without switching immediately. */
        private void on_switcher_select (uint id) {
            if (grab != null && grab.showing && id >= 1 && id <= model.n_workspaces) {
                grab.selected = (int) id - 1;
                server.get_shell_ui ().update_selected (id);
            }
        }

        /* A press outside the popup ends the grab. */
        public void button (Backend.ButtonEvent event) {
            if (grab == null || !event.pressed) {
                return;
            }
            if (grab.showing && event.view != null) {
                unowned Backend.Client? client = event.view.get_client ();
                if (client != null && client == server.get_shell_ui ().get_client ()) {
                    return;
                }
            }
            end_grab ();
        }

        private void end_grab () {
            grab.seat.modifiers_changed.disconnect (on_modifiers_changed);
            server.get_shell_ui ().switcher_select.disconnect (on_switcher_select);
            if (grab.showing) {
                server.get_shell_ui ().hide_switcher ();
            }
            grab = null;
        }
    }
}
