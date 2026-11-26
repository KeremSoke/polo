/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Scale's layouts, which screen shows which window, keyboard navigation,
 * and which windows each mode picks.
 */

using Polo;
using Polo.Policy;

class FakeAdapter : GLib.Object, WindowAdapter {
    public void configure (Marco.Rect frame) {}
    public void set_activated (bool activated) {}
    public void set_maximized (bool maximized) {}
    public void set_fullscreen (bool fullscreen) {}
    public void take_focus () {}
    public void close () {}
    public void ping () {}
}

Marco.Rect rect (int x, int y, int width, int height) {
    return Marco.Rect () { x = x, y = y, width = width, height = height };
}

bool overlap (ScaleSlot a, ScaleSlot b) {
    return a.x < b.x + b.width () && b.x < a.x + a.width () && a.y < b.y + b.height () && b.y < a.y + a.height ();
}

void check_slots (GLib.GenericArray<ScaleSlot> slots, Marco.Rect area, int n) {
    assert (slots.length == n);
    var seen = new bool[n];
    foreach (var slot in slots) {
        assert (slot.index >= 0 && slot.index < n && !seen[slot.index]);
        seen[slot.index] = true;
        assert (slot.scale > 0.0 && slot.scale <= 1.0);
        assert (slot.x >= area.x - 0.01 && slot.y >= area.y - 0.01);
        assert (slot.x + slot.width () <= area.x + area.width + 0.01);
        assert (slot.y + slot.height () <= area.y + area.height + 0.01);
    }
    for (int i = 0; i < slots.length; i++) {
        for (int j = i + 1; j < slots.length; j++) {
            assert (!overlap (slots[i], slots[j]));
        }
    }
}

void test_natural_keeps_places () {
    var area = rect (0, 0, 1920, 1080);
    Marco.Rect[] windows = { rect (100, 100, 400, 300), rect (1200, 600, 500, 400) };
    var slots = ScaleLayout.natural (area, windows, 16);
    check_slots (slots, area, 2);
    assert (slots[0].scale == 1.0);
    assert (slots[0].centre_x () < slots[1].centre_x () && slots[0].centre_y () < slots[1].centre_y ());
}

void test_natural_separates () {
    var area = rect (0, 0, 1024, 768);
    Marco.Rect[] windows = {};
    for (int i = 0; i < 9; i++) {
        windows += rect (100 + i * 10, 100 + i * 10, 600, 400);
    }
    var slots = ScaleLayout.natural (area, windows, 16);
    check_slots (slots, area, 9);
    assert (slots[0].scale < 1.0);
}

void test_natural_same_place () {
    var area = rect (0, 0, 1024, 768);
    Marco.Rect[] windows = { rect (0, 0, 1024, 768), rect (0, 0, 1024, 768), rect (0, 0, 1024, 768) };
    check_slots (ScaleLayout.natural (area, windows, 16), area, 3);
}

void test_natural_offset_area () {
    var area = rect (1920, -200, 1280, 1024);
    Marco.Rect[] windows = { rect (1950, -150, 800, 600), rect (2000, -100, 800, 600), rect (100, 100, 300, 200) };
    check_slots (ScaleLayout.natural (area, windows, 10), area, 3);
}

void test_grid () {
    var area = rect (0, 0, 1024, 768);
    Marco.Rect[] windows = {};
    for (int i = 0; i < 7; i++) {
        windows += rect (0, 0, 640, 480);
    }
    var slots = ScaleLayout.grid (area, windows, 16);
    check_slots (slots, area, 7);
    assert (slots[0].y == slots[1].y);
    assert (slots[6].y > slots[0].y);
    Marco.Rect[] one = { rect (0, 0, 200, 100) };
    var single = ScaleLayout.grid (area, one, 16);
    check_slots (single, area, 1);
    assert (single[0].scale == 1.0);
    assert ((single[0].centre_x () - 512.0).abs () < 1.0 && (single[0].centre_y () - 384.0).abs () < 1.0);
}

void test_empty () {
    var area = rect (0, 0, 800, 600);
    Marco.Rect[] none = {};
    assert (ScaleLayout.natural (area, none, 16).length == 0);
    assert (ScaleLayout.grid (area, none, 16).length == 0);
    int[] workspaces = { 0, 1 };
    int[] window_workspaces = {};
    assert (ScaleLayout.grouped (ScaleArrangement.GRID, area, workspaces, none, window_workspaces, 16, 30).length ==
            0);
}

