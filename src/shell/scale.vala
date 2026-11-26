/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Scale: the windows of the workspace, of every workspace or of one
 * application spread out side by side, scaled down, to pick one.
 */

namespace Polo.Shell {
    public class Scale : GLib.Object {
        private const string GROUP = "polo-scale";
        private const string KEYS_SCHEMA = "org.mate.polo.keybindings";
        private const string SCHEMA = "org.mate.polo.scale";
        private const uint TICK_MS = 16;
        private const int OUTLINE_WIDTH = 5;
        private const int DRAG_THRESHOLD = 6;
        private const int LABEL_GAP = 6;
        private const int LABEL_PADDING = 8;
        private const int TITLE_INSET = 6;
        private const double DIMMED = 0.5;
        private const uint BTN_LEFT = 0x110;
        private const uint BTN_MIDDLE = 0x112;
        private const uint KEY_ESC = 1;
        private const uint KEY_TAB = 15;
        private const uint KEY_ENTER = 28;
        private const uint KEY_SPACE = 57;
        private const uint KEY_KPENTER = 96;
        private const uint KEY_HOME = 102;
        private const uint KEY_UP = 103;
        private const uint KEY_LEFT = 105;
        private const uint KEY_RIGHT = 106;
        private const uint KEY_END = 107;
        private const uint KEY_DOWN = 108;

        private class Item {
            public Window window;
            public GLib.GenericArray<Window> members;
            public Marco.Rect rect;
            public int workspace;
            public bool minimized;
            public bool in_place;
        }

        private class Mirror {
            public Backend.View view;
            public double dx;
            public double dy;
        }

        private class Label {
            public Backend.PixelBuffer buffer;
            public ulong ready = 0;
            public bool shown = false;
            public double x;
            public double y;
            public int width;
            public int height;
        }

        private class Thumb {
            public Item item;
            public unowned Screen screen;
            public Policy.ScaleSlot slot;
            public GLib.GenericArray<Mirror> mirrors = new GLib.GenericArray<Mirror> ();
            public Label? title;
            public double start_x;
            public double start_y;
            public double start_scale;
            public double x;
            public double y;
            public double scale;
            public double alpha;
        }

        private class Screen {
            public Policy.Output output;
            public bool shown;
            public Backend.PixelBuffer? backdrop;
            public ulong backdrop_ready = 0;
            public double backdrop_zoom = 1.0;
            public Marco.Rect backdrop_rect;
            public int backdrop_scale;
            public Outline outline;
            public bool outlined = false;
            public Marco.Rect outlined_rect;
            public GLib.GenericArray<Thumb> thumbs = new GLib.GenericArray<Thumb> ();
            public GLib.GenericArray<Policy.ScaleBand> bands = new GLib.GenericArray<Policy.ScaleBand> ();
            public GLib.GenericArray<Label> labels = new GLib.GenericArray<Label> ();
        }

        private class Drag {
            public unowned Thumb thumb;
            public double press_x;
            public double press_y;
            public double dx = 0.0;
            public double dy = 0.0;
            public bool moving = false;
        }

        private class State {
            public unowned Backend.Seat seat;
            public Backend.PointerGrab? grab;
            public Policy.ScaleMode mode;
            public Policy.ScaleScreens where;
            public GLib.GenericArray<Item> items = new GLib.GenericArray<Item> ();
            public GLib.GenericArray<Screen> screens = new GLib.GenericArray<Screen> ();
            public Window? like;
            public Window? selected;
            public unowned Screen? current;
            public int summoned;
            public double t = 0.0;
            public bool closing = false;
            public Window? picked;
            public uint timer = 0;
            public int64 last_usec = 0;
            public double pointer_x;
            public double pointer_y;
            public Drag? drag;
            public Backend.Modifiers hold = 0;
            public ulong hold_handler = 0;
        }

        private Backend.Server server;
        private Policy.Model model;
        private Desktop desktop;
        private Input.Bindings bindings;
        private GLib.Settings? keys = null;
        private GLib.Settings? settings = null;
        private State? state = null;

        public bool active { get { return state != null; } }

        public Scale (Backend.Server server, Policy.Model model, Desktop desktop, Input.Bindings bindings) {
            this.server = server;
            this.model = model;
            this.desktop = desktop;
            this.bindings = bindings;
            var source = GLib.SettingsSchemaSource.get_default ();
            var schema = source != null ? source.lookup (KEYS_SCHEMA, true) : null;
            if (schema != null) {
                keys = new GLib.Settings.full (schema, null, null);
                keys.changed.connect (on_key_changed);
            }
            var own = source != null ? source.lookup (SCHEMA, true) : null;
            if (own != null) {
                settings = new GLib.Settings.full (own, null, null);
            }
            foreach (var mode in Policy.ScaleMode.all ()) {
                var m = mode;
                bindings.register (binding_name (mode), (binding, seat, event) => {
                    toggle (seat, m, event.modifiers);
                });
            }
            desktop.layout_changed.connect (on_layout_changed);
            bind_keys ();
        }

