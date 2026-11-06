/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

using Polo;
using Polo.Policy;

const Marco.Rect WINDOW = { 100, 100, 400, 300 };

void check_pointer (int x, int y, int tx, int ty, bool on_border, LampEdge edge) {
    bool border;
    LampEdge side;
    var target = LampGeometry.pointer_target (WINDOW, x, y, out border, out side);
    assert (target.x == tx && target.y == ty && target.width == 0 && target.height == 0);
    assert (border == on_border);
    if (on_border) {
        assert (side == edge);
    }
}

void test_pointer () {
    check_pointer (900, 50, 900, 50, false, LampEdge.BOTTOM);
    check_pointer (110, 250, 100, 250, true, LampEdge.LEFT);
    check_pointer (300, 105, 300, 100, true, LampEdge.TOP);
    check_pointer (300, 395, 300, 399, true, LampEdge.BOTTOM);
    check_pointer (495, 250, 499, 250, true, LampEdge.RIGHT);
    /* KWin's tie order: top, then left, bottom, right. */
    check_pointer (100, 100, 100, 100, true, LampEdge.TOP);
}

void test_anchor () {
    LampEdge edge;
    const uint T = LampGeometry.ANCHOR_TOP, B = LampGeometry.ANCHOR_BOTTOM;
    const uint L = LampGeometry.ANCHOR_LEFT, R = LampGeometry.ANCHOR_RIGHT;
    assert (LampGeometry.edge_for_anchor (B | L | R, out edge) && edge == LampEdge.BOTTOM);
    assert (LampGeometry.edge_for_anchor (T | L | R, out edge) && edge == LampEdge.TOP);
    assert (LampGeometry.edge_for_anchor (L | T | B, out edge) && edge == LampEdge.LEFT);
    assert (LampGeometry.edge_for_anchor (R | T | B, out edge) && edge == LampEdge.RIGHT);
    assert (LampGeometry.edge_for_anchor (B, out edge) && edge == LampEdge.BOTTOM);
    assert (!LampGeometry.edge_for_anchor (T | L, out edge));
    assert (!LampGeometry.edge_for_anchor (T | B | L | R, out edge));
    assert (!LampGeometry.edge_for_anchor (0, out edge));
}

void test_nearest_edge () {
    var output = Marco.Rect () { x = 0, y = 0, width = 1024, height = 768 };
    assert (LampGeometry.nearest_edge (Marco.Rect () { x = 500, y = 760 }, output) == LampEdge.BOTTOM);
    assert (LampGeometry.nearest_edge (Marco.Rect () { x = 500, y = 5 }, output) == LampEdge.TOP);
    assert (LampGeometry.nearest_edge (Marco.Rect () { x = 3, y = 400 }, output) == LampEdge.LEFT);
    assert (LampGeometry.nearest_edge (Marco.Rect () { x = 1000, y = 300 }, output) == LampEdge.RIGHT);
    var second = Marco.Rect () { x = 1024, y = 0, width = 1024, height = 768 };
    assert (LampGeometry.nearest_edge (Marco.Rect () { x = 1030, y = 400 }, second) == LampEdge.LEFT);
}

int main (string[] args) {
    Test.init (ref args);
    Test.add_func ("/lamp-target/pointer", test_pointer);
    Test.add_func ("/lamp-target/anchor", test_anchor);
    Test.add_func ("/lamp-target/nearest-edge", test_nearest_edge);
    return Test.run ();
}
