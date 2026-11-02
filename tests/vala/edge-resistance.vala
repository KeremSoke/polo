/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The edge-resistance.c port on its own: keyboard stops, edges hidden by
 * higher windows, docks, size increments and the mouse snap rules.
 */

using Polo;

const int SCREEN_W = 1000;
const int SCREEN_H = 800;

Marco.Rect rect (int x, int y, int width, int height) {
    return Marco.Rect () { x = x, y = y, width = width, height = height };
}

Marco.EdgeWindow window (int x, int y, int width, int height, bool is_dock = false) {
    return Marco.EdgeWindow () { outer = rect (x, y, width, height), relevant = true, is_dock = is_dock };
}

Marco.EdgeResistance edges (Marco.EdgeWindow[] stacked) {
    var outputs = new GLib.GenericArray<Policy.Output> ();
    outputs.add (new Policy.Output ("eDP-1", rect (0, 0, SCREEN_W, SCREEN_H)));
    var areas = new Policy.WorkAreas (outputs, new GLib.SList<Marco.Strut?> ());
    return new Marco.EdgeResistance (areas.screen, stacked, areas.xinerama_edges, areas.screen_edges);
}

const Marco.Rect GRABBED = { 100, 150, 200, 100 };

int move_x (Marco.EdgeResistance data, int new_x, bool snap, bool keyboard) {
    int x = new_x;
    int y = GRABBED.y;
    data.for_move (GRABBED, GRABBED.x, GRABBED.y, ref x, ref y, snap, keyboard);
    return x;
}

void test_keyboard_stops_at_crossed_edge () {
    var data = edges ({ window (400, 100, 200, 200) });
    assert_cmpint (move_x (data, 250, false, true), CompareOperator.EQ, 200);
    assert_cmpint (move_x (data, 400, false, true), CompareOperator.EQ, 200);
}

void test_higher_window_hides_edge () {
    var data = edges ({ window (400, 100, 200, 200), window (250, 140, 450, 120) });
    assert_cmpint (move_x (data, 245, false, true), CompareOperator.EQ, 245);
    var below = edges ({ window (250, 140, 450, 120), window (400, 100, 200, 200) });
    assert_cmpint (move_x (below, 245, false, true), CompareOperator.EQ, 200);
}

void test_dock_has_no_window_edges () {
    var data = edges ({ window (400, 100, 50, 200, true) });
    assert_cmpint (move_x (data, 250, false, true), CompareOperator.EQ, 250);
}

void test_mouse_window_threshold () {
    var data = edges ({ window (400, 100, 200, 200) });
    assert_cmpint (move_x (data, 210, false, false), CompareOperator.EQ, 200);
    assert_cmpint (move_x (data, 215, false, false), CompareOperator.EQ, 200);
    assert_cmpint (move_x (data, 216, false, false), CompareOperator.EQ, 216);
}

void test_mouse_screen_threshold () {
    var data = edges ({});
    assert_cmpint (move_x (data, -131, false, false), CompareOperator.EQ, -131);
    assert_cmpint (move_x (data, -31, false, false), CompareOperator.EQ, 0);
    assert_cmpint (move_x (data, -32, false, false), CompareOperator.EQ, -32);
}

void test_mouse_snap () {
    var data = edges ({ window (400, 100, 200, 200) });
    assert_cmpint (move_x (data, 105, true, false), CompareOperator.EQ, 100);
    assert_cmpint (move_x (data, 180, true, false), CompareOperator.EQ, 200);
}

void test_keyboard_snap () {
    var data = edges ({ window (400, 100, 200, 200) });
    assert_cmpint (move_x (data, 101, true, true), CompareOperator.EQ, 200);
    assert_cmpint (move_x (data, 99, true, true), CompareOperator.EQ, 0);
}

void test_resize_increments () {
    var data = edges ({ window (400, 100, 200, 200) });
    int width = 350, height = GRABBED.height;
    data.for_resize (GRABBED, 1, 1, GRABBED.width, GRABBED.height, ref width, ref height,
                     Marco.Gravity.WEST, false, true);
    assert_cmpint (width, CompareOperator.EQ, 300);
    width = 350;
    data.for_resize (GRABBED, 10, 1, GRABBED.width, GRABBED.height, ref width, ref height,
                     Marco.Gravity.WEST, false, true);
    assert_cmpint (width, CompareOperator.EQ, 350);
}

int main (string[] args) {
    Test.init (ref args);
    Test.add_func ("/edge-resistance/keyboard-stops", test_keyboard_stops_at_crossed_edge);
    Test.add_func ("/edge-resistance/hidden-edge", test_higher_window_hides_edge);
    Test.add_func ("/edge-resistance/dock", test_dock_has_no_window_edges);
    Test.add_func ("/edge-resistance/mouse-window", test_mouse_window_threshold);
    Test.add_func ("/edge-resistance/mouse-screen", test_mouse_screen_threshold);
    Test.add_func ("/edge-resistance/mouse-snap", test_mouse_snap);
    Test.add_func ("/edge-resistance/keyboard-snap", test_keyboard_snap);
    Test.add_func ("/edge-resistance/resize-increments", test_resize_increments);
    return Test.run ();
}
