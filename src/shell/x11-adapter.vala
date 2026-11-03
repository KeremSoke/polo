/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * An X window as a Polo.Window. Its surface behaves like an xdg toplevel's, so
 * commits stay with XdgWindow; what goes to the client goes through the XWM.
 */

namespace Polo.Shell {
    public class X11Window : XdgWindow {
        private const int ICCCM_NORMAL_STATE = 1;
        private const int ICCCM_ICONIC_STATE = 3;

        public Xwm.XWindow xwindow { get; construct; }
        private bool sent_iconic = false;
        private bool x_gone = false;
        private const uint32 ALL_DESKTOPS = 0xffffffffU;
        private uint32 user_time_window = 0;
        private uint32 sent_desktop = 0xfffffffeU;
        private string sent_state = "?";
        private int sent_x = int.MIN;
        private int sent_y = int.MIN;
        private bool extents_sent = false;
        private Marco.FrameBorders sent_extents;
        private int inset_left = 0;
        private int inset_right = 0;
        private int inset_top = 0;
        private int inset_bottom = 0;

        public X11Window (Desktop desktop, Backend.Toplevel toplevel, Xwm.XWindow xwindow) {
            Object (toplevel: toplevel, xwindow: xwindow);
            this.desktop = desktop;
        }

        construct {
            xwindow.property_changed.connect (on_property_changed);
            xwindow.pong.connect (on_x_pong);
            xwindow.unmapped.connect (on_x_gone);
            xwindow.destroyed.connect (on_x_gone);
            xwindow.client_message.connect (on_client_message);
            xwindow.focus_in.connect (on_x_focus_in);
            xwindow.configure_requested.connect (on_configure_requested);
            xwindow.set_intercept_configure (true);
            toplevel.destroyed.connect (stop_following_model);
        }

        protected override WindowKind window_kind () {
            return WindowKind.X11;
        }

        /* Xwayland may keep the wl_surface a second longer; Marco unmanages at once. */
        private void on_x_gone () {
            x_gone = true;
            xwindow.unmapped.disconnect (on_x_gone);
            xwindow.destroyed.disconnect (on_x_gone);
            xwindow.client_message.disconnect (on_client_message);
            xwindow.focus_in.disconnect (on_x_focus_in);
            xwindow.configure_requested.disconnect (on_configure_requested);
            xwindow.set_intercept_configure (false);
            if (mapped) {
                stop_following_model ();
                withdraw ();
            }
        }

        /* X clients that want no frame say so in _MOTIF_WM_HINTS (mwm_decorated). */
        protected override bool server_decorated () {
            return true;
        }

        protected override bool shows_content () {
            return !x_gone && toplevel.has_content ();
        }

        /* A client FocusIn requests focus for itself. */
        private void on_x_focus_in () {
            if (mapped && desktop.model.is_showing (window)) {
                desktop.model.focus (window);
            }
        }

        private void on_x_pong () {
            desktop.model.pong (window);
        }

        private void on_property_changed (string atom) {
            switch (atom) {
                case "WM_NAME":
                case "_NET_WM_NAME":
                case "WM_CLIENT_MACHINE":
                    update_title ();
                    break;
                case "WM_CLASS":
                    update_class ();
                    break;
                case "WM_HINTS":
                case "WM_PROTOCOLS":
                    update_wm_hints ();
                    break;
                case "WM_TRANSIENT_FOR":
                    if (mapped) {
                        update_group ();
                        window.transient_parent_is_root_window = xwindow.get_transient_for_root ();
                        desktop.relink_children ();
                    }
                    break;
                case "WM_NORMAL_HINTS":
                    if (mapped) {
                        update_hints ();
                    }
                    break;
                case "_NET_WM_USER_TIME":
                    update_user_time ();
                    break;
                case "_NET_WM_USER_TIME_WINDOW":
                    update_user_time_window ();
                    break;
                case "_NET_WM_STRUT":
                case "_NET_WM_STRUT_PARTIAL":
                case "_GNOME_WM_STRUT_AREA":
                    if (mapped) {
                        update_struts ();
                    }
                    break;
                case "_NET_WM_WINDOW_TYPE":
                    window.type_hint = window_type (first_atom ("_NET_WM_WINDOW_TYPE"));
                    if (mapped) {
                        desktop.model.recalc_window_type (window);
                        desktop.model.recalc_window_features (window);
                    }
                    break;
                case "_MOTIF_WM_HINTS":
                    update_motif_hints ();
                    if (mapped) {
                        desktop.model.recalc_window_features (window);
                        decorations_changed ();
                        sync_from_model ();
                    }
                    break;
                case "_NET_STARTUP_ID":
                    update_startup_id ();
                    break;
                case "_GTK_FRAME_EXTENTS":
                    update_frame_extents ();
                    if (mapped) {
                        desktop.model.move_resize_now (window);
                    }
                    break;
                default:
                    break;
            }
        }

