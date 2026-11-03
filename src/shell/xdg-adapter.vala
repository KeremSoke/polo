/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * One xdg_toplevel as a Polo.Window. Decisions belong to
 * Polo.Policy.Model; this adapter carries them out over xdg-shell, where
 * the client resizes asynchronously, and turns requests into model calls.
 */

namespace Polo.Shell {
    public class XdgWindow : GLib.Object, WindowAdapter {
        public Backend.Toplevel toplevel { get; construct; }
        public Window window { get; private set; }
        public bool mapped { get; private set; default = false; }

        protected unowned Desktop desktop;

        /* Window geometry offset inside the surface (CSD shadows). */
        private int geometry_x;
        private int geometry_y;

        /* A frame the model asked for whose size the client has not shown yet. */
        private bool pending = false;
        private Marco.Rect pending_frame;
        private const uint CONFIGURE_TIMEOUT_MS = 50;
        private bool configure_in_flight = false;
        private uint configure_timeout = 0;
        private int width_at_send;
        private int height_at_send;
        private int sent_width;
        private int sent_height;
        /* The frame size the client last drew. The model may already have
         * stored its target in window.frame when it calls configure (). */
        private int drawn_width = 0;
        private int drawn_height = 0;

        private Decor.Frame? decor = null;

        private bool mapped_hints = false;
        private int last_min_w;
        private int last_min_h;
        private int last_max_w;
        private int last_max_h;

        public XdgWindow (Desktop desktop, Backend.Toplevel toplevel) {
            Object (toplevel: toplevel);
            this.desktop = desktop;
        }

        /* xdg_toplevel.set_parent, kept across unmaps. */
        public unowned Backend.Toplevel? parent_toplevel { get; private set; default = null; }

        protected virtual WindowKind window_kind () {
            return WindowKind.XDG;
        }

        internal virtual int client_pid () {
            return toplevel.get_pid ();
        }

        /* The window's data as the protocol knows it, before the model maps it. */
        protected virtual void before_map () {
        }

        /* Ask the client for this frame's size. */
        protected virtual void send_size (Marco.Rect frame) {
            toplevel.configure_size (client_width (frame), client_height (frame));
        }

        /* The frame went to x, y. */
        protected virtual void moved (int x, int y) {
        }

        private void new_window () {
            if (window != null) {
                window.notify["resizing"].disconnect (on_resizing_changed);
            }
            window = new Window (window_kind (), this);
            window.notify["resizing"].connect (on_resizing_changed);
            window.mwm_decorated = server_decorated ();
            window.group = (uint64) (ulong) (void*) toplevel.get_client ();
            window.title = toplevel.title;
            window.app_id = toplevel.app_id;
        }

        construct {
            new_window ();

            toplevel.notify["title"].connect (on_title_changed);
            toplevel.notify["app-id"].connect (on_app_id_changed);
            toplevel.committed.connect (on_committed);
            toplevel.request_move.connect (on_request_move);
            toplevel.request_resize.connect (on_request_resize);
            toplevel.request_maximize.connect (on_request_maximize);
            toplevel.request_fullscreen.connect (on_request_fullscreen);
            toplevel.request_minimize.connect (on_request_minimize);
            toplevel.parent_changed.connect (on_parent_changed);
            toplevel.modal_hint_changed.connect (on_modal_hint_changed);
            toplevel.request_window_menu.connect (on_request_window_menu);
            toplevel.ping_timeout.connect (on_ping_timeout);
            toplevel.pong.connect (on_pong);
            toplevel.destroyed.connect (on_destroyed);
            toplevel.decoration_changed.connect (on_decoration_changed);
        }

        private unowned LogScope log () {
            return Logging.scope (LogDomain.POLICY);
        }

        private void on_title_changed () {
            window.title = toplevel.title;
            refresh_frame ();
        }

        /* ---- Server-side frame ------------------------------------------- */

