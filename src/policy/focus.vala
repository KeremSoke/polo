/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Marco's focus policy (window.c, display.c, workspace.c): user times,
 * focus on map, click/sloppy/mouse focus, auto-raise, modal redirection
 * and the default window after a close, minimize or workspace switch.
 */

namespace Polo.Policy {
    public class FocusPolicy : GLib.Object {
        private unowned Model model;

        /* display->mouse_mode: the last focus change came from the pointer. */
        public bool mouse_mode { get; private set; default = true; }
        public bool allow_terminal_deactivation { get; private set; default = true; }

        private Window? autoraise_window = null;
        private uint autoraise_timeout = 0;
        private Window? entered = null;

        public FocusPolicy (Model model) {
            this.model = model;
        }

        public void shutdown () {
            remove_autoraise_callback ();
        }

        private static bool time_is_before (int64 time1, int64 time2) {
            return time1 == 0 || (time2 != 0 && time1 < time2);
        }

        public void set_user_time (Window window, int64 timestamp) {
            if (window.user_time != Window.USER_TIME_UNSET && time_is_before (timestamp, window.user_time)) {
                return;
            }
            window.user_time = timestamp;
            if (time_is_before (model.last_user_time, timestamp)) {
                model.note_user_time (timestamp);
            }
            if (model.prefs.focus_new_windows == FocusNewWindows.STRICT && is_terminal (window)) {
                allow_terminal_deactivation = false;
            }
        }

        private bool intervening_user_event_occurred (Window window) {
            var focus_window = model.focused;
            bool user_time_set = window.user_time != Window.USER_TIME_UNSET;
            bool initial_set = window.initial_timestamp != Window.USER_TIME_UNSET;
            if ((user_time_set && window.user_time == 0) || (initial_set && window.initial_timestamp == 0)) {
                return true;
            }
            if (!user_time_set && !initial_set) {
                return false;
            }
            if (focus_window != null && focus_window.user_time == Window.USER_TIME_UNSET) {
                return false;
            }
            int64 compare;
            if (user_time_set && initial_set) {
                compare = time_is_before (window.user_time, window.initial_timestamp)
                    ? window.initial_timestamp : window.user_time;
            } else if (user_time_set) {
                compare = window.user_time;
            } else {
                compare = window.initial_timestamp;
            }
            return focus_window != null && time_is_before (compare, focus_window.user_time);
        }

        private static bool is_terminal (Window? window) {
            if (window == null || window.res_class == null) {
                return false;
            }
            switch (window.res_class) {
                case "Mate-terminal":
                case "XTerm":
                case "Konsole":
                case "URxvt":
                case "Eterm":
                case "KTerm":
                case "Multi-mate-terminal":
                case "mlterm":
                case "Terminal":
                    return true;
                default:
                    return false;
            }
        }

        public void state_on_map (Window window, out bool takes_focus, out bool places_on_top) {
            takes_focus = !intervening_user_event_occurred (window);
            places_on_top = takes_focus;

            if (!window.accepts_focus) {
                takes_focus = false;
                return;
            }

            if (takes_focus && model.prefs.focus_new_windows == FocusNewWindows.STRICT &&
                !allow_terminal_deactivation && is_terminal (model.focused) &&
                !Model.is_ancestor_of_transient (model.focused, window)) {
                takes_focus = false;
                places_on_top = false;
            }

            switch (window.window_type) {
                case Marco.WindowType.UTILITY:
                case Marco.WindowType.TOOLBAR:
                    takes_focus = false;
                    places_on_top = false;
                    break;
                case Marco.WindowType.DOCK:
                case Marco.WindowType.DESKTOP:
                case Marco.WindowType.SPLASHSCREEN:
                case Marco.WindowType.MENU:
                    takes_focus = false;
                    break;
                default:
                    break;
            }
        }

        private Window? get_modal_transient (Window window) {
            var modal_transient = window;
            var windows = model.get_windows ();
            for (int i = 0; i < windows.length; i++) {
                var transient = windows[i];
                if (transient.transient_for == modal_transient && transient.has_state (WindowState.MODAL)) {
                    modal_transient = transient;
                    i = -1;
                }
            }
            return modal_transient == window ? null : modal_transient;
        }

        public void focus_window (Window window, int64 timestamp) {
            var modal_transient = get_modal_transient (window);
            if (modal_transient != null && model.is_managed (modal_transient)) {
                if (modal_transient.workspace != -1 && modal_transient.workspace != model.current_workspace) {
                    model.move_to_workspace (modal_transient, model.current_workspace);
                }
                window = modal_transient;
            }

            if (!model.is_showing (window) && !window.has_state (WindowState.SHADED)) {
                return;
            }

            if (window.accepts_focus || window.has_frame ()) {
                model.set_input_focus (window);
            }

            if (window.has_state (WindowState.DEMANDS_ATTENTION)) {
                model.set_state (window, WindowState.DEMANDS_ATTENTION, false);
            }
        }

        public void focus_no_window () {
            model.set_input_focus (null);
            remove_autoraise_callback ();
        }

