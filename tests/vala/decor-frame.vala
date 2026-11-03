/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Tests that Decor.Frame redraws only when its inputs change.
 */

Polo.Backend.Server start_server () {
    if (GLib.Environment.get_variable ("XDG_RUNTIME_DIR") == null) {
        try {
            GLib.Environment.set_variable ("XDG_RUNTIME_DIR", GLib.DirUtils.make_tmp ("polo-decor-XXXXXX"), true);
        } catch (GLib.FileError e) {
            GLib.error ("no runtime directory: %s", e.message);
        }
    }
    var server = new Polo.Backend.Server ();
    var config = Polo.Backend.Config ();
    config.kind = Polo.Backend.Kind.HEADLESS;
    config.renderer = Polo.Backend.Renderer.PIXMAN;
    config.width = 640;
    config.height = 480;
    config.scale = 1;
    config.output_count = 1;
    string socket_name = "polo-decor-test-%d".printf ((int) Posix.getpid ());
    config.socket_name = socket_name;
    config.cursor_theme = "default";
    config.cursor_size = 24;
    try {
        assert (server.start (config));
    } catch (GLib.Error e) {
        GLib.error ("server start failed: %s", e.message);
    }
    server.attach (GLib.MainContext.default ());
    return server;
}

void check_edges (Polo.Decor.Frame frame) {
    unowned Polo.Marco.Border total = frame.geometry.borders.total;
    Polo.Marco.Rect rect;
    var top = frame.get_edge (Polo.Decor.Frame.Edge.TOP, out rect);
    assert (top != null && rect.x == 0 && rect.y == 0);
    assert (rect.width == frame.geometry.width && rect.height == total.top);
    int width, height, scale;
    top.get_size (out width, out height, out scale);
    assert (width == rect.width && height == rect.height && scale == 1);
    frame.get_edge (Polo.Decor.Frame.Edge.RIGHT, out rect);
    assert (rect.x == frame.geometry.width - total.right && rect.y == total.top);
    assert (rect.height == frame.geometry.height - total.top - total.bottom);
}

Polo.Backend.Server server;
const Polo.Marco.FrameFlags FLAGS = Polo.Marco.FrameFlags.HAS_FOCUS | Polo.Marco.FrameFlags.ALLOWS_DELETE |
                                    Polo.Marco.FrameFlags.ALLOWS_MAXIMIZE |
                                    Polo.Marco.FrameFlags.ALLOWS_MINIMIZE | Polo.Marco.FrameFlags.ALLOWS_MENU;

void test_no_theme () {
    var flags = FLAGS;
    var normal = Polo.Marco.FrameType.NORMAL;
    {
        var decorations = new Polo.Decor.Decorations ();
        var frame = new Polo.Decor.Frame (server, decorations);
        assert (!frame.update (normal, flags, 300, 200, "x"));
        Polo.Marco.Rect rect;
        assert (frame.get_edge (Polo.Decor.Frame.Edge.TOP, out rect) == null);
        assert (!decorations.load_theme ("PoloNoSuchTheme"));
        assert (decorations.get_theme () == null);
    }
}

