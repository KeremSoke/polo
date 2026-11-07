/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Expo. 
 */

namespace Polo.Shell {
    public class Expo : GLib.Object {
        public const string BINDING = "polo-expo";
        private const string GROUP = "polo";
        private const string KEYS_SCHEMA = "org.mate.polo.keybindings";
        private const int GAP = 16;
        private const double DURATION_MS = 300.0;
        private const uint TICK_MS = 16;
        private const int OUTLINE_WIDTH = 5;
        private const int DRAG_THRESHOLD = 6;
        private const int LABEL_GAP = 6;
        private const int LABEL_PADDING = 8;
        private const uint BTN_LEFT = 0x110;
        private const uint KEY_ESC = 1;
        private const uint KEY_ENTER = 28;
        private const uint KEY_KPENTER = 96;
        private const uint KEY_SPACE = 57;
        private const uint KEY_UP = 103;
        private const uint KEY_LEFT = 105;
        private const uint KEY_RIGHT = 106;
        private const uint KEY_DOWN = 108;

        private class Mirror {
            public Backend.View view;
            public Window? window;
            public int column;
            public int row;
            public double x;
            public double y;
        }

        private class Label {
            public Backend.PixelBuffer buffer;
            public ulong ready = 0;
            public bool shown = false;
            public int column;
            public int row;
            public int width;
            public int height;
        }

        private class Screen {
            public GLib.GenericArray<Label> labels = new GLib.GenericArray<Label> ();
            public Policy.Output output;
            public Policy.ExpoLayout layout;
            public Backend.PixelBuffer? backdrop;
            public ulong backdrop_ready = 0;
            public double backdrop_zoom = 1.0;
            public Outline outline;
            public bool outlined = false;
            public Marco.Rect outlined_rect;
            public GLib.GenericArray<Mirror> mirrors = new GLib.GenericArray<Mirror> ();
        }

        private class Drag {
            public Window window;
            public unowned Screen screen;
            public int column;
            public int row;
            public double press_x;
            public double press_y;
            public double dx = 0.0;
            public double dy = 0.0;
            public bool moving = false;
        }

        private class State {
            public unowned Backend.Seat seat;
            public Backend.PointerGrab? grab;
            public Policy.WorkspaceLayout workspaces;
            public GLib.GenericArray<Screen> screens = new GLib.GenericArray<Screen> ();
            public int start;
            public int selected;
            public double t = 0.0;
            public bool closing = false;
            public uint timer = 0;
            public int64 last_usec = 0;
            public double pointer_x;
            public double pointer_y;
            public Drag? drag;
        }

        private Backend.Server server;
        private Policy.Model model;
        private Desktop desktop;
        private Input.Bindings bindings;
        private GLib.Settings? keys = null;
        private State? state = null;

        public bool active { get { return state != null; } }

        public Expo (Backend.Server server, Policy.Model model, Desktop desktop, Input.Bindings bindings) {
            this.server = server;
            this.model = model;
            this.desktop = desktop;
            this.bindings = bindings;
            var source = GLib.SettingsSchemaSource.get_default ();
            var schema = source != null ? source.lookup (KEYS_SCHEMA, true) : null;
            if (schema != null) {
                keys = new GLib.Settings.full (schema, null, null);
                keys.changed["expo"].connect (bind_key);
            }
            bindings.register (BINDING, (binding, seat, event) => toggle (seat));
            bind_key ();
        }

        public void shutdown () {
            if (state != null) {
                finish (false);
            }
            if (keys != null) {
                keys.changed["expo"].disconnect (bind_key);
            }
            bindings.unregister (BINDING);
            bindings.set_custom (GROUP, new GLib.GenericArray<Input.Binding> ());
        }

        private void bind_key () {
            var custom = new GLib.GenericArray<Input.Binding> ();
            var accelerator = Input.Accelerator ();
            bool parsed = keys != null && Input.Accelerators.parse (keys.get_string ("expo"), out accelerator);
            if (parsed && !accelerator.is_disabled ()) {
                custom.add (new Input.Binding () {
                    name = BINDING, kind = Input.BindingKind.GLOBAL, accelerator = accelerator
                });
            }
            bindings.set_custom (GROUP, custom);
        }

        private bool enabled () {
            var effects = desktop.effects.settings;
            return effects != null && effects.settings_schema.has_key ("expo") && effects.get_boolean ("expo");
        }