        public void shutdown () {
            if (state != null) {
                finish ();
            }
            if (keys != null) {
                keys.changed.disconnect (on_key_changed);
            }
            desktop.layout_changed.disconnect (on_layout_changed);
            foreach (var mode in Policy.ScaleMode.all ()) {
                bindings.unregister (binding_name (mode));
            }
            bindings.set_custom (GROUP, new GLib.GenericArray<Input.Binding> ());
        }

        private static string binding_name (Policy.ScaleMode mode) {
            return "polo-" + mode.to_key ();
        }

        private void on_key_changed (string key) {
            Policy.ScaleMode mode;
            if (Policy.ScaleMode.from_key (key, out mode)) {
                bind_keys ();
            }
        }

        private void bind_keys () {
            var custom = new GLib.GenericArray<Input.Binding> ();
            foreach (var mode in Policy.ScaleMode.all ()) {
                var accelerator = Input.Accelerator ();
                if (keys == null || !keys.settings_schema.has_key (mode.to_key ())) {
                    continue;
                }
                if (Input.Accelerators.parse (keys.get_string (mode.to_key ()), out accelerator) &&
                    !accelerator.is_disabled ()) {
                    custom.add (new Input.Binding () {
                        name = binding_name (mode), kind = Input.BindingKind.GLOBAL, accelerator = accelerator
                    });
                }
            }
            bindings.set_custom (GROUP, custom);
        }

        private string get_string (string key, string fallback) {
            return settings != null && settings.settings_schema.has_key (key) ? settings.get_string (key) : fallback;
        }

        private uint get_uint (string key, uint fallback) {
            return settings != null && settings.settings_schema.has_key (key) ? settings.get_uint (key) : fallback;
        }

        private double get_double (string key, double fallback) {
            return settings != null && settings.settings_schema.has_key (key) ? settings.get_double (key) : fallback;
        }

        private bool get_boolean (string key, bool fallback) {
            return settings != null && settings.settings_schema.has_key (key) ? settings.get_boolean (key) : fallback;
        }

        public bool enabled () {
            return get_boolean ("enabled", true);
        }

        private Policy.ScaleScreens screens_for (Policy.ScaleMode mode) {
            var where = Policy.ScaleScreens.parse (get_string ("screens", "each"));
            if (settings != null && settings.settings_schema.has_key ("mode-screens")) {
                var overrides = settings.get_value ("mode-screens");
                string? value = null;
                if (overrides.lookup (mode.to_mode_name (), "s", out value)) {
                    where = Policy.ScaleScreens.parse (value, where);
                }
            }
            return where;
        }

        public void toggle (Backend.Seat seat, Policy.ScaleMode mode, Backend.Modifiers held = 0) {
            if (state != null) {
                if (state.closing) {
                    return;
                }
                if (state.mode == mode) {
                    close (null);
                } else {
                    state.mode = mode;
                    state.where = screens_for (mode);
                    rebuild ();
                    if (state.items.length == 0) {
                        close (null);
                    }
                }
                return;
            }
            if (!enabled () || desktop.grab_active || desktop.cube.active || model.get_outputs ().length == 0 ||
                server.get_lock_state () != Backend.LockState.UNLOCKED) {
                return;
            }
            var like = model.focused;
            if (mode.application () && like == null) {
                return;
            }
            var grab = seat.begin_pointer_grab ();
            if (grab == null) {
                return;
            }
            state = new State ();
            state.seat = seat;
            state.grab = grab;
            state.mode = mode;
            state.where = screens_for (mode);
            state.like = like;
            state.selected = model.focused;
            state.pointer_x = model.pointer_x;
            state.pointer_y = model.pointer_y;
            grab.motion.connect (on_motion);
            grab.button.connect (on_button);
            grab.ended.connect (on_grab_ended);
            model.stack_changed.connect (on_stack_changed);
            build ();
            if (state.items.length == 0) {
                Logging.scope (LogDomain.POLO).debug ("scale: %s has no windows", mode.to_mode_name ());
                finish ();
                return;
            }
            if (get_boolean ("hold", false)) {
                var mask = Input.Accelerators.from_real (held);
                state.hold = Switcher.primary_modifier (mask);
                if (state.hold != 0) {
                    state.hold_handler = seat.modifiers_changed.connect (on_modifiers_changed);
                }
            }
            start_timer ();
        }

