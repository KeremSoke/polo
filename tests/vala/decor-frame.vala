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
                  width, height, stride);
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

int main (string[] args) {
    GLib.Test.init (ref args);
    server = start_server ();
    GLib.Test.add_func ("/decor/no-theme", test_no_theme);
    GLib.Test.add_func ("/decor/frame", test_frame);
    GLib.Test.add_func ("/decor/rounded-corners", test_rounded_corners);
    int status = GLib.Test.run ();
    server.stop ();
    Polo.Marco.release_fonts ();
    return status;
}