        private bool server_decorated () {
            return window_kind () == WindowKind.XDG && toplevel.get_server_decorated ();
        }

        private void on_decoration_changed () {
            bool decorated = server_decorated ();
            if (window.mwm_decorated == decorated) {
                return;
            }
            window.mwm_decorated = decorated;
            desktop.model.recalc_window_features (window);
            decorations_changed ();
        }

        /* Refreshes this window after a theme, font, or decoration change. */
        internal void decorations_changed () {
            if (update_borders () && mapped) {
                configure (window.frame);
            }
            refresh_frame ();
            desktop.frames_changed ();
        }

        public void calc_borders () {
            update_borders ();
        }

        /* Updates Marco's visible frame borders; returns true if they changed. */
        private bool update_borders () {
            var old = window.borders;
            window.borders = Marco.FrameBorders ();
            unowned Marco.Theme? theme = desktop.decorations.get_theme ();
            Marco.FrameType? type = Decor.FrameState.type_of (window);
            var flags = Decor.FrameState.flags_of (window);
            if (!window.has_frame () || !server_decorated () || theme == null || type == null ||
                window.has_state (WindowState.FULLSCREEN) || theme.get_frame_style (type, flags) == null) {
                if (decor != null) {
                    decor.clear ();
                    decor.ready.disconnect (desktop.frames_changed);
                    decor = null;
                }
                return !equal_borders (old, window.borders);
            }
            int text_height = theme.text_height (type, flags, desktop.decorations.system_font,
                                                 desktop.decorations.titlebar_font, Decor.Decorations.DPI);
            Marco.ThemeBorders borders;
            theme.get_frame_borders (type, text_height, flags, out borders);
            window.borders = Marco.FrameBorders () {
                left = borders.visible.left, right = borders.visible.right,
                top = borders.visible.top, bottom = borders.visible.bottom
            };
            if (decor == null) {
                decor = new Decor.Frame (desktop.server, desktop.decorations);
                decor.ready.connect (desktop.frames_changed);
            }
            return !equal_borders (old, window.borders);
        }

        private static bool equal_borders (Marco.FrameBorders a, Marco.FrameBorders b) {
            return a.left == b.left && a.right == b.right && a.top == b.top && a.bottom == b.bottom;
        }

        /* Redraws and positions the frame when its visible state changes. */
        internal void refresh_frame () {
            if (decor == null || !mapped) {
                return;
            }
            Marco.FrameType? type = Decor.FrameState.type_of (window);
            if (type == null) {
                return;
            }
            decor.update (type, Decor.FrameState.flags_of (window),
                          window.frame.width - window.borders.left - window.borders.right,
                          window.frame.height - window.borders.top - window.borders.bottom, window.title);
            place_frame ();
        }

        private void place_frame () {
            if (decor == null) {
                return;
            }
            int x = window.frame.x - decor.geometry.borders.invisible.left;
            int y = window.frame.y - decor.geometry.borders.invisible.top;
            for (int i = 0; i < Decor.Frame.Edge.COUNT; i++) {
                Marco.Rect rect;
                unowned Backend.PixelBuffer? edge = decor.get_edge ((Decor.Frame.Edge) i, out rect);
                unowned Backend.View? view = edge != null ? edge.get_view () : null;
                if (view != null) {
                    view.set_position (x + rect.x, y + rect.y);
                }
            }
        }

        /* Puts frame views below the window on layer, or removes them from all layers. */
        internal void stack_frame (GLib.GenericArray<Backend.View> views, Backend.Layer? layer) {
            if (decor == null) {
                return;
            }
            for (int i = 0; i < Decor.Frame.Edge.COUNT; i++) {
                Marco.Rect rect;
                unowned Backend.PixelBuffer? edge = decor.get_edge ((Decor.Frame.Edge) i, out rect);
                unowned Backend.View? view = edge != null ? edge.get_view () : null;
                if (view != null) {
                    view.move_to_layer (layer);
                    if (layer != null) {
                        views.add (view);
                    }
                }
            }
        }