        internal override int client_pid () {
            return xwindow.get_pid ();
        }

        private void update_title () {
            bool modified;
            var title = Policy.X11Props.format_title (xwindow.get_net_wm_name () ?? xwindow.get_wm_name (),
                                                      xwindow.get_machine (), hostname (),
                                                      owner_of (xwindow.get_pid ()), out modified);
            window.title = title;
            if (modified) {
                xwindow.set_visible_name (title);
            }
        }

        private static string hostname () {
            char buffer[256];
            Posix.gethostname (buffer);
            return (string) buffer;
        }

        /* null: the user's own or unknown; see X11Props.format_title (). */
        private static string? owner_of (int pid) {
            if (pid <= 0) {
                return null;
            }
            Posix.Stat info;
            if (Posix.stat ("/proc/%d".printf (pid), out info) != 0 || info.st_uid == Posix.getuid ()) {
                return null;
            }
            if (info.st_uid == 0) {
                return "";
            }
            unowned Posix.Passwd? user = Posix.getpwuid (info.st_uid);
            return user != null ? user.pw_name : "?";
        }

        private void update_class () {
            window.res_class = xwindow.get_res_class ();
            window.app_id = xwindow.get_res_class () ?? xwindow.get_res_name ();
        }

        private void update_motif_hints () {
            uint32 flags, functions, decorations;
            xwindow.get_motif_hints (out flags, out functions, out decorations);
            window.mwm_functions = Policy.X11Props.motif_functions (flags, functions);
            Policy.X11Props.motif_decorations (flags, decorations, out window.mwm_wants_frame,
                                               out window.mwm_border_only);
            window.mwm_decorated = window.mwm_wants_frame;
        }

        private void update_wm_hints () {
            window.accepts_focus = xwindow.get_input () || xwindow.get_supports_take_focus ();
            update_group ();
        }

        private void update_group () {
            uint64 ancestor_group = 0;
            var ancestor = xwindow.get_transient_for ();
            for (int depth = 0; ancestor != null && depth < 64; depth++) {
                ancestor_group = Policy.X11Props.compute_group (0, ancestor.get_window_group (), ancestor.get_id ());
                ancestor = ancestor.get_transient_for ();
            }
            window.group = Policy.X11Props.compute_group (ancestor_group, xwindow.get_window_group (),
                                                          xwindow.get_id ());
        }

        private string? first_atom (string property) {
            var atoms = xwindow.get_atoms (property);
            return atoms.length > 0 ? atoms[0] : null;
        }

        private void update_user_time () {
            var values = user_time_window != 0
                ? desktop.xwm_manager.get_cardinals (user_time_window, "_NET_WM_USER_TIME")
                : xwindow.get_cardinals ("_NET_WM_USER_TIME");
            if (values.length == 1) {
                desktop.model.focus_policy.set_user_time (window, values[0]);
            }
        }

        private void update_user_time_window () {
            var values = xwindow.get_cardinals ("_NET_WM_USER_TIME_WINDOW");
            user_time_window = values.length == 1 ? values[0] : 0;
            if (user_time_window != 0) {
                desktop.xwm_manager.watch_window (user_time_window);
                update_user_time ();
            }
        }

        public void on_watched_property (uint xid, string atom) {
            if (xid == user_time_window && atom == "_NET_WM_USER_TIME") {
                update_user_time ();
            }
        }