void test_grouped () {
    var area = rect (0, 0, 1024, 768);
    Marco.Rect[] windows = { rect (0, 0, 500, 400), rect (100, 100, 500, 400), rect (0, 0, 300, 200),
                             rect (50, 50, 300, 200) };
    int[] window_workspaces = { 2, 0, 2, 2 };
    int[] order = { 0, 1, 2, 3 };
    foreach (var arrangement in new ScaleArrangement[] { ScaleArrangement.NATURAL, ScaleArrangement.GRID }) {
        var bands = ScaleLayout.grouped (arrangement, area, order, windows, window_workspaces, 16, 30);
        assert (bands.length == 2);
        assert (bands[0].workspace == 0 && bands[1].workspace == 2);
        assert (bands[0].slots.length == 1 && bands[0].slots[0].index == 1);
        assert (bands[1].slots.length == 3);
        assert (bands[0].area.y >= 30 && bands[1].area.y >= bands[0].area.y + bands[0].area.height + 30);
        assert (bands[1].area.y + bands[1].area.height <= 768);
        var all = new GLib.GenericArray<ScaleSlot> ();
        foreach (var band in bands) {
            foreach (var slot in band.slots) {
                assert (slot.y >= band.area.y - 0.01 && slot.y + slot.height () <= band.area.y + band.area.height + 0.01);
                all.add (slot);
            }
        }
        check_slots (all, area, 4);
    }
}

void test_screen_of () {
    Marco.Rect[] screens = { rect (0, 0, 1920, 1080), rect (1920, 0, 1280, 1024) };
    assert (ScaleScreensOf.screen_of (rect (100, 100, 400, 300), screens) == 0);
    assert (ScaleScreensOf.screen_of (rect (1800, 100, 400, 300), screens) == 1);
    assert (ScaleScreensOf.screen_of (rect (1700, 100, 400, 300), screens) == 0);
    assert (ScaleScreensOf.screen_of (rect (4000, 100, 400, 300), screens) == 1);
    assert (ScaleScreensOf.screen_of (rect (-900, 2000, 400, 300), screens) == 0);
    Marco.Rect[] none = {};
    assert (ScaleScreensOf.screen_of (rect (0, 0, 10, 10), none) == -1);
}

void test_distinct () {
    Marco.Rect[] screens = { rect (0, 0, 1920, 1080), rect (0, 0, 1920, 1080), rect (1920, 0, 1280, 1024) };
    int[] kept = ScaleScreensOf.distinct (screens);
    assert (kept.length == 2 && kept[0] == 0 && kept[1] == 2);
}

string lists_of (GLib.GenericArray<GLib.Array<int>> lists) {
    string[] parts = {};
    foreach (var list in lists) {
        string[] items = {};
        for (uint i = 0; i < list.length; i++) {
            items += list.index (i).to_string ();
        }
        parts += string.joinv (",", items);
    }
    return string.joinv ("|", parts);
}

void test_distribute () {
    Marco.Rect[] screens = { rect (0, 0, 1920, 1080), rect (1920, 0, 1280, 1024) };
    Marco.Rect[] windows = { rect (100, 100, 400, 300), rect (2000, 100, 400, 300), rect (1800, 500, 400, 300) };
    assert (lists_of (ScaleScreensOf.distribute (ScaleScreens.EACH, false, 0, windows, screens)) == "0|1,2");
    assert (lists_of (ScaleScreensOf.distribute (ScaleScreens.MIRRORED, false, 0, windows, screens)) ==
            "0,1,2|0,1,2");
    assert (lists_of (ScaleScreensOf.distribute (ScaleScreens.SUMMONED, false, 1, windows, screens)) == "|1,2");
    assert (lists_of (ScaleScreensOf.distribute (ScaleScreens.SUMMONED, false, 0, windows, screens)) == "0|");
    assert (lists_of (ScaleScreensOf.distribute (ScaleScreens.SUMMONED, true, 0, windows, screens)) == "0,1,2|");
    assert (lists_of (ScaleScreensOf.distribute (ScaleScreens.SUMMONED, true, 7, windows, screens)) == "|0,1,2");
    assert (ScaleScreensOf.shows (ScaleScreens.EACH, 1, 0));
    assert (ScaleScreensOf.shows (ScaleScreens.MIRRORED, 1, 0));
    assert (!ScaleScreensOf.shows (ScaleScreens.SUMMONED, 1, 0));
    assert (ScaleScreensOf.shows (ScaleScreens.SUMMONED, 0, 0));
}

void test_parsing () {
    assert (ScaleScreens.parse ("summoned") == ScaleScreens.SUMMONED);
    assert (ScaleScreens.parse ("mirrored") == ScaleScreens.MIRRORED);
    assert (ScaleScreens.parse ("bogus") == ScaleScreens.EACH);
    assert (ScaleScreens.parse (null, ScaleScreens.MIRRORED) == ScaleScreens.MIRRORED);
    assert (ScaleArrangement.parse ("grid") == ScaleArrangement.GRID);
    assert (ScaleArrangement.parse ("natural") == ScaleArrangement.NATURAL);
    foreach (var mode in ScaleMode.all ()) {
        ScaleMode parsed;
        assert (ScaleMode.from_key (mode.to_key (), out parsed) && parsed == mode);
    }
    ScaleMode none;
    assert (!ScaleMode.from_key ("expo", out none));
    assert (ScaleMode.ALL_WORKSPACES.all_workspaces () && !ScaleMode.WORKSPACE.all_workspaces ());
    assert (ScaleMode.APPLICATION.application () && ScaleMode.APPLICATION_ALL_WORKSPACES.application ());
    assert (ScaleMode.APPLICATION_ALL_WORKSPACES.all_workspaces ());
}