void test_frame () {
    var flags = FLAGS;
    var normal = Polo.Marco.FrameType.NORMAL;
    {
        if (!GLib.FileUtils.test ("/usr/share/themes/Atlanta/metacity-1", GLib.FileTest.IS_DIR)) {
            GLib.Test.skip ("Atlanta is not installed");
            return;
        }
        var decorations = new Polo.Decor.Decorations ();
        assert (decorations.load_theme ("Atlanta"));
        assert (!decorations.load_theme ("PoloNoSuchTheme"));
        assert (decorations.get_theme () != null);
        decorations.set_fonts ("Sans 10", null);
        var frame = new Polo.Decor.Frame (server, decorations);

        assert (frame.update (normal, flags, 300, 200, "A title"));
        unowned Polo.Marco.Border total = frame.geometry.borders.total;
        assert (frame.geometry.width == 300 + total.left + total.right);
        assert (frame.geometry.height == 200 + total.top + total.bottom);
        check_edges (frame);
        int small_top = total.top;

        assert (!frame.update (normal, flags, 300, 200, "A title"));
        assert (frame.update (normal, flags, 300, 200, "Another title"));
        assert (frame.update (normal, flags & ~Polo.Marco.FrameFlags.HAS_FOCUS, 300, 200, "Another title"));
        assert (frame.update (normal, flags, 500, 260, "Another title"));
        check_edges (frame);
        assert (!frame.update (normal, flags, 500, 260, "Another title"));
        assert (frame.update (normal, flags, 501, 260, "Another title"));
        assert (frame.update (normal, flags, 501, 261, "Another title"));
        assert (frame.update (normal, flags, 500, 260, "Another title"));
        if (decorations.get_theme ().get_frame_style (Polo.Marco.FrameType.DIALOG, flags) != null) {
            assert (frame.update (Polo.Marco.FrameType.DIALOG, flags, 500, 260, "Another title"));
            assert (frame.update (normal, flags, 500, 260, "Another title"));
        }

        frame.set_button_state (Polo.Marco.ButtonType.CLOSE, Polo.Marco.ButtonState.PRELIGHT);
        assert (frame.update (normal, flags, 500, 260, "Another title"));
        frame.set_button_state (Polo.Marco.ButtonType.CLOSE, Polo.Marco.ButtonState.PRELIGHT);
        assert (!frame.update (normal, flags, 500, 260, "Another title"));

        decorations.set_fonts ("Sans 30", null);
        assert (frame.update (normal, flags, 500, 260, "Another title"));
        assert (frame.geometry.borders.total.top > small_top);
        decorations.set_fonts ("Sans 30", "Sans 8");
        assert (frame.update (normal, flags, 500, 260, "Another title"));
        decorations.set_fonts ("Sans 30", "Sans 8");
        assert (!frame.update (normal, flags, 500, 260, "Another title"));
        assert (decorations.load_theme ("Atlanta"));
        assert (frame.update (normal, flags, 500, 260, "Another title"));
        assert (frame.geometry.borders.total.top < small_top + 1);

        decorations.set_button_layout ("close:");
        assert (frame.update (normal, flags, 500, 260, "Another title"));
        assert (frame.geometry.n_left_buttons == 1 && frame.geometry.n_right_buttons == 0);
        decorations.set_button_layout ("close:");
        assert (!frame.update (normal, flags, 500, 260, "Another title"));

        var colours = new float[160];
        for (int i = 0; i < colours.length; i++) {
            colours[i] = 0.5f;
        }
        assert (decorations.get_colors ().set_table (colours));

        frame.clear ();
        Polo.Marco.Rect rect;
        assert (frame.get_edge (Polo.Decor.Frame.Edge.TOP, out rect) == null);
        assert (frame.update (normal, flags, 500, 260, "Another title"));
    }
}

uint8 alpha_at (uint8[] pixels, int stride, int x, int y) {
    return pixels[y * stride + x * 4 + 3];
}

/* Menta's frames.c visible region has 4-pixel rounded top corners. */
void test_rounded_corners () {
    if (!GLib.FileUtils.test ("/usr/share/themes/Menta/metacity-1", GLib.FileTest.IS_DIR)) {
        GLib.Test.skip ("Menta is not installed");
        return;
    }
    var decorations = new Polo.Decor.Decorations ();
    assert (decorations.load_theme ("Menta"));
    decorations.set_fonts ("Sans 10", null);
    var normal = Polo.Marco.FrameType.NORMAL;
    unowned Polo.Marco.Theme theme = decorations.get_theme ();
    int text_height = theme.text_height (normal, FLAGS, "Sans 10", null, Polo.Decor.Decorations.DPI);
    Polo.Marco.FrameGeometry geometry;
    theme.calc_geometry (normal, text_height, FLAGS, 300, 200, decorations.button_layout, out geometry);
    int width = geometry.width;
    int height = geometry.borders.total.top;
    int stride = width * 4;
    var pixels = new uint8[stride * height];
    var states = new Polo.Marco.ButtonState[Polo.Marco.ButtonType.LAST];
    theme.render (decorations.get_colors (), normal, FLAGS, 300, 200, "A title", "Sans 10", null,
                  Polo.Decor.Decorations.DPI, decorations.button_layout, states, 0, 0, pixels,
                  width, height, stride, 1);
    int left = geometry.borders.invisible.left;
    int right = width - geometry.borders.invisible.right - 1;
    int top = geometry.borders.invisible.top;
    assert (alpha_at (pixels, stride, left, top) == 0 && alpha_at (pixels, stride, right, top) == 0);
    assert (alpha_at (pixels, stride, left + 1, top) == 0 && alpha_at (pixels, stride, left, top + 1) == 0);
    assert (alpha_at (pixels, stride, width / 2, top) != 0);
    assert (alpha_at (pixels, stride, left, top + 10) != 0 && alpha_at (pixels, stride, right, top + 10) != 0);
    if (left > 0) {
        assert (alpha_at (pixels, stride, left - 1, top + 10) == 0);
    }
}

