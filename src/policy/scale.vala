/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Scale: which windows each mode shows, which screens show them, and where
 * each window goes. The natural layout keeps windows near their places
 * (KWin, GNOME); the grid lays them out in rows (Compiz, Cinnamon); the
 * grouped layout gives each workspace a band of its own.
 */

namespace Polo.Policy {
    public enum ScaleMode {
        WORKSPACE,
        ALL_WORKSPACES,
        APPLICATION,
        APPLICATION_ALL_WORKSPACES;

        public bool all_workspaces () {
            return this == ALL_WORKSPACES || this == APPLICATION_ALL_WORKSPACES;
        }

        public bool application () {
            return this == APPLICATION || this == APPLICATION_ALL_WORKSPACES;
        }

        public unowned string to_key () {
            switch (this) {
                case ALL_WORKSPACES: return "scale-all-workspaces";
                case APPLICATION: return "scale-application";
                case APPLICATION_ALL_WORKSPACES: return "scale-application-all-workspaces";
                default: return "scale";
            }
        }

        public unowned string to_mode_name () {
            switch (this) {
                case ALL_WORKSPACES: return "all-workspaces";
                case APPLICATION: return "application";
                case APPLICATION_ALL_WORKSPACES: return "application-all-workspaces";
                default: return "workspace";
            }
        }

        public static bool from_key (string key, out ScaleMode mode) {
            foreach (var candidate in all ()) {
                if (candidate.to_key () == key) {
                    mode = candidate;
                    return true;
                }
            }
            mode = WORKSPACE;
            return false;
        }

        public static ScaleMode[] all () {
            return { WORKSPACE, ALL_WORKSPACES, APPLICATION, APPLICATION_ALL_WORKSPACES };
        }
    }

    public enum ScaleScreens {
        SUMMONED,
        EACH,
        MIRRORED;

        public static ScaleScreens parse (string? text, ScaleScreens fallback = EACH) {
            switch (text) {
                case "summoned": return SUMMONED;
                case "each": return EACH;
                case "mirrored": return MIRRORED;
                default: return fallback;
            }
        }
    }

    public enum ScaleArrangement {
        NATURAL,
        GRID;

        public static ScaleArrangement parse (string? text) {
            return text == "grid" ? GRID : NATURAL;
        }
    }

    public class ScaleSlot {
        public int index;
        public Marco.Rect source;
        public double x;
        public double y;
        public double scale;

        public double width () {
            return source.width * scale;
        }

        public double height () {
            return source.height * scale;
        }

        public bool contains (double px, double py) {
            return px >= x && px < x + width () && py >= y && py < y + height ();
        }

        public double centre_x () {
            return x + width () / 2.0;
        }

        public double centre_y () {
            return y + height () / 2.0;
        }
    }

    public class ScaleBand {
        public int workspace;
        public Marco.Rect area;
        public GLib.GenericArray<ScaleSlot> slots = new GLib.GenericArray<ScaleSlot> ();
    }

    namespace ScaleLayout {
        private const int MAX_PASSES = 300;

        private class Box {
            public int index;
            public double x;
            public double y;
            public double width;
            public double height;
        }

        public GLib.GenericArray<ScaleSlot> arrange (ScaleArrangement arrangement, Marco.Rect area,
                                                     Marco.Rect[] windows, int spacing) {
            if (arrangement == ScaleArrangement.GRID) {
                return grid (area, windows, spacing);
            }
            return natural (area, windows, spacing);
        }

        private static Marco.Rect inset (Marco.Rect area, int spacing) {
            return Marco.Rect () {
                x = area.x + spacing, y = area.y + spacing,
                width = int.max (area.width - 2 * spacing, 1), height = int.max (area.height - 2 * spacing, 1)
            };
        }

