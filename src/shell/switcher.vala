/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Marco's window switching: keybindings.c do_choose_window () and
 * process_tab_grab (), the tab grab parts of display.c, and
 * meta_screen_ensure_tab_popup (). polo-shell-ui draws the popup
 * (tabpopup.c); Polo keeps the selection, as the popup's grid, and draws
 * the outline around the selected window.
 */

namespace Polo.Shell {
    public delegate bool MinimizeTarget (Window window, out Marco.Rect rect);

    public class Switcher : GLib.Object {
        private const int OUTLINE_WIDTH = 5;
        private const double MAX_PREVIEW_SCREEN_FRACTION = 0.33;
        private const double MAX_PREVIEW_SIZE = 300.0;
        /* keybindings.h ARROW_*, as evdev codes. */
        private const uint KEY_UP = 103;
        private const uint KEY_LEFT = 105;
        private const uint KEY_RIGHT = 106;
        private const uint KEY_DOWN = 108;
        private const uint KEY_KP8 = 72;
        private const uint KEY_KP4 = 75;
        private const uint KEY_KP6 = 77;
        private const uint KEY_KP2 = 80;

        private enum Action {
            NONE,
            CYCLE,
            CYCLE_BACKWARD,
            SWITCH,
            SWITCH_BACKWARD,
            CYCLE_GROUP,
            CYCLE_GROUP_BACKWARD,
            SWITCH_GROUP,
            SWITCH_GROUP_BACKWARD
        }

        private enum Direction {
            NEXT,
            PREV,
            UP,
            DOWN,
            LEFT,
            RIGHT
        }

        private class Grab {
            public unowned Backend.Seat seat;
            public Policy.TabList type;
            public bool tabbing;
            public Backend.Modifiers primary;
            public Marco.VirtualModifier mask;
            public GLib.GenericArray<Window> entries;
            public int current = -1;
            public GLib.GenericArray<Window> old_stacking;
            public bool showing = false;
            public uint columns;
        }

        private Backend.Server server;
        private Policy.Model model;
        private Desktop desktop;
        private Input.Bindings bindings;
        private Outline outline;
        private Grab? grab = null;
        private MinimizeTarget? minimize_target = null;

        public bool active { get { return grab != null; } }

        public Switcher (Backend.Server server, Policy.Model model, Desktop desktop, Input.Bindings bindings) {
            this.server = server;
            this.model = model;
            this.desktop = desktop;
            this.bindings = bindings;
            outline = new Outline (server);
            add ("switch-group", Policy.TabList.GROUP, true);
            add ("switch-windows", Policy.TabList.NORMAL, true);
            add ("switch-windows-all", Policy.TabList.NORMAL_ALL_WORKSPACES, true);
            add ("switch-panels", Policy.TabList.DOCKS, true);
            add ("cycle-group", Policy.TabList.GROUP, false);
            add ("cycle-windows", Policy.TabList.NORMAL, false);
            add ("cycle-panels", Policy.TabList.DOCKS, false);
        }

        public void shutdown () {
            if (grab != null) {
                end_grab ();
            }
            outline.hide ();
            minimize_target = null;
        }

        public void set_minimize_target (owned MinimizeTarget? target) {
            minimize_target = (owned) target;
        }

        private void add (string name, Policy.TabList type, bool show_popup) {
            bindings.register (name, (binding, seat, event) => {
                do_choose_window (seat, event, type, false, show_popup);
            });
            bindings.register (name + "-backward", (binding, seat, event) => {
                do_choose_window (seat, event, type, true, show_popup);
            });
        }

        public static Backend.Modifiers primary_modifier (Marco.VirtualModifier mask) {
            if (Marco.VirtualModifier.MOD5_MASK in mask) {
                return Backend.Modifiers.MOD5;
            } else if (Marco.VirtualModifier.SUPER_MASK in mask) {
                return Backend.Modifiers.MOD4;
            } else if (Marco.VirtualModifier.MOD3_MASK in mask) {
                return Backend.Modifiers.MOD3;
            } else if (Marco.VirtualModifier.ALT_MASK in mask) {
                return Backend.Modifiers.MOD1;
            } else if (Marco.VirtualModifier.CONTROL_MASK in mask) {
                return Backend.Modifiers.CONTROL;
            } else if (Marco.VirtualModifier.SHIFT_MASK in mask) {
                return Backend.Modifiers.SHIFT;
            }
            return 0;
        }

        public static bool is_modifier (uint32 keysym) {
            return (keysym >= 0xffe1 && keysym <= 0xffee) || (keysym >= 0xfe01 && keysym <= 0xfe13) ||
                   keysym == 0xff7e || keysym == 0xff7f;
        }

        private bool managed (Window window) {
            return model.stack_index (window) >= 0;
        }