/* Marco's focused shadow extends 15 pixels up and left, and 21 down and right. */
void test_shadow () {
    if (!GLib.FileUtils.test ("/usr/share/themes/Menta/metacity-1", GLib.FileTest.IS_DIR)) {
        GLib.Test.skip ("Menta is not installed");
        return;
    }
    var decorations = new Polo.Decor.Decorations ();
    assert (decorations.load_theme ("Menta"));
    var normal = Polo.Marco.FrameType.NORMAL;
    unowned Polo.Marco.Theme theme = decorations.get_theme ();
    int text_height = theme.text_height (normal, FLAGS, "Sans 10", null, Polo.Decor.Decorations.DPI);
    Polo.Marco.FrameGeometry geometry;
    theme.calc_geometry (normal, text_height, FLAGS, 300, 200, decorations.button_layout, out geometry);
    unowned Polo.Marco.Border invisible = geometry.borders.invisible;
    int visible_width = geometry.width - invisible.left - invisible.right;
    int visible_height = geometry.height - invisible.top - invisible.bottom;

    var mask = Polo.Marco.ShadowMask.create (Polo.Marco.ShadowType.LARGE, Polo.Marco.SHADOW_OPACITY,
                                             geometry, FLAGS, 1);
    int x, y, width, height;
    mask.get_bounds (out x, out y, out width, out height);
    assert (x == invisible.left - 15 && y == invisible.top - 15);
    assert (width == visible_width + 36 && height == visible_height + 36);

    int px, py, pw, ph;
    mask.get_piece (Polo.Marco.ShadowPiece.LEFT, out px, out py, out pw, out ph);
    assert (px == 0 && pw == 15);
    mask.get_piece (Polo.Marco.ShadowPiece.RIGHT, out px, out py, out pw, out ph);
    assert (pw == 21 && px == width - 21);
    mask.get_piece (Polo.Marco.ShadowPiece.TOP, out px, out py, out pw, out ph);
    assert (pw == width && ph == 15 + 4);
    var pixels = new uint8[pw * ph * 4];
    mask.render (px, py, pixels, pw, ph, pw * 4);
    assert (alpha_at (pixels, pw * 4, width / 2, 14) > 0);
    assert (alpha_at (pixels, pw * 4, width / 2, 15) == 0);
    assert (alpha_at (pixels, pw * 4, 15, 15) > 0);
    assert (alpha_at (pixels, pw * 4, 0, 0) < alpha_at (pixels, pw * 4, width / 2, 14));

    var medium = Polo.Marco.ShadowMask.create (Polo.Marco.ShadowType.MEDIUM, Polo.Marco.SHADOW_OPACITY,
                                               geometry, FLAGS, 1);
    medium.get_bounds (out x, out y, out width, out height);
    assert (x == invisible.left - 9 && y == invisible.top - 7);
    assert (width == visible_width + 18 && height == visible_height + 18);
}