        private void on_app_id_changed () {
            window.app_id = toplevel.app_id;
        }

        /* ---- WindowAdapter ---------------------------------------------- */

        private void show_at (int x, int y) {
            window.frame.x = x;
            window.frame.y = y;
            toplevel.get_view ().set_position (x + window.borders.left - geometry_x,
                                               y + window.borders.top - geometry_y);
            place_frame ();
            moved (x, y);
            window.geometry_changed ();
        }

        private int client_width (Marco.Rect frame) {
            return frame.width - window.borders.left - window.borders.right;
        }

        private int client_height (Marco.Rect frame) {
            return frame.height - window.borders.top - window.borders.bottom;
        }

        public virtual void configure (Marco.Rect frame) {
            update_borders ();
            pending = true;
            pending_frame = frame;
            if (configure_in_flight) {
                return;
            }
            if (frame.width == drawn_width && frame.height == drawn_height) {
                /* A maximized state without a size lets the client pick one, and
                 * libweston disconnects it for not being the size it never got. */
                if (window.has_state (WindowState.MAXIMIZED_H) || window.has_state (WindowState.MAXIMIZED_V) ||
                    window.has_state (WindowState.FULLSCREEN)) {
                    send_size (frame);
                }
                pending = false;
                show_at (frame.x, frame.y);
                return;
            }
            configure_in_flight = true;
            width_at_send = drawn_width;
            height_at_send = drawn_height;
            /* A client may round the size (terminals keep whole cells) to what it already shows, and not commit. */
            configure_timeout = GLib.Timeout.add (CONFIGURE_TIMEOUT_MS, on_configure_timeout);
            sent_width = frame.width;
            sent_height = frame.height;
            send_size (frame);
        }

        private void clear_configure_in_flight () {
            configure_in_flight = false;
            if (configure_timeout != 0) {
                GLib.Source.remove (configure_timeout);
                configure_timeout = 0;
            }
        }

        private bool on_configure_timeout () {
            configure_timeout = 0;
            configure_in_flight = false;
            if (pending && (pending_frame.width != sent_width || pending_frame.height != sent_height)) {
                configure (pending_frame);
            }
            return GLib.Source.REMOVE;
        }

        public virtual void set_activated (bool activated) {
            toplevel.set_activated (activated);
        }

        public virtual void set_maximized (bool maximized) {
            toplevel.set_maximized (maximized);
        }

        public virtual void set_fullscreen (bool fullscreen) {
            toplevel.set_fullscreen (fullscreen);
        }

        public virtual void set_tiled (WindowState edges) {
            Backend.Edges tiled = Backend.Edges.NONE;
            if (WindowState.TILED_LEFT in edges) {
                tiled |= Backend.Edges.LEFT;
            }
            if (WindowState.TILED_RIGHT in edges) {
                tiled |= Backend.Edges.RIGHT;
            }
            if (WindowState.TILED_TOP in edges) {
                tiled |= Backend.Edges.TOP;
            }
            if (WindowState.TILED_BOTTOM in edges) {
                tiled |= Backend.Edges.BOTTOM;
            }
            toplevel.set_tiled (tiled);
        }

        public virtual void take_focus () {
            desktop.set_keyboard_focus (toplevel.get_view ());
        }

        public virtual void close () {
            toplevel.close ();
        }

        public virtual void ping () {
            toplevel.ping ();
        }

        /* ---- Commits ---------------------------------------------------- */

        /* False once the client's window is gone though its surface lingers. */
        protected virtual bool shows_content () {
            return toplevel.has_content ();
        }