        private void on_modifiers_changed (Backend.Seat seat, Backend.Modifiers modifiers) {
            if (state == null || state.closing || state.hold == 0 || state.hold in modifiers) {
                return;
            }
            close (state.selected);
        }

        private void on_layout_changed () {
            if (state == null || state.closing) {
                return;
            }
            if (model.get_outputs ().length == 0) {
                finish ();
                return;
            }
            rebuild ();
        }

        private void on_stack_changed () {
            if (state == null || state.closing || state.drag != null) {
                return;
            }
            rebuild ();
            if (state.items.length == 0) {
                close (null);
            }
        }

        private Item make_item (Window window) {
            var members = Policy.ScaleWindows.family (model, window);
            var rect = window.outer_rect ();
            foreach (var member in members) {
                var r = member.outer_rect ();
                int x1 = int.min (rect.x, r.x), y1 = int.min (rect.y, r.y);
                int x2 = int.max (rect.x + rect.width, r.x + r.width);
                int y2 = int.max (rect.y + rect.height, r.y + r.height);
                rect = Marco.Rect () { x = x1, y = y1, width = x2 - x1, height = y2 - y1 };
            }
            return new Item () {
                window = window, members = members, rect = rect,
                workspace = window.workspace == -1 ? model.current_workspace : window.workspace,
                minimized = window.has_state (WindowState.MINIMIZED),
                in_place = model.is_showing (window)
            };
        }

        private void collect_items () {
            state.items = new GLib.GenericArray<Item> ();
            string minimized = get_string ("minimized", "show");
            foreach (var window in Policy.ScaleWindows.pick (model, state.mode, minimized != "hide", state.like)) {
                var xdg = window.adapter as XdgWindow;
                if (xdg == null) {
                    continue;
                }
                state.items.add (make_item (window));
            }
            bool still = false;
            foreach (var item in state.items) {
                still |= item.window == state.selected;
            }
            if (!still) {
                state.selected = state.items.length > 0 ? state.items[0].window : null;
            }
        }

        private int output_scale (Policy.Output output) {
            var head = desktop.backend_output (output);
            return head != null ? int.max (head.get_scale (), 1) : 1;
        }

        private Backend.PixelBuffer make_backdrop (Policy.Output output, out double zoom) {
            return Backdrop.make (server, desktop, output, get_string ("background", "blurred-wallpaper"),
                                  Backdrop.DEFAULT_COLOUR, get_uint ("background-blur", 40),
                                  get_double ("background-dim", 0.3), out zoom);
        }

        private Backend.PixelBuffer? take_backdrop (GLib.GenericArray<Screen>? old, Screen screen) {
            if (old == null) {
                return null;
            }
            foreach (var previous in old) {
                if (previous.backdrop == null || previous.output != screen.output ||
                    previous.backdrop_scale != screen.backdrop_scale ||
                    !previous.backdrop_rect.equal (screen.output.rect)) {
                    continue;
                }
                var backdrop = previous.backdrop;
                screen.backdrop_zoom = previous.backdrop_zoom;
                if (previous.backdrop_ready != 0) {
                    backdrop.disconnect (previous.backdrop_ready);
                    previous.backdrop_ready = 0;
                }
                previous.backdrop = null;
                return backdrop;
            }
            return null;
        }

        private void place_backdrop (Screen screen) {
            unowned Backend.View? view = screen.backdrop != null ? screen.backdrop.get_view () : null;
            if (view != null) {
                view.place (screen.output.rect.x, screen.output.rect.y, screen.backdrop_zoom);
                view.move_to_layer (server.get_layer (Backend.LayerKind.OVERLAY));
            }
        }

        private Label? make_label (string text, int max_width) {
            if (max_width <= 2 * LABEL_PADDING) {
                return null;
            }
            string shown = text;
            int width, height;
            Marco.label_size_padded (shown, null, LABEL_PADDING, out width, out height);
            int chars = shown.char_count ();
            while (width > max_width && chars > 1) {
                chars = chars * 3 / 4;
                shown = text.substring (0, text.index_of_nth_char (chars)) + "…";
                Marco.label_size_padded (shown, null, LABEL_PADDING, out width, out height);
            }
            if (width > max_width) {
                return null;
            }
            var label = new Label () { width = width, height = height };
            label.buffer = new Backend.PixelBuffer (server, width, height, 1);
            int stride;
            unowned uint8[] pixels = label.buffer.begin (out stride);
            if (pixels.length > 0) {
                Marco.label_render_osd (shown, null, 6, 0.75, width, height, pixels, stride);
            }
            label.buffer.set_accepts_input (false);
            label.buffer.commit ();
            if (!label.buffer.is_ready ()) {
                label.ready = label.buffer.ready.connect (() => place ());
            }
            return label;
        }

