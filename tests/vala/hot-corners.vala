/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Hot corner geometry for every layout and screen arrangement, and the
 * tracker's pressure, hover, cooldown, re-arm and click rules.
 */

using Polo;
using Polo.Policy;

Marco.Rect rect (int x, int y, int width, int height) {
    return Marco.Rect () { x = x, y = y, width = width, height = height };
}

bool[] all_allowed (int n) {
    var allowed = new bool[n];
    for (int i = 0; i < n; i++) {
        allowed[i] = true;
    }
    return allowed;
}

int count_corners (GLib.GenericArray<HotRegion> regions) {
    int n = 0;
    foreach (var r in regions) {
        if (r.spot.is_corner ()) {
            n++;
        }
    }
    return n;
}

HotRegion? find (GLib.GenericArray<HotRegion> regions, HotSpot spot, int screen) {
    foreach (var r in regions) {
        if (r.spot == spot && r.screen == screen) {
            return r;
        }
    }
    return null;
}

GLib.GenericArray<HotRegion> edges_of (GLib.GenericArray<HotRegion> regions, HotSpot spot) {
    var found = new GLib.GenericArray<HotRegion> ();
    foreach (var r in regions) {
        if (r.spot == spot) {
            found.add (r);
        }
    }
    return found;
}

void test_single () {
    Marco.Rect[] screens = { rect (0, 0, 1024, 768) };
    foreach (var layout in new HotLayout[] { HotLayout.OUTER, HotLayout.EVERY_SCREEN, HotLayout.WHOLE_AREA }) {
        var regions = HotCornerGeometry.compute (screens, all_allowed (1), layout);
        assert (count_corners (regions) == 4);
        var tl = find (regions, HotSpot.TOP_LEFT, 0);
        assert (tl.x == 0 && tl.y == 0 && tl.can_push ());
        var br = find (regions, HotSpot.BOTTOM_RIGHT, 0);
        assert (br.x == 1023 && br.y == 767 && br.can_push ());
        assert (find (regions, HotSpot.TOP_RIGHT, 0).x == 1023);
        assert (find (regions, HotSpot.BOTTOM_LEFT, 0).y == 767);
        var top = edges_of (regions, HotSpot.TOP);
        assert (top.length == 1 && top[0].width == 1024 && top[0].y == 0);
        var right = edges_of (regions, HotSpot.RIGHT);
        assert (right.length == 1 && right[0].x == 1023 && right[0].height == 768);
    }
}

void test_side_by_side () {
    Marco.Rect[] screens = { rect (0, 0, 1000, 800), rect (1000, 0, 1000, 800) };
    var outer = HotCornerGeometry.compute (screens, all_allowed (2), HotLayout.OUTER);
    assert (count_corners (outer) == 4);
    assert (find (outer, HotSpot.TOP_LEFT, 0) != null);
    assert (find (outer, HotSpot.BOTTOM_LEFT, 0) != null);
    assert (find (outer, HotSpot.TOP_RIGHT, 1) != null);
    assert (find (outer, HotSpot.BOTTOM_RIGHT, 1) != null);
    assert (find (outer, HotSpot.TOP_RIGHT, 0) == null);
    assert (find (outer, HotSpot.TOP_LEFT, 1) == null);
    assert (edges_of (outer, HotSpot.TOP).length == 2);
    assert (edges_of (outer, HotSpot.LEFT).length == 1 && edges_of (outer, HotSpot.LEFT)[0].screen == 0);
    assert (edges_of (outer, HotSpot.RIGHT).length == 1 && edges_of (outer, HotSpot.RIGHT)[0].screen == 1);

    var every = HotCornerGeometry.compute (screens, all_allowed (2), HotLayout.EVERY_SCREEN);
    assert (count_corners (every) == 8);
    var seam = find (every, HotSpot.TOP_RIGHT, 0);
    assert (seam.x == 999 && seam.y == 0 && !seam.wall_x && seam.wall_y && !seam.can_push ());
    assert (!find (every, HotSpot.BOTTOM_LEFT, 1).can_push ());
    assert (find (every, HotSpot.TOP_LEFT, 0).can_push ());

    var whole = HotCornerGeometry.compute (screens, all_allowed (2), HotLayout.WHOLE_AREA);
    assert (count_corners (whole) == 4);
    assert (find (whole, HotSpot.TOP_RIGHT, 1).x == 1999);
}

