/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * VAPI coverage test for polo-fx.vapi: uses every symbol.
 */

int main () {
    assert (Polo.Fx.source () == "picom wobbly branch");

    var lamp = Polo.Fx.MagicLamp () {
        direction = Polo.Fx.MagicLampDirection.BOTTOM,
        origin = Polo.Fx.Vec2 () { x = 100, y = 200 },
        size = Polo.Fx.Vec2 () { x = 400, y = 300 },
        target = Polo.Fx.Vec2 () { x = 700, y = 800 },
        target_size = Polo.Fx.Vec2 () { x = 40, y = 20 }
    };
    lamp.progress = Polo.Fx.MagicLamp.advance (0, false, 400, 800);
    var point = lamp.point (Polo.Fx.Vec2 () { x = 200, y = 150 });
    assert ((point.y - 508.203125).abs () < 1e-6);
    uint cols, rows;
    lamp.grid_size (out cols, out rows);
    assert (cols == 11 && rows == 9);
    var grid = Polo.Fx.Grid.create (cols, rows);
    lamp.fill_grid (grid);
    assert (grid.vertices[2] == 0 && grid.vertices[3] == 0);

    var pixels = new uint8[8 * 8 * 4];
    GLib.Memory.set (pixels, 0x80, pixels.length);
    Polo.Fx.blur (pixels, 8, 8, 32, 2);
    assert (pixels[0] == 0x80 && pixels[pixels.length - 1] == 0x80);
    Polo.Fx.dim (pixels, 8, 8, 32, 0.5);
    assert (pixels[0] == 0x40 && pixels[3] == 0x80);
    var small = new uint8[4 * 4 * 4];
    Polo.Fx.downscale (pixels, 8, 8, 32, 2, small, 16);
    assert (small[0] == 0x40 && small[3] == 0x80);
    return 0;
}