        private void on_committed () {
            if (!shows_content ()) {
                if (mapped) {
                    withdraw ();
                }
                return;
            }

            int x, y, width, height;
            toplevel.get_window_geometry (out x, out y, out width, out height);
            geometry_x = x;
            geometry_y = y;
            int frame_width = width + window.borders.left + window.borders.right;
            int frame_height = height + window.borders.top + window.borders.bottom;

            drawn_width = frame_width;
            drawn_height = frame_height;
            if (!mapped) {
                window.frame.width = frame_width;
                window.frame.height = frame_height;
                before_map ();
                update_hints ();
                mapped = true;
                /* The model places the window (through configure ()). */
                desktop.window_ready (this);
                toplevel.get_view ().move_to_layer (
                    desktop.server.get_layer (Backend.LayerKind.NORMAL));
                toplevel.map ();
                log ().debug ("mapped %s at %d,%d %dx%d", window.describe (),
                              window.frame.x, window.frame.y, width, height);
                desktop.model.stack_changed ();
                return;
            }

            int new_x = window.frame.x;
            int new_y = window.frame.y;
            if (window.resizing && (frame_width != window.frame.width ||
                                    frame_height != window.frame.height)) {
                var anchored = window.frame;
                Marco.Rect.resize_with_gravity (window.frame, ref anchored, window.resize_gravity,
                                                frame_width, frame_height);
                new_x = anchored.x;
                new_y = anchored.y;
            }

            if (pending && frame_width == pending_frame.width &&
                frame_height == pending_frame.height) {
                new_x = pending_frame.x;
                new_y = pending_frame.y;
                pending = false;
            }

            window.frame.width = frame_width;
            window.frame.height = frame_height;
            update_hints ();
            show_at (new_x, new_y);
            refresh_frame ();
            bool answered = frame_width == sent_width && frame_height == sent_height;
            if (configure_in_flight && (answered || frame_width != width_at_send ||
                                        frame_height != height_at_send)) {
                clear_configure_in_flight ();
                if (pending && !answered && pending_frame.width == sent_width &&
                    pending_frame.height == sent_height) {
                    pending = false;
                }
                if (pending) {
                    configure (pending_frame);
                }
            }
        }

        /* xdg_toplevel min/max size as size hints; 0 leaves an axis unbounded. */
        protected virtual void update_hints () {
            int min_w, min_h, max_w, max_h;
            toplevel.get_min_size (out min_w, out min_h);
            toplevel.get_max_size (out max_w, out max_h);
            if (mapped_hints && min_w == last_min_w && min_h == last_min_h &&
                max_w == last_max_w && max_h == last_max_h) {
                return;
            }
            mapped_hints = true;
            last_min_w = min_w;
            last_min_h = min_h;
            last_max_w = max_w;
            last_max_h = max_h;
            var hints = Marco.SizeHints ();
            if (min_w > 0 || min_h > 0) {
                hints.flags |= Marco.SizeHintFlags.P_MIN_SIZE;
                hints.min_width = min_w;
                hints.min_height = min_h;
            }
            if (max_w > 0 || max_h > 0) {
                hints.flags |= Marco.SizeHintFlags.P_MAX_SIZE;
                hints.max_width = max_w > 0 ? max_w : int.MAX;
                hints.max_height = max_h > 0 ? max_h : int.MAX;
            }
            desktop.model.set_size_hints (window, hints);
        }

        /* ---- Interactive move and resize --------------------------------- */

        /* _NET_WM_MOVERESIZE's counterpart: the client asks with the serial of its press. */
        private void on_request_move (Backend.Seat seat, uint serial) {
            if (!window.has_feature (WindowFeatures.MOVE)) {
                return;
            }
            var grab = seat.start_pointer_grab (serial, toplevel.get_view ());
            if (grab == null) {
                return;
            }
            double x, y;
            seat.get_pointer_position (out x, out y);
            desktop.move_resize.begin (seat, window, Input.GrabOp.MOVING, grab, false, 0,
                                       (int) x, (int) y, desktop.model.last_user_time);
        }