void test_grid () {
    Marco.Rect[] screens = { rect (0, 0, 800, 600), rect (800, 0, 800, 600),
                             rect (0, 600, 800, 600), rect (800, 600, 800, 600) };
    assert (count_corners (HotCornerGeometry.compute (screens, all_allowed (4), HotLayout.OUTER)) == 4);
    var every = HotCornerGeometry.compute (screens, all_allowed (4), HotLayout.EVERY_SCREEN);
    assert (count_corners (every) == 12);
    assert (find (every, HotSpot.BOTTOM_RIGHT, 0) == null);
    assert (find (every, HotSpot.TOP_LEFT, 3) == null);
    assert (count_corners (HotCornerGeometry.compute (screens, all_allowed (4), HotLayout.WHOLE_AREA)) == 4);
}

void test_step () {
    Marco.Rect[] screens = { rect (0, 0, 1920, 1200), rect (1920, 0, 1920, 1080) };
    var outer = HotCornerGeometry.compute (screens, all_allowed (2), HotLayout.OUTER);
    assert (count_corners (outer) == 5);
    var step = find (outer, HotSpot.BOTTOM_RIGHT, 0);
    assert (step != null && step.x == 1919 && step.y == 1199 && step.can_push ());
    assert (find (outer, HotSpot.BOTTOM_LEFT, 1) == null);
    assert (find (outer, HotSpot.BOTTOM_RIGHT, 1) != null);
    var right = edges_of (outer, HotSpot.RIGHT);
    assert (right.length == 2);
    foreach (var r in right) {
        if (r.screen == 0) {
            assert (r.x == 1919 && r.y == 1080 && r.height == 120);
        } else {
            assert (r.x == 3839 && r.y == 0 && r.height == 1080);
        }
    }
    assert (edges_of (outer, HotSpot.LEFT).length == 1);

    var whole = HotCornerGeometry.compute (screens, all_allowed (2), HotLayout.WHOLE_AREA);
    assert (count_corners (whole) == 4);
    var br = find (whole, HotSpot.BOTTOM_RIGHT, 1);
    assert (br != null && br.x == 3839 && br.y == 1079);
    assert (find (whole, HotSpot.BOTTOM_RIGHT, 0) == null);
    assert (find (whole, HotSpot.BOTTOM_LEFT, 0) != null);
}

void test_l_shape () {
    Marco.Rect[] screens = { rect (0, 0, 1000, 1000), rect (1000, 0, 1000, 1000), rect (0, 1000, 1000, 1000) };
    var outer = HotCornerGeometry.compute (screens, all_allowed (3), HotLayout.OUTER);
    assert (count_corners (outer) == 5);
    assert (find (outer, HotSpot.BOTTOM_RIGHT, 1) != null);
    assert (find (outer, HotSpot.BOTTOM_RIGHT, 2) != null);
    assert (find (outer, HotSpot.BOTTOM_LEFT, 1) == null);
    assert (find (outer, HotSpot.TOP_RIGHT, 2) == null);
    var whole = HotCornerGeometry.compute (screens, all_allowed (3), HotLayout.WHOLE_AREA);
    assert (count_corners (whole) == 4);
    assert (find (whole, HotSpot.BOTTOM_RIGHT, 1) != null);
}

void test_negative () {
    Marco.Rect[] screens = { rect (-1024, -100, 1024, 768), rect (0, 0, 1280, 1024) };
    var outer = HotCornerGeometry.compute (screens, all_allowed (2), HotLayout.OUTER);
    var tl = find (outer, HotSpot.TOP_LEFT, 0);
    assert (tl.x == -1024 && tl.y == -100);
    assert (find (outer, HotSpot.TOP_LEFT, 1) == null);
    assert (find (outer, HotSpot.BOTTOM_LEFT, 1) != null);
    assert (find (outer, HotSpot.TOP_RIGHT, 0) != null);
}

void test_allowed () {
    Marco.Rect[] screens = { rect (0, 0, 1000, 800), rect (1000, 0, 1000, 800) };
    bool[] allowed = { true, false };
    var outer = HotCornerGeometry.compute (screens, allowed, HotLayout.OUTER);
    foreach (var r in outer) {
        assert (r.screen == 0);
    }
    assert (count_corners (outer) == 2);
    assert (find (outer, HotSpot.TOP_RIGHT, 0) == null);
    var every = HotCornerGeometry.compute (screens, allowed, HotLayout.EVERY_SCREEN);
    assert (count_corners (every) == 4);
    var whole = HotCornerGeometry.compute (screens, allowed, HotLayout.WHOLE_AREA);
    assert (count_corners (whole) == 2);
    bool[] none = { false, false };
    assert (HotCornerGeometry.compute (screens, none, HotLayout.OUTER).length == 0);
}