        private void update_struts () {
            var screen = Marco.Rect ();
            var outputs = desktop.model.get_outputs ();
            foreach (var output in outputs) {
                Marco.Rect merged;
                screen.union (output.rect, out merged);
                screen = merged;
            }
            window.struts = Policy.X11Props.parse_struts (xwindow.get_cardinals ("_GNOME_WM_STRUT_AREA"),
                                                          xwindow.get_cardinals ("_NET_WM_STRUT_PARTIAL"),
                                                          xwindow.get_cardinals ("_NET_WM_STRUT"), screen, outputs);
            if (mapped) {
                desktop.model.invalidate_work_areas ();
                desktop.model.work_areas_changed ();
            }
        }

        /* Consume the X startup token Polo exported as DESKTOP_STARTUP_ID. */
        private void update_startup_id () {
            var id = xwindow.get_text ("_NET_STARTUP_ID");
            if (id == null) {
                return;
            }
            int64 time;
            bool valid = desktop.activation_tokens.consume (id, desktop.server.get_time_msec (), out time);
            if (valid && !mapped) {
                window.initial_timestamp = time;
            }
        }

        /* _GTK_FRAME_EXTENTS: the client draws its own shadows inside the X window. */
        private void update_frame_extents () {
            var values = xwindow.get_cardinals ("_GTK_FRAME_EXTENTS");
            inset_left = values.length == 4 ? (int) values[0] : 0;
            inset_right = values.length == 4 ? (int) values[1] : 0;
            inset_top = values.length == 4 ? (int) values[2] : 0;
            inset_bottom = values.length == 4 ? (int) values[3] : 0;
            xwindow.set_frame_inset (inset_left, inset_right, inset_top, inset_bottom);
        }

        /* Read initial state once; Polo owns the property afterwards. */
        private void read_initial_state () {
            foreach (unowned string atom in xwindow.get_atoms ("_NET_WM_STATE")) {
                switch (atom) {
                    case "_NET_WM_STATE_SHADED": window.set_state_flags (WindowState.SHADED, true); break;
                    case "_NET_WM_STATE_MAXIMIZED_HORZ": window.set_state_flags (WindowState.MAXIMIZED_H, true); break;
                    case "_NET_WM_STATE_MAXIMIZED_VERT": window.set_state_flags (WindowState.MAXIMIZED_V, true); break;
                    case "_NET_WM_STATE_HIDDEN": window.minimize_after_placement = true; break;
                    case "_NET_WM_STATE_MODAL": window.set_state_flags (WindowState.MODAL, true); break;
                    case "_NET_WM_STATE_SKIP_TASKBAR": window.wm_state_skip_taskbar = true; break;
                    case "_NET_WM_STATE_SKIP_PAGER": window.wm_state_skip_pager = true; break;
                    case "_NET_WM_STATE_FULLSCREEN": window.set_state_flags (WindowState.FULLSCREEN, true); break;
                    case "_NET_WM_STATE_ABOVE": window.set_state_flags (WindowState.ABOVE, true); break;
                    case "_NET_WM_STATE_BELOW": window.set_state_flags (WindowState.BELOW, true); break;
                    case "_NET_WM_STATE_DEMANDS_ATTENTION":
                        window.set_state_flags (WindowState.DEMANDS_ATTENTION, true);
                        break;
                    case "_NET_WM_STATE_STICKY": window.set_state_flags (WindowState.STICKY, true); break;
                    default: break;
                }
            }
            var desktops = xwindow.get_cardinals ("_NET_WM_DESKTOP");
            if (desktops.length == 1) {
                if (desktops[0] == ALL_DESKTOPS) {
                    window.set_state_flags (WindowState.STICKY, true);
                } else {
                    window.initial_workspace = (int) desktops[0];
                }
            }
        }

