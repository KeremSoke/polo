/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Pointer input on server-side frames, as Marco's ui/frames.c handles it.
 */

namespace Polo.Shell {
    public class FrameInput : GLib.Object {
        private const uint BTN_LEFT = 0x110;
        private const uint BTN_RIGHT = 0x111;
        private const uint BTN_MIDDLE = 0x112;
        private const int DOUBLE_CLICK_DISTANCE = 5;
        private const uint DEFAULT_DOUBLE_CLICK_TIME = 400;

        /* The MetaGrabOp CLICKING_* variants of a maximize press. */
        private enum Clicking {
            PLAIN,
            VERTICAL,
            HORIZONTAL
        }

        private unowned Desktop desktop;
        private Policy.Model model;
        private GLib.Settings? mouse_settings = null;

        private XdgWindow? hovered = null;
        private Backend.Cursor hovered_cursor = Backend.Cursor.DEFAULT;

        private XdgWindow? clicking_window = null;
        private Decor.FrameControl clicking = Decor.FrameControl.NONE;
        private Clicking clicking_variant = Clicking.PLAIN;
        private uint clicking_button = 0;
        private Backend.PointerGrab? clicking_grab = null;
        private Backend.Seat? clicking_seat = null;

        private XdgWindow? last_click_window = null;
        private uint last_click_button = 0;
        private uint last_click_time = 0;
        private double last_click_x = 0;
        private double last_click_y = 0;
        private int click_count = 0;

        public FrameInput (Desktop desktop, Policy.Model model) {
            this.desktop = desktop;
            this.model = model;
            var source = GLib.SettingsSchemaSource.get_default ();
            var schema = source != null ? source.lookup ("org.mate.peripherals-mouse", true) : null;
            if (schema != null && schema.has_key ("double-click")) {
                mouse_settings = new GLib.Settings.full (schema, null, null);
            }
        }

        public bool active {
            get { return clicking_grab != null; }
        }

        public void shutdown () {
            if (clicking_grab != null) {
                clicking_grab.end ();
            }
            hovered = null;
            last_click_window = null;
        }

        private uint double_click_time () {
            return mouse_settings != null ? (uint) mouse_settings.get_int ("double-click") : DEFAULT_DOUBLE_CLICK_TIME;
        }

        private static uint x_button (uint button) {
            switch (button) {
                case BTN_LEFT: return 1;
                case BTN_MIDDLE: return 2;
                case BTN_RIGHT: return 3;
                default: return button;
            }
        }

        private static Decor.FrameControl control_at (XdgWindow xdg, double x, double y) {
            unowned Decor.Frame? frame = xdg.get_decor ();
            if (frame == null) {
                return Decor.FrameControl.NONE;
            }
            int origin_x, origin_y;
            xdg.frame_origin (out origin_x, out origin_y);
            return frame.control_at ((int) Math.floor (x) - origin_x, (int) Math.floor (y) - origin_y,
                                     Decor.FrameState.flags_of (xdg.window));
        }

        private static Backend.Cursor cursor_for (Decor.FrameControl control) {
            switch (control) {
                case Decor.FrameControl.RESIZE_SE: return Backend.Cursor.SE_RESIZE;
                case Decor.FrameControl.RESIZE_S: return Backend.Cursor.S_RESIZE;
                case Decor.FrameControl.RESIZE_SW: return Backend.Cursor.SW_RESIZE;
                case Decor.FrameControl.RESIZE_N: return Backend.Cursor.N_RESIZE;
                case Decor.FrameControl.RESIZE_NE: return Backend.Cursor.NE_RESIZE;
                case Decor.FrameControl.RESIZE_NW: return Backend.Cursor.NW_RESIZE;
                case Decor.FrameControl.RESIZE_W: return Backend.Cursor.W_RESIZE;
                case Decor.FrameControl.RESIZE_E: return Backend.Cursor.E_RESIZE;
                default: return Backend.Cursor.DEFAULT;
            }
        }

