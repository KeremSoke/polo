/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

using Polo;
using Polo.Policy;

bool near (double a, double b) {
    return (a - b).abs () < 1e-6;
}

void test_grid () {
    var output = Marco.Rect () { x = 0, y = 0, width = 1000, height = 800 };
    var layout = new ExpoLayout (output, 2, 2, 20);
    assert (near (layout.scale, double.min (940.0 / 2000.0, 740.0 / 1600.0)));
    var first = layout.cell (0, 0);
    var last = layout.cell (1, 1);
    assert (first.width == last.width && first.height == last.height);
    assert (first.y >= 20 && last.y + last.height <= 780);
    assert ((last.x - (first.x + first.width) - 20).abs () <= 1);
    assert ((first.x - output.x - (output.x + output.width - (last.x + last.width))).abs () <= 1);
    assert ((first.width * 800 - first.height * 1000).abs () <= 1000);
}

void test_mapping () {
    var output = Marco.Rect () { x = 1920, y = 0, width = 1280, height = 1024 };
    var layout = new ExpoLayout (output, 3, 1, 10);
    double cx, cy, x, y;
    layout.into_cell (2, 0, output.x, output.y, out cx, out cy);
    var cell = layout.cell (2, 0);
    assert ((int) GLib.Math.round (cx) == cell.x && (int) GLib.Math.round (cy) == cell.y);
    layout.into_cell (1, 0, 2500, 700, out cx, out cy);
    layout.out_of_cell (1, 0, cx, cy, out x, out y);
    assert (near (x, 2500) && near (y, 700));
}

void test_hit () {
    var output = Marco.Rect () { x = 0, y = 0, width = 1000, height = 800 };
    var layout = new ExpoLayout (output, 2, 2, 20);
    int column, row;
    var cell = layout.cell (1, 0);
    assert (layout.hit (cell.x + 5, cell.y + 5, out column, out row) && column == 1 && row == 0);
    cell = layout.cell (0, 1);
    assert (layout.hit (cell.x + cell.width - 2, cell.y + cell.height - 2, out column, out row));
    assert (column == 0 && row == 1);
    assert (!layout.hit (layout.cell (0, 0).x + layout.cell (0, 0).width + 10, 300, out column, out row));
    assert (!layout.hit (2, 2, out column, out row));
}

void test_zoom () {
    var output = Marco.Rect () { x = 0, y = 0, width = 1000, height = 800 };
    var layout = new ExpoLayout (output, 2, 2, 20);
    var cell = layout.cell (1, 1);
    double cx, cy, zx, zy;
    layout.into_cell (1, 1, 0, 0, out cx, out cy);
    layout.zoom_point (1, 1, 0.0, cx, cy, out zx, out zy);
    assert (near (zx, 0) && near (zy, 0));
    layout.into_cell (1, 1, 1000, 800, out cx, out cy);
    layout.zoom_point (1, 1, 0.0, cx, cy, out zx, out zy);
    assert (near (zx, 1000) && near (zy, 800));
    assert (near (layout.zoom_factor (0.0) * layout.scale, 1.0));
    layout.zoom_point (1, 1, 1.0, cell.x + 7, cell.y + 9, out zx, out zy);
    assert (near (zx, cell.x + 7) && near (zy, cell.y + 9));
    assert (near (layout.zoom_factor (1.0), 1.0));
}

void test_labels () {
    var output = Marco.Rect () { x = 0, y = 0, width = 1000, height = 800 };
    var plain = new ExpoLayout (output, 2, 2, 20);
    var named = new ExpoLayout (output, 2, 2, 20, 30);
    assert (named.scale < plain.scale);
    var first = named.cell (0, 0);
    var second = named.cell (0, 1);
    assert ((second.y - (first.y + first.height) - 50).abs () <= 1);
    assert (second.y + second.height + 30 <= 800 - 20 + 1);
    int column, row;
    assert (!named.hit (first.x + 5, first.y + first.height + 10, out column, out row));
    assert (named.hit (second.x + 5, second.y + 5, out column, out row) && row == 1);
}

int main (string[] args) {
    Test.init (ref args);
    Test.add_func ("/expo/grid", test_grid);
    Test.add_func ("/expo/mapping", test_mapping);
    Test.add_func ("/expo/hit", test_hit);
    Test.add_func ("/expo/zoom", test_zoom);
    Test.add_func ("/expo/labels", test_labels);
    return Test.run ();
}