        private void activate (Window window) {
            model.focus_policy.leave_mouse_mode ();
            model.activate (window, Policy.ActivationSource.PAGER, 0);
            if (window.workspace != -1) {
                model.switch_workspace (window.workspace);
            }
        }

        /* ---- do_choose_window () ------------------------------------------ */

        private void do_choose_window (Backend.Seat seat, Backend.KeyEvent event, Policy.TabList type,
                                       bool backward, bool show_popup) {
            if (desktop.grab_active) {
                return;
            }
            if (Backend.Modifiers.SHIFT in event.modifiers) {
                backward = !backward;
            }
            var initial = Policy.TabChain.get_tab_next (model, type, model.current_workspace, null, backward);
            if (initial == null) {
                initial = Policy.TabChain.get_tab_current (model, type, model.current_workspace);
            }
            if (initial == null) {
                return;
            }

            var mask = Input.Accelerators.from_real (event.modifiers);
            if (mask == 0) {
                activate (initial);
                return;
            }

            grab = new Grab () {
                seat = seat,
                type = type,
                tabbing = show_popup,
                primary = primary_modifier (mask),
                mask = mask,
                entries = Policy.TabChain.get_tab_list (model, type, model.current_workspace),
                old_stacking = model.get_stack_positions (),
                columns = (uint) int.max (model.prefs.alt_tab_max_columns, 1)
            };
            seat.modifiers_changed.connect (on_modifiers_changed);
            server.get_shell_ui ().switcher_select.connect (on_switcher_select);

            if (!(grab.primary in seat.get_modifiers ())) {
                end_grab ();
                activate (initial);
                return;
            }

            select (initial);
            if (show_popup) {
                show_popup_now ();
            } else {
                model.raise (initial);
                initial.tab_unminimized = initial.has_state (WindowState.MINIMIZED);
                model.unminimize (initial);
            }
        }

        private void show_popup_now () {
            var shell_ui = server.get_shell_ui ();
            if (!shell_ui.is_bound ()) {
                return;
            }
            var output = model.output_at (model.pointer_x, model.pointer_y);
            if (output == null && model.get_outputs ().length > 0) {
                output = model.get_outputs ()[0];
            }
            double landscape = MAX_PREVIEW_SIZE;
            double portrait = MAX_PREVIEW_SIZE;
            if (output != null) {
                landscape = double.min (MAX_PREVIEW_SIZE,
                                        MAX_PREVIEW_SCREEN_FRACTION * output.rect.width / grab.columns);
                portrait = double.min (MAX_PREVIEW_SIZE,
                                       MAX_PREVIEW_SCREEN_FRACTION * output.rect.height / grab.columns);
            }
            for (int i = 0; i < grab.entries.length; i++) {
                var window = grab.entries[i];
                Backend.SwitcherEntryFlags flags = 0;
                if (!model.showing_on_its_workspace (window)) {
                    flags |= Backend.SwitcherEntryFlags.HIDDEN;
                }
                if (window.has_state (WindowState.DEMANDS_ATTENTION)) {
                    flags |= Backend.SwitcherEntryFlags.DEMANDS_ATTENTION;
                }
                shell_ui.switcher_entry (i + 1, window.title ?? "", window.app_id, flags);
                var xdg = window.adapter as XdgWindow;
                if (xdg != null) {
                    shell_ui.switcher_icon (i + 1, xdg.toplevel);
                }
                if (!model.prefs.compositing_fast_alt_tab && xdg != null) {
                    unowned Backend.View? view = xdg.toplevel.get_view ();
                    if (view != null) {
                        shell_ui.switcher_thumbnail (i + 1, view, landscape, portrait);
                    }
                }
            }
            shell_ui.show_switcher (output != null ? desktop.backend_output (output) : null,
                                    grab.current + 1, grab.columns, model.prefs.alt_tab_expand_to_fit_title,
                                    (uint) model.prefs.icon_size);
            grab.showing = true;
        }

        /* ---- The popup's selection (tabpopup.c) -------------------------- */

        /* Select the first entry for this window. */
        private void select (Window window) {
            for (int i = 0; i < grab.entries.length; i++) {
                if (grab.entries[i] == window) {
                    grab.current = i;
                    display_entry ();
                    return;
                }
            }
        }

        private Window? selected () {
            return grab.current >= 0 ? grab.entries[grab.current] : null;
        }

        private void forward () {
            if (grab.entries.length == 0) {
                return;
            }
            grab.current = grab.current + 1 < grab.entries.length ? grab.current + 1 : 0;
            display_entry ();
        }

        private void backward () {
            if (grab.entries.length == 0) {
                return;
            }
            grab.current = grab.current > 0 ? grab.current - 1 : (int) grab.entries.length - 1;
            display_entry ();
        }