        private static bool is_button (Decor.FrameControl control) {
            switch (control) {
                case Decor.FrameControl.MENU:
                case Decor.FrameControl.APPMENU:
                case Decor.FrameControl.MINIMIZE:
                case Decor.FrameControl.MAXIMIZE:
                case Decor.FrameControl.DELETE:
                case Decor.FrameControl.SHADE:
                case Decor.FrameControl.UNSHADE:
                case Decor.FrameControl.ABOVE:
                case Decor.FrameControl.UNABOVE:
                case Decor.FrameControl.STICK:
                case Decor.FrameControl.UNSTICK:
                case Decor.FrameControl.UNMAXIMIZE:
                    return true;
                default:
                    return false;
            }
        }

        private static Marco.ButtonType? button_type (Decor.FrameControl control) {
            switch (control) {
                case Decor.FrameControl.MENU: return Marco.ButtonType.MENU;
                case Decor.FrameControl.APPMENU: return Marco.ButtonType.APPMENU;
                case Decor.FrameControl.MINIMIZE: return Marco.ButtonType.MINIMIZE;
                case Decor.FrameControl.MAXIMIZE: return Marco.ButtonType.MAXIMIZE;
                case Decor.FrameControl.UNMAXIMIZE: return Marco.ButtonType.MAXIMIZE;
                case Decor.FrameControl.SHADE: return Marco.ButtonType.SHADE;
                case Decor.FrameControl.UNSHADE: return Marco.ButtonType.UNSHADE;
                case Decor.FrameControl.ABOVE: return Marco.ButtonType.ABOVE;
                case Decor.FrameControl.UNABOVE: return Marco.ButtonType.UNABOVE;
                case Decor.FrameControl.STICK: return Marco.ButtonType.STICK;
                case Decor.FrameControl.UNSTICK: return Marco.ButtonType.UNSTICK;
                case Decor.FrameControl.DELETE: return Marco.ButtonType.CLOSE;
                default: return null;
            }
        }

        /* Whether control matches the active clicking grab. */
        private bool clicking_matches (Decor.FrameControl control) {
            if (clicking == Decor.FrameControl.MAXIMIZE || clicking == Decor.FrameControl.UNMAXIMIZE) {
                return control == Decor.FrameControl.MAXIMIZE || control == Decor.FrameControl.UNMAXIMIZE;
            }
            return control == clicking;
        }

        /* meta_frames_paint (): the button states from the prelit control and the grab. */
        private void redraw (XdgWindow xdg, Decor.FrameControl prelit) {
            unowned Decor.Frame? frame = xdg.get_decor ();
            if (frame == null) {
                return;
            }
            for (int i = 0; i < Marco.ButtonType.LAST; i++) {
                frame.set_button_state ((Marco.ButtonType) i, Marco.ButtonState.NORMAL);
            }
            Marco.ButtonType? type = button_type (prelit);
            if (type != null) {
                /* Marco shows the app menu pressed for CLICKING_MENU. */
                var pressed_by = prelit == Decor.FrameControl.APPMENU ? Decor.FrameControl.MENU : prelit;
                bool pressed = clicking_window == xdg && clicking == pressed_by;
                frame.set_button_state (type, pressed ? Marco.ButtonState.PRESSED : Marco.ButtonState.PRELIGHT);
            }
            xdg.refresh_frame ();
        }

        /* meta_frames_update_prelit_control () */
        private void update_prelit (Backend.Seat seat, XdgWindow xdg, Decor.FrameControl control) {
            var cursor = cursor_for (control);
            if (hovered != xdg || hovered_cursor != cursor) {
                seat.set_cursor (cursor);
            }
            if (hovered != null && hovered != xdg) {
                redraw (hovered, Decor.FrameControl.NONE);
            }
            hovered = xdg;
            hovered_cursor = cursor;
            redraw (xdg, is_button (control) ? control : Decor.FrameControl.NONE);
        }

        /* The pointer moved over view, with no grab. */
        public void motion (Backend.Seat seat, double x, double y, XdgWindow? frame_owner) {
            if (frame_owner == null) {
                if (hovered != null) {
                    redraw (hovered, Decor.FrameControl.NONE);
                    hovered = null;
                }
                return;
            }
            update_prelit (seat, frame_owner, control_at (frame_owner, x, y));
        }

        public void forget (XdgWindow xdg) {
            if (hovered == xdg) {
                hovered = null;
            }
            if (last_click_window == xdg) {
                last_click_window = null;
            }
            if (clicking_window == xdg && clicking_grab != null) {
                clicking_grab.end ();
            }
        }