        private static Input.GrabOp op_from_edges (Backend.Edges edges) {
            bool left = Backend.Edges.LEFT in edges;
            bool right = Backend.Edges.RIGHT in edges;
            bool top = Backend.Edges.TOP in edges;
            bool bottom = Backend.Edges.BOTTOM in edges;
            if (top && left) {
                return Input.GrabOp.RESIZING_NW;
            } else if (top && right) {
                return Input.GrabOp.RESIZING_NE;
            } else if (bottom && left) {
                return Input.GrabOp.RESIZING_SW;
            } else if (bottom && right) {
                return Input.GrabOp.RESIZING_SE;
            } else if (top) {
                return Input.GrabOp.RESIZING_N;
            } else if (bottom) {
                return Input.GrabOp.RESIZING_S;
            } else if (left) {
                return Input.GrabOp.RESIZING_W;
            } else if (right) {
                return Input.GrabOp.RESIZING_E;
            }
            return Input.GrabOp.NONE;
        }

        private void on_request_resize (Backend.Seat seat, uint serial, uint edges) {
            var op = op_from_edges ((Backend.Edges) edges);
            if (op == Input.GrabOp.NONE || !window.has_feature (WindowFeatures.RESIZE)) {
                return;
            }
            var grab = seat.start_pointer_grab (serial, toplevel.get_view ());
            if (grab == null) {
                return;
            }
            double x, y;
            seat.get_pointer_position (out x, out y);
            desktop.move_resize.begin (seat, window, op, grab, false, 0, (int) x, (int) y,
                                       desktop.model.last_user_time);
        }

        private void on_resizing_changed () {
            toplevel.set_resizing (window.resizing);
        }

        /* ---- Requests --------------------------------------------------- */

        private void on_request_maximize (bool maximized) {
            desktop.model.request_state (window, WindowState.MAXIMIZED, maximized);
        }

        private void on_request_fullscreen (bool fullscreen) {
            desktop.model.request_state (window, WindowState.FULLSCREEN, fullscreen);
        }

        /* Marco's transient_for, from xdg_toplevel.set_parent. */
        private void on_parent_changed (Backend.Toplevel? parent) {
            parent_toplevel = parent;
            desktop.model.set_transient_for (window, desktop.resolve_parent (this));
        }

        private void on_modal_hint_changed (bool modal) {
            window.set_state_flags (WindowState.MODAL, modal);
            desktop.model.recalc_window_type (window);
        }

        private void on_request_minimize () {
            /* Task lists restore it (zwlr_foreign_toplevel_manager_v1). */
            desktop.model.minimize (window);
        }

        /* The _GTK_SHOW_WINDOW_MENU message. */
        private void on_request_window_menu (Backend.Seat seat, double x, double y) {
            if (desktop.model.prefs.raise_on_click) {
                desktop.model.raise (window);
            }
            desktop.window_menu.show (window, (int) x, (int) y);
        }

        private void on_ping_timeout () {
            log ().debug ("libweston ping timeout for %s", window.describe ());
        }

        private void on_pong () {
            desktop.model.pong (window);
        }

        /*
         * A null buffer unmapped the toplevel: Marco unmanages a withdrawn
         * window, and the next map is a new window.
         */
        protected void withdraw () {
            mapped = false;
            pending = false;
            clear_configure_in_flight ();
            drawn_width = 0;
            drawn_height = 0;
            mapped_hints = false;
            toplevel.get_view ().move_to_layer (null);
            if (decor != null) {
                decor.clear ();
                decor.ready.disconnect (desktop.frames_changed);
                decor = null;
            }
            var old = window;
            new_window ();
            old.unmanaged ();
            desktop.window_withdrawn (this, old);
        }

        private void on_destroyed () {
            /* The desktop drops its reference below; stay alive until
             * this handler returns. */
            var self = this;

            mapped = false;
            window.unmanaged ();
            desktop.window_destroyed (self);
        }
    }
}