/* HiDPI buffers use the output scale, including corner and shadow clipping. */
void test_scale () {
    if (!GLib.FileUtils.test ("/usr/share/themes/Menta/metacity-1", GLib.FileTest.IS_DIR)) {
        GLib.Test.skip ("Menta is not installed");
        return;
    }
    var decorations = new Polo.Decor.Decorations ();
    assert (decorations.load_theme ("Menta"));
    decorations.set_fonts ("Sans 10", null);
    var normal = Polo.Marco.FrameType.NORMAL;
    var frame = new Polo.Decor.Frame (server, decorations);

    assert (frame.update (normal, FLAGS, 300, 200, "A title", 1));
    int width_at_one = frame.geometry.width, height_at_one = frame.geometry.height;
    assert (!frame.update (normal, FLAGS, 300, 200, "A title", 1));
    assert (frame.update (normal, FLAGS, 300, 200, "A title", 2));
    assert (frame.geometry.width == width_at_one && frame.geometry.height == height_at_one);
    Polo.Marco.Rect rect;
    int width, height, scale;
    frame.get_edge (Polo.Decor.Frame.Edge.TOP, out rect).get_size (out width, out height, out scale);
    assert (width == rect.width * 2 && height == rect.height * 2 && scale == 2);
    frame.get_edge (Polo.Decor.Frame.Edge.LEFT, out rect).get_size (out width, out height, out scale);
    assert (width == rect.width * 2 && height == rect.height * 2 && scale == 2);

    /* At scale 2, the 4-pixel corner is cut 8 pixels deep, one row at a time. */
    unowned Polo.Marco.Theme theme = decorations.get_theme ();
    int left = frame.geometry.borders.invisible.left * 2;
    int top = frame.geometry.borders.invisible.top * 2;
    int buffer_width = frame.geometry.width * 2;
    int buffer_height = frame.geometry.borders.total.top * 2;
    int stride = buffer_width * 4;
    var pixels = new uint8[stride * buffer_height];
    var states = new Polo.Marco.ButtonState[Polo.Marco.ButtonType.LAST];
    theme.render (decorations.get_colors (), normal, FLAGS, 300, 200, "A title", "Sans 10", null,
                  Polo.Decor.Decorations.DPI, decorations.button_layout, states, 0, 0, pixels,
                  buffer_width, buffer_height, stride, 2);
    int cut_row0 = 0, cut_row1 = 0;
    while (alpha_at (pixels, stride, left + cut_row0, top) == 0) {
        cut_row0++;
    }
    while (alpha_at (pixels, stride, left + cut_row1, top + 1) == 0) {
        cut_row1++;
    }
    assert (cut_row0 > 2 && cut_row1 < cut_row0);
    assert (alpha_at (pixels, stride, buffer_width / 2, top) != 0);

    var mask1 = Polo.Marco.ShadowMask.create (Polo.Marco.ShadowType.LARGE, Polo.Marco.SHADOW_OPACITY,
                                              frame.geometry, FLAGS, 1);
    var mask2 = Polo.Marco.ShadowMask.create (Polo.Marco.ShadowType.LARGE, Polo.Marco.SHADOW_OPACITY,
                                              frame.geometry, FLAGS, 2);
    int x1, y1, w1, h1, x2, y2, w2, h2;
    mask1.get_bounds (out x1, out y1, out w1, out h1);
    mask2.get_bounds (out x2, out y2, out w2, out h2);
    assert (x1 == x2 && y1 == y2 && w1 == w2 && h1 == h2);
    for (int i = 0; i < Polo.Marco.ShadowPiece.COUNT; i++) {
        mask1.get_piece ((Polo.Marco.ShadowPiece) i, out x1, out y1, out w1, out h1);
        mask2.get_piece ((Polo.Marco.ShadowPiece) i, out x2, out y2, out w2, out h2);
        assert (x1 == x2 && y1 == y2 && w1 == w2 && h1 == h2);
    }
    mask2.get_piece (Polo.Marco.ShadowPiece.TOP, out x2, out y2, out w2, out h2);
    var shadow = new uint8[w2 * 2 * h2 * 2 * 4];
    mask2.render (x2, y2, shadow, w2 * 2, h2 * 2, w2 * 2 * 4);
    /* Just above the frame's middle: shadow; in it, nothing. */
    assert (alpha_at (shadow, w2 * 2 * 4, w2, 15 * 2 - 1) > 0);
    assert (alpha_at (shadow, w2 * 2 * 4, w2, 15 * 2) == 0);
}

int main (string[] args) {
    GLib.Test.init (ref args);
    server = start_server ();
    GLib.Test.add_func ("/decor/no-theme", test_no_theme);
    GLib.Test.add_func ("/decor/frame", test_frame);
    GLib.Test.add_func ("/decor/rounded-corners", test_rounded_corners);
    GLib.Test.add_func ("/decor/shadow", test_shadow);
    GLib.Test.add_func ("/decor/scale", test_scale);
    int status = GLib.Test.run ();
    server.stop ();
    Polo.Marco.release_fonts ();
    return status;
}
