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
        public const string BINDING_LEFT = "polo-cube-rotate-left";
        public const string BINDING_RIGHT = "polo-cube-rotate-right";
        private const string GROUP = "polo-cube";
        private const string KEYS_SCHEMA = "org.mate.polo.keybindings";
        private const uint LAYER_BACKGROUND = 0;
        private const uint LAYER_BOTTOM = 1;
        private const uint LAYER_TOP = 2;

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

        private class Screen {
            public Policy.Output output;
            public GLib.GenericArray<Side> sides = new GLib.GenericArray<Side> ();
            public Backend.PixelBuffer? backdrop = null;
            public ulong backdrop_ready = 0;
            public double backdrop_zoom = 1.0;
            public int[] order = {};
            public bool stacked = false;
        }

        private class State {
            public unowned Backend.Seat seat;
            public Backend.PointerGrab? grab;
            public int[] row;
            public int start;
            public GLib.GenericArray<Screen> screens = new GLib.GenericArray<Screen> ();
            public double last_ms = -1;
        }

        private Backend.Server server;
        private Policy.Model model;
        private Desktop desktop;
        private Input.Bindings bindings;
        private GLib.Settings? keys = null;
        private Policy.CubeAnimation animation;
        private State? state = null;

        public bool active { get { return state != null; } }
        public bool animating { get; private set; default = false; }

        public Cube (Backend.Server server, Policy.Model model, Desktop desktop, Input.Bindings bindings) {
            this.server = server;
            this.model = model;
            this.desktop = desktop;
            this.bindings = bindings;
            animation = new Policy.CubeAnimation (350);
            var source = GLib.SettingsSchemaSource.get_default ();
            var schema = source != null ? source.lookup (KEYS_SCHEMA, true) : null;
            if (schema != null && schema.has_key ("cube-rotate-left")) {
                keys = new GLib.Settings.full (schema, null, null);
                keys.changed.connect (on_keys_changed);
            }
            bindings.register (BINDING_LEFT, (binding, seat, event) => turn (seat, -1));
            bindings.register (BINDING_RIGHT, (binding, seat, event) => turn (seat, 1));
            bind_keys ();
        }

        public void shutdown () {
            if (state != null) {
                end ();
            }
            if (keys != null) {
                keys.changed.disconnect (on_keys_changed);
            }
            bindings.unregister (BINDING_LEFT);
            bindings.unregister (BINDING_RIGHT);
            bindings.set_custom (GROUP, new GLib.GenericArray<Input.Binding> ());
        }

        private void on_keys_changed (string key) {
            if (key.has_prefix ("cube-rotate-")) {
                bind_keys ();
            }
        }

        private void bind_keys () {
            var custom = new GLib.GenericArray<Input.Binding> ();
            add_key (custom, "cube-rotate-left", BINDING_LEFT);
            add_key (custom, "cube-rotate-right", BINDING_RIGHT);
            bindings.set_custom (GROUP, custom);
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
            animation.tick (now_ms ());
            animation.move (state.row.length, direction);
            draw ();
            return true;
        }

        private static double now_ms () {
            return GLib.get_monotonic_time () / 1000.0;
        }

        private bool begin (Backend.Seat seat) {
            if (!available () || desktop.grab_active || model.get_outputs ().length == 0) {
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
            state.grab = seat.begin_pointer_grab ();
            if (state.grab != null) {
                state.grab.ended.connect (on_grab_ended);
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
            if (animation.done ()) {
                int landed = animation.finish ();
                int workspace = state.row[(state.start + landed) % state.row.length];
                end ();
                if (workspace != model.current_workspace) {
                    model.switch_workspace (workspace);
                }
            }
        }

        public Backend.FilterResult key (Backend.Seat seat, Backend.KeyEvent event) {
            return state == null ? Backend.FilterResult.PASS : Backend.FilterResult.CONSUME;
        }

        private void on_grab_ended (bool cancelled) {
            if (state != null) {
                state.grab = null;
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
                done.grab.ended.disconnect (on_grab_ended);
                done.grab.end ();
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
                state.screens.add (screen);
            }
        }

        private void make_backdrop (Screen screen) {
            screen.backdrop = Backdrop.make (server, desktop, screen.output,
                                             text ("cube-background", "solid"),
                                             Backdrop.parse_colour (text ("cube-background-color", "#1a1a1a")),
                                             number ("expo-background-blur", 40),
                                             real ("expo-background-dim", 0.3), out screen.backdrop_zoom);
            if (!screen.backdrop.is_ready ()) {
                unowned Screen placed = screen;
                screen.backdrop_ready = screen.backdrop.ready.connect (() => {
                    placed.stacked = false;
                    draw ();
                });
            }
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
                if (xdg == null || !xdg.mapped || !on_workspace (model, window, side.workspace) ||
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
                if (screen.backdrop != null && screen.backdrop_ready != 0) {
                    screen.backdrop.disconnect (screen.backdrop_ready);
                }
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
            screen.order = order;
            screen.stacked = backdrop != null;
            foreach (var side in screen.sides) {
                if (side.shown && side.shade != null && side.shade.get_view () == null) {
                    screen.stacked = false;
                }
            }
        }
    }
}