        public GLib.GenericArray<ScaleSlot> natural (Marco.Rect area, Marco.Rect[] windows, int spacing) {
            var slots = new GLib.GenericArray<ScaleSlot> ();
            if (windows.length == 0) {
                return slots;
            }
            var room = inset (area, spacing);
            double half = int.max (spacing, 0) / 2.0;
            var boxes = new Box[windows.length];
            for (int i = 0; i < windows.length; i++) {
                boxes[i] = new Box () {
                    index = i,
                    x = windows[i].x - half, y = windows[i].y - half,
                    width = int.max (windows[i].width, 1) + 2 * half,
                    height = int.max (windows[i].height, 1) + 2 * half
                };
            }
            for (int pass = 0; pass < MAX_PASSES; pass++) {
                bool moved = false;
                for (int i = 0; i < boxes.length; i++) {
                    for (int j = i + 1; j < boxes.length; j++) {
                        moved |= separate (boxes[i], boxes[j]);
                    }
                }
                if (!moved) {
                    break;
                }
            }
            double left = double.MAX, top = double.MAX, right = -double.MAX, bottom = -double.MAX;
            foreach (var box in boxes) {
                left = double.min (left, box.x);
                top = double.min (top, box.y);
                right = double.max (right, box.x + box.width);
                bottom = double.max (bottom, box.y + box.height);
            }
            double scale = double.min (1.0, double.min (room.width / (right - left), room.height / (bottom - top)));
            double offset_x = room.x + (room.width - (right - left) * scale) / 2.0;
            double offset_y = room.y + (room.height - (bottom - top) * scale) / 2.0;
            foreach (var box in boxes) {
                slots.add (new ScaleSlot () {
                    index = box.index, source = windows[box.index], scale = scale,
                    x = offset_x + (box.x + half - left) * scale,
                    y = offset_y + (box.y + half - top) * scale
                });
            }
            return slots;
        }

        private static bool separate (Box a, Box b) {
            double overlap_x = double.min (a.x + a.width, b.x + b.width) - double.max (a.x, b.x);
            double overlap_y = double.min (a.y + a.height, b.y + b.height) - double.max (a.y, b.y);
            if (overlap_x <= 0.0 || overlap_y <= 0.0) {
                return false;
            }
            double dx = (b.x + b.width / 2.0) - (a.x + a.width / 2.0);
            double dy = (b.y + b.height / 2.0) - (a.y + a.height / 2.0);
            if (overlap_x <= overlap_y) {
                double sign = dx > 0.0 || (dx == 0.0 && a.index < b.index) ? 1.0 : -1.0;
                a.x -= sign * overlap_x / 2.0;
                b.x += sign * overlap_x / 2.0;
            } else {
                double sign = dy > 0.0 || (dy == 0.0 && a.index < b.index) ? 1.0 : -1.0;
                a.y -= sign * overlap_y / 2.0;
                b.y += sign * overlap_y / 2.0;
            }
            return true;
        }

        public GLib.GenericArray<ScaleSlot> grid (Marco.Rect area, Marco.Rect[] windows, int spacing) {
            var slots = new GLib.GenericArray<ScaleSlot> ();
            int n = windows.length;
            if (n == 0) {
                return slots;
            }
            spacing = int.max (spacing, 0);
            int best_columns = 1;
            double best_area = -1.0;
            for (int columns = 1; columns <= n; columns++) {
                int rows = (n + columns - 1) / columns;
                double cell_w = (area.width - (columns + 1) * spacing) / (double) columns;
                double cell_h = (area.height - (rows + 1) * spacing) / (double) rows;
                if (cell_w <= 0.0 || cell_h <= 0.0) {
                    continue;
                }
                double shown = 0.0;
                foreach (var window in windows) {
                    double s = fit (window, cell_w, cell_h);
                    shown += window.width * s * window.height * s;
                }
                if (shown > best_area * 1.0001) {
                    best_area = shown;
                    best_columns = columns;
                }
            }
            int columns = best_columns;
            int rows = (n + columns - 1) / columns;
            double cell_w = double.max ((area.width - (columns + 1) * spacing) / (double) columns, 1.0);
            double cell_h = double.max ((area.height - (rows + 1) * spacing) / (double) rows, 1.0);
            for (int i = 0; i < n; i++) {
                int row = i / columns;
                int column = i % columns;
                int in_row = row == rows - 1 ? n - row * columns : columns;
                double row_left = area.x + (area.width - in_row * cell_w - (in_row + 1) * spacing) / 2.0;
                double cell_x = row_left + spacing + column * (cell_w + spacing);
                double cell_y = area.y + spacing + row * (cell_h + spacing);
                double s = fit (windows[i], cell_w, cell_h);
                slots.add (new ScaleSlot () {
                    index = i, source = windows[i], scale = s,
                    x = cell_x + (cell_w - windows[i].width * s) / 2.0,
                    y = cell_y + (cell_h - windows[i].height * s) / 2.0
                });
            }
            return slots;
        }