        public void toggle (Backend.Seat seat) {
            if (state != null) {
                if (!state.closing) {
                    close (state.selected);
                }
                return;
            }
            if (!enabled () || desktop.grab_active || desktop.cube.active || model.get_outputs ().length == 0) {
                return;
            }
            var grab = seat.begin_pointer_grab ();
            if (grab == null) {
                return;
            }
            state = new State ();
            state.seat = seat;
            state.grab = grab;
            state.start = model.current_workspace;
            state.selected = model.current_workspace;
            state.pointer_x = model.pointer_x;
            state.pointer_y = model.pointer_y;
            grab.motion.connect (on_motion);
            grab.button.connect (on_button);
            grab.ended.connect (on_grab_ended);
            model.stack_changed.connect (rebuild);
            build ();
            start_timer ();
        }

        /* ---- The grid -------------------------------------------------- */

        private bool cell_of (int workspace, out int column, out int row) {
            var grid = state.workspaces;
            for (int i = 0; i < grid.grid.length; i++) {
                if (grid.grid[i] == workspace) {
                    column = i % grid.cols;
                    row = i / grid.cols;
                    return true;
                }
            }
            column = row = 0;
            return false;
        }

        private int workspace_at (int column, int row) {
            var grid = state.workspaces;
            if (column < 0 || row < 0 || column >= grid.cols || row >= grid.rows) {
                return -1;
            }
            return grid.grid[row * grid.cols + column];
        }

        private bool on_workspace (Window window, int workspace) {
            return window.has_state (WindowState.MAPPED) && model.showing_on_its_workspace (window) &&
                   (window.workspace == -1 || window.workspace == workspace);
        }

        private static bool intersects (Marco.Rect a, Marco.Rect b) {
            return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height;
        }

        private void add_mirror (Screen screen, Backend.View source, Window? window, int column, int row) {
            var view = source.mirror ();
            if (view == null) {
                return;
            }
            double x, y;
            source.get_position (out x, out y);
            var mirror = new Mirror () { view = view, window = window, column = column, row = row };
            screen.layout.into_cell (column, row, x, y, out mirror.x, out mirror.y);
            view.move_to_layer (server.get_layer (Backend.LayerKind.OVERLAY));
            screen.mirrors.add (mirror);
        }

        private Backend.PixelBuffer make_backdrop (Policy.Output output, out double zoom) {
            return Backdrop.make (server, desktop, output, effects_string ("expo-background", "blurred-wallpaper"),
                                  Backdrop.DEFAULT_COLOUR, effects_uint ("expo-background-blur", 40),
                                  effects_double ("expo-background-dim", 0.3), out zoom);
        }

        private string effects_string (string key, string fallback) {
            var effects = desktop.effects.settings;
            return effects != null && effects.settings_schema.has_key (key) ? effects.get_string (key) : fallback;
        }

        private uint effects_uint (string key, uint fallback) {
            var effects = desktop.effects.settings;
            return effects != null && effects.settings_schema.has_key (key) ? effects.get_uint (key) : fallback;
        }

        private double effects_double (string key, double fallback) {
            var effects = desktop.effects.settings;
            return effects != null && effects.settings_schema.has_key (key) ? effects.get_double (key) : fallback;
        }

        private void place_backdrop (Screen screen) {
            unowned Backend.View? view = screen.backdrop != null ? screen.backdrop.get_view () : null;
            if (view != null) {
                view.place (screen.output.rect.x, screen.output.rect.y, screen.backdrop_zoom);
                view.move_to_layer (server.get_layer (Backend.LayerKind.OVERLAY));
            }
        }

        private Policy.WorkspaceLayout grid_layout () {
            string mode = effects_string ("expo-layout", "workspaces");
            int n = model.n_workspaces;
            switch (mode) {
                case "row":
                    return new Policy.WorkspaceLayout (n, model.current_workspace, 1, n, false,
                                                       Policy.Corner.TOPLEFT);
                case "grid":
                    int columns = (int) GLib.Math.ceil (GLib.Math.sqrt (n));
                    return new Policy.WorkspaceLayout (n, model.current_workspace, 0, columns, false,
                                                       Policy.Corner.TOPLEFT);
                default:
                    return model.workspace_layout ();
            }
        }

        private bool show_names () {
            var effects = desktop.effects.settings;
            return effects != null && effects.settings_schema.has_key ("expo-workspace-names") &&
                   effects.get_boolean ("expo-workspace-names");
        }

        private int label_room () {
            if (!show_names ()) {
                return 0;
            }
            int tallest = 0;
            for (int i = 0; i < model.n_workspaces; i++) {
                int width, height;
                Marco.label_size_padded (model.workspace_name (i), null, LABEL_PADDING, out width, out height);
                tallest = int.max (tallest, height);
            }
            return tallest + LABEL_GAP;
        }

