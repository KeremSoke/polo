/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Bindings for libpolo-fx (lib/fx/polo-fx.h).
 */

[CCode (cheader_filename = "polo-fx.h")]
namespace Polo.Fx {
    [CCode (cname = "polo_fx_source")]
    public unowned string source ();

    [CCode (cname = "POLO_FX_GRID_MAX_VERTICES")]
    public const uint GRID_MAX_VERTICES;

    [CCode (cname = "POLO_FX_WOBBLY_MARGIN")]
    public const uint WOBBLY_MARGIN;

    [CCode (cname = "PoloFxVec2", has_type_id = false)]
    [SimpleType]
    public struct Vec2 {
        public double x;
        public double y;
    }

    [CCode (cname = "PoloFxOrigin", has_type_id = false)]
    [SimpleType]
    public struct Origin {
        public int x;
        public int y;
    }

    [CCode (cname = "PoloFxSize", has_type_id = false)]
    [SimpleType]
    public struct Size {
        public int width;
        public int height;
    }

    [CCode (cname = "PoloFxWobblyParameters", has_type_id = false)]
    [SimpleType]
    public struct WobblyParameters {
        public double stiffness;
        public double drag;
        public double move_factor;
        public double min_velocity;
        public double max_velocity;
        public double stop_velocity;
        public double min_acceleration;
        public double max_acceleration;
        public double stop_acceleration;
        public int x_tessellation;
        public int y_tessellation;
    }

    [CCode (cname = "polo_fx_wobbly_parameters_for_level")]
    public WobblyParameters wobbly_parameters_for_level (int level);

    /* cols × rows vertices, row-major: x, y (global logical), u, v (0..1 over the surface). */
    [Compact]
    [CCode (cname = "PoloFxGrid", free_function = "polo_fx_grid_free")]
    public class Grid {
        public uint cols;
        public uint rows;
        [CCode (array_length_cname = "n_floats")]
        public float[] vertices;

        [CCode (cname = "polo_fx_grid_new")]
        public static Grid? create (uint cols, uint rows);

        /* The undeformed surface on x, y, width × height. */
        [CCode (cname = "polo_fx_grid_set_rect")]
        public void set_rect (float x, float y, float width, float height);

        [CCode (cname = "polo_fx_grid_set_subgrid")]
        public void set_subgrid (Grid source, float u0, float v0, float u1, float v1);
    }

    [Compact]
    [CCode (cname = "PoloFxWobbly", free_function = "polo_fx_wobbly_free")]
    public class Wobbly {
        [CCode (cname = "polo_fx_wobbly_new")]
        public Wobbly ();

        [CCode (cname = "polo_fx_wobbly_start")]
        public void start (Origin origin, Size size, WobblyParameters parameters,
                           double anchor_u, double anchor_v);

        [CCode (cname = "polo_fx_wobbly_advance")]
        public bool advance (Origin origin, Size size, double elapsed_ms);

        [CCode (cname = "polo_fx_wobbly_fill_grid")]
        public void fill_grid (Grid grid);

        /* Ease the bend out once the springs stop, for maximize and tile. */
        [CCode (cname = "polo_fx_wobbly_set_ease_out")]
        public void set_ease_out (bool ease);

        /* Draws the bend whole for half of total_ms, then fades it to none by total_ms. */
        [CCode (cname = "polo_fx_wobbly_set_fade")]
        public void set_fade (double elapsed_ms, double total_ms);

        [CCode (cname = "polo_fx_wobbly_point")]
        public static Vec2 point ([CCode (array_length = false)] Vec2[] points,
                                  double x, double y);
    }

    [CCode (cname = "PoloFxMagicLampDirection", cprefix = "POLO_FX_MAGIC_LAMP_", has_type_id = false)]
    public enum MagicLampDirection {
        BOTTOM,
        TOP,
        LEFT,
        RIGHT
    }

    [CCode (cname = "PoloFxMagicLamp", has_type_id = false)]
    public struct MagicLamp {
        public double progress;
        public MagicLampDirection direction;
        public Vec2 origin;
        public Vec2 size;
        public Vec2 target;
        public Vec2 target_size;

        [CCode (cname = "polo_fx_magic_lamp_point")]
        public Vec2 point (Vec2 source);

        [CCode (cname = "polo_fx_magic_lamp_grid_size")]
        public void grid_size (out uint cols, out uint rows);

        [CCode (cname = "polo_fx_magic_lamp_fill_grid")]
        public void fill_grid (Grid grid);

        [CCode (cname = "polo_fx_magic_lamp_advance")]
        public static double advance (double progress, bool restoring, double elapsed_ms, double duration_ms);
    }

    [CCode (cname = "PoloFxCubeDeform", cprefix = "POLO_FX_CUBE_DEFORM_", has_type_id = false)]
    public enum CubeDeform {
        NONE,
        CYLINDER,
        STAR
    }

    [CCode (cname = "PoloFxCube", has_type_id = false)]
    public struct Cube {
        public int sides;
        public double rotation;
        public double offset_y;
        public double offset_z;
        public double zoom;
        public double ease;
        public CubeDeform deform;
        public Vec2 origin;
        public Vec2 size;

        [CCode (cname = "polo_fx_cube_side_angle")]
        public static double side_angle (int sides);

        [CCode (cname = "polo_fx_cube_rest_offset_z")]
        public static double rest_offset_z (int sides);

        [CCode (cname = "polo_fx_cube_point")]
        public bool point (int side, Vec2 local, out Vec2 screen);

        [CCode (cname = "polo_fx_cube_side_facing")]
        public bool side_facing (int side);

        [CCode (cname = "polo_fx_cube_side_depth")]
        public double side_depth (int side);

        [CCode (cname = "polo_fx_cube_side_order")]
        public void side_order ([CCode (array_length = false)] int[] order);

        [CCode (cname = "polo_fx_cube_shade")]
        public double shade (int side, Vec2 local);

        [CCode (cname = "polo_fx_cube_grid_size")]
        public void grid_size (double width, double height, double spacing, out uint cols, out uint rows);

        [CCode (cname = "polo_fx_cube_fill_grid")]
        public bool fill_grid (int side, double view_x, double view_y, double view_width, double view_height,
                               double clip_x, double clip_y, double clip_width, double clip_height, Grid grid);
    }

    [CCode (cname = "polo_fx_blur")]
    public static void blur ([CCode (array_length = false)] uint8[] pixels, int width, int height, int stride,
                             int radius);

    [CCode (cname = "polo_fx_dim")]
    public static void dim ([CCode (array_length = false)] uint8[] pixels, int width, int height, int stride,
                            double amount);

    [CCode (cname = "polo_fx_downscale")]
    public static void downscale ([CCode (array_length = false)] uint8[] src, int width, int height, int stride,
                                  int factor, [CCode (array_length = false)] uint8[] dst, int dst_stride);
}
