/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Hot corners and edges: where they are on an arrangement of screens, and
 * when the pointer fires one. Pressure is the push against the screen edge,
 * at most 15 px an event, motion along the edge not counted, within a time
 * window. Hover is a delay, then a cooldown, and nothing fires while a
 * button is held. Corners between screens and absolute pointers use hover,
 * as nothing stops the pointer there.
 */

namespace Polo.Policy {
    public enum HotSpot {
        TOP_LEFT,
        TOP_RIGHT,
        BOTTOM_LEFT,
        BOTTOM_RIGHT,
        TOP,
        BOTTOM,
        LEFT,
        RIGHT;

        public string to_key () {
            switch (this) {
                case TOP_LEFT: return "top-left";
                case TOP_RIGHT: return "top-right";
                case BOTTOM_LEFT: return "bottom-left";
                case BOTTOM_RIGHT: return "bottom-right";
                case TOP: return "top";
                case BOTTOM: return "bottom";
                case LEFT: return "left";
                default: return "right";
            }
        }

        public bool is_corner () {
            return this <= BOTTOM_RIGHT;
        }

        public int dir_x () {
            switch (this) {
                case TOP_LEFT:
                case BOTTOM_LEFT:
                case LEFT:
                    return -1;
                case TOP_RIGHT:
                case BOTTOM_RIGHT:
                case RIGHT:
                    return 1;
                default:
                    return 0;
            }
        }

        public int dir_y () {
            switch (this) {
                case TOP_LEFT:
                case TOP_RIGHT:
                case TOP:
                    return -1;
                case BOTTOM_LEFT:
                case BOTTOM_RIGHT:
                case BOTTOM:
                    return 1;
                default:
                    return 0;
            }
        }
    }

    public enum HotLayout {
        OUTER,
        EVERY_SCREEN,
        WHOLE_AREA;

        public static HotLayout parse (string text) {
            switch (text) {
                case "every-screen": return EVERY_SCREEN;
                case "whole-area": return WHOLE_AREA;
                default: return OUTER;
            }
        }
    }

    public enum HotTrigger {
        PRESSURE,
        HOVER;

        public static HotTrigger parse (string text) {
            return text == "hover" ? HOVER : PRESSURE;
        }
    }

    public class HotRegion {
        public const int LEG = 10;
        public const int HOVER = 2;

        public HotSpot spot;
        public int screen;
        public int x;
        public int y;
        public int width;
        public int height;
        public bool wall_x;
        public bool wall_y;

        public bool can_push () {
            if (spot.is_corner ()) {
                return wall_x && wall_y;
            }
            return spot.dir_x () != 0 ? wall_x : wall_y;
        }

        public bool hover_contains (int px, int py) {
            if (!spot.is_corner ()) {
                return px >= x && px < x + width && py >= y && py < y + height;
            }
            int dx = (x - px) * spot.dir_x ();
            int dy = (y - py) * spot.dir_y ();
            return dx >= 0 && dx < HOVER && dy >= 0 && dy < HOVER;
        }

        public bool pressure_contains (int px, int py) {
            if (!spot.is_corner ()) {
                return hover_contains (px, py);
            }
            int dx = (x - px) * spot.dir_x ();
            int dy = (y - py) * spot.dir_y ();
            return (dx == 0 && dy >= 0 && dy < LEG) || (dy == 0 && dx >= 0 && dx < LEG);
        }

        public double push (double push_x, double push_y) {
            double amount = 0.0;
            if (spot.dir_x () != 0 && wall_x) {
                amount += double.max (0.0, push_x * spot.dir_x ());
            }
            if (spot.dir_y () != 0 && wall_y) {
                amount += double.max (0.0, push_y * spot.dir_y ());
            }
            return amount;
        }
    }

    public class HotCornerGeometry {
        private static bool on_screen (Marco.Rect[] screens, int px, int py) {
            foreach (unowned Marco.Rect s in screens) {
                if (px >= s.x && px < s.x + s.width && py >= s.y && py < s.y + s.height) {
                    return true;
                }
            }
            return false;
        }

        private static HotRegion corner (Marco.Rect[] screens, int index, HotSpot spot) {
            var s = screens[index];
            var region = new HotRegion () {
                spot = spot,
                screen = index,
                x = spot.dir_x () < 0 ? s.x : s.x + s.width - 1,
                y = spot.dir_y () < 0 ? s.y : s.y + s.height - 1,
                width = 1,
                height = 1
            };
            region.wall_x = !on_screen (screens, region.x + spot.dir_x (), region.y);
            region.wall_y = !on_screen (screens, region.x, region.y + spot.dir_y ());
            return region;
        }