        private void on_client_message (string type, uint d0, uint d1, uint d2, uint d3, uint d4) {
            if (!mapped) {
                return;
            }
            var model = desktop.model;
            switch (type) {
                case "_NET_CLOSE_WINDOW":
                    model.close (window);
                    break;
                case "_NET_WM_DESKTOP":
                    if (d0 == ALL_DESKTOPS) {
                        model.set_state (window, WindowState.STICKY, true);
                    } else if (d0 < model.n_workspaces) {
                        if (window.has_state (WindowState.STICKY)) {
                            model.set_state (window, WindowState.STICKY, false);
                        }
                        model.move_to_workspace (window, (int) d0);
                    }
                    break;
                case "_NET_WM_STATE":
                    apply_state_atom (d0, atom_name (d1));
                    apply_state_atom (d0, atom_name (d2));
                    break;
                case "WM_CHANGE_STATE":
                    if (d0 == ICCCM_ICONIC_STATE) {
                        model.minimize (window);
                    }
                    break;
                case "_NET_WM_MOVERESIZE":
                    desktop.net_wm_moveresize (window, (int) d0, (int) d1, d2, d3);
                    break;
                case "_NET_MOVERESIZE_WINDOW":
                    uint gravity = d0 & 0xff;
                    uint mask = (d0 & 0xf00) >> 8;
                    bool move = (mask & 3) != 0;
                    bool resize = (mask & 12) != 0;
                    model.configure_request (window, move, (int) d1, (int) d2, resize, (int) d3, (int) d4,
                                             gravity != 0 ? (Marco.Gravity?) gravity : null);
                    break;
                case "_NET_ACTIVE_WINDOW":
                    var source = d0 == 1 ? Policy.ActivationSource.APPLICATION : Policy.ActivationSource.PAGER;
                    if (d0 == 0) {
                        source = Policy.ActivationSource.APPLICATION;
                    }
                    model.activate (window, source, d1 != 0 ? (int64) d1 : desktop.server.get_time_msec ());
                    break;
                case "_NET_WM_FULLSCREEN_MONITORS":
                    int outputs = (int) model.get_outputs ().length;
                    if (d0 < outputs && d1 < outputs && d2 < outputs && d3 < outputs) {
                        window.fullscreen_monitors = { (int) d0, (int) d1, (int) d2, (int) d3 };
                    } else {
                        window.fullscreen_monitors[0] = -1;
                    }
                    if (window.has_state (WindowState.FULLSCREEN)) {
                        model.move_resize_now (window);
                    }
                    break;
                case "_GTK_SHOW_WINDOW_MENU":
                    desktop.window_menu.show (window, (int) d1, (int) d2);
                    break;
                default:
                    break;
            }
        }

        private string? atom_name (uint atom) {
            return atom != 0 ? xwindow.name_of_atom (atom) : null;
        }

        private void apply_state_atom (uint action, string? atom) {
            var model = desktop.model;
            bool raise = model.prefs.raise_on_click;
            switch (atom) {
                case "_NET_WM_STATE_SHADED":
                    bool shade = Policy.X11Props.state_after (action, window.has_state (WindowState.SHADED));
                    model.request_state (window, WindowState.SHADED, shade);
                    break;
                case "_NET_WM_STATE_FULLSCREEN":
                    bool full = Policy.X11Props.state_after (action, window.has_state (WindowState.FULLSCREEN));
                    model.request_state (window, WindowState.FULLSCREEN, full);
                    break;
                case "_NET_WM_STATE_MAXIMIZED_HORZ":
                case "_NET_WM_STATE_MAXIMIZED_VERT":
                    var flag = atom == "_NET_WM_STATE_MAXIMIZED_HORZ" ? WindowState.MAXIMIZED_H
                                                                      : WindowState.MAXIMIZED_V;
                    if (raise) {
                        model.raise (window);
                    }
                    model.request_state (window, flag, Policy.X11Props.state_after (action, window.has_state (flag)));
                    break;
                case "_NET_WM_STATE_MODAL":
                    window.set_state_flags (WindowState.MODAL,
                                            Policy.X11Props.state_after (action, window.has_state (WindowState.MODAL)));
                    model.recalc_window_type (window);
                    model.move_resize_now (window);
                    break;
                case "_NET_WM_STATE_SKIP_PAGER":
                    window.wm_state_skip_pager = Policy.X11Props.state_after (
                        action, window.has_state (WindowState.SKIP_PAGER));
                    model.recalc_window_features (window);
                    break;
                case "_NET_WM_STATE_SKIP_TASKBAR":
                    window.wm_state_skip_taskbar = Policy.X11Props.state_after (
                        action, window.has_state (WindowState.SKIP_TASKBAR));
                    model.recalc_window_features (window);
                    break;
                case "_NET_WM_STATE_ABOVE":
                    model.set_state (window, WindowState.ABOVE,
                                     Policy.X11Props.state_after (action, window.has_state (WindowState.ABOVE)));
                    break;
                case "_NET_WM_STATE_BELOW":
                    model.set_state (window, WindowState.BELOW,
                                     Policy.X11Props.state_after (action, window.has_state (WindowState.BELOW)));
                    break;
                case "_NET_WM_STATE_DEMANDS_ATTENTION":
                    if (Policy.X11Props.state_after (action, window.has_state (WindowState.DEMANDS_ATTENTION))) {
                        model.focus_policy.set_demands_attention (window);
                    } else {
                        window.set_state_flags (WindowState.DEMANDS_ATTENTION, false);
                        model.stack_changed ();
                    }
                    break;
                case "_NET_WM_STATE_STICKY":
                    model.set_state (window, WindowState.STICKY,
                                     Policy.X11Props.state_after (action, window.has_state (WindowState.STICKY)));
                    break;
                default:
                    break;
            }
            sync_net_wm_state ();
        }

