/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The desktop cube, after Wayfire's cube plugin: the workspaces of the
 * current row as the sides of a prism that turns. Wayfire renders each
 * workspace into a texture and draws the sides with its own GL program;
 * here each side is built from mirrors (Backend.View.mirror) of what the
 * output shows on that workspace (wallpaper, background and bottom layer
 * surfaces, the workspace's windows, top layer surfaces), every view of
 * them bent onto the side by a G1 grid from Fx.Cube. Sides are stacked
 * back to front in the OVERLAY layer over a backdrop, each under its
 * lighting shade. Policy.CubeAnimation holds the motion; frames follow the
 * server's repaint clock.
 */

namespace Polo.Shell {
    public class Cube : GLib.Object {
        public const string BINDING_OPEN = "polo-cube";
        public const string BINDING_LEFT = "polo-cube-rotate-left";
        public const string BINDING_RIGHT = "polo-cube-rotate-right";
        private const string GROUP = "polo-cube";
        private const string KEYS_SCHEMA = "org.mate.polo.keybindings";
        private const uint LAYER_BACKGROUND = 0;
        private const uint LAYER_BOTTOM = 1;
        private const uint LAYER_TOP = 2;
        private const uint KEY_ESC = 1;
        private const uint KEY_ENTER = 28;
        private const uint KEY_SPACE = 57;
        private const uint KEY_KPENTER = 96;
        private const uint KEY_LEFT = 105;
        private const uint KEY_RIGHT = 106;

        private class Piece {
            public Backend.View view;
            public double x;
            public double y;
            public double width;
            public double height;
            public double clip_x;
            public double clip_y;
            public double clip_width;
            public double clip_height;
            public Fx.Grid? grid = null;
            public uint cols;
            public uint rows;
            public bool drawn = true;
        }

        private class Side {
            public int workspace;
            public GLib.GenericArray<Backend.View> mirrors = new GLib.GenericArray<Backend.View> ();
            public GLib.GenericArray<Piece> pieces = new GLib.GenericArray<Piece> ();
            public Backend.PixelBuffer? shade = null;
            public Fx.Grid? shade_grid = null;
            public uint shade_cols;
            public uint shade_rows;
            public int shade_width;
            public int shade_height;
            public bool shown = false;
        }

        private class BackgroundFace {
            public Backend.PixelBuffer buffer;
            public ulong ready = 0;
        }

        private class Screen {
            public Policy.Output output;
            public GLib.GenericArray<Side> sides = new GLib.GenericArray<Side> ();
            public Backend.PixelBuffer? backdrop = null;
            public ulong backdrop_ready = 0;
            public double backdrop_zoom = 1.0;
            public bool sky = false;
            public bool sky_mirror = false;
            public Fx.Grid? sky_grid = null;
            public GLib.GenericArray<BackgroundFace> faces = new GLib.GenericArray<BackgroundFace> ();
            public int[] order = {};
            public bool stacked = false;
            public GLib.GenericArray<Backend.View> carried = new GLib.GenericArray<Backend.View> ();
        }

        private class State {
            public Backend.Seat? seat;
            public Backend.PointerGrab? grab;
            public int[] row;
            public int start;
            public bool model_switch = false;
            public bool interactive = false;
            public bool cancelled = false;
            public uint drag_button = 0;
            public uint held_button = 0;
            public Window? carried;
            public GLib.GenericArray<Screen> screens = new GLib.GenericArray<Screen> ();
            public double last_ms = -1;
        }

        private Backend.Server server;
        private Policy.Model model;
        private Desktop desktop;
        private Input.Bindings bindings;
        private GLib.Settings? keys = null;
        private Input.ButtonAccelerator drag_binding;
        private Policy.CubeAnimation animation;
        private State? state = null;
        private GLib.HashTable<string, string> failed_images =
            new GLib.HashTable<string, string> (str_hash, str_equal);

        public bool active { get { return state != null; } }
        public bool grabbing { get { return state != null && state.grab != null; } }
        public bool animating { get; private set; default = false; }

        public Cube (Backend.Server server, Policy.Model model, Desktop desktop, Input.Bindings bindings) {
            this.server = server;
            this.model = model;
            this.desktop = desktop;
            this.bindings = bindings;
            animation = new Policy.CubeAnimation (350);
            var source = GLib.SettingsSchemaSource.get_default ();
            var schema = source != null ? source.lookup (KEYS_SCHEMA, true) : null;
            if (schema != null && schema.has_key ("cube")) {
                keys = new GLib.Settings.full (schema, null, null);
                keys.changed.connect (on_keys_changed);
            }
            bindings.register (BINDING_OPEN, (binding, seat, event) => toggle (seat));
            bindings.register (BINDING_LEFT, (binding, seat, event) => turn (seat, -1));
            bindings.register (BINDING_RIGHT, (binding, seat, event) => turn (seat, 1));
            model.workspace_switching.connect (on_workspace_switching);
            bind_keys ();
            bind_drag ();
        }

        public void shutdown () {
            if (state != null) {
                end ();
            }
            if (keys != null) {
                keys.changed.disconnect (on_keys_changed);
            }
            bindings.unregister (BINDING_OPEN);
            bindings.unregister (BINDING_LEFT);
            bindings.unregister (BINDING_RIGHT);
            model.workspace_switching.disconnect (on_workspace_switching);
            bindings.set_custom (GROUP, new GLib.GenericArray<Input.Binding> ());
        }

        private void on_keys_changed (string key) {
            if (key == "cube" || key.has_prefix ("cube-rotate-")) {
                bind_keys ();
            } else if (key == "cube-drag") {
                bind_drag ();
            }
        }

        private void bind_keys () {
            var custom = new GLib.GenericArray<Input.Binding> ();
            add_key (custom, "cube", BINDING_OPEN);
            add_key (custom, "cube-rotate-left", BINDING_LEFT);
            add_key (custom, "cube-rotate-right", BINDING_RIGHT);
            bindings.set_custom (GROUP, custom);
        }

        private void bind_drag () {
            drag_binding = Input.ButtonAccelerator ();
            if (keys != null) {
                Input.Accelerators.parse_button (keys.get_string ("cube-drag"), out drag_binding);
            }
        }

        private void add_key (GLib.GenericArray<Input.Binding> custom, string key, string name) {
            if (keys == null) {
                return;
            }
            var accelerator = Input.Accelerator ();
            bool parsed = Input.Accelerators.parse (keys.get_string (key), out accelerator);
            if (parsed && !accelerator.is_disabled ()) {
                custom.add (new Input.Binding () {
                    name = name, kind = Input.BindingKind.GLOBAL, accelerator = accelerator
                });
            }
        }

        /* ---- Settings -------------------------------------------------- */

        private GLib.Settings? effects { get { return desktop.effects.settings; } }

        private bool has (string key) {
            return effects != null && effects.settings_schema.has_key (key);
        }

        private string text (string key, string fallback) {
            return has (key) ? effects.get_string (key) : fallback;
        }

        private uint number (string key, uint fallback) {
            return has (key) ? effects.get_uint (key) : fallback;
        }

        private double real (string key, double fallback) {
            return has (key) ? effects.get_double (key) : fallback;
        }

        private bool flag (string key, bool fallback) {
            return has (key) ? effects.get_boolean (key) : fallback;
        }

        public bool available () {
            return desktop.effects.permits ("cube", true);
        }

        private Fx.CubeDeform deform () {
            switch (text ("cube-deform", "none")) {
                case "cylinder":
                    return Fx.CubeDeform.CYLINDER;
                case "star":
                    return Fx.CubeDeform.STAR;
                default:
                    return Fx.CubeDeform.NONE;
            }
        }

        /* ---- Turning --------------------------------------------------- */

        public bool turn (Backend.Seat seat, int direction) {
            if (state == null && !begin (seat)) {
                return false;
            }
            if (state.model_switch) {
                return false;
            }
            animation.tick (now_ms ());
            if (state.interactive) {
                animation.turn_while_open (direction);
            } else {
                animation.move (state.row.length, direction);
            }
            draw ();
            return true;
        }

        public void toggle (Backend.Seat seat) {
            if (state != null) {
                if (state.interactive && !animation.in_exit) {
                    release (false);
                }
                return;
            }
            if (!begin (seat)) {
                return;
            }
            state.interactive = true;
            prepare_interactive ();
        }

        public bool drag_button (Backend.Seat seat, Backend.ButtonEvent event) {
            if (!event.pressed || !Input.Accelerators.matches_button (drag_binding, event.button,
                                                                       (Backend.Modifiers) event.modifiers) ||
                state != null || !begin (seat)) {
                return false;
            }
            state.interactive = true;
            state.drag_button = event.button;
            state.held_button = event.button;
            prepare_interactive ();
            return true;
        }

        private void prepare_interactive () {
            state.grab.set_relative_locked (true);
            state.seat.set_pointer_hidden (true);
            animation.grab (state.row.length, real ("cube-zoom", 0.1));
            draw ();
        }

        private void release (bool cancel) {
            if (state == null || !state.interactive || (animation.in_exit && !cancel)) {
                return;
            }
            state.cancelled = cancel;
            animation.tick (now_ms ());
            if (cancel) {
                animation.cancel ();
            } else {
                animation.release ();
            }
            draw ();
        }

        private void on_workspace_switching (int from, int to, Window? carried, bool animate) {
            if (!animate || !available ()) {
                if (state != null && state.model_switch) {
                    end ();
                }
                return;
            }
            var layout = model.workspace_layout ();
            int from_column = -1;
            int to_column = -1;
            int index = 0;
            for (int column = 0; column < layout.cols; column++) {
                int workspace = layout.grid[layout.current_row * layout.cols + column];
                if (workspace < 0) {
                    continue;
                }
                if (workspace == from) {
                    from_column = index;
                }
                if (workspace == to) {
                    to_column = index;
                }
                index++;
            }
            if (from_column < 0 || to_column < 0) {
                if (state != null && state.model_switch) {
                    end ();
                }
                return;
            }
            if (state == null && !begin (null, false)) {
                return;
            }
            if (!state.model_switch && animation.active) {
                return;
            }
            state.model_switch = true;
            state.carried = carried;
            int direction = model.workspace_switch_direction;
            int steps = direction != 0 ? direction :
                Policy.CubeAnimation.shortest_turn (from_column, to_column, state.row.length);
            if (steps == 0) {
                return;
            }
            animation.tick (now_ms ());
            animation.move (state.row.length, steps);
            rebuild ();
        }

        private static double now_ms () {
            return GLib.get_monotonic_time () / 1000.0;
        }

        private bool begin (Backend.Seat? seat, bool pointer_grab = true) {
            if (!available () || (desktop.grab_active && (pointer_grab || !desktop.workspace_switcher.active)) ||
                model.get_outputs ().length == 0) {
                return false;
            }
            var layout = model.workspace_layout ();
            int[] row = {};
            int start = -1;
            for (int column = 0; column < layout.cols; column++) {
                int workspace = layout.grid[layout.current_row * layout.cols + column];
                if (workspace < 0) {
                    continue;
                }
                if (workspace == model.current_workspace) {
                    start = row.length;
                }
                row += workspace;
            }
            if (row.length < 2 || start < 0) {
                return false;
            }
            state = new State ();
            state.seat = seat;
            state.row = row;
            state.start = start;
            if (!pointer_grab) {
                desktop.workspace_switcher.hide_popup ();
            }
            if (pointer_grab && seat != null) {
                state.grab = seat.begin_pointer_grab ();
                if (state.grab == null) {
                    state = null;
                    return false;
                }
            }
            if (state.grab != null) {
                state.grab.ended.connect (on_grab_ended);
                state.grab.motion_delta.connect (on_motion_delta);
                state.grab.button.connect (on_button);
                state.grab.axis.connect (on_axis);
            }
            animation.duration_ms = number ("cube-duration-ms", 350);
            animation.tick (now_ms ());
            model.stack_changed.connect (rebuild);
            build ();
            animating = true;
            return true;
        }

        public void frame () {
            if (state == null) {
                return;
            }
            double now = now_ms ();
            if (now <= state.last_ms) {
                return;
            }
            state.last_ms = now;
            animation.tick (now);
            draw ();
            if (animation.done () && state.held_button == 0) {
                int landed = animation.finish ();
                int workspace = state.row[(state.start + landed) % state.row.length];
                bool model_switch = state.model_switch;
                bool cancelled = state.cancelled;
                end ();
                if (!model_switch && !cancelled && workspace != model.current_workspace) {
                    model.switch_workspace (workspace, null, false);
                }
            }
        }

        public Backend.FilterResult key (Backend.Seat seat, Backend.KeyEvent event) {
            if (state == null) {
                return Backend.FilterResult.PASS;
            }
            if (!event.pressed || !state.interactive || animation.in_exit) {
                return Backend.FilterResult.CONSUME;
            }
            unowned Input.Binding? binding = bindings.match (event);
            if (binding != null && binding.name == BINDING_OPEN) {
                release (false);
                return Backend.FilterResult.CONSUME;
            }
            switch (event.keycode) {
                case KEY_ESC:
                    release (true);
                    break;
                case KEY_ENTER:
                case KEY_KPENTER:
                case KEY_SPACE:
                    release (false);
                    break;
                case KEY_LEFT:
                    turn (seat, -1);
                    break;
                case KEY_RIGHT:
                    turn (seat, 1);
                    break;
                default:
                    break;
            }
            return Backend.FilterResult.CONSUME;
        }

        private void on_motion_delta (double dx, double dy, bool relative, uint time_msec) {
            if (state == null || !state.interactive || animation.in_exit) {
                return;
            }
            if (!relative) {
                double x, y;
                state.seat.get_pointer_position (out x, out y);
                model.pointer_moved (x, y);
            }
            animation.tick (now_ms ());
            animation.rotate (dx, dy, real ("cube-speed-spin-horizontal", 0.02),
                              real ("cube-speed-spin-vertical", 0.02));
            draw ();
        }

        private void on_button (uint button, bool pressed, uint buttons_held) {
            if (state == null || !state.interactive) {
                return;
            }
            if (!pressed && button == state.held_button) {
                state.held_button = 0;
            }
            if (animation.in_exit) {
                return;
            }
            if (state.drag_button != 0) {
                if (!pressed && button == state.drag_button) {
                    release (false);
                }
            } else if (pressed) {
                state.held_button = button;
                release (false);
            }
        }

        private void on_axis (uint axis, double value, bool has_discrete, int discrete) {
            if (state == null || !state.interactive || animation.in_exit || axis != 0) {
                return;
            }
            animation.tick (now_ms ());
            animation.scroll (has_discrete ? discrete : value / 10.0,
                              real ("cube-speed-zoom", 0.07));
            draw ();
        }

        private void on_grab_ended (bool cancelled) {
            if (state != null) {
                state.grab = null;
                state.held_button = 0;
                if (state.seat != null) {
                    state.seat.set_pointer_hidden (false);
                }
                if (state.interactive) {
                    release (true);
                } else {
                    state.cancelled = true;
                    animation.tick (now_ms ());
                    animation.cancel ();
                    draw ();
                }
            }
        }

        private void end () {
            var done = state;
            state = null;
            if (animation.active) {
                animation.finish ();
            }
            model.stack_changed.disconnect (rebuild);
            clear (done);
            if (done.grab != null) {
                done.grab.motion_delta.disconnect (on_motion_delta);
                done.grab.button.disconnect (on_button);
                done.grab.axis.disconnect (on_axis);
                done.grab.ended.disconnect (on_grab_ended);
                done.grab.end ();
            }
            if (done.seat != null && done.interactive) {
                done.seat.set_pointer_hidden (false);
            }
            animating = false;
        }

        /* ---- Sides ----------------------------------------------------- */

        private void build () {
            uint spacing = number ("cube-mesh-spacing", 32);
            bool light = flag ("cube-light", true);
            foreach (var output in model.get_outputs ()) {
                var screen = new Screen ();
                screen.output = output;
                make_backdrop (screen);
                var geometry = geometry_for (output);
                for (int i = 0; i < state.row.length; i++) {
                    var side = new Side ();
                    side.workspace = state.row[(state.start + i) % state.row.length];
                    fill (screen, side, geometry, spacing);
                    if (light) {
                        make_shade (side, geometry, spacing);
                    }
                    screen.sides.add (side);
                }
                build_carried (screen);
                state.screens.add (screen);
            }
        }

        private void make_backdrop (Screen screen) {
            string mode = text ("cube-background", "solid");
            if (mode == "skydome" || mode == "cubemap") {
                string path = text (mode == "skydome" ? "cube-skydome-image" : "cube-cubemap-image", "");
                bool loaded = mode == "skydome" ? (screen.backdrop = image_backdrop (path)) != null :
                                                    load_faces (screen, path);
                if (loaded) {
                    screen.sky = true;
                    screen.sky_mirror = mode == "skydome" && flag ("cube-skydome-mirror", false);
                    screen.sky_grid = Fx.Grid.create (129, 129);
                    if (screen.sky_grid == null) {
                        screen.backdrop = null;
                        screen.sky = false;
                        screen.faces.remove_range (0, screen.faces.length);
                        loaded = false;
                    }
                }
                if (!loaded && failed_images.lookup (mode + path) == null) {
                    warning ("Cannot load cube %s image '%s'; using solid background", mode, path);
                    failed_images.insert (mode + path, mode + path);
                }
                if (!loaded || mode == "cubemap") {
                    mode = "solid";
                }
            }
            if (screen.backdrop == null) {
                screen.backdrop = Backdrop.make (server, desktop, screen.output, mode,
                                                 Backdrop.parse_colour (text ("cube-background-color", "#1a1a1a")),
                                                 number ("expo-background-blur", 40),
                                                 real ("expo-background-dim", 0.3), out screen.backdrop_zoom);
            }
            if (!screen.backdrop.is_ready ()) {
                unowned Screen placed = screen;
                screen.backdrop_ready = screen.backdrop.ready.connect (() => {
                    placed.stacked = false;
                    draw ();
                });
            }
        }

        private Backend.PixelBuffer? image_backdrop (string path) {
            var image = Bg.Image.load (path);
            if (image == null) {
                return null;
            }
            int width = image.width (), height = image.height ();
            if (width <= 0 || height <= 0 || width > 4096 || height > 4096) {
                return null;
            }
            var buffer = new Backend.PixelBuffer (server, width, height, 1);
            int stride;
            unowned uint8[] pixels = buffer.begin (out stride);
            if (pixels.length < stride * height) {
                return null;
            }
            image.copy_argb (pixels, stride);
            buffer.set_accepts_input (false);
            buffer.commit ();
            return buffer;
        }

        private bool load_faces (Screen screen, string path) {
            var image = Bg.Image.load (path);
            if (image == null) {
                return false;
            }
            int width = image.width (), height = image.height ();
            if (width < 4 || height < 3 || width > 4096 || height > 4096 ||
                width % 4 != 0 || height % 3 != 0 || width / 4 != height / 3) {
                return false;
            }
            uint8[] source = new uint8[width * height * 4];
            image.copy_argb (source, width * 4);
            int out_width = int.min (width, 1024), out_height = out_width / 2;
            for (int i = 0; i < 6; i++) {
                var face = new BackgroundFace ();
                face.buffer = new Backend.PixelBuffer (server, out_width, out_height, 1);
                int stride;
                unowned uint8[] pixels = face.buffer.begin (out stride);
                if (pixels.length < stride * out_height ||
                    !Fx.cubemap_face (source, width, height, width * 4, pixels,
                                      out_width, out_height, stride, i)) {
                    screen.faces.remove_range (0, screen.faces.length);
                    return false;
                }
                face.buffer.set_accepts_input (false);
                face.buffer.commit ();
                if (!face.buffer.is_ready ()) {
                    unowned Screen placed = screen;
                    face.ready = face.buffer.ready.connect (() => {
                        placed.stacked = false;
                        draw ();
                    });
                }
                screen.faces.add (face);
            }
            return true;
        }

        private static bool on_workspace (Policy.Model model, Window window, int workspace) {
            return window.has_state (WindowState.MAPPED) && model.showing_on_its_workspace (window) &&
                   (window.workspace == -1 || window.workspace == workspace);
        }

        private static bool intersects (Marco.Rect a, Marco.Rect b) {
            return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height;
        }

        private void fill (Screen screen, Side side, Fx.Cube geometry, uint spacing) {
            unowned Backend.View? wallpaper = desktop.background.wallpaper_view (screen.output);
            if (wallpaper != null) {
                add_tree (screen, side, wallpaper, geometry, spacing);
            }
            foreach (var view in desktop.layer_shell.views_on (screen.output, { LAYER_BACKGROUND, LAYER_BOTTOM })) {
                add_tree (screen, side, view, geometry, spacing);
            }
            foreach (var window in model.get_stack ()) {
                var xdg = window.adapter as XdgWindow;
                if (xdg == null || !xdg.mapped || window == state.carried ||
                    !on_workspace (model, window, side.workspace) ||
                    !intersects (window.outer_rect (), screen.output.rect)) {
                    continue;
                }
                foreach (var view in xdg.all_views ()) {
                    add_tree (screen, side, view, geometry, spacing);
                }
            }
            foreach (var view in desktop.layer_shell.views_on (screen.output, { LAYER_TOP })) {
                add_tree (screen, side, view, geometry, spacing);
            }
        }

        private void add_tree (Screen screen, Side side, Backend.View source, Fx.Cube geometry, uint spacing) {
            var mirror = source.mirror ();
            if (mirror == null) {
                return;
            }
            double x, y;
            source.get_position (out x, out y);
            mirror.set_position (x, y);
            side.mirrors.add (mirror);
            foreach (var view in mirror.mirror_views ()) {
                side.pieces.add (make_piece (screen, view, geometry, spacing));
            }
        }

        private void build_carried (Screen screen) {
            foreach (var mirror in screen.carried) {
                mirror.move_to_layer (null);
            }
            screen.carried.remove_range (0, screen.carried.length);
            var xdg = state.carried != null ? state.carried.adapter as XdgWindow : null;
            if (xdg == null || !xdg.mapped || !intersects (state.carried.outer_rect (), screen.output.rect)) {
                return;
            }
            foreach (var source in xdg.all_views ()) {
                var mirror = source.mirror ();
                if (mirror != null) {
                    double x, y;
                    source.get_position (out x, out y);
                    mirror.set_position (x, y);
                    screen.carried.add (mirror);
                }
            }
        }

        private Piece make_piece (Screen screen, Backend.View view, Fx.Cube geometry, uint spacing) {
            var piece = new Piece () { view = view };
            var rect = screen.output.rect;
            int width, height;
            view.get_position (out piece.x, out piece.y);
            view.get_size (out width, out height);
            piece.x -= rect.x;
            piece.y -= rect.y;
            piece.width = width;
            piece.height = height;
            piece.clip_x = double.max (piece.x, 0);
            piece.clip_y = double.max (piece.y, 0);
            piece.clip_width = double.min (piece.x + width, rect.width) - piece.clip_x;
            piece.clip_height = double.min (piece.y + height, rect.height) - piece.clip_y;
            if (width <= 0 || height <= 0 || piece.clip_width <= 0 || piece.clip_height <= 0) {
                piece.drawn = false;
                view.set_alpha (0);
                return piece;
            }
            geometry.grid_size (piece.clip_width, piece.clip_height, spacing, out piece.cols, out piece.rows);
            piece.grid = Fx.Grid.create (piece.cols, piece.rows);
            if (piece.grid == null) {
                piece.drawn = false;
                view.set_alpha (0);
            }
            return piece;
        }

        private void make_shade (Side side, Fx.Cube geometry, uint spacing) {
            side.shade_width = int.max ((int) (geometry.size.x / spacing), 2);
            side.shade_height = int.max ((int) (geometry.size.y / spacing), 2);
            side.shade = new Backend.PixelBuffer (server, side.shade_width, side.shade_height, 1);
            side.shade.set_accepts_input (false);
            geometry.grid_size (geometry.size.x, geometry.size.y, spacing, out side.shade_cols, out side.shade_rows);
            side.shade_grid = Fx.Grid.create (side.shade_cols, side.shade_rows);
        }

        private void clear (State done) {
            foreach (var screen in done.screens) {
                clear_sides (screen);
                foreach (var mirror in screen.carried) {
                    mirror.move_to_layer (null);
                }
                screen.carried.remove_range (0, screen.carried.length);
                if (screen.backdrop != null && screen.backdrop_ready != 0) {
                    screen.backdrop.disconnect (screen.backdrop_ready);
                }
                foreach (var face in screen.faces) {
                    if (face.ready != 0) {
                        face.buffer.disconnect (face.ready);
                    }
                    unowned Backend.View? face_view = face.buffer.get_view ();
                    if (face_view != null) {
                        face_view.move_to_layer (null);
                    }
                }
                screen.faces.remove_range (0, screen.faces.length);
                unowned Backend.View? view = screen.backdrop != null ? screen.backdrop.get_view () : null;
                if (view != null) {
                    view.move_to_layer (null);
                }
                screen.backdrop = null;
            }
            done.screens.remove_range (0, done.screens.length);
        }

        private static void clear_sides (Screen screen) {
            foreach (var side in screen.sides) {
                foreach (var mirror in side.mirrors) {
                    mirror.move_to_layer (null);
                }
                unowned Backend.View? shade = side.shade != null ? side.shade.get_view () : null;
                if (shade != null) {
                    shade.move_to_layer (null);
                }
            }
            screen.sides.remove_range (0, screen.sides.length);
            screen.order = {};
            screen.stacked = false;
        }

        private void rebuild () {
            if (state == null) {
                return;
            }
            uint spacing = number ("cube-mesh-spacing", 32);
            bool light = flag ("cube-light", true);
            foreach (var screen in state.screens) {
                clear_sides (screen);
                build_carried (screen);
                var geometry = geometry_for (screen.output);
                for (int i = 0; i < state.row.length; i++) {
                    var side = new Side ();
                    side.workspace = state.row[(state.start + i) % state.row.length];
                    fill (screen, side, geometry, spacing);
                    if (light) {
                        make_shade (side, geometry, spacing);
                    }
                    screen.sides.add (side);
                }
            }
            draw ();
        }

        /* ---- Drawing --------------------------------------------------- */

        private Fx.Cube geometry_for (Policy.Output output) {
            return Fx.Cube () {
                sides = state.row.length,
                rotation = animation.rotation.value,
                offset_y = animation.offset_y.value,
                offset_z = animation.offset_z.value,
                zoom = animation.zoom.value,
                ease = animation.ease.value,
                deform = deform (),
                origin = Fx.Vec2 () { x = output.rect.x, y = output.rect.y },
                size = Fx.Vec2 () { x = output.rect.width, y = output.rect.height }
            };
        }

        private void draw () {
            if (state == null) {
                return;
            }
            bool tilted = animation.offset_y.value.abs () > 1e-3;
            foreach (var screen in state.screens) {
                var geometry = geometry_for (screen.output);
                bool changed = !screen.stacked;
                for (int i = 0; i < screen.sides.length; i++) {
                    var side = screen.sides[i];
                    bool shown = tilted || geometry.side_facing (i);
                    changed |= shown != side.shown;
                    side.shown = shown;
                    if (shown) {
                        bend (side, i, geometry);
                    }
                }
                var order = new int[screen.sides.length];
                geometry.side_order (order);
                for (int i = 0; i < order.length && !changed; i++) {
                    changed = i >= screen.order.length || order[i] != screen.order[i];
                }
                if (changed) {
                    restack (screen, order);
                }
                if (screen.sky && screen.sky_grid != null) {
                    geometry.fill_sky_grid (screen.sky_mirror, screen.sky_grid);
                    if (screen.faces.length == 0) {
                        unowned Backend.View? view = screen.backdrop.get_view ();
                        if (view != null) {
                            view.set_deformation (screen.sky_grid.vertices, 129, 129);
                        }
                    }
                    foreach (var face in screen.faces) {
                        unowned Backend.View? view = face.buffer.get_view ();
                        if (view == null) {
                            continue;
                        }
                        view.set_deformation (screen.sky_grid.vertices, 129, 129);
                    }
                }
            }
        }

        private void bend (Side side, int index, Fx.Cube geometry) {
            foreach (var piece in side.pieces) {
                if (piece.grid == null) {
                    continue;
                }
                bool in_front = geometry.fill_grid (index, piece.x, piece.y, piece.width, piece.height,
                                                    piece.clip_x, piece.clip_y, piece.clip_width,
                                                    piece.clip_height, piece.grid);
                bool drawn = in_front && piece.view.set_deformation (piece.grid.vertices, piece.cols, piece.rows);
                if (drawn != piece.drawn) {
                    piece.view.set_alpha (drawn ? 1.0f : 0.0f);
                    piece.drawn = drawn;
                }
            }
            if (side.shade == null) {
                return;
            }
            int stride;
            unowned uint8[] pixels = side.shade.begin (out stride);
            if (pixels.length > 0) {
                geometry.fill_shade (index, pixels, side.shade_width, side.shade_height, stride);
                side.shade.commit ();
            }
            unowned Backend.View? view = side.shade.get_view ();
            if (view != null && side.shade_grid != null &&
                geometry.fill_grid (index, 0, 0, geometry.size.x, geometry.size.y, 0, 0, geometry.size.x,
                                    geometry.size.y, side.shade_grid)) {
                view.set_deformation (side.shade_grid.vertices, side.shade_cols, side.shade_rows);
            }
        }

        private void restack (Screen screen, int[] order) {
            unowned Backend.Layer overlay = server.get_layer (Backend.LayerKind.OVERLAY);
            unowned Backend.View? backdrop = screen.backdrop != null ? screen.backdrop.get_view () : null;
            if (backdrop != null) {
                backdrop.place (screen.output.rect.x, screen.output.rect.y, screen.backdrop_zoom);
                backdrop.move_to_layer (overlay);
            }
            foreach (var face in screen.faces) {
                unowned Backend.View? view = face.buffer.get_view ();
                if (view != null) {
                    view.place (screen.output.rect.x, screen.output.rect.y, 1.0);
                    view.move_to_layer (overlay);
                }
            }
            foreach (int index in order) {
                var side = screen.sides[index];
                unowned Backend.View? shade = side.shade != null ? side.shade.get_view () : null;
                foreach (var mirror in side.mirrors) {
                    mirror.move_to_layer (side.shown ? overlay : null);
                }
                if (shade != null) {
                    shade.move_to_layer (side.shown ? overlay : null);
                }
            }
            foreach (var mirror in screen.carried) {
                mirror.move_to_layer (overlay);
            }
            screen.order = order;
            screen.stacked = backdrop != null;
            foreach (var face in screen.faces) {
                if (face.buffer.get_view () == null) {
                    screen.stacked = false;
                }
            }
            foreach (var side in screen.sides) {
                if (side.shown && side.shade != null && side.shade.get_view () == null) {
                    screen.stacked = false;
                }
            }
        }
    }
}