        private void add_label (Screen screen, int column, int row, int workspace) {
            var label = new Label () { column = column, row = row };
            string text = model.workspace_name (workspace);
            Marco.label_size_padded (text, null, LABEL_PADDING, out label.width, out label.height);
            label.buffer = new Backend.PixelBuffer (server, label.width, label.height, 1);
            int stride;
            unowned uint8[] pixels = label.buffer.begin (out stride);
            if (pixels.length > 0) {
                Marco.label_render_osd (text, null, effects_uint ("expo-label-radius", 6),
                                        effects_double ("expo-label-opacity", 0.75), label.width, label.height,
                                        pixels, stride);
            }
            label.buffer.set_accepts_input (false);
            label.buffer.commit ();
            if (!label.buffer.is_ready ()) {
                label.ready = label.buffer.ready.connect (() => place ());
            }
            screen.labels.add (label);
        }

        private void build () {
            state.workspaces = grid_layout ();
            int room = label_room ();
            foreach (var output in model.get_outputs ()) {
                var screen = new Screen ();
                screen.output = output;
                screen.layout = new Policy.ExpoLayout (output.rect, state.workspaces.cols, state.workspaces.rows, GAP,
                                                       room);
                screen.outline = new Outline (server);
                screen.backdrop = make_backdrop (output, out screen.backdrop_zoom);
                if (screen.backdrop.is_ready ()) {
                    place_backdrop (screen);
                } else {
                    unowned Screen placed = screen;
                    screen.backdrop_ready = screen.backdrop.ready.connect (() => {
                        place_backdrop (placed);
                        restack ();
                    });
                }
                state.screens.add (screen);
                fill (screen);
            }
            place ();
        }

        private void fill (Screen screen) {
            unowned Backend.View? wallpaper = desktop.background.wallpaper_view (screen.output);
            for (int row = 0; row < state.workspaces.rows; row++) {
                for (int column = 0; column < state.workspaces.cols; column++) {
                    int workspace = workspace_at (column, row);
                    if (workspace < 0) {
                        continue;
                    }
                    if (wallpaper != null) {
                        add_mirror (screen, wallpaper, null, column, row);
                    }
                    if (show_names ()) {
                        add_label (screen, column, row, workspace);
                    }
                    foreach (var window in model.get_stack ()) {
                        var xdg = window.adapter as XdgWindow;
                        if (xdg == null || !xdg.mapped || !on_workspace (window, workspace) ||
                            !intersects (window.outer_rect (), screen.output.rect)) {
                            continue;
                        }
                        foreach (var view in xdg.all_views ()) {
                            add_mirror (screen, view, window, column, row);
                        }
                    }
                }
            }
        }

        private void restack () {
            if (state == null) {
                return;
            }
            foreach (var screen in state.screens) {
                foreach (var mirror in screen.mirrors) {
                    mirror.view.move_to_layer (server.get_layer (Backend.LayerKind.OVERLAY));
                }
                foreach (var label in screen.labels) {
                    unowned Backend.View? view = label.shown ? label.buffer.get_view () : null;
                    if (view != null) {
                        view.move_to_layer (server.get_layer (Backend.LayerKind.OVERLAY));
                    }
                }
            }
            foreach (var screen in state.screens) {
                screen.outlined = false;
            }
            show_outline ();
        }

        private void clear (State state) {
            foreach (var screen in state.screens) {
                screen.outline.hide ();
                screen.mirrors.remove_range (0, screen.mirrors.length);
                foreach (var label in screen.labels) {
                    if (label.ready != 0) {
                        label.buffer.disconnect (label.ready);
                    }
                    unowned Backend.View? view = label.buffer.get_view ();
                    if (view != null) {
                        view.move_to_layer (null);
                    }
                }
                screen.labels.remove_range (0, screen.labels.length);
                if (screen.backdrop != null && screen.backdrop_ready != 0) {
                    screen.backdrop.disconnect (screen.backdrop_ready);
                    screen.backdrop_ready = 0;
                }
                unowned Backend.View? view = screen.backdrop != null ? screen.backdrop.get_view () : null;
                if (view != null) {
                    view.move_to_layer (null);
                }
                screen.backdrop = null;
            }
            state.screens.remove_range (0, state.screens.length);
        }

        private void rebuild () {
            if (state == null || state.drag != null) {
                return;
            }
            clear (state);
            build ();
        }

        /* ---- Drawing --------------------------------------------------- */