        /* GTK's GDK_2BUTTON_PRESS: two nearby presses of the same button. */
        private bool count_click (XdgWindow xdg, Backend.ButtonEvent event) {
            if (click_count == 1 && last_click_window == xdg && last_click_button == event.button &&
                event.time_msec - last_click_time <= double_click_time () &&
                (event.x - last_click_x).abs () <= DOUBLE_CLICK_DISTANCE &&
                (event.y - last_click_y).abs () <= DOUBLE_CLICK_DISTANCE) {
                click_count = 2;
                return true;
            }
            click_count = 1;
            last_click_window = xdg;
            last_click_button = event.button;
            last_click_time = event.time_msec;
            last_click_x = event.x;
            last_click_y = event.y;
            return false;
        }

        /* meta_frames_button_press_event (); returns whether the press was on the frame. */
        public bool press (Backend.Seat seat, XdgWindow xdg, Backend.ButtonEvent event) {
            var window = xdg.window;
            var control = control_at (xdg, event.x, event.y);
            uint button = x_button (event.button);
            bool double_click = count_click (xdg, event);

            model.focus_policy.set_user_time (window, event.time_msec);
            if (button == 1 && control != Decor.FrameControl.MINIMIZE && control != Decor.FrameControl.DELETE &&
                control != Decor.FrameControl.MAXIMIZE) {
                model.focus_policy.focus_window (window, event.time_msec);
            }

            if (control == Decor.FrameControl.CLIENT_AREA) {
                return false;
            }

            if (control == Decor.FrameControl.TITLE && button == 1 && double_click) {
                if (model.prefs.raise_on_click) {
                    model.raise (window);
                }
                titlebar_action (xdg, model.prefs.action_double_click_titlebar, event);
                return true;
            }

            if (desktop.grab_active) {
                return true;
            }

            if ((button == 1 && (control == Decor.FrameControl.MINIMIZE || control == Decor.FrameControl.DELETE ||
                                 control == Decor.FrameControl.SHADE || control == Decor.FrameControl.UNSHADE ||
                                 control == Decor.FrameControl.ABOVE || control == Decor.FrameControl.UNABOVE ||
                                 control == Decor.FrameControl.STICK || control == Decor.FrameControl.UNSTICK ||
                                 control == Decor.FrameControl.MENU)) ||
                control == Decor.FrameControl.MAXIMIZE || control == Decor.FrameControl.UNMAXIMIZE) {
                if (control == Decor.FrameControl.MENU) {
                    /* The menu takes the pointer, so there is no clicking grab to hold. */
                    show_menu_under_button (xdg);
                    return true;
                }
                begin_clicking (seat, xdg, control, event);
            } else if (button == 1 && control.is_resize ()) {
                begin_grab (seat, xdg, resize_op (control), event);
            } else if (control == Decor.FrameControl.TITLE && button == 1) {
                if (Marco.FrameFlags.ALLOWS_MOVE in Decor.FrameState.flags_of (window)) {
                    begin_grab (seat, xdg, Input.GrabOp.MOVING, event);
                }
            } else if (button == 2) {
                titlebar_action (xdg, model.prefs.action_middle_click_titlebar, event);
            } else if (button == 3) {
                titlebar_action (xdg, model.prefs.action_right_click_titlebar, event);
            }
            return true;
        }

        private static Input.GrabOp resize_op (Decor.FrameControl control) {
            switch (control) {
                case Decor.FrameControl.RESIZE_SE: return Input.GrabOp.RESIZING_SE;
                case Decor.FrameControl.RESIZE_S: return Input.GrabOp.RESIZING_S;
                case Decor.FrameControl.RESIZE_SW: return Input.GrabOp.RESIZING_SW;
                case Decor.FrameControl.RESIZE_NE: return Input.GrabOp.RESIZING_NE;
                case Decor.FrameControl.RESIZE_N: return Input.GrabOp.RESIZING_N;
                case Decor.FrameControl.RESIZE_NW: return Input.GrabOp.RESIZING_NW;
                case Decor.FrameControl.RESIZE_E: return Input.GrabOp.RESIZING_E;
                default: return Input.GrabOp.RESIZING_W;
            }
        }