void test_regions () {
    Marco.Rect[] screens = { rect (0, 0, 1024, 768) };
    var regions = HotCornerGeometry.compute (screens, all_allowed (1), HotLayout.OUTER);
    var tl = find (regions, HotSpot.TOP_LEFT, 0);
    assert (tl.hover_contains (0, 0) && tl.hover_contains (1, 1) && !tl.hover_contains (2, 0));
    assert (tl.pressure_contains (9, 0) && tl.pressure_contains (0, 9) && !tl.pressure_contains (10, 0));
    assert (!tl.pressure_contains (1, 1));
    var br = find (regions, HotSpot.BOTTOM_RIGHT, 0);
    assert (br.hover_contains (1022, 766) && !br.hover_contains (1021, 767));
    assert (tl.push (-5, -3) == 8 && tl.push (5, 3) == 0);
    var top = edges_of (regions, HotSpot.TOP)[0];
    assert (top.push (-20, -4) == 4);
    assert (HotSpot.BOTTOM_LEFT.to_key () == "bottom-left" && HotSpot.RIGHT.to_key () == "right");
    assert (HotLayout.parse ("whole-area") == HotLayout.WHOLE_AREA);
    assert (HotLayout.parse ("every-screen") == HotLayout.EVERY_SCREEN);
    assert (HotLayout.parse ("x") == HotLayout.OUTER);
    assert (HotTrigger.parse ("hover") == HotTrigger.HOVER && HotTrigger.parse ("pressure") == HotTrigger.PRESSURE);
}

HotCornerTracker tracker_for (Marco.Rect[] screens, HotLayout layout, HotCornerSettings settings) {
    var tracker = new HotCornerTracker ();
    tracker.configure (HotCornerGeometry.compute (screens, all_allowed (screens.length), layout), settings);
    return tracker;
}

HotCornerSettings settings_with (HotSpot[] spots) {
    var settings = new HotCornerSettings ();
    foreach (var spot in spots) {
        settings.spot_enabled[(int) spot] = true;
    }
    return settings;
}

void test_pressure () {
    Marco.Rect[] screens = { rect (0, 0, 1024, 768) };
    var tracker = tracker_for (screens, HotLayout.OUTER, settings_with ({ HotSpot.TOP_LEFT }));
    assert (tracker.get_regions ().length == 1);
    for (int i = 0; i < 9; i++) {
        assert (tracker.motion (0, 0, -10, 0, false, 0, i * 10) == null);
    }
    var fired = tracker.motion (0, 0, -10, 0, false, 0, 90);
    assert (fired != null && fired.spot == HotSpot.TOP_LEFT);
    assert (tracker.motion (0, 0, -50, -50, false, 0, 100) == null);
    assert (tracker.motion (0, 0, -500, 0, false, 0, 110) == null);

    tracker = tracker_for (screens, HotLayout.OUTER, settings_with ({ HotSpot.TOP_LEFT }));
    assert (tracker.motion (0, 0, -60, 0, false, 0, 0) == null);
    for (int i = 1; i < 6; i++) {
        assert (tracker.motion (0, 0, -60, 0, false, 0, i) == null);
    }
    assert (tracker.motion (0, 0, -60, 0, false, 0, 6) != null);

    tracker = tracker_for (screens, HotLayout.OUTER, settings_with ({ HotSpot.TOP_LEFT }));
    assert (tracker.motion (0, 0, -100, 0, false, 0, 0) != null);

    tracker = tracker_for (screens, HotLayout.OUTER, settings_with ({ HotSpot.TOP_LEFT }));
    assert (tracker.motion (5, 0, 0, -10, false, 0, 0) == null);
    for (int i = 1; i < 30; i++) {
        assert (tracker.motion (i % 2 == 0 ? 0 : 9, 0, 0, -5, false, 0, i) == null);
    }

    var slow = settings_with ({ HotSpot.TOP_LEFT });
    slow.pressure_time = 1000;
    tracker = tracker_for (screens, HotLayout.OUTER, slow);
    for (int i = 0; i < 30; i++) {
        assert (tracker.motion (0, 0, -10, 0, false, 0, i * 200) == null);
    }

    var light = settings_with ({ HotSpot.TOP_LEFT });
    light.pressure_threshold = 20;
    tracker = tracker_for (screens, HotLayout.OUTER, light);
    assert (tracker.motion (0, 0, -10, 0, false, 0, 0) == null);
    assert (tracker.motion (0, 0, -10, 0, false, 0, 1) != null);

    tracker = tracker_for (screens, HotLayout.OUTER, settings_with ({ HotSpot.TOP_LEFT }));
    for (int i = 0; i < 20; i++) {
        assert (tracker.motion (0, 0, -10, 0, false, 1, i) == null);
    }
    tracker = tracker_for (screens, HotLayout.OUTER, settings_with ({ HotSpot.TOP_LEFT }));
    for (int i = 0; i < 20; i++) {
        assert (tracker.motion (0, 0, 0, 0, false, 0, i * 100) == null);
    }
    assert (tracker.hover_deadline () == -1);
}