        /* value_mask uses X11 CW bits. */
        private void on_configure_requested (uint mask, int x, int y, int width, int height) {
            bool move = (mask & 3) != 0;
            bool resize = (mask & 12) != 0;
            if (mapped && (move || resize)) {
                var current = window.client_rect ();
                desktop.model.configure_request (window, move, (mask & 1) != 0 ? x : current.x,
                                                 (mask & 2) != 0 ? y : current.y, resize,
                                                 (mask & 4) != 0 ? width : current.width,
                                                 (mask & 8) != 0 ? height : current.height);
            }
            /* ICCCM 4.1.5: granted or not, the client hears where it is. */
            send_size (window.frame);
        }

        private void sync_allowed_actions () {
            string[] atoms = {};
            bool minimized = window.has_state (WindowState.MINIMIZED);
            bool shaded = window.has_state (WindowState.SHADED);
            bool maximized = window.has_state (WindowState.MAXIMIZED);
            if (window.has_feature (WindowFeatures.MOVE) && !minimized) {
                atoms += "_NET_WM_ACTION_MOVE";
            }
            if (window.has_feature (WindowFeatures.RESIZE) && !minimized && !shaded && !window.tiled && !maximized) {
                atoms += "_NET_WM_ACTION_RESIZE";
            }
            if (window.has_feature (WindowFeatures.FULLSCREEN) && !minimized && !shaded) {
                atoms += "_NET_WM_ACTION_FULLSCREEN";
            }
            if (window.has_feature (WindowFeatures.MINIMIZE)) {
                atoms += "_NET_WM_ACTION_MINIMIZE";
            }
            if (window.has_feature (WindowFeatures.SHADE) && !minimized) {
                atoms += "_NET_WM_ACTION_SHADE";
            }
            if (window.has_feature (WindowFeatures.MAXIMIZE) && !minimized && !shaded) {
                atoms += "_NET_WM_ACTION_MAXIMIZE_HORZ";
                atoms += "_NET_WM_ACTION_MAXIMIZE_VERT";
            }
            atoms += "_NET_WM_ACTION_CHANGE_DESKTOP";
            if (window.has_feature (WindowFeatures.CLOSE)) {
                atoms += "_NET_WM_ACTION_CLOSE";
            }
            atoms += "_NET_WM_ACTION_ABOVE";
            atoms += "_NET_WM_ACTION_BELOW";
            xwindow.set_allowed_actions (atoms);
        }

        private void sync_from_model () {
            sync_wm_state ();
            if (!mapped) {
                return;
            }
            sync_net_wm_state ();
            sync_allowed_actions ();
            uint32 desktop_id = window.workspace < 0 ? ALL_DESKTOPS : (uint32) window.workspace;
            if (desktop_id != sent_desktop) {
                sent_desktop = desktop_id;
                xwindow.set_net_wm_desktop (desktop_id);
            }
        }

        /* A mapped-but-hidden window is iconic. */
        private void sync_wm_state () {
            if (!mapped) {
                return;
            }
            bool iconic = !desktop.model.is_showing (window);
            if (iconic != sent_iconic) {
                sent_iconic = iconic;
                xwindow.set_wm_state (iconic ? ICCCM_ICONIC_STATE : ICCCM_NORMAL_STATE);
            }
        }

        private void stop_following_model () {
            desktop.model.stack_changed.disconnect (sync_from_model);
            desktop.model.focus_changed.disconnect (sync_from_model);
            toplevel.destroyed.disconnect (stop_following_model);
        }