        /* Move to the grid cell above or below. */
        private void vertical (int rows) {
            if (grab.current < 0) {
                return;
            }
            int target = grab.current + rows * (int) grab.columns;
            if (target >= 0 && target < grab.entries.length) {
                grab.current = target;
                display_entry ();
            }
        }

        private void display_entry () {
            var window = selected ();
            if (window == null) {
                return;
            }
            if (grab.showing) {
                server.get_shell_ui ().update_selected (grab.current + 1);
            }
            if (!model.prefs.show_tab_border) {
                return;
            }
            Marco.Rect rect = window.outer_rect ();
            if (grab.tabbing && !model.showing_on_its_workspace (window) && minimize_target != null) {
                Marco.Rect icon;
                if (minimize_target (window, out icon)) {
                    rect = icon;
                }
            }
            outline.show (rect, OUTLINE_WIDTH);
        }

        private void on_switcher_select (uint id) {
            if (grab == null || !grab.showing || id < 1 || id > grab.entries.length) {
                return;
            }
            grab.current = (int) id - 1;
            display_entry ();
        }

        /* ---- process_tab_grab () ------------------------------------------- */

        private Action action_of (Backend.KeyEvent event) {
            unowned Input.Binding? binding = bindings.match_mask (event, grab.mask);
            if (binding == null) {
                return Action.NONE;
            }
            unowned string name = binding.name;
            bool reversed = name.has_suffix ("-backward");
            if (name.has_prefix ("cycle-group")) {
                return reversed ? Action.CYCLE_GROUP_BACKWARD : Action.CYCLE_GROUP;
            }
            if (name.has_prefix ("switch-group")) {
                return reversed ? Action.SWITCH_GROUP_BACKWARD : Action.SWITCH_GROUP;
            }
            if (name.has_prefix ("cycle-windows") || name.has_prefix ("cycle-panels")) {
                return reversed ? Action.CYCLE_BACKWARD : Action.CYCLE;
            }
            if (name.has_prefix ("switch-windows") || name.has_prefix ("switch-panels")) {
                return reversed ? Action.SWITCH_BACKWARD : Action.SWITCH;
            }
            return Action.NONE;
        }

        public Backend.FilterResult key (Backend.Seat seat, Backend.KeyEvent event) {
            if (grab == null || seat != grab.seat) {
                return Backend.FilterResult.PASS;
            }
            if (!event.pressed) {
                return Backend.FilterResult.CONSUME;
            }
            if (!process_tab_grab (event)) {
                end_grab ();
            }
            return Backend.FilterResult.CONSUME;
        }

        private void restore_stacking () {
            model.set_stack_positions (grab.old_stacking);
        }

        private void reminimize (Window? window) {
            if (window != null && window.tab_unminimized) {
                model.minimize (window);
                window.tab_unminimized = false;
            }
        }

        private bool process_tab_grab (Backend.KeyEvent event) {
            if (is_modifier (event.keysym) || is_modifier (event.base_keysym)) {
                return true;
            }
            var prev_window = selected ();
            var action = action_of (event);
            bool raise_windows = model.prefs.alt_tab_raise_windows;
            bool escaped_alt_tab = false;
            bool group_op = grab.type == Policy.TabList.GROUP;
            bool all_op = grab.type == Policy.TabList.NORMAL_ALL_WORKSPACES;

            switch (action) {
                case Action.CYCLE:
                case Action.CYCLE_BACKWARD:
                    if (!grab.tabbing && !group_op) {
                        break;
                    }
                    if (grab.tabbing && grab.type == Policy.TabList.NORMAL && raise_windows) {
                        escaped_alt_tab = true;
                        break;
                    }
                    return false;
                case Action.SWITCH:
                case Action.SWITCH_BACKWARD:
                    if (grab.tabbing && (grab.type == Policy.TabList.NORMAL || all_op ||
                                         grab.type == Policy.TabList.DOCKS)) {
                        break;
                    }
                    restore_stacking ();
                    reminimize (prev_window);
                    return false;
                case Action.CYCLE_GROUP:
                case Action.CYCLE_GROUP_BACKWARD:
                case Action.SWITCH_GROUP:
                case Action.SWITCH_GROUP_BACKWARD:
                    if (group_op) {
                        break;
                    }
                    return false;
                default:
                    break;
            }

            bool popup_not_showing = false;
            bool key_used = false;
            var direction = Direction.NEXT;
            switch (action) {
                case Action.CYCLE:
                case Action.CYCLE_GROUP:
                    popup_not_showing = true;
                    key_used = !escaped_alt_tab;
                    break;
                case Action.CYCLE_BACKWARD:
                case Action.CYCLE_GROUP_BACKWARD:
                    popup_not_showing = true;
                    key_used = true;
                    direction = Direction.PREV;
                    break;
                case Action.SWITCH:
                case Action.SWITCH_GROUP:
                    key_used = true;
                    break;
                case Action.SWITCH_BACKWARD:
                case Action.SWITCH_GROUP_BACKWARD:
                    key_used = true;
                    direction = Direction.PREV;
                    break;
                default:
                    break;
            }

            if (event.keycode == KEY_RIGHT || event.keycode == KEY_KP6) {
                key_used = true;
                direction = Direction.RIGHT;
            } else if (event.keycode == KEY_LEFT || event.keycode == KEY_KP4) {
                key_used = true;
                direction = Direction.LEFT;
            } else if (event.keycode == KEY_UP || event.keycode == KEY_KP8) {
                key_used = true;
                direction = Direction.UP;
            } else if (event.keycode == KEY_DOWN || event.keycode == KEY_KP2) {
                key_used = true;
                direction = Direction.DOWN;
            }

            if (key_used) {
                if (Backend.Modifiers.SHIFT in event.modifiers &&
                    (direction == Direction.NEXT || direction == Direction.PREV)) {
                    direction = direction == Direction.NEXT ? Direction.PREV : Direction.NEXT;
                }
                if (direction == Direction.PREV || direction == Direction.LEFT) {
                    backward ();
                } else if (direction == Direction.NEXT || direction == Direction.RIGHT) {
                    forward ();
                } else if (direction == Direction.DOWN) {
                    vertical (1);
                } else {
                    vertical (-1);
                }

                if (popup_not_showing || raise_windows) {
                    restore_stacking ();
                    var target = selected ();
                    reminimize (prev_window);
                    if (target != null) {
                        model.raise (target);
                        target.tab_unminimized = target.has_state (WindowState.MINIMIZED);
                        model.unminimize (target);
                    }
                }
            } else {
                restore_stacking ();
                reminimize (prev_window);
            }
            return key_used;
        }