        private static double fit (Marco.Rect window, double width, double height) {
            return double.min (1.0, double.min (width / int.max (window.width, 1),
                                                height / int.max (window.height, 1)));
        }

        public GLib.GenericArray<ScaleBand> grouped (ScaleArrangement arrangement, Marco.Rect area,
                                                     int[] workspaces, Marco.Rect[] windows,
                                                     int[] window_workspaces, int spacing, int label_room) {
            var bands = new GLib.GenericArray<ScaleBand> ();
            int total = 0;
            var counts = new int[workspaces.length];
            for (int b = 0; b < workspaces.length; b++) {
                foreach (int w in window_workspaces) {
                    if (w == workspaces[b]) {
                        counts[b]++;
                    }
                }
                total += counts[b];
            }
            if (total == 0) {
                return bands;
            }
            int used = 0;
            foreach (int c in counts) {
                used += c > 0 ? 1 : 0;
            }
            double free_height = double.max (area.height - used * label_room, used);
            double y = area.y;
            for (int b = 0; b < workspaces.length; b++) {
                if (counts[b] == 0) {
                    continue;
                }
                double share = double.max (GLib.Math.sqrt (counts[b]), 1.0);
                double all_shares = 0.0;
                foreach (int c in counts) {
                    all_shares += c > 0 ? double.max (GLib.Math.sqrt (c), 1.0) : 0.0;
                }
                int height = (int) GLib.Math.floor (free_height * share / all_shares);
                var band = new ScaleBand () {
                    workspace = workspaces[b],
                    area = Marco.Rect () {
                        x = area.x, y = (int) GLib.Math.round (y + label_room), width = area.width,
                        height = int.max (height, 1)
                    }
                };
                Marco.Rect[] members = {};
                var indices = new GLib.Array<int> ();
                for (int i = 0; i < windows.length; i++) {
                    if (window_workspaces[i] == workspaces[b]) {
                        members += windows[i];
                        indices.append_val (i);
                    }
                }
                var local = arrangement == ScaleArrangement.GRID
                            ? grid (band.area, members, spacing)
                            : natural (band.area, members, spacing);
                foreach (var slot in local) {
                    slot.index = indices.index (slot.index);
                    band.slots.add (slot);
                }
                bands.add (band);
                y += label_room + height;
            }
            return bands;
        }

    }

    namespace ScaleScreensOf {
        private static int64 overlap (Marco.Rect a, Marco.Rect b) {
            int64 w = int.min (a.x + a.width, b.x + b.width) - int.max (a.x, b.x);
            int64 h = int.min (a.y + a.height, b.y + b.height) - int.max (a.y, b.y);
            return w > 0 && h > 0 ? w * h : 0;
        }

        public int screen_of (Marco.Rect window, Marco.Rect[] screens) {
            int best = -1;
            int64 best_overlap = 0;
            for (int i = 0; i < screens.length; i++) {
                int64 o = overlap (window, screens[i]);
                if (o > best_overlap) {
                    best_overlap = o;
                    best = i;
                }
            }
            if (best >= 0 || screens.length == 0) {
                return best;
            }
            double cx = window.x + window.width / 2.0, cy = window.y + window.height / 2.0;
            double nearest = double.MAX;
            for (int i = 0; i < screens.length; i++) {
                double sx = cx.clamp (screens[i].x, screens[i].x + screens[i].width);
                double sy = cy.clamp (screens[i].y, screens[i].y + screens[i].height);
                double d = (sx - cx) * (sx - cx) + (sy - cy) * (sy - cy);
                if (d < nearest) {
                    nearest = d;
                    best = i;
                }
            }
            return best;
        }