        private void place () {
            int focus = state.closing ? state.selected : state.start;
            foreach (var screen in state.screens) {
                int fc, fr;
                cell_of (focus, out fc, out fr);
                double k = screen.layout.zoom_factor (state.t);
                foreach (var mirror in screen.mirrors) {
                    double x = mirror.x, y = mirror.y;
                    var drag = state.drag;
                    if (drag != null && drag.moving && mirror.window == drag.window && drag.screen == screen &&
                        mirror.column == drag.column && mirror.row == drag.row) {
                        x += drag.dx;
                        y += drag.dy;
                    }
                    double zx, zy;
                    screen.layout.zoom_point (fc, fr, state.t, x, y, out zx, out zy);
                    mirror.view.place (zx, zy, screen.layout.scale * k);
                }
                foreach (var label in screen.labels) {
                    unowned Backend.View? view = label.buffer.get_view ();
                    if (view == null) {
                        continue;
                    }
                    var cell = screen.layout.cell (label.column, label.row);
                    double zx, zy;
                    screen.layout.zoom_point (fc, fr, state.t, cell.x + (cell.width - label.width) / 2.0,
                                              cell.y + cell.height + LABEL_GAP, out zx, out zy);
                    view.place (zx, zy, k);
                    if (!label.shown) {
                        view.move_to_layer (server.get_layer (Backend.LayerKind.OVERLAY));
                        label.shown = true;
                    }
                }
            }
            show_outline ();
        }

        private void show_outline () {
            foreach (var screen in state.screens) {
                int column = 0, row = 0;
                bool selected = cell_of (state.selected, out column, out row);
                bool dragging = state.drag != null && state.drag.moving;
                if (state.t >= 1.0 && !state.closing && selected && !dragging) {
                    var cell = screen.layout.cell (column, row);
                    var rect = Marco.Rect () {
                        x = cell.x - OUTLINE_WIDTH, y = cell.y - OUTLINE_WIDTH,
                        width = cell.width + 2 * OUTLINE_WIDTH, height = cell.height + 2 * OUTLINE_WIDTH
                    };
                    if (!screen.outlined || !rect.equal (screen.outlined_rect)) {
                        screen.outline.show (rect, OUTLINE_WIDTH);
                        screen.outlined = true;
                        screen.outlined_rect = rect;
                    }
                } else if (screen.outlined) {
                    screen.outline.hide ();
                    screen.outlined = false;
                }
            }
        }

        private void start_timer () {
            state.last_usec = GLib.get_monotonic_time ();
            if (state.timer == 0) {
                state.timer = GLib.Timeout.add (TICK_MS, tick);
            }
        }

        private bool tick () {
            if (state == null) {
                return GLib.Source.REMOVE;
            }
            int64 now = GLib.get_monotonic_time ();
            double step = (now - state.last_usec) / 1000.0 / DURATION_MS;
            state.last_usec = now;
            state.t = (state.closing ? state.t - step : state.t + step).clamp (0.0, 1.0);
            place ();
            if (state.closing ? state.t > 0.0 : state.t < 1.0) {
                return GLib.Source.CONTINUE;
            }
            state.timer = 0;
            if (state.closing) {
                finish (true);
            }
            return GLib.Source.REMOVE;
        }

        private void close (int workspace) {
            state.selected = workspace;
            state.closing = true;
            state.drag = null;
            start_timer ();
        }

        private void finish (bool switch_to) {
            var done = state;
            state = null;
            model.stack_changed.disconnect (rebuild);
            if (done.timer != 0) {
                GLib.Source.remove (done.timer);
            }
            clear (done);
            if (done.grab != null) {
                done.grab.motion.disconnect (on_motion);
                done.grab.button.disconnect (on_button);
                done.grab.ended.disconnect (on_grab_ended);
                done.grab.end ();
            }
            if (switch_to && done.selected != model.current_workspace) {
                model.switch_workspace (done.selected, null, false);
            }
        }

        /* ---- Input ----------------------------------------------------- */

        public Backend.FilterResult key (Backend.Seat seat, Backend.KeyEvent event) {
            if (state == null) {
                return Backend.FilterResult.PASS;
            }
            if (!event.pressed || state.closing) {
                return Backend.FilterResult.CONSUME;
            }
            unowned Input.Binding? binding = bindings.match (event);
            if (binding != null && binding.name == BINDING) {
                close (state.selected);
                return Backend.FilterResult.CONSUME;
            }
            int column, row;
            cell_of (state.selected, out column, out row);
            switch (event.keycode) {
                case KEY_ESC:
                    close (state.start);
                    break;
                case KEY_ENTER:
                case KEY_KPENTER:
                case KEY_SPACE:
                    close (state.selected);
                    break;
                case KEY_LEFT:
                    select_cell (column - 1, row);
                    break;
                case KEY_RIGHT:
                    select_cell (column + 1, row);
                    break;
                case KEY_UP:
                    select_cell (column, row - 1);
                    break;
                case KEY_DOWN:
                    select_cell (column, row + 1);
                    break;
                default:
                    break;
            }
            return Backend.FilterResult.CONSUME;
        }