        private int label_room () {
            int width, height;
            Marco.label_size_padded ("Workspace", null, LABEL_PADDING, out width, out height);
            return height + LABEL_GAP;
        }

        private void add_mirrors (Thumb thumb) {
            foreach (var member in thumb.item.members) {
                var xdg = member.adapter as XdgWindow;
                if (xdg == null) {
                    continue;
                }
                foreach (var source in xdg.all_views ()) {
                    var view = source.mirror ();
                    if (view == null) {
                        continue;
                    }
                    double x, y;
                    source.get_position (out x, out y);
                    thumb.mirrors.add (new Mirror () {
                        view = view, dx = x - thumb.item.rect.x, dy = y - thumb.item.rect.y
                    });
                    view.move_to_layer (server.get_layer (Backend.LayerKind.OVERLAY));
                }
            }
        }

        private static string thumb_key (Policy.Output output, Window window) {
            return "%s/%llu".printf (output.name, window.id);
        }

        private void build (GLib.GenericArray<Screen>? old = null, GLib.HashTable<string, Thumb>? before = null) {
            collect_items ();
            var outputs = model.get_outputs ();
            Marco.Rect[] all_rects = {};
            foreach (var output in outputs) {
                all_rects += output.rect;
            }
            int[] distinct = Policy.ScaleScreensOf.distinct (all_rects);
            Marco.Rect[] rects = {};
            foreach (int i in distinct) {
                rects += all_rects[i];
            }
            var at = model.output_at (state.pointer_x, state.pointer_y);
            state.summoned = 0;
            for (int s = 0; s < distinct.length; s++) {
                if (at != null && rects[s].equal (at.rect)) {
                    state.summoned = s;
                }
            }
            Marco.Rect[] item_rects = {};
            foreach (var item in state.items) {
                item_rects += item.rect;
            }
            bool every_screen = get_string ("summoned-windows", "this-screen") == "every-screen";
            var lists = Policy.ScaleScreensOf.distribute (state.where, every_screen, state.summoned, item_rects, rects);
            var arrangement = Policy.ScaleArrangement.parse (get_string ("layout", "natural"));
            bool grouped = state.mode.all_workspaces () &&
                           get_string ("all-workspaces-layout", "grouped") == "grouped";
            int spacing = (int) get_uint ("spacing", 16);
            bool titles = get_boolean ("window-titles", true);
            unowned var log = Logging.scope (LogDomain.POLO);
            log.debug ("scale: %s on %d screens, %d windows", state.mode.to_mode_name (), distinct.length,
                       state.items.length);
            for (int s = 0; s < distinct.length; s++) {
                var screen = new Screen ();
                screen.output = outputs[distinct[s]];
                screen.shown = Policy.ScaleScreensOf.shows (state.where, s, state.summoned);
                screen.outline = new Outline (server);
                state.screens.add (screen);
                if (!screen.shown) {
                    continue;
                }
                screen.backdrop_rect = screen.output.rect;
                screen.backdrop_scale = output_scale (screen.output);
                screen.backdrop = take_backdrop (old, screen);
                if (screen.backdrop == null) {
                    screen.backdrop = make_backdrop (screen.output, out screen.backdrop_zoom);
                }
                if (screen.backdrop.is_ready ()) {
                    place_backdrop (screen);
                } else {
                    unowned Screen placed = screen;
                    screen.backdrop_ready = screen.backdrop.ready.connect (() => {
                        place_backdrop (placed);
                        restack ();
                    });
                }
                var list = lists[s];
                Marco.Rect[] members = {};
                int[] workspaces = {};
                for (uint i = 0; i < list.length; i++) {
                    var item = state.items[list.index (i)];
                    members += item.rect;
                    workspaces += item.workspace;
                }
                var slots = new GLib.GenericArray<Policy.ScaleSlot> ();
                if (grouped) {
                    int[] order = {};
                    for (int w = 0; w < model.n_workspaces; w++) {
                        order += w;
                    }
                    screen.bands = Policy.ScaleLayout.grouped (arrangement, screen.output.rect, order, members,
                                                               workspaces, spacing, label_room ());
                    foreach (var band in screen.bands) {
                        foreach (var slot in band.slots) {
                            slots.add (slot);
                        }
                        var label = make_label (model.workspace_name (band.workspace),
                                                band.area.width - 2 * spacing);
                        if (label != null) {
                            label.x = band.area.x + spacing;
                            label.y = band.area.y - label.height - LABEL_GAP / 2;
                            screen.labels.add (label);
                        }
                    }
                } else {
                    slots = Policy.ScaleLayout.arrange (arrangement, screen.output.rect, members, spacing);
                }
                var names = new GLib.StringBuilder ();
                foreach (var slot in slots) {
                    var item = state.items[list.index (slot.index)];
                    var thumb = new Thumb () { item = item, screen = screen, slot = slot };
                    Thumb? previous = before != null ? before.lookup (thumb_key (screen.output, item.window)) : null;
                    if (previous != null) {
                        thumb.start_x = previous.x;
                        thumb.start_y = previous.y;
                        thumb.start_scale = previous.scale;
                    } else {
                        thumb.start_x = item.rect.x;
                        thumb.start_y = item.rect.y;
                        thumb.start_scale = 1.0;
                    }
                    add_mirrors (thumb);
                    if (titles) {
                        thumb.title = make_label (item.window.title ?? item.window.app_id ?? "",
                                                  (int) slot.width () - 2 * TITLE_INSET);
                    }
                    screen.thumbs.add (thumb);
                    names.append (names.len > 0 ? ", " : "");
                    names.append (item.window.title ?? "?");
                }
                log.debug ("scale: %s shows %s", screen.output.name, names.len > 0 ? names.str : "nothing");
            }
            state.current = null;
            foreach (var screen in state.screens) {
                if (screen.shown && (state.current == null || screen == state.screens[state.summoned])) {
                    state.current = screen;
                }
            }
            if (old != null) {
                drop_backdrops (old);
            }
            place ();
        }