        public int[] distinct (Marco.Rect[] screens) {
            int[] kept = {};
            for (int i = 0; i < screens.length; i++) {
                bool seen = false;
                foreach (int k in kept) {
                    if (screens[k].equal (screens[i])) {
                        seen = true;
                        break;
                    }
                }
                if (!seen) {
                    kept += i;
                }
            }
            return kept;
        }

        public GLib.GenericArray<GLib.Array<int>> distribute (ScaleScreens where, bool every_screen, int summoned,
                                                             Marco.Rect[] windows, Marco.Rect[] screens) {
            var lists = new GLib.GenericArray<GLib.Array<int>> ();
            for (int s = 0; s < screens.length; s++) {
                lists.add (new GLib.Array<int> ());
            }
            if (screens.length == 0) {
                return lists;
            }
            summoned = summoned.clamp (0, screens.length - 1);
            for (int i = 0; i < windows.length; i++) {
                int own = screen_of (windows[i], screens);
                switch (where) {
                    case ScaleScreens.MIRRORED:
                        for (int s = 0; s < screens.length; s++) {
                            lists[s].append_val (i);
                        }
                        break;
                    case ScaleScreens.SUMMONED:
                        if (every_screen || own == summoned) {
                            lists[summoned].append_val (i);
                        }
                        break;
                    default:
                        lists[own].append_val (i);
                        break;
                }
            }
            return lists;
        }

        public bool shows (ScaleScreens where, int screen, int summoned) {
            return where != ScaleScreens.SUMMONED || screen == summoned;
        }
    }

    namespace ScaleNavigation {
        public int nearest (double[] centres_x, double[] centres_y, int current, int dx, int dy) {
            int best = -1;
            double best_cost = double.MAX;
            if (current < 0 || current >= centres_x.length) {
                return centres_x.length > 0 ? 0 : -1;
            }
            double cx = centres_x[current], cy = centres_y[current];
            for (int i = 0; i < centres_x.length; i++) {
                if (i == current) {
                    continue;
                }
                double along = (centres_x[i] - cx) * dx + (centres_y[i] - cy) * dy;
                double across = dx != 0 ? (centres_y[i] - cy).abs () : (centres_x[i] - cx).abs ();
                if (along <= 0.5) {
                    continue;
                }
                double cost = along + 2.0 * across;
                if (cost < best_cost) {
                    best_cost = cost;
                    best = i;
                }
            }
            return best;
        }
    }

    namespace ScaleWindows {
        public bool same_application (Window a, Window b) {
            if (a == b) {
                return true;
            }
            if (a.app_id != null && a.app_id != "" && b.app_id != null && b.app_id != "") {
                return a.app_id == b.app_id;
            }
            return a.group != 0 && a.group == b.group;
        }

        public GLib.GenericArray<Window> pick (Model model, ScaleMode mode, bool minimized, Window? like) {
            var type = mode.all_workspaces () ? TabList.NORMAL_ALL_WORKSPACES : TabList.NORMAL;
            var listed = TabChain.get_tab_list (model, type, model.current_workspace);
            var windows = new GLib.GenericArray<Window> ();
            foreach (var w in listed) {
                if (!minimized && w.has_state (WindowState.MINIMIZED)) {
                    continue;
                }
                if (mode.application () && (like == null || !same_application (w, like))) {
                    continue;
                }
                if (windows.find (w)) {
                    continue;
                }
                windows.add (w);
            }
            var kept = new GLib.GenericArray<Window> ();
            foreach (var w in windows) {
                bool rides = false;
                for (var parent = w.transient_for; parent != null; parent = parent.transient_for) {
                    if (windows.find (parent)) {
                        rides = true;
                        break;
                    }
                }
                if (!rides) {
                    kept.add (w);
                }
            }
            return kept;
        }

        public GLib.GenericArray<Window> family (Model model, Window window) {
            var members = new GLib.GenericArray<Window> ();
            members.add (window);
            foreach (var w in model.get_stack ()) {
                if (w == window || w.has_state (WindowState.MINIMIZED) != window.has_state (WindowState.MINIMIZED)) {
                    continue;
                }
                for (var parent = w.transient_for; parent != null; parent = parent.transient_for) {
                    if (parent == window) {
                        members.add (w);
                        break;
                    }
                }
            }
            return members;
        }
    }
}