        private static void add_edge_runs (GLib.GenericArray<HotRegion> regions, Marco.Rect[] screens, int index,
                                           HotSpot spot) {
            var s = screens[index];
            bool horizontal = spot.dir_y () != 0;
            int start = horizontal ? s.x : s.y;
            int end = start + (horizontal ? s.width : s.height);
            int line = horizontal ? (spot.dir_y () < 0 ? s.y : s.y + s.height - 1)
                                  : (spot.dir_x () < 0 ? s.x : s.x + s.width - 1);
            int run_start = -1;
            for (int along = start; along <= end; along++) {
                bool free = false;
                if (along < end) {
                    free = horizontal ? !on_screen (screens, along, line + spot.dir_y ())
                                      : !on_screen (screens, line + spot.dir_x (), along);
                }
                if (free && run_start < 0) {
                    run_start = along;
                } else if (!free && run_start >= 0) {
                    var region = new HotRegion () {
                        spot = spot,
                        screen = index,
                        x = horizontal ? run_start : line,
                        y = horizontal ? line : run_start,
                        width = horizontal ? along - run_start : 1,
                        height = horizontal ? 1 : along - run_start,
                        wall_x = !horizontal,
                        wall_y = horizontal
                    };
                    regions.add (region);
                    run_start = -1;
                }
            }
        }

        private static HotRegion? whole_area_corner (Marco.Rect[] screens, HotSpot spot) {
            int left = int.MAX, top = int.MAX, right = int.MIN, bottom = int.MIN;
            foreach (unowned Marco.Rect s in screens) {
                left = int.min (left, s.x);
                top = int.min (top, s.y);
                right = int.max (right, s.x + s.width - 1);
                bottom = int.max (bottom, s.y + s.height - 1);
            }
            int px = spot.dir_x () < 0 ? left : right;
            int py = spot.dir_y () < 0 ? top : bottom;
            HotRegion? best = null;
            int64 best_distance = int64.MAX;
            int64 best_area = -1;
            for (int i = 0; i < screens.length; i++) {
                var candidate = corner (screens, i, spot);
                if (!candidate.wall_x || !candidate.wall_y) {
                    continue;
                }
                int64 dx = candidate.x - px;
                int64 dy = candidate.y - py;
                int64 distance = dx * dx + dy * dy;
                int64 area = (int64) screens[i].width * screens[i].height;
                if (distance < best_distance || (distance == best_distance && area > best_area)) {
                    best = candidate;
                    best_distance = distance;
                    best_area = area;
                }
            }
            return best;
        }

        public static GLib.GenericArray<HotRegion> compute (Marco.Rect[] screens, bool[] allowed, HotLayout layout) {
            var regions = new GLib.GenericArray<HotRegion> ();
            HotSpot[] corners = { HotSpot.TOP_LEFT, HotSpot.TOP_RIGHT, HotSpot.BOTTOM_LEFT, HotSpot.BOTTOM_RIGHT };
            HotSpot[] edges = { HotSpot.TOP, HotSpot.BOTTOM, HotSpot.LEFT, HotSpot.RIGHT };
            if (layout == HotLayout.WHOLE_AREA) {
                foreach (var spot in corners) {
                    var region = whole_area_corner (screens, spot);
                    if (region != null && allowed[region.screen]) {
                        regions.add (region);
                    }
                }
            } else {
                for (int i = 0; i < screens.length; i++) {
                    if (!allowed[i]) {
                        continue;
                    }
                    foreach (var spot in corners) {
                        var region = corner (screens, i, spot);
                        bool keep = layout == HotLayout.OUTER ? region.wall_x && region.wall_y
                                                              : region.wall_x || region.wall_y;
                        if (keep) {
                            regions.add (region);
                        }
                    }
                }
            }
            for (int i = 0; i < screens.length; i++) {
                if (!allowed[i]) {
                    continue;
                }
                foreach (var spot in edges) {
                    add_edge_runs (regions, screens, i, spot);
                }
            }
            return regions;
        }
    }

    public class HotCornerSettings {
        public const int SPOTS = 8;

        public HotTrigger trigger = HotTrigger.PRESSURE;
        public double pressure_threshold = 100.0;
        public int64 pressure_time = 1000;
        public int64[] hover_delay = { 150, 150, 150, 150, 150, 150, 150, 150 };
        public int64 cooldown = 350;
        public bool[] spot_enabled = { false, false, false, false, false, false, false, false };
    }

    public class HotCornerTracker {
        public const double MAX_STEP = 15.0;

        private class Sample {
            public int64 time;
            public double amount;
        }

        private class State {
            public bool inside = false;
            public bool armed = true;
            public int64 hover_start = -1;
            public int64 last_fire = int64.MIN;
            public GLib.GenericArray<Sample> samples = new GLib.GenericArray<Sample> ();
            public double pressure = 0.0;

            public void reset () {
                samples.remove_range (0, samples.length);
                pressure = 0.0;
                hover_start = -1;
            }
        }

