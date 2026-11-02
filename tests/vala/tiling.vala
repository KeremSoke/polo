/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * handle_toggle_tiled () and meta_window_tile (), with the widths and the
 * untiled geometry real Marco 1.28.2 gives.
 */

using Polo;

class FakeAdapter : GLib.Object, WindowAdapter {
    public WindowState edges = 0;
    public bool maximized = false;

    public void configure (Marco.Rect frame) {}
    public void set_activated (bool activated) {}
    public void set_maximized (bool maximized) {
        this.maximized = maximized;
    }
    public void set_fullscreen (bool fullscreen) {}
    public void set_tiled (WindowState edges) {
        this.edges = edges;
    }
    public void take_focus () {}
    public void close () {}
    public void ping () {}
}

Policy.Model new_model (bool cycling = true) {
    var overrides = new GLib.HashTable<string, GLib.Variant> (GLib.str_hash, GLib.str_equal);
    overrides.insert ("general/allow-tile-cycling", new GLib.Variant.boolean (cycling));
    MarcoPrefs prefs;
    try {
        prefs = MarcoPrefs.with_overrides (overrides);
    } catch (PrefsError e) {
        GLib.error ("%s", e.message);
    }
    var model = new Policy.Model (prefs);
    model.add_output (new Policy.Output ("eDP-1", Marco.Rect () { width = 1280, height = 800 }));
    return model;
}

Window map (Policy.Model model) {
    var adapter = new FakeAdapter ();
    var w = new Window (WindowKind.XDG, adapter);
    w.label = "a";
    w.frame = Marco.Rect () { x = 100, y = 100, width = 400, height = 300 };
    model.manage (w);
    model.map (w);
    model.pointer_moved (640, 400);
    return w;
}

void assert_frame (Window w, int x, int y, int width, int height) {
    if (w.frame.x != x || w.frame.y != y || w.frame.width != width || w.frame.height != height) {
        GLib.error ("frame %d,%d %dx%d, wanted %d,%d %dx%d", w.frame.x, w.frame.y, w.frame.width,
                    w.frame.height, x, y, width, height);
    }
}

void test_cycle () {
    var model = new_model ();
    var w = map (model);
    int[] widths = { 640, 426, 320, 1280, 960, 853 };
    foreach (int width in widths) {
        model.toggle_tiled (w, Marco.TileMode.LEFT);
        assert_frame (w, 0, 0, width, 800);
        assert (w.has_state (WindowState.MAXIMIZED_V) && !w.has_state (WindowState.MAXIMIZED_H));
        assert (w.has_state (WindowState.TILED_LEFT));
    }
    var adapter = (FakeAdapter) w.adapter;
    assert (adapter.edges == (WindowState.TILED_LEFT | WindowState.TILED_TOP | WindowState.TILED_BOTTOM));
    assert (!adapter.maximized);
    model.toggle_tiled (w, Marco.TileMode.LEFT);
    assert (!w.tiled && w.tile_mode == Marco.TileMode.NONE);
    assert (!w.has_state (WindowState.MAXIMIZED_V) && !w.has_state (WindowState.TILED_LEFT));
    assert (adapter.edges == 0);
    assert (w.frame.width == 400 && w.frame.height == 300);
}

void test_no_cycling () {
    var model = new_model (false);
    var w = map (model);
    model.toggle_tiled (w, Marco.TileMode.RIGHT);
    assert_frame (w, 640, 0, 640, 800);
    model.toggle_tiled (w, Marco.TileMode.RIGHT);
    assert (!w.tiled && w.frame.width == 400);
}

void test_changing_side_resets_the_cycle () {
    var model = new_model ();
    var w = map (model);
    model.toggle_tiled (w, Marco.TileMode.LEFT);
    model.toggle_tiled (w, Marco.TileMode.LEFT);
    assert (w.frame.width == 426);
    model.toggle_tiled (w, Marco.TileMode.RIGHT);
    assert_frame (w, 640, 0, 640, 800);
}

void test_corners () {
    var model = new_model ();
    var w = map (model);
    var adapter = (FakeAdapter) w.adapter;
    model.toggle_tiled (w, Marco.TileMode.BOTTOM_RIGHT);
    assert_frame (w, 640, 400, 640, 400);
    assert (!w.has_state (WindowState.MAXIMIZED_V));
    assert (!w.has_state (WindowState.TILED_RIGHT));
    assert (adapter.edges == (WindowState.TILED_RIGHT | WindowState.TILED_BOTTOM));
    model.toggle_tiled (w, Marco.TileMode.TOP_LEFT);
    assert_frame (w, 0, 0, 640, 400);
    assert (adapter.edges == (WindowState.TILED_LEFT | WindowState.TILED_TOP));
}

void test_corner_untile_stays () {
    var model = new_model (false);
    var w = map (model);
    model.toggle_tiled (w, Marco.TileMode.TOP_RIGHT);
    model.toggle_tiled (w, Marco.TileMode.TOP_RIGHT);
    assert (!w.tiled);
    assert_frame (w, 640, 0, 640, 400);
}

void test_saved_rect_is_not_overwritten () {
    var model = new_model (false);
    var w = map (model);
    var start = w.frame;
    model.move (w, true, 300, 200);
    assert (w.frame.x == 300);
    model.toggle_tiled (w, Marco.TileMode.LEFT);
    model.toggle_tiled (w, Marco.TileMode.LEFT);
    assert (w.frame.width == start.width && w.frame.height == start.height);
    assert (w.frame.x != 300);
}

void test_maximized_then_tiled_then_maximized () {
    var model = new_model ();
    var w = map (model);
    model.maximize (w, WindowState.MAXIMIZED);
    assert_frame (w, 0, 0, 1280, 800);
    model.toggle_tiled (w, Marco.TileMode.RIGHT);
    assert_frame (w, 640, 0, 640, 800);
    assert (!w.has_state (WindowState.MAXIMIZED_H));
    var adapter = (FakeAdapter) w.adapter;
    assert (!adapter.maximized);
    model.maximize (w, WindowState.MAXIMIZED);
    assert_frame (w, 0, 0, 1280, 800);
    assert (adapter.maximized && adapter.edges == 0);
    assert (w.tile_mode == Marco.TileMode.NONE);
}

void test_can_tile () {
    var model = new_model ();
    var w = map (model);
    w.hints.min_width = 700;
    model.toggle_tiled (w, Marco.TileMode.LEFT);
    assert (!w.tiled && w.tile_mode == Marco.TileMode.NONE);
    w.hints.min_width = 1;
    w.set_state_flags (WindowState.SHADED, true);
    model.toggle_tiled (w, Marco.TileMode.LEFT);
    assert (!w.tiled);
}

public static int main (string[] args) {
    GLib.Test.init (ref args);
    GLib.Test.add_func ("/tiling/cycle", test_cycle);
    GLib.Test.add_func ("/tiling/no-cycling", test_no_cycling);
    GLib.Test.add_func ("/tiling/changing-side", test_changing_side_resets_the_cycle);
    GLib.Test.add_func ("/tiling/corners", test_corners);
    GLib.Test.add_func ("/tiling/corner-untile", test_corner_untile_stays);
    GLib.Test.add_func ("/tiling/saved-rect", test_saved_rect_is_not_overwritten);
    GLib.Test.add_func ("/tiling/maximize", test_maximized_then_tiled_then_maximized);
    GLib.Test.add_func ("/tiling/can-tile", test_can_tile);
    return GLib.Test.run ();
}