        private void drop_backdrops (GLib.GenericArray<Screen> screens) {
            foreach (var screen in screens) {
                unowned Backend.View? view = screen.backdrop != null ? screen.backdrop.get_view () : null;
                if (view != null) {
                    view.move_to_layer (null);
                }
                if (screen.backdrop != null && screen.backdrop_ready != 0) {
                    screen.backdrop.disconnect (screen.backdrop_ready);
                    screen.backdrop_ready = 0;
                }
                screen.backdrop = null;
            }
        }

        private void hide_label (Label label) {
            if (label.ready != 0) {
                label.buffer.disconnect (label.ready);
                label.ready = 0;
            }
            unowned Backend.View? view = label.buffer.get_view ();
            if (view != null) {
                view.move_to_layer (null);
            }
        }

        private void clear (State state, bool keep_backdrops = false) {
            foreach (var screen in state.screens) {
                screen.outline.hide ();
                foreach (var thumb in screen.thumbs) {
                    foreach (var mirror in thumb.mirrors) {
                        mirror.view.move_to_layer (null);
                    }
                    thumb.mirrors.remove_range (0, thumb.mirrors.length);
                    if (thumb.title != null) {
                        hide_label (thumb.title);
                    }
                }
                screen.thumbs.remove_range (0, screen.thumbs.length);
                foreach (var label in screen.labels) {
                    hide_label (label);
                }
                screen.labels.remove_range (0, screen.labels.length);
            }
            if (!keep_backdrops) {
                drop_backdrops (state.screens);
            }
            state.screens.remove_range (0, state.screens.length);
        }

        private void rebuild () {
            var old = state.screens.copy ((screen) => screen);
            var before = new GLib.HashTable<string, Thumb> (GLib.str_hash, GLib.str_equal);
            foreach (var screen in old) {
                foreach (var thumb in screen.thumbs) {
                    before.replace (thumb_key (screen.output, thumb.item.window), thumb);
                }
            }
            clear (state, true);
            build (old, before);
            bool moved = false;
            foreach (var screen in state.screens) {
                foreach (var thumb in screen.thumbs) {
                    moved |= (thumb.start_x - thumb.slot.x).abs () > 0.5 ||
                             (thumb.start_y - thumb.slot.y).abs () > 0.5 ||
                             (thumb.start_scale - thumb.slot.scale).abs () > 0.001;
                }
            }
            if (moved) {
                state.t = 0.0;
                start_timer ();
            } else {
                place ();
            }
        }

        private static double ease (double t) {
            return t * t * (3.0 - 2.0 * t);
        }