        private GLib.GenericArray<HotRegion> regions = new GLib.GenericArray<HotRegion> ();
        private GLib.GenericArray<State> states = new GLib.GenericArray<State> ();
        private HotCornerSettings settings = new HotCornerSettings ();
        private double last_x = double.NAN;
        private double last_y = double.NAN;
        private int current = -1;

        public void configure (GLib.GenericArray<HotRegion> all, HotCornerSettings settings) {
            this.settings = settings;
            regions = new GLib.GenericArray<HotRegion> ();
            states = new GLib.GenericArray<State> ();
            foreach (var region in all) {
                if (settings.spot_enabled[(int) region.spot]) {
                    regions.add (region);
                    states.add (new State ());
                }
            }
            current = -1;
        }

        public unowned GLib.GenericArray<HotRegion> get_regions () {
            return regions;
        }

        public int64 hover_deadline () {
            if (current < 0 || states[current].hover_start < 0) {
                return -1;
            }
            return states[current].hover_start + settings.hover_delay[(int) regions[current].spot];
        }

        private bool uses_pressure (HotRegion region, bool absolute) {
            return settings.trigger == HotTrigger.PRESSURE && !absolute && region.can_push ();
        }

        private int find (int px, int py) {
            for (int i = 0; i < regions.length; i++) {
                if (regions[i].hover_contains (px, py) || regions[i].pressure_contains (px, py)) {
                    return i;
                }
            }
            return -1;
        }

        private HotRegion? fire (int index, int64 now) {
            var state = states[index];
            state.reset ();
            if (!state.armed) {
                return null;
            }
            if (state.last_fire != int64.MIN && now - state.last_fire < settings.cooldown) {
                return null;
            }
            state.armed = false;
            state.last_fire = now;
            return regions[index];
        }

        private void leave (int index) {
            var state = states[index];
            state.inside = false;
            state.armed = true;
            state.reset ();
        }

        public HotRegion? motion (double x, double y, double push_x, double push_y, bool absolute, uint buttons,
                                  int64 now) {
            int px = (int) GLib.Math.floor (x);
            int py = (int) GLib.Math.floor (y);
            double slide = last_x.is_nan () ? 0.0
                         : GLib.Math.sqrt ((x - last_x) * (x - last_x) + (y - last_y) * (y - last_y));
            last_x = x;
            last_y = y;
            int index = find (px, py);
            if (current >= 0 && current != index) {
                leave (current);
            }
            current = index;
            if (index < 0) {
                return null;
            }
            var region = regions[index];
            var state = states[index];
            state.inside = true;
            if (buttons > 0) {
                state.reset ();
                return null;
            }
            if (uses_pressure (region, absolute)) {
                state.hover_start = -1;
                if (!region.pressure_contains (px, py)) {
                    return null;
                }
                double push = region.push (push_x, push_y);
                if (push <= 0.0) {
                    return null;
                }
                if (push >= settings.pressure_threshold) {
                    return fire (index, now);
                }
                if (slide > push) {
                    return null;
                }
                while (state.samples.length > 0 && state.samples[0].time < now - settings.pressure_time) {
                    state.pressure -= state.samples[0].amount;
                    state.samples.remove_index (0);
                }
                double amount = double.min (MAX_STEP, push);
                state.samples.add (new Sample () { time = now, amount = amount });
                state.pressure += amount;
                if (state.pressure >= settings.pressure_threshold) {
                    return fire (index, now);
                }
                return null;
            }
            if (!region.hover_contains (px, py)) {
                state.hover_start = -1;
                return null;
            }
            if (state.hover_start < 0) {
                if (!state.armed) {
                    return null;
                }
                state.hover_start = now;
            }
            if (now - state.hover_start >= settings.hover_delay[(int) region.spot]) {
                return fire (index, now);
            }
            return null;
        }

        public HotRegion? tick (int64 now) {
            int64 deadline = hover_deadline ();
            if (deadline < 0 || now < deadline) {
                return null;
            }
            return fire (current, now);
        }

        public HotRegion? click (double x, double y, int64 now) {
            int px = (int) GLib.Math.floor (x);
            int py = (int) GLib.Math.floor (y);
            for (int i = 0; i < regions.length; i++) {
                if (!regions[i].hover_contains (px, py)) {
                    continue;
                }
                var state = states[i];
                if (state.last_fire != int64.MIN && now - state.last_fire < settings.cooldown) {
                    return null;
                }
                state.reset ();
                state.armed = false;
                state.last_fire = now;
                return regions[i];
            }
            return null;
        }

        public HotRegion? region_at (double x, double y) {
            int px = (int) GLib.Math.floor (x);
            int py = (int) GLib.Math.floor (y);
            foreach (var region in regions) {
                if (region.hover_contains (px, py)) {
                    return region;
                }
            }
            return null;
        }

        public bool covers (double x, double y) {
            return region_at (x, y) != null;
        }
    }
}
