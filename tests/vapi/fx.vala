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

    var cube = Polo.Fx.Cube () {
        sides = 4, offset_z = Polo.Fx.Cube.rest_offset_z (4), zoom = 1, ease = 0,
        deform = Polo.Fx.CubeDeform.CYLINDER,
        origin = Polo.Fx.Vec2 () { x = 0, y = 0 }, size = Polo.Fx.Vec2 () { x = 800, y = 600 }
    };
    cube.rotation = -Polo.Fx.Cube.side_angle (4);
    Polo.Fx.Vec2 corner;
    assert (cube.point (1, Polo.Fx.Vec2 () { x = 800, y = 600 }, out corner));
    assert ((corner.x - 800).abs () < 0.01 && (corner.y - 600).abs () < 0.01);
    assert (cube.side_facing (1) && !cube.side_facing (3));
    assert (cube.side_depth (1) < cube.side_depth (3));
    var order = new int[4];
    cube.side_order (order);
    assert (order[0] == 3 && order[3] == 1);
    assert ((cube.shade (1, Polo.Fx.Vec2 () { x = 400, y = 300 }) - 1.0).abs () < 1e-6);
    var shade = new uint8[4 * 3 * 4];
    cube.fill_shade (1, shade, 4, 3, 16);
    assert (shade[3] == 0);
    uint cube_cols, cube_rows;
    cube.grid_size (800, 600, 40, out cube_cols, out cube_rows);
    assert (cube_cols == 31 && cube_rows == 31);
    var cube_grid = Polo.Fx.Grid.create (cube_cols, cube_rows);
    assert (cube.fill_grid (1, 0, 0, 800, 600, 0, 0, 800, 600, cube_grid));
    assert (cube_grid.vertices[2] == 0 && cube_grid.vertices[3] == 0);

    var sky_cube = Polo.Fx.Cube () {
        sides = 4, offset_z = Polo.Fx.Cube.rest_offset_z (4), zoom = 1,
        origin = Polo.Fx.Vec2 () { x = 0, y = 0 }, size = Polo.Fx.Vec2 () { x = 800, y = 600 }
    };
    var ahead = sky_cube.sky_uv (Polo.Fx.Vec2 () { x = 400, y = 300 }, false);
    assert ((ahead.x - 0.75).abs () < 1e-6 && (ahead.y - 0.5).abs () < 1e-6);
    var sky_grid = Polo.Fx.Grid.create (9, 9);
    sky_cube.fill_sky_grid (false, true, sky_grid);
    assert ((sky_grid.vertices[40 * 4 + 2] - 0.375).abs () < 1e-6);
    var cross = new uint8[4 * 3 * 4];
    GLib.Memory.set (cross, 0x7f, cross.length);
    var panorama = new uint8[8 * 4 * 4];
    assert (Polo.Fx.cubemap_equirect (cross, 4, 3, 16, panorama, 8, 4, 32));
    var doubled = new uint8[16 * 4 * 4];
    Polo.Fx.sky_texture (panorama, 8, 4, 32, doubled, 64);
    assert (doubled[0] == 0x7f && doubled[doubled.length - 1] == 0x7f);

    var squeeze = Polo.Fx.Squeezimize () {
        progress = 0.5, direction = Polo.Fx.MagicLampDirection.BOTTOM,
        origin = Polo.Fx.Vec2 () { x = 100, y = 100 }, size = Polo.Fx.Vec2 () { x = 400, y = 300 },
        target = Polo.Fx.Vec2 () { x = 200, y = 700 }, target_size = Polo.Fx.Vec2 () { x = 40, y = 30 }
    };
    var on_screen = squeeze.point (Polo.Fx.Vec2 () { x = 0.25, y = 0.75 });
    Polo.Fx.Vec2 back;
    assert (squeeze.texture_at (on_screen, out back));
    assert ((back.x - 0.25).abs () < 1e-6 && (back.y - 0.75).abs () < 1e-6);
    uint squeeze_cols, squeeze_rows;
    squeeze.grid_size (out squeeze_cols, out squeeze_rows);
    var squeeze_grid = Polo.Fx.Grid.create (squeeze_cols, squeeze_rows);
    squeeze.fill_grid (squeeze_grid);
    assert (Polo.Fx.Squeezimize.advance (0, false, 75, 150) == 0.5);
    return 0;
}