        private void place () {
            if (state == null) {
                return;
            }
            double e = ease (state.t);
            foreach (var screen in state.screens) {
                foreach (var thumb in screen.thumbs) {
                    var slot = thumb.slot;
                    double sx = state.closing ? thumb.item.rect.x : thumb.start_x;
                    double sy = state.closing ? thumb.item.rect.y : thumb.start_y;
                    double ss = state.closing ? 1.0 : thumb.start_scale;
                    thumb.x = sx + (slot.x - sx) * e;
                    thumb.y = sy + (slot.y - sy) * e;
                    thumb.scale = ss + (slot.scale - ss) * e;
                    var drag = state.drag;
                    if (drag != null && drag.moving && drag.thumb == thumb) {
                        thumb.x += drag.dx;
                        thumb.y += drag.dy;
                    }
                    double alpha = thumb.item.in_place ? 1.0 : e;
                    if (thumb.item.minimized && get_string ("minimized", "show") == "dimmed") {
                        alpha *= DIMMED;
                    }
                    thumb.alpha = alpha;
                    foreach (var mirror in thumb.mirrors) {
                        mirror.view.place (thumb.x + mirror.dx * thumb.scale, thumb.y + mirror.dy * thumb.scale,
                                           thumb.scale);
                        mirror.view.set_alpha ((float) alpha);
                    }
                    place_title (thumb);
                }
                foreach (var label in screen.labels) {
                    show_label (label, label.x, label.y, state.t >= 1.0 && !state.closing);
                }
            }
            show_outline ();
        }

        private void place_title (Thumb thumb) {
            if (thumb.title == null) {
                return;
            }
            var label = thumb.title;
            double x = thumb.slot.centre_x () - label.width / 2.0;
            double y = thumb.slot.y + thumb.slot.height () - label.height - TITLE_INSET;
            bool visible = state.t >= 1.0 && !state.closing && (state.drag == null || state.drag.thumb != thumb ||
                                                                 !state.drag.moving);
            show_label (label, x, y, visible);
        }

        private void show_label (Label label, double x, double y, bool visible) {
            unowned Backend.View? view = label.buffer.get_view ();
            if (view == null) {
                return;
            }
            if (!visible) {
                if (label.shown) {
                    view.move_to_layer (null);
                    label.shown = false;
                }
                return;
            }
            view.place (x, y, 1.0);
            if (!label.shown) {
                view.move_to_layer (server.get_layer (Backend.LayerKind.OVERLAY));
                label.shown = true;
            }
        }

        private void restack () {
            if (state == null) {
                return;
            }
            foreach (var screen in state.screens) {
                foreach (var thumb in screen.thumbs) {
                    foreach (var mirror in thumb.mirrors) {
                        mirror.view.move_to_layer (server.get_layer (Backend.LayerKind.OVERLAY));
                    }
                    if (thumb.title != null && thumb.title.shown) {
                        unowned Backend.View? view = thumb.title.buffer.get_view ();
                        if (view != null) {
                            view.move_to_layer (server.get_layer (Backend.LayerKind.OVERLAY));
                        }
                    }
                }
                foreach (var label in screen.labels) {
                    unowned Backend.View? view = label.shown ? label.buffer.get_view () : null;
                    if (view != null) {
                        view.move_to_layer (server.get_layer (Backend.LayerKind.OVERLAY));
                    }
                }
                screen.outlined = false;
            }
            show_outline ();
        }