void test_navigation () {
    double[] xs = { 100, 300, 500, 100, 300 };
    double[] ys = { 100, 100, 100, 300, 300 };
    assert (ScaleNavigation.nearest (xs, ys, 0, 1, 0) == 1);
    assert (ScaleNavigation.nearest (xs, ys, 1, 1, 0) == 2);
    assert (ScaleNavigation.nearest (xs, ys, 2, 1, 0) == -1);
    assert (ScaleNavigation.nearest (xs, ys, 0, 0, 1) == 3);
    assert (ScaleNavigation.nearest (xs, ys, 4, 0, -1) == 1);
    assert (ScaleNavigation.nearest (xs, ys, 4, -1, 0) == 3);
    assert (ScaleNavigation.nearest (xs, ys, 2, 0, 1) == 4);
    assert (ScaleNavigation.nearest (xs, ys, -1, 1, 0) == 0);
    double[] none = {};
    assert (ScaleNavigation.nearest (none, none, -1, 1, 0) == -1);
}

Policy.Model new_model () {
    var model = new Policy.Model (MarcoPrefs.defaults ());
    model.add_output (new Policy.Output ("eDP-1", Marco.Rect () { width = 1920, height = 1080 }));
    return model;
}

Window map (Policy.Model model, string label, string? app = null, Window? parent = null) {
    var w = new Window (WindowKind.XDG, new FakeAdapter ());
    w.label = label;
    w.app_id = app;
    w.transient_for = parent;
    w.frame = Marco.Rect () { x = 100, y = 100, width = 400, height = 300 };
    model.manage (w);
    model.map (w);
    model.activate (w);
    return w;
}

string labels (GLib.GenericArray<Window> windows) {
    string[] names = {};
    foreach (var w in windows) {
        names += w.label;
    }
    return string.joinv (" ", names);
}

void test_pick () {
    var model = new_model ();
    assert (model.n_workspaces >= 2);
    var a = map (model, "a", "term");
    var b = map (model, "b", "edit");
    var c = map (model, "c", "term");
    var d = map (model, "d", "term", c);
    model.move_to_workspace (b, 1);
    model.minimize (a);
    assert (labels (ScaleWindows.pick (model, ScaleMode.WORKSPACE, true, c)) == "c a");
    assert (labels (ScaleWindows.pick (model, ScaleMode.WORKSPACE, false, c)) == "c");
    assert (labels (ScaleWindows.pick (model, ScaleMode.ALL_WORKSPACES, true, c)) == "c b a");
    assert (labels (ScaleWindows.pick (model, ScaleMode.APPLICATION, true, c)) == "c a");
    assert (labels (ScaleWindows.pick (model, ScaleMode.APPLICATION_ALL_WORKSPACES, true, b)) == "b");
    assert (labels (ScaleWindows.pick (model, ScaleMode.APPLICATION, true, null)) == "");
    assert (labels (ScaleWindows.family (model, c)) == "c d");
    assert (labels (ScaleWindows.family (model, a)) == "a");
    assert (ScaleWindows.same_application (a, c) && !ScaleWindows.same_application (a, b));
    var x = new Window (WindowKind.X11, new FakeAdapter ());
    var y = new Window (WindowKind.X11, new FakeAdapter ());
    x.group = 5;
    y.group = 5;
    assert (ScaleWindows.same_application (x, y));
    y.group = 6;
    assert (!ScaleWindows.same_application (x, y));
    assert (d.transient_for == c);
}

int main (string[] args) {
    GLib.Test.init (ref args);
    GLib.Test.add_func ("/scale/natural/keeps-places", test_natural_keeps_places);
    GLib.Test.add_func ("/scale/natural/separates", test_natural_separates);
    GLib.Test.add_func ("/scale/natural/same-place", test_natural_same_place);
    GLib.Test.add_func ("/scale/natural/offset-area", test_natural_offset_area);
    GLib.Test.add_func ("/scale/grid", test_grid);
    GLib.Test.add_func ("/scale/empty", test_empty);
    GLib.Test.add_func ("/scale/grouped", test_grouped);
    GLib.Test.add_func ("/scale/screen-of", test_screen_of);
    GLib.Test.add_func ("/scale/distinct", test_distinct);
    GLib.Test.add_func ("/scale/distribute", test_distribute);
    GLib.Test.add_func ("/scale/parsing", test_parsing);
    GLib.Test.add_func ("/scale/navigation", test_navigation);
    GLib.Test.add_func ("/scale/pick", test_pick);
    return GLib.Test.run ();
}
