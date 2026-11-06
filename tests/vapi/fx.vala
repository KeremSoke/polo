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
    return 0;
}