        private void show_outline () {
            bool dragging = state.drag != null && state.drag.moving;
            foreach (var screen in state.screens) {
                Thumb? found = null;
                if (state.t >= 1.0 && !state.closing && !dragging && state.selected != null) {
                    foreach (var thumb in screen.thumbs) {
                        if (thumb.item.window == state.selected) {
                            found = thumb;
                        }
                    }
                }
                if (found == null) {
                    if (screen.outlined) {
                        screen.outline.hide ();
                        screen.outlined = false;
                    }
                    continue;
                }
                var rect = Marco.Rect () {
                    x = (int) GLib.Math.round (found.slot.x) - OUTLINE_WIDTH,
                    y = (int) GLib.Math.round (found.slot.y) - OUTLINE_WIDTH,
                    width = (int) GLib.Math.round (found.slot.width ()) + 2 * OUTLINE_WIDTH,
                    height = (int) GLib.Math.round (found.slot.height ()) + 2 * OUTLINE_WIDTH
                };
                if (!screen.outlined || !rect.equal (screen.outlined_rect)) {
                    screen.outline.show (rect, OUTLINE_WIDTH);
                    screen.outlined = true;
                    screen.outlined_rect = rect;
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
            double duration = double.max (get_uint ("duration-ms", 250), 1.0);
            double step = (now - state.last_usec) / 1000.0 / duration;
            state.last_usec = now;
            state.t = (state.closing ? state.t - step : state.t + step).clamp (0.0, 1.0);
            place ();
            if (state.closing ? state.t > 0.0 : state.t < 1.0) {
                return GLib.Source.CONTINUE;
            }
            state.timer = 0;
            if (state.closing) {
                finish ();
            }
            return GLib.Source.REMOVE;
        }

        private void close (Window? picked) {
            state.picked = picked;
            state.closing = true;
            state.drag = null;
            if (state.t > 0.0) {
                foreach (var screen in state.screens) {
                    foreach (var thumb in screen.thumbs) {
                        thumb.start_x = thumb.item.rect.x;
                        thumb.start_y = thumb.item.rect.y;
                        thumb.start_scale = 1.0;
                    }
                }
            }
            start_timer ();
        }

        private void finish () {
            var done = state;
            state = null;
            model.stack_changed.disconnect (on_stack_changed);
            if (done.timer != 0) {
                GLib.Source.remove (done.timer);
            }
            if (done.hold_handler != 0) {
                GLib.SignalHandler.disconnect (done.seat, done.hold_handler);
            }
            clear (done);
            if (done.grab != null) {
                done.grab.motion.disconnect (on_motion);
                done.grab.button.disconnect (on_button);
                done.grab.ended.disconnect (on_grab_ended);
                done.grab.end ();
            }
            if (done.picked != null) {
                pick (done.picked);
            }
        }

        private void pick (Window window) {
            if (!model.get_stack ().find (window) && !window.has_state (WindowState.MAPPED)) {
                return;
            }
            Logging.scope (LogDomain.POLO).debug ("scale: picked %s", window.title ?? "?");
            model.focus_policy.leave_mouse_mode ();
            if (window.workspace != -1 && window.workspace != model.current_workspace) {
                if (get_string ("other-workspace", "switch") == "bring") {
                    model.move_to_workspace (window, model.current_workspace);
                } else {
                    model.switch_workspace (window.workspace, window);
                }
            }
            model.activate (window, Policy.ActivationSource.PAGER, 0);
        }

        public Backend.FilterResult key (Backend.Seat seat, Backend.KeyEvent event) {
            if (state == null) {
                return Backend.FilterResult.PASS;
            }
            if (!event.pressed || state.closing) {
                return Backend.FilterResult.CONSUME;
            }
            unowned Input.Binding? binding = bindings.match (event);
            if (binding != null && binding.name.has_prefix ("polo-scale")) {
                return Backend.FilterResult.CONSUME;
            }
            if (binding != null && binding.name == Expo.BINDING) {
                finish ();
                GLib.Idle.add (() => {
                    if (!desktop.grab_active) {
                        desktop.expo.toggle (seat);
                    }
                    return GLib.Source.REMOVE;
                });
                return Backend.FilterResult.CONSUME;
            }
            switch (event.keycode) {
                case KEY_ESC:
                    close (null);
                    break;
                case KEY_ENTER:
                case KEY_KPENTER:
                case KEY_SPACE:
                    close (state.selected);
                    break;
                case KEY_LEFT:
                    step (-1, 0);
                    break;
                case KEY_RIGHT:
                    step (1, 0);
                    break;
                case KEY_UP:
                    step (0, -1);
                    break;
                case KEY_DOWN:
                    step (0, 1);
                    break;
                case KEY_TAB:
                    cycle (Backend.Modifiers.SHIFT in event.modifiers ? -1 : 1);
                    break;
                case KEY_HOME:
                    select_item (0);
                    break;
                case KEY_END:
                    select_item (state.items.length - 1);
                    break;
                default:
                    break;
            }
            return Backend.FilterResult.CONSUME;
        }

        private GLib.GenericArray<Thumb> navigable () {
            var thumbs = new GLib.GenericArray<Thumb> ();
            foreach (var screen in state.screens) {
                if (state.where == Policy.ScaleScreens.MIRRORED && screen != state.current) {
                    continue;
                }
                foreach (var thumb in screen.thumbs) {
                    thumbs.add (thumb);
                }
            }
            return thumbs;
        }

        private void step (int dx, int dy) {
            var thumbs = navigable ();
            if (thumbs.length == 0) {
                return;
            }
            double[] xs = {}, ys = {};
            int current = -1;
            for (int i = 0; i < thumbs.length; i++) {
                xs += thumbs[i].slot.centre_x ();
                ys += thumbs[i].slot.centre_y ();
                if (thumbs[i].item.window == state.selected && (current < 0 || thumbs[i].screen == state.current)) {
                    current = i;
                }
            }
            int next = Policy.ScaleNavigation.nearest (xs, ys, current, dx, dy);
            if (next >= 0) {
                state.selected = thumbs[next].item.window;
                state.current = thumbs[next].screen;
                show_outline ();
            }
        }

        private void cycle (int direction) {
            int n = state.items.length;
            if (n == 0) {
                return;
            }
            int index = 0;
            for (int i = 0; i < n; i++) {
                if (state.items[i].window == state.selected) {
                    index = i;
                }
            }
            select_item (((index + direction) % n + n) % n);
        }

        private void select_item (int index) {
            if (index < 0 || index >= state.items.length) {
                return;
            }
            state.selected = state.items[index].window;
            show_outline ();
        }

        private unowned Thumb? thumb_at (double x, double y) {
            foreach (unowned Screen screen in state.screens) {
                if (!screen.shown || !screen.output.contains (x, y)) {
                    continue;
                }
                for (int i = (int) screen.thumbs.length - 1; i >= 0; i--) {
                    if (screen.thumbs[i].slot.contains (x, y)) {
                        return screen.thumbs[i];
                    }
                }
            }
            return null;
        }

        private unowned Screen? screen_at (double x, double y) {
            foreach (unowned Screen screen in state.screens) {
                if (screen.output.contains (x, y)) {
                    return screen;
                }
            }
            return null;
        }

        private void on_motion (double x, double y, uint time_msec) {
            state.pointer_x = x;
            state.pointer_y = y;
            if (state.closing) {
                return;
            }
            var drag = state.drag;
            if (drag != null) {
                double dx = x - drag.press_x, dy = y - drag.press_y;
                if (!drag.moving && dx * dx + dy * dy >= DRAG_THRESHOLD * DRAG_THRESHOLD) {
                    drag.moving = true;
                    foreach (var mirror in drag.thumb.mirrors) {
                        mirror.view.move_to_layer (server.get_layer (Backend.LayerKind.OVERLAY));
                    }
                }
                if (drag.moving) {
                    drag.dx = dx;
                    drag.dy = dy;
                    place ();
                }
                return;
            }
            if (state.t < 1.0) {
                return;
            }
            unowned Thumb? under = thumb_at (x, y);
            if (under != null && (under.item.window != state.selected || under.screen != state.current)) {
                state.selected = under.item.window;
                state.current = under.screen;
                show_outline ();
            }
        }

        private void on_button (uint button, bool pressed, uint buttons_held) {
            if (state.closing) {
                return;
            }
            unowned Thumb? under = state.t >= 1.0 ? thumb_at (state.pointer_x, state.pointer_y) : null;
            if (button == BTN_MIDDLE) {
                if (pressed && under != null && get_string ("middle-click", "close-window") == "close-window") {
                    model.close (under.item.window);
                }
                return;
            }
            if (button != BTN_LEFT) {
                return;
            }
            if (pressed) {
                state.drag = under == null ? null : new Drag () {
                    thumb = under, press_x = state.pointer_x, press_y = state.pointer_y
                };
                return;
            }
            var drag = state.drag;
            state.drag = null;
            if (drag != null && drag.moving) {
                drop (drag);
                return;
            }
            if (under != null) {
                close (under.item.window);
                return;
            }
            if (state.t < 1.0) {
                return;
            }
            if (get_string ("empty-click", "close") == "show-desktop") {
                close (null);
                model.show_desktop ();
            } else {
                close (null);
            }
        }

        private void drop (Drag drag) {
            var window = drag.thumb.item.window;
            unowned Screen? target = screen_at (state.pointer_x, state.pointer_y);
            int band_workspace = -1;
            if (target != null) {
                foreach (var band in target.bands) {
                    var a = band.area;
                    if (state.pointer_x >= a.x && state.pointer_x < a.x + a.width && state.pointer_y >= a.y &&
                        state.pointer_y < a.y + a.height) {
                        band_workspace = band.workspace;
                    }
                }
            }
            if (band_workspace >= 0 && window.workspace != -1 && window.workspace != band_workspace) {
                model.move_to_workspace (window, band_workspace);
            }
            var rect = window.outer_rect ();
            var own = model.output_at (rect.x + rect.width / 2.0, rect.y + rect.height / 2.0);
            if (target != null && target.shown && own != null && !target.output.rect.equal (own.rect)) {
                var from = own.rect;
                var to = target.output.rect;
                int x = to.x + (rect.x - from.x).clamp (0, int.max (to.width - rect.width, 0));
                int y = to.y + (rect.y - from.y).clamp (0, int.max (to.height - rect.height, 0));
                model.move (window, true, x, y);
            }
            state.selected = window;
            rebuild ();
        }

        private void on_grab_ended (bool cancelled) {
            if (state != null) {
                state.grab = null;
                if (!state.closing) {
                    close (null);
                }
            }
        }
    }
}