void test_rearm_and_cooldown () {
    Marco.Rect[] screens = { rect (0, 0, 1024, 768) };
    var tracker = tracker_for (screens, HotLayout.OUTER, settings_with ({ HotSpot.TOP_LEFT }));
    assert (tracker.motion (0, 0, -100, 0, false, 0, 0) != null);
    assert (tracker.motion (0, 0, -100, 0, false, 0, 1000) == null);
    assert (tracker.motion (500, 500, 0, 0, false, 0, 1001) == null);
    assert (tracker.motion (0, 0, -100, 0, false, 0, 1002) != null);
    assert (tracker.motion (500, 500, 0, 0, false, 0, 1003) == null);
    assert (tracker.motion (0, 0, -100, 0, false, 0, 1100) == null);
    assert (tracker.motion (500, 500, 0, 0, false, 0, 1101) == null);
    assert (tracker.motion (0, 0, -100, 0, false, 0, 1352) != null);

    var none = settings_with ({ HotSpot.TOP_LEFT });
    none.cooldown = 0;
    tracker = tracker_for (screens, HotLayout.OUTER, none);
    assert (tracker.motion (0, 0, -100, 0, false, 0, 0) != null);
    assert (tracker.motion (500, 500, 0, 0, false, 0, 1) == null);
    assert (tracker.motion (0, 0, -100, 0, false, 0, 2) != null);
}

void test_hover () {
    Marco.Rect[] screens = { rect (0, 0, 1024, 768) };
    var hover = settings_with ({ HotSpot.TOP_LEFT, HotSpot.BOTTOM_RIGHT });
    hover.trigger = HotTrigger.HOVER;
    var tracker = tracker_for (screens, HotLayout.OUTER, hover);
    assert (tracker.motion (1, 1, 0, 0, false, 0, 1000) == null);
    assert (tracker.hover_deadline () == 1150);
    assert (tracker.tick (1149) == null);
    var fired = tracker.tick (1150);
    assert (fired != null && fired.spot == HotSpot.TOP_LEFT);
    assert (tracker.hover_deadline () == -1);
    assert (tracker.motion (0, 0, 0, 0, false, 0, 2000) == null);
    assert (tracker.tick (3000) == null);

    assert (tracker.motion (1023, 767, 0, 0, false, 0, 3000) == null);
    assert (tracker.motion (500, 500, 0, 0, false, 0, 3100) == null);
    assert (tracker.tick (3200) == null);
    assert (tracker.motion (1022, 767, 0, 0, false, 0, 4000) == null);
    assert (tracker.motion (1023, 766, 0, 0, false, 0, 4100) == null);
    assert (tracker.motion (1023, 767, 0, 0, false, 0, 4150) != null);

    assert (tracker.motion (5, 0, 0, 0, false, 0, 5000) == null);
    assert (tracker.hover_deadline () == -1);

    tracker = tracker_for (screens, HotLayout.OUTER, hover);
    assert (tracker.motion (0, 0, 0, 0, false, 2, 0) == null);
    assert (tracker.tick (1000) == null);

    var zero = settings_with ({ HotSpot.TOP_LEFT, HotSpot.TOP_RIGHT });
    zero.trigger = HotTrigger.HOVER;
    zero.hover_delay[(int) HotSpot.TOP_LEFT] = 0;
    tracker = tracker_for (screens, HotLayout.OUTER, zero);
    assert (tracker.motion (0, 0, 0, 0, false, 0, 0) != null);
    assert (tracker.motion (1023, 0, 0, 0, false, 0, 10) == null);
    assert (tracker.hover_deadline () == 160);
}

void test_pressure_falls_back () {
    Marco.Rect[] screens = { rect (0, 0, 1024, 768) };
    var tracker = tracker_for (screens, HotLayout.OUTER, settings_with ({ HotSpot.TOP_LEFT }));
    assert (tracker.motion (0, 0, -300, -300, true, 0, 0) == null);
    assert (tracker.hover_deadline () == 150);
    assert (tracker.tick (150) != null);

    Marco.Rect[] two = { rect (0, 0, 1000, 800), rect (1000, 0, 1000, 800) };
    tracker = tracker_for (two, HotLayout.EVERY_SCREEN, settings_with ({ HotSpot.TOP_RIGHT }));
    assert (tracker.get_regions ().length == 2);
    assert (tracker.motion (999, 0, 0, -100, false, 0, 0) == null);
    assert (tracker.hover_deadline () == 150);
    assert (tracker.tick (150).screen == 0);
    assert (tracker.motion (1999, 0, 0, -100, false, 0, 1000) != null);
}