        private void begin_grab (Backend.Seat seat, XdgWindow xdg, Input.GrabOp op, Backend.ButtonEvent event) {
            var grab = seat.start_pointer_grab_for_binding (event.view);
            if (grab == null) {
                return;
            }
            hovered = null;
            desktop.move_resize.begin (seat, xdg.window, op, grab, true, event.button, (int) event.x,
                                       (int) event.y, event.time_msec);
        }

        private void begin_clicking (Backend.Seat seat, XdgWindow xdg, Decor.FrameControl control,
                                     Backend.ButtonEvent event) {
            var grab = seat.start_pointer_grab_for_binding (event.view);
            if (grab == null) {
                return;
            }
            uint button = x_button (event.button);
            clicking_window = xdg;
            clicking = control;
            clicking_button = event.button;
            clicking_variant = Clicking.PLAIN;
            if (control == Decor.FrameControl.MAXIMIZE) {
                clicking_variant = button == 2 ? Clicking.VERTICAL : button == 3 ? Clicking.HORIZONTAL : Clicking.PLAIN;
            }
            clicking_grab = grab;
            clicking_seat = seat;
            hovered = null;
            grab.motion.connect (on_clicking_motion);
            grab.button.connect (on_clicking_button);
            grab.ended.connect (on_clicking_ended);
            redraw (xdg, control);
        }

        /* meta_frames_motion_notify_event () during a CLICKING_* grab. */
        private void on_clicking_motion (Backend.PointerGrab grab, double x, double y, uint time_msec) {
            var xdg = clicking_window;
            var control = control_at (xdg, x, y);
            redraw (xdg, clicking_matches (control) ? control : Decor.FrameControl.NONE);
        }

        /* meta_frames_button_release_event () */
        private void on_clicking_button (Backend.PointerGrab grab, uint button, bool pressed, uint held) {
            if (pressed || button != clicking_button) {
                return;
            }
            var xdg = clicking_window;
            var window = xdg.window;
            var seat = clicking_seat;
            double x, y;
            seat.get_pointer_position (out x, out y);
            var control = control_at (xdg, x, y);
            var op = clicking;
            var variant = clicking_variant;
            grab.end ();

            switch (op) {
                case Decor.FrameControl.MINIMIZE:
                    if (control == Decor.FrameControl.MINIMIZE) {
                        model.minimize (window);
                    }
                    break;
                case Decor.FrameControl.MAXIMIZE:
                    if (control == Decor.FrameControl.MAXIMIZE) {
                        model.focus_policy.focus_window (window, model.last_user_time);
                        if (variant == Clicking.PLAIN) {
                            maximize (window);
                        } else if (variant == Clicking.VERTICAL) {
                            toggle_maximize (window, WindowState.MAXIMIZED_V);
                        } else {
                            toggle_maximize (window, WindowState.MAXIMIZED_H);
                        }
                    }
                    break;
                case Decor.FrameControl.UNMAXIMIZE:
                    if (control == Decor.FrameControl.UNMAXIMIZE) {
                        model.unmaximize (window, WindowState.MAXIMIZED);
                    }
                    break;
                case Decor.FrameControl.DELETE:
                    if (control == Decor.FrameControl.DELETE) {
                        model.close (window);
                    }
                    break;
                case Decor.FrameControl.SHADE:
                    if (control == Decor.FrameControl.SHADE) {
                        model.set_state (window, WindowState.SHADED, true);
                    }
                    break;
                case Decor.FrameControl.UNSHADE:
                    if (control == Decor.FrameControl.UNSHADE) {
                        model.set_state (window, WindowState.SHADED, false);
                    }
                    break;
                case Decor.FrameControl.ABOVE:
                    if (control == Decor.FrameControl.ABOVE) {
                        model.set_state (window, WindowState.ABOVE, true);
                        model.raise (window);
                    }
                    break;
                case Decor.FrameControl.UNABOVE:
                    if (control == Decor.FrameControl.UNABOVE) {
                        model.set_state (window, WindowState.ABOVE, false);
                        model.raise (window);
                    }
                    break;
                case Decor.FrameControl.STICK:
                    if (control == Decor.FrameControl.STICK) {
                        model.set_state (window, WindowState.STICKY, true);
                    }
                    break;
                case Decor.FrameControl.UNSTICK:
                    if (control == Decor.FrameControl.UNSTICK) {
                        model.set_state (window, WindowState.STICKY, false);
                    }
                    break;
                default:
                    break;
            }

            if (xdg.mapped && model.is_managed (window)) {
                update_prelit (seat, xdg, control);
            }
        }