        public void focus_default_window (Window? not_this_one, int64 timestamp) {
            if (model.prefs.focus_mode == FocusMode.CLICK || !mouse_mode) {
                focus_ancestor_or_top_window (not_this_one, timestamp);
                return;
            }
            var window = model.get_stack_object ().get_default_focus_window (
                model.current_workspace, not_this_one, true, (int) model.pointer_x, (int) model.pointer_y);
            if (window != null && window.window_type != Marco.WindowType.DOCK &&
                window.window_type != Marco.WindowType.DESKTOP) {
                focus_window (window, timestamp);
                if (autoraise_window != window && model.prefs.auto_raise) {
                    queue_autoraise_callback (window);
                }
            } else if (model.prefs.focus_mode == FocusMode.SLOPPY) {
                focus_ancestor_or_top_window (not_this_one, timestamp);
            } else {
                focus_no_window ();
            }
        }

        private void focus_ancestor_or_top_window (Window? not_this_one, int64 timestamp) {
            if (not_this_one != null && model.is_on_current_workspace (not_this_one)) {
                var ancestor = not_this_one.transient_for;
                if (ancestor != null && model.is_managed (ancestor)) {
                    focus_window (ancestor, timestamp);
                    if (model.prefs.focus_mode == FocusMode.CLICK) {
                        model.raise (ancestor);
                    }
                    return;
                }
            }

            var window = model.get_stack_object ().get_default_focus_window (model.current_workspace, null);
            if (window != null) {
                focus_window (window, timestamp);
                if (model.prefs.focus_mode == FocusMode.CLICK) {
                    model.raise (window);
                }
            } else {
                focus_no_window ();
            }
        }

        public void button_press (Window window, bool on_frame, int64 timestamp) {
            if (timestamp != 0) {
                set_user_time (window, timestamp);
            }
            if (on_frame || model.prefs.raise_on_click) {
                model.raise (window);
            }
            if (window.window_type != Marco.WindowType.DOCK) {
                focus_window (window, timestamp);
            } else {
                allow_terminal_deactivation = true;
            }
        }

        /*
         * Marco: display.c event_callback (), EnterNotify and LeaveNotify.
         * window is the window now under the pointer, or null for the root.
         */
        public void pointer_entered (Window? window, int64 timestamp) {
            if (window == entered) {
                return;
            }
            var left = entered;
            entered = window;

            if (left != null && model.is_managed (left) && left.window_type == Marco.WindowType.DOCK &&
                model.focused != left) {
                model.lower (left);
            }

            if (window == null) {
                return;
            }
            switch (model.prefs.focus_mode) {
                case FocusMode.SLOPPY:
                case FocusMode.MOUSE:
                    mouse_mode = true;
                    if (window.window_type != Marco.WindowType.DOCK &&
                        window.window_type != Marco.WindowType.DESKTOP) {
                        focus_window (window, timestamp);
                        if (model.prefs.auto_raise) {
                            queue_autoraise_callback (window);
                        }
                    }
                    if (window.window_type == Marco.WindowType.DESKTOP &&
                        model.prefs.focus_mode == FocusMode.MOUSE && model.focused != null) {
                        focus_no_window ();
                    }
                    break;
                default:
                    break;
            }
            if (window.window_type == Marco.WindowType.DOCK) {
                model.raise (window);
            }
        }

        /* process_event (): any key binding lets a terminal lose focus to new windows. */
        public void binding_ran () {
            allow_terminal_deactivation = true;
        }

        /* A keyboard action (alt-tab, workspace keys) chose the focus. */
        public void leave_mouse_mode () {
            mouse_mode = false;
        }

        private bool raise_with_delay () {
            autoraise_timeout = 0;
            var window = autoraise_window;
            autoraise_window = null;
            if (window == null || !model.is_managed (window)) {
                return GLib.Source.REMOVE;
            }
            if (model.get_stack_object ().get_top () != window) {
                var r = window.has_frame () ? Placement.frame_rect (window) : window.client_rect ();
                double x = model.pointer_x;
                double y = model.pointer_y;
                if (x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height) {
                    model.raise (window);
                }
            }
            return GLib.Source.REMOVE;
        }

        private void queue_autoraise_callback (Window window) {
            remove_autoraise_callback ();
            autoraise_window = window;
            autoraise_timeout = GLib.Timeout.add (int.max (model.prefs.auto_raise_delay, 0),
                                                  raise_with_delay);
        }

        public void remove_autoraise_callback () {
            if (autoraise_timeout != 0) {
                GLib.Source.remove (autoraise_timeout);
                autoraise_timeout = 0;
            }
            autoraise_window = null;
        }

        /* Run a pending auto-raise now (the oracle samples after it fires). */
        public void flush_autoraise () {
            if (autoraise_timeout != 0) {
                GLib.Source.remove (autoraise_timeout);
                raise_with_delay ();
            }
        }

        public void forget (Window window) {
            if (autoraise_window == window) {
                remove_autoraise_callback ();
            }
            if (entered == window) {
                entered = null;
            }
        }

        public void set_demands_attention (Window window) {
            bool obscured = false;
            if (window.workspace != -1 && window.workspace != model.current_workspace) {
                obscured = true;
            } else if (window.has_state (WindowState.MINIMIZED)) {
                obscured = true;
            } else {
                var order = model.get_stack ();
                for (int i = (int) order.length - 1; i >= 0 && order[i] != window; i--) {
                    var other = order[i];
                    if ((other.workspace == -1 || window.workspace == -1 ||
                         other.workspace == window.workspace) && window.frame.overlap (other.frame)) {
                        obscured = true;
                        break;
                    }
                }
            }
            if (obscured) {
                model.set_state (window, WindowState.DEMANDS_ATTENTION, true);
            }
        }
    }
}