        private void select_cell (int column, int row) {
            int workspace = workspace_at (column, row);
            if (workspace >= 0) {
                state.selected = workspace;
                show_outline ();
            }
        }

        private unowned Screen? screen_at (double x, double y, out int column, out int row) {
            column = row = -1;
            foreach (unowned Screen screen in state.screens) {
                if (screen.layout.hit (x, y, out column, out row)) {
                    return screen;
                }
            }
            return null;
        }

        private Window? window_at (Screen screen, int column, int row, double x, double y) {
            Window? found = null;
            int workspace = workspace_at (column, row);
            foreach (var window in model.get_stack ()) {
                var xdg = window.adapter as XdgWindow;
                if (xdg == null || !xdg.mapped || !on_workspace (window, workspace) ||
                    window.window_type == Marco.WindowType.DESKTOP || window.window_type == Marco.WindowType.DOCK) {
                    continue;
                }
                var rect = window.outer_rect ();
                double x1, y1, x2, y2;
                screen.layout.into_cell (column, row, rect.x, rect.y, out x1, out y1);
                screen.layout.into_cell (column, row, rect.x + rect.width, rect.y + rect.height, out x2, out y2);
                if (x >= x1 && x < x2 && y >= y1 && y < y2) {
                    found = window;
                }
            }
            return found;
        }

        private void raise_drag (Drag drag) {
            foreach (var mirror in drag.screen.mirrors) {
                if (mirror.window == drag.window && mirror.column == drag.column && mirror.row == drag.row) {
                    mirror.view.move_to_layer (server.get_layer (Backend.LayerKind.OVERLAY));
                }
            }
        }

        private void on_motion (double x, double y, uint time_msec) {
            state.pointer_x = x;
            state.pointer_y = y;
            var drag = state.drag;
            if (drag == null || state.closing) {
                return;
            }
            double dx = x - drag.press_x, dy = y - drag.press_y;
            if (!drag.moving && dx * dx + dy * dy >= DRAG_THRESHOLD * DRAG_THRESHOLD) {
                drag.moving = true;
                raise_drag (drag);
            }
            if (drag.moving) {
                drag.dx = dx;
                drag.dy = dy;
                int column, row;
                if (screen_at (x, y, out column, out row) != null && workspace_at (column, row) >= 0) {
                    state.selected = workspace_at (column, row);
                }
                place ();
            }
        }

        private void on_button (uint button, bool pressed, uint buttons_held) {
            if (state.closing || button != BTN_LEFT) {
                return;
            }
            int column, row;
            unowned Screen? screen = screen_at (state.pointer_x, state.pointer_y, out column, out row);
            if (pressed) {
                var window = screen != null && state.t >= 1.0
                             ? window_at (screen, column, row, state.pointer_x, state.pointer_y) : null;
                state.drag = window == null ? null : new Drag () {
                    window = window, screen = screen, column = column, row = row,
                    press_x = state.pointer_x, press_y = state.pointer_y
                };
                return;
            }
            var drag = state.drag;
            state.drag = null;
            if (drag != null && drag.moving) {
                drop (drag, screen, column, row);
                return;
            }
            if (screen != null && workspace_at (column, row) >= 0) {
                close (workspace_at (column, row));
            }
        }

        /* The window lands where its mirror was let go, on that cell's workspace and output. */
        private void drop (Drag drag, Screen? screen, int column, int row) {
            int workspace = screen != null ? workspace_at (column, row) : -1;
            if (workspace < 0) {
                place ();
                return;
            }
            var rect = drag.window.outer_rect ();
            double gx, gy, x, y;
            drag.screen.layout.into_cell (drag.column, drag.row, rect.x, rect.y, out gx, out gy);
            screen.layout.out_of_cell (column, row, gx + drag.dx, gy + drag.dy, out x, out y);
            if (drag.window.workspace != -1 && drag.window.workspace != workspace) {
                model.move_to_workspace (drag.window, workspace);
            }
            model.move (drag.window, true, (int) GLib.Math.round (x), (int) GLib.Math.round (y));
            state.selected = workspace;
            rebuild ();
        }

        private void on_grab_ended (bool cancelled) {
            if (state != null) {
                state.grab = null;
                if (!state.closing) {
                    close (state.start);
                }
            }
        }
    }
}