        private void on_clicking_ended (Backend.PointerGrab grab, bool cancelled) {
            grab.motion.disconnect (on_clicking_motion);
            grab.button.disconnect (on_clicking_button);
            grab.ended.disconnect (on_clicking_ended);
            var xdg = clicking_window;
            clicking_window = null;
            clicking = Decor.FrameControl.NONE;
            clicking_grab = null;
            clicking_seat = null;
            if (xdg != null) {
                redraw (xdg, Decor.FrameControl.NONE);
            }
        }

        /* The menu button's bottom-left corner. */
        private void show_menu_under_button (XdgWindow xdg) {
            unowned Decor.Frame? frame = xdg.get_decor ();
            if (frame == null) {
                return;
            }
            int origin_x, origin_y;
            xdg.frame_origin (out origin_x, out origin_y);
            var rect = frame.geometry.menu_rect.visible;
            if (!Policy.WorkspaceLayout.ltr ()) {
                origin_x += rect.width;
            }
            desktop.window_menu.show (xdg.window, origin_x + rect.x, origin_y + rect.y + rect.height);
        }

        /* meta_core_maximize () */
        private void maximize (Window window) {
            if (model.prefs.raise_on_click) {
                model.raise (window);
            }
            model.maximize (window, WindowState.MAXIMIZED);
        }

        /* meta_core_toggle_maximize* () */
        private void toggle_maximize (Window window, WindowState directions) {
            if (model.prefs.raise_on_click) {
                model.raise (window);
            }
            if (window.has_state (directions)) {
                model.unmaximize (window, directions);
            } else {
                model.maximize (window, directions);
            }
        }

        /* meta_frame_titlebar_event () */
        private void titlebar_action (XdgWindow xdg, TitlebarAction action, Backend.ButtonEvent event) {
            var window = xdg.window;
            var flags = Decor.FrameState.flags_of (window);
            switch (action) {
                case TitlebarAction.TOGGLE_SHADE:
                    if (Marco.FrameFlags.ALLOWS_SHADE in flags) {
                        model.set_state (window, WindowState.SHADED, !(Marco.FrameFlags.SHADED in flags));
                    }
                    break;
                case TitlebarAction.TOGGLE_MAXIMIZE:
                    if (Marco.FrameFlags.ALLOWS_MAXIMIZE in flags) {
                        toggle_maximize (window, WindowState.MAXIMIZED);
                    }
                    break;
                case TitlebarAction.TOGGLE_MAXIMIZE_HORIZONTALLY:
                    if (Marco.FrameFlags.ALLOWS_MAXIMIZE in flags) {
                        toggle_maximize (window, WindowState.MAXIMIZED_H);
                    }
                    break;
                case TitlebarAction.TOGGLE_MAXIMIZE_VERTICALLY:
                    if (Marco.FrameFlags.ALLOWS_MAXIMIZE in flags) {
                        toggle_maximize (window, WindowState.MAXIMIZED_V);
                    }
                    break;
                case TitlebarAction.MINIMIZE:
                    if (Marco.FrameFlags.ALLOWS_MINIMIZE in flags) {
                        model.minimize (window);
                    }
                    break;
                case TitlebarAction.LOWER:
                    lower_and_unfocus (window, event.time_msec);
                    break;
                case TitlebarAction.MENU:
                    desktop.window_menu.show (window, (int) event.x, (int) event.y);
                    break;
                default:
                    break;
            }
        }

        /* meta_core_user_lower_and_unfocus () */
        private void lower_and_unfocus (Window window, int64 time) {
            model.lower (window);
            if (model.prefs.raise_on_click) {
                model.mru_to_back (window);
            }
            if (model.focused == window) {
                model.focus_policy.focus_default_window (null, time);
            }
        }
    }
}