void test_edges () {
    Marco.Rect[] screens = { rect (0, 0, 1024, 768) };
    var tracker = tracker_for (screens, HotLayout.OUTER, settings_with ({ HotSpot.TOP }));
    assert (tracker.motion (500, 0, -100, 0, false, 0, 0) == null);
    var fired = tracker.motion (500, 0, 0, -100, false, 0, 1);
    assert (fired != null && fired.spot == HotSpot.TOP);
    tracker = tracker_for (screens, HotLayout.OUTER, settings_with ({ HotSpot.TOP, HotSpot.TOP_LEFT }));
    fired = tracker.motion (3, 0, 0, -100, false, 0, 0);
    assert (fired != null && fired.spot == HotSpot.TOP_LEFT);
    tracker = tracker_for (screens, HotLayout.OUTER, settings_with ({ HotSpot.RIGHT, HotSpot.BOTTOM, HotSpot.LEFT }));
    assert (tracker.motion (1023, 300, 100, 0, false, 0, 0).spot == HotSpot.RIGHT);
    assert (tracker.motion (300, 767, 0, 100, false, 0, 1).spot == HotSpot.BOTTOM);
    assert (tracker.motion (0, 300, -100, 0, false, 0, 2).spot == HotSpot.LEFT);
    var hover = settings_with ({ HotSpot.BOTTOM });
    hover.trigger = HotTrigger.HOVER;
    hover.hover_delay[(int) HotSpot.BOTTOM] = 0;
    tracker = tracker_for (screens, HotLayout.OUTER, hover);
    assert (tracker.motion (300, 766, 0, 0, false, 0, 0) == null);
    assert (tracker.motion (300, 767, 0, 0, false, 0, 1) != null);
}

void test_disabled () {
    Marco.Rect[] screens = { rect (0, 0, 1024, 768) };
    var tracker = tracker_for (screens, HotLayout.OUTER, settings_with ({}));
    assert (tracker.get_regions ().length == 0);
    assert (tracker.motion (0, 0, -1000, -1000, false, 0, 0) == null);
    assert (!tracker.covers (0, 0));
    assert (tracker.click (0, 0, 0) == null);
}

void test_click () {
    Marco.Rect[] screens = { rect (0, 0, 1024, 768) };
    var tracker = tracker_for (screens, HotLayout.OUTER, settings_with ({ HotSpot.TOP_RIGHT }));
    assert (tracker.covers (1023, 0) && tracker.covers (1022, 1) && !tracker.covers (1021, 0));
    assert (tracker.region_at (1023, 0).spot == HotSpot.TOP_RIGHT && tracker.region_at (0, 0) == null);
    assert (tracker.click (500, 500, 0) == null);
    var fired = tracker.click (1023, 0, 1000);
    assert (fired != null && fired.spot == HotSpot.TOP_RIGHT);
    assert (tracker.click (1023, 0, 1100) == null);
    assert (tracker.click (1023, 0, 1400) != null);
    assert (tracker.motion (1023, 0, 100, 0, false, 0, 1500) == null);
}

int main (string[] args) {
    GLib.Test.init (ref args);
    GLib.Test.add_func ("/hot-corners/single", test_single);
    GLib.Test.add_func ("/hot-corners/side-by-side", test_side_by_side);
    GLib.Test.add_func ("/hot-corners/grid", test_grid);
    GLib.Test.add_func ("/hot-corners/step", test_step);
    GLib.Test.add_func ("/hot-corners/l-shape", test_l_shape);
    GLib.Test.add_func ("/hot-corners/negative", test_negative);
    GLib.Test.add_func ("/hot-corners/allowed", test_allowed);
    GLib.Test.add_func ("/hot-corners/regions", test_regions);
    GLib.Test.add_func ("/hot-corners/pressure", test_pressure);
    GLib.Test.add_func ("/hot-corners/rearm-and-cooldown", test_rearm_and_cooldown);
    GLib.Test.add_func ("/hot-corners/hover", test_hover);
    GLib.Test.add_func ("/hot-corners/pressure-falls-back", test_pressure_falls_back);
    GLib.Test.add_func ("/hot-corners/edges", test_edges);
    GLib.Test.add_func ("/hot-corners/disabled", test_disabled);
    GLib.Test.add_func ("/hot-corners/click", test_click);
    return GLib.Test.run ();
}