        private static Marco.WindowType? window_type (string? atom) {
            switch (atom) {
                case "_NET_WM_WINDOW_TYPE_DESKTOP": return Marco.WindowType.DESKTOP;
                case "_NET_WM_WINDOW_TYPE_DOCK": return Marco.WindowType.DOCK;
                case "_NET_WM_WINDOW_TYPE_TOOLBAR": return Marco.WindowType.TOOLBAR;
                case "_NET_WM_WINDOW_TYPE_MENU": return Marco.WindowType.MENU;
                case "_NET_WM_WINDOW_TYPE_UTILITY": return Marco.WindowType.UTILITY;
                case "_NET_WM_WINDOW_TYPE_SPLASH": return Marco.WindowType.SPLASHSCREEN;
                case "_NET_WM_WINDOW_TYPE_DIALOG": return Marco.WindowType.DIALOG;
                case "_NET_WM_WINDOW_TYPE_NORMAL": return Marco.WindowType.NORMAL;
                default: return null;
            }
        }

        protected override void before_map () {
            int x, y, width, height;
            xwindow.get_geometry (out x, out y, out width, out height);
            window.frame.x = x;
            window.frame.y = y;
            window.type_hint = window_type (first_atom ("_NET_WM_WINDOW_TYPE"));
            update_title ();
            update_class ();
            update_wm_hints ();
            window.transient_parent_is_root_window = xwindow.get_transient_for_root ();
            window.initially_iconic = xwindow.get_initially_iconic ();
            read_initial_state ();
            update_motif_hints ();
            update_user_time_window ();
            update_user_time ();
            update_struts ();
            update_startup_id ();
            update_frame_extents ();
            desktop.model.stack_changed.connect (sync_from_model);
            desktop.model.focus_changed.connect (sync_from_model);
        }

        protected override void update_hints () {
            var hints = Marco.SizeHints ();
            int gravity;
            xwindow.get_size_hints (out hints.flags, out hints.min_width, out hints.min_height,
                                    out hints.max_width, out hints.max_height, out hints.width_inc,
                                    out hints.height_inc, out hints.base_width, out hints.base_height,
                                    out gravity);
            hints.win_gravity = (Marco.Gravity) gravity;
            int min_x, min_y, max_x, max_y;
            xwindow.get_aspect (out min_x, out min_y, out max_x, out max_y);
            hints.min_aspect.x = min_x;
            hints.min_aspect.y = min_y;
            hints.max_aspect.x = max_x;
            hints.max_aspect.y = max_y;
            desktop.model.set_size_hints (window, hints);
        }

        /* The X window holds the client area, the frame drawn around it. */
        protected override void send_size (Marco.Rect frame) {
            unowned Marco.FrameBorders b = window.borders;
            send_configure (frame.x + b.left - inset_left, frame.y + b.top - inset_top,
                            frame.width - b.left - b.right + inset_left + inset_right,
                            frame.height - b.top - b.bottom + inset_top + inset_bottom);
        }

        public override void configure (Marco.Rect frame) {
            base.configure (frame);
            update_net_frame_extents ();
        }

        private void update_net_frame_extents () {
            unowned Marco.FrameBorders b = window.borders;
            if (extents_sent && sent_extents.left == b.left && sent_extents.right == b.right &&
                sent_extents.top == b.top && sent_extents.bottom == b.bottom) {
                return;
            }
            extents_sent = true;
            sent_extents = b;
            xwindow.set_frame_extents (b.left, b.right, b.top, b.bottom);
        }

        /* get_geometry () lags behind what X has been asked for. */
        private void send_configure (int x, int y, int width, int height) {
            sent_x = x;
            sent_y = y;
            xwindow.configure (x, y, width, height);
        }

        protected override void moved (int x, int y) {
            int old_x, old_y, width, height;
            xwindow.get_geometry (out old_x, out old_y, out width, out height);
            x += window.borders.left - inset_left;
            y += window.borders.top - inset_top;
            if (sent_x != x || sent_y != y) {
                send_configure (x, y, width, height);
            }
        }