        /* Complete the grab after its primary modifier is released. */
        private void on_modifiers_changed (Backend.Seat seat, Backend.Modifiers modifiers) {
            if (grab == null || seat != grab.seat || grab.primary in modifiers) {
                return;
            }
            var target = selected ();
            if (target != null && managed (target)) {
                target.tab_unminimized = false;
                end_grab ();
                activate (target);
                return;
            }
            end_grab ();
        }

        /* A press outside the popup ends the grab with the old stacking. */
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
            restore_stacking ();
            end_grab ();
        }

        private void end_grab () {
            grab.seat.modifiers_changed.disconnect (on_modifiers_changed);
            server.get_shell_ui ().switcher_select.disconnect (on_switcher_select);
            if (grab.showing) {
                server.get_shell_ui ().hide_switcher ();
            }
            outline.hide ();
            grab = null;
        }
    }

    /* tabpopup.c's outline window: a black frame with white edges, drawn by Polo. */
    internal class Outline {
        private Backend.Server server;
        private Backend.PixelBuffer? buffer = null;
        private Marco.Rect rect;

        public Outline (Backend.Server server) {
            this.server = server;
        }

        public void show (Marco.Rect rect, int width) {
            hide ();
            if (rect.width <= 0 || rect.height <= 0) {
                return;
            }
            this.rect = rect;
            buffer = new Backend.PixelBuffer (server, rect.width, rect.height, 1);
            int stride;
            unowned uint8[] pixels = buffer.begin (out stride);
            if (pixels.length > 0) {
                for (int y = 0; y < rect.height; y++) {
                    for (int x = 0; x < rect.width; x++) {
                        int d = int.min (int.min (x, y), int.min (rect.width - 1 - x, rect.height - 1 - y));
                        uint8 value = d == 0 || d == width - 1 ? 0xff : 0x00;
                        uint8 alpha = d < width ? 0xff : 0x00;
                        int offset = y * stride + x * 4;
                        pixels[offset] = alpha != 0 ? value : 0;
                        pixels[offset + 1] = alpha != 0 ? value : 0;
                        pixels[offset + 2] = alpha != 0 ? value : 0;
                        pixels[offset + 3] = alpha;
                    }
                }
            }
            buffer.commit ();
            if (buffer.is_ready ()) {
                place ();
            } else {
                buffer.ready.connect (place);
            }
        }

        private void place () {
            unowned Backend.View? view = buffer != null ? buffer.get_view () : null;
            if (view == null) {
                return;
            }
            view.set_position (rect.x, rect.y);
            view.move_to_layer (server.get_layer (Backend.LayerKind.OVERLAY));
        }

        public void hide () {
            if (buffer == null) {
                return;
            }
            buffer.ready.disconnect (place);
            unowned Backend.View? view = buffer.get_view ();
            if (view != null) {
                view.move_to_layer (null);
            }
            buffer = null;
        }
    }
}
