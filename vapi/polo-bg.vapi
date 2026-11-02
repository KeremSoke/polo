/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Bindings for libpolo-bg's MATE desktop background API.
 */

[CCode (cheader_filename = "bg.h")]
namespace Polo.Bg {
    /* org.mate.background's color-shading-type setting. */
    [CCode (cname = "PoloBgColorType", cprefix = "POLO_BG_COLOR_", has_type_id = false)]
    public enum ColorType {
        SOLID,
        H_GRADIENT,
        V_GRADIENT
    }

    /* org.mate.background's picture-options setting. */
    [CCode (cname = "PoloBgPlacement", cprefix = "POLO_BG_PLACEMENT_", has_type_id = false)]
    public enum Placement {
        TILED,
        ZOOMED,
        CENTERED,
        SCALED,
        FILL_SCREEN,
        SPANNED
    }

    /* A background is drawn by one thread at a time. */
    [Compact]
    [CCode (cname = "PoloBg", ref_function = "polo_bg_ref", unref_function = "polo_bg_unref")]
    public class Background {
        [CCode (cname = "polo_bg_new")]
        public Background ();

        [CCode (cname = "polo_bg_set_color")]
        public void set_color (ColorType type, string? primary, string? secondary);

        [CCode (cname = "polo_bg_set_placement")]
        public void set_placement (Placement placement);

        /* Image or XML slideshow; null draws colours only. */
        [CCode (cname = "polo_bg_set_filename")]
        public void set_filename (string? filename);

        /* Blocking. Fills a width × height buffer of premultiplied ARGB32
         * pixels for one output's part of the screen. */
        [CCode (cname = "polo_bg_draw_output")]
        public void draw_output ([CCode (array_length = false)] uint8[] data, int width, int height,
                                 int stride, int scale, int output_x, int output_y,
                                 int screen_width, int screen_height);

        /* Time until the next slideshow change; uint.MAX means never. */
        [CCode (cname = "polo_bg_get_timeout")]
        public uint get_timeout ();
    }
}