        private void sync_net_wm_state () {
            string[] atoms = {};
            if (window.has_state (WindowState.SHADED)) {
                atoms += "_NET_WM_STATE_SHADED";
            }
            if (window.has_state (WindowState.MODAL)) {
                atoms += "_NET_WM_STATE_MODAL";
            }
            if (window.has_state (WindowState.SKIP_PAGER)) {
                atoms += "_NET_WM_STATE_SKIP_PAGER";
            }
            if (window.has_state (WindowState.SKIP_TASKBAR)) {
                atoms += "_NET_WM_STATE_SKIP_TASKBAR";
            }
            if (window.has_state (WindowState.MAXIMIZED_H)) {
                atoms += "_NET_WM_STATE_MAXIMIZED_HORZ";
            }
            if (window.has_state (WindowState.MAXIMIZED_V)) {
                atoms += "_NET_WM_STATE_MAXIMIZED_VERT";
            }
            if (window.has_state (WindowState.FULLSCREEN)) {
                atoms += "_NET_WM_STATE_FULLSCREEN";
            }
            if (!desktop.model.showing_on_its_workspace (window) || window.has_state (WindowState.SHADED)) {
                atoms += "_NET_WM_STATE_HIDDEN";
            }
            if (window.has_state (WindowState.ABOVE)) {
                atoms += "_NET_WM_STATE_ABOVE";
            }
            if (window.has_state (WindowState.BELOW)) {
                atoms += "_NET_WM_STATE_BELOW";
            }
            if (window.has_state (WindowState.DEMANDS_ATTENTION)) {
                atoms += "_NET_WM_STATE_DEMANDS_ATTENTION";
            }
            if (window.has_state (WindowState.STICKY)) {
                atoms += "_NET_WM_STATE_STICKY";
            }
            if (desktop.model.focused == window || window.window_type == Marco.WindowType.DOCK ||
                window.window_type == Marco.WindowType.SPLASHSCREEN) {
                atoms += "_NET_WM_STATE_FOCUSED";
            }
            string joined = string.joinv (" ", atoms);
            if (joined != sent_state) {
                sent_state = joined;
                xwindow.set_net_wm_state (atoms);
            }
        }

        public override void set_maximized (bool maximized) {
            sync_net_wm_state ();
        }

        public override void set_fullscreen (bool fullscreen) {
            sync_net_wm_state ();
        }

        public override void set_tiled (WindowState edges) {
        }

        public override void set_activated (bool activated) {
        }

        /* A client without _NET_WM_PING is treated as responsive. */
        public override void ping () {
            if (xwindow.get_supports_ping ()) {
                xwindow.send_ping ((uint32) desktop.server.get_time_msec ());
            } else {
                desktop.model.pong (window);
            }
        }
    }

    /* No Polo.Window: Marco does not manage these. They sit at the X position. */
    public class OverrideRedirectWindow : GLib.Object {
        private unowned Desktop desktop;
        private Backend.Toplevel toplevel;
        private Xwm.XWindow xwindow;
        private bool mapped = false;
        private bool gone = false;

        public OverrideRedirectWindow (Desktop desktop, Backend.Toplevel toplevel, Xwm.XWindow xwindow) {
            this.desktop = desktop;
            this.toplevel = toplevel;
            this.xwindow = xwindow;
            toplevel.committed.connect (on_committed);
            toplevel.destroyed.connect (on_destroyed);
            xwindow.geometry_changed.connect (place);
            xwindow.unmapped.connect (hide);
            xwindow.destroyed.connect (hide);
        }

        /* Xwayland may keep the wl_surface a second longer. */
        private void hide () {
            gone = true;
            xwindow.unmapped.disconnect (hide);
            xwindow.destroyed.disconnect (hide);
            toplevel.get_view ().move_to_layer (null);
        }

        private void on_committed () {
            if (gone || !toplevel.has_content ()) {
                return;
            }
            if (!mapped) {
                mapped = true;
                place ();
                toplevel.get_view ().move_to_layer (
                    desktop.server.get_layer (Backend.LayerKind.OVERRIDE_REDIRECT));
                toplevel.map ();
            }
        }

        private void place () {
            int x, y, width, height;
            xwindow.get_geometry (out x, out y, out width, out height);
            toplevel.get_view ().set_position (x, y);
        }

        private void on_destroyed () {
            xwindow.geometry_changed.disconnect (place);
            toplevel.committed.disconnect (on_committed);
            toplevel.destroyed.disconnect (on_destroyed);
            desktop.override_redirect_gone (toplevel);
        }
    }
}
