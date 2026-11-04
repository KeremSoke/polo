/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * VAPI coverage test for polo-marco.vapi: uses every
 * symbol, and checks the boxes port through Vala with cases taken from
 * Marco's testboxes.c.
 */

Polo.Marco.Rect rect (int x, int y, int width, int height) {
    return Polo.Marco.Rect () { x = x, y = y, width = width, height = height };
}

/* A 1600x1200 screen with the struts of testboxes.c get_strut_list (1). */
GLib.SList<Polo.Marco.Strut?> struts () {
    var list = new GLib.SList<Polo.Marco.Strut?> ();
    list.prepend (Polo.Marco.Strut () {
        rect = rect (0, 0, 1600, 20), side = Polo.Marco.Side.TOP, edge = Polo.Marco.EdgeType.SCREEN
    });
    list.prepend (Polo.Marco.Strut () {
        rect = rect (400, 1160, 1600, 40), side = Polo.Marco.Side.BOTTOM, edge = Polo.Marco.EdgeType.SCREEN
    });
    return list;
}

void test_rects () {
    var a = rect (0, 0, 10, 10);
    var b = rect (5, 5, 10, 10);
    Polo.Marco.Rect dest;

    assert (a.area () == 100);
    assert (a.intersect (b, out dest) && dest.equal (rect (5, 5, 5, 5)));
    assert (!a.intersect (rect (20, 20, 1, 1), out dest));
    a.union (b, out dest);
    assert (dest.equal (rect (0, 0, 15, 15)));
    assert (a.overlap (b) && a.vert_overlap (b) && a.horiz_overlap (b));
    assert (!a.overlap (rect (10, 0, 5, 5)));
    assert (rect (0, 0, 100, 100).could_fit_rect (rect (50, 50, 60, 60)));
    assert (!rect (0, 0, 100, 100).contains_rect (rect (50, 50, 60, 60)));
    assert (rect (0, 0, 100, 100).contains_rect (rect (10, 10, 60, 60)));

    /* testboxes.c test_gravity_resize: the bottom-right corner stays. */
    var old_rect = rect (50, 300, 250, 400);
    var resized = old_rect;
    Polo.Marco.Rect.resize_with_gravity (old_rect, ref resized, Polo.Marco.Gravity.SOUTH_EAST, 20, 5);
    assert (resized.equal (rect (280, 695, 20, 5)));
    resized = old_rect;
    Polo.Marco.Rect.resize_with_gravity (old_rect, ref resized, Polo.Marco.Gravity.CENTER, 350, 300);
    assert (resized.equal (rect (0, 350, 350, 300)));
    foreach (var gravity in new Polo.Marco.Gravity[] {
            Polo.Marco.Gravity.FORGET, Polo.Marco.Gravity.NORTH_WEST, Polo.Marco.Gravity.NORTH,
            Polo.Marco.Gravity.NORTH_EAST, Polo.Marco.Gravity.WEST, Polo.Marco.Gravity.EAST,
            Polo.Marco.Gravity.SOUTH_WEST, Polo.Marco.Gravity.SOUTH,
            Polo.Marco.Gravity.STATIC }) {
        resized = old_rect;
        Polo.Marco.Rect.resize_with_gravity (old_rect, ref resized, gravity, 10, 10);
        assert (resized.width == 10 && resized.height == 10);
    }
}

void test_regions () {
    var screen = rect (0, 0, 1600, 1200);
    var all_struts = struts ();

    /* testboxes.c test_regions_okay, strut list 1. */
    var region = Polo.Marco.Region.get_minimal_spanning_set (screen, all_struts, false);
    assert (region.length () == 2);
    assert (Polo.Marco.Region.contains (region, rect (0, 20, 400, 1180)));
    assert (Polo.Marco.Region.could_fit (region, rect (0, 0, 1600, 1140)));
    assert (Polo.Marco.Region.overlaps (region, rect (0, 0, 10, 30)));
    assert (!Polo.Marco.Region.contains (region, rect (0, 0, 10, 10)));

    var clamped = rect (0, 0, 3000, 3000);
    Polo.Marco.Region.clamp_to_fit (region, Polo.Marco.FixedDirections.NONE, ref clamped, rect (0, 0, 10, 10));
    assert (clamped.width == 1600 && clamped.height == 1140);

    var clipped = rect (-50, -50, 100, 100);
    Polo.Marco.Region.clip (region, Polo.Marco.FixedDirections.NONE, ref clipped);
    assert (clipped.equal (rect (0, 20, 50, 30)));

    var shoved = rect (-50, 1190, 100, 100);
    Polo.Marco.Region.shove (region, Polo.Marco.FixedDirections.X | Polo.Marco.FixedDirections.Y, ref shoved);
    Polo.Marco.Region.shove (region, Polo.Marco.FixedDirections.NONE, ref shoved);
    assert (Polo.Marco.Region.contains (region, shoved));

    region = Polo.Marco.Region.expand ((owned) region, 1, 2, 3, 4);
    region = Polo.Marco.Region.expand_conditionally ((owned) region, 1, 1, 1, 1, 100, 100);
    assert (region.length () == 2);

    var grown = rect (100, 100, 10, 10);
    Polo.Marco.Region.expand_to_avoiding_struts (ref grown, screen, Polo.Marco.Direction.HORIZONTAL, all_struts);
    assert (grown.x == 0 && grown.width == 1600);
    Polo.Marco.Region.expand_to_avoiding_struts (ref grown, screen, Polo.Marco.Direction.VERTICAL, all_struts);
    assert (grown.y == 20 && grown.height == 1140);
    assert (Polo.Marco.Direction.UP == Polo.Marco.Direction.TOP);
    assert (Polo.Marco.Direction.DOWN == Polo.Marco.Direction.BOTTOM);
    assert ((Polo.Marco.Direction.LEFT | Polo.Marco.Direction.RIGHT) == Polo.Marco.Direction.HORIZONTAL);

    /* Edges. */
    var edges = Polo.Marco.Region.find_onscreen_edges (screen, all_struts);
    assert (edges.length () > 0);
    unowned Polo.Marco.Edge? first = edges.data;
    assert (first.rect.width == 0 || first.rect.height == 0);
    assert (Polo.Marco.Region.edge_cmp (first, first) == 0);
    assert (Polo.Marco.Region.edge_cmp_ignore_type (first, first) == 0);
    var aligned = rect (0, 20, 100, 100);
    bool any_aligned = false;
    foreach (unowned Polo.Marco.Edge? edge in edges) {
        any_aligned |= aligned.edge_aligns (edge);
        assert (edge.side_type in new Polo.Marco.Side[] {
            Polo.Marco.Side.LEFT, Polo.Marco.Side.RIGHT, Polo.Marco.Side.TOP, Polo.Marco.Side.BOTTOM
        });
        assert (edge.edge_type == Polo.Marco.EdgeType.SCREEN || edge.edge_type == Polo.Marco.EdgeType.WINDOW ||
                edge.edge_type == Polo.Marco.EdgeType.XINERAMA);
    }
    assert (any_aligned);

    var windows = new GLib.SList<Polo.Marco.Rect?> ();
    windows.prepend (rect (0, 20, 300, 300));
    edges = Polo.Marco.Region.remove_intersections_with_boxes_from_edges ((owned) edges, windows);
    assert (edges.length () > 0);

    var monitors = new GLib.List<Polo.Marco.Rect?> ();
    monitors.append (rect (0, 0, 800, 1200));
    monitors.append (rect (800, 0, 800, 1200));
    var xinerama = Polo.Marco.Region.find_nonintersected_xinerama_edges (screen, monitors, all_struts);
    assert (xinerama.length () == 2);

    double vx, vy;
    Polo.Marco.Region.find_linepoint_closest_to_point (0, 0, 10, 0, 5, 5, out vx, out vy);
    assert (vx == 5 && vy == 0);
}

void test_theme () {
    try {
        Polo.Marco.Theme.load ("PoloNoSuchTheme");
        assert_not_reached ();
    } catch (GLib.Error e) {
        assert (e.message != "");
    }
    if (!GLib.FileUtils.test ("/usr/share/themes/Atlanta/metacity-1", GLib.FileTest.IS_DIR)) {
        return;
    }
    Polo.Marco.Theme theme;
    try {
        theme = Polo.Marco.Theme.load ("Atlanta");
    } catch (GLib.Error e) {
        GLib.error ("Atlanta: %s", e.message);
    }
    var flags = Polo.Marco.FrameFlags.HAS_FOCUS | Polo.Marco.FrameFlags.ALLOWS_DELETE;
    assert (theme.get_frame_style (Polo.Marco.FrameType.NORMAL, flags) != null);
    Polo.Marco.ThemeBorders borders;
    theme.get_frame_borders (Polo.Marco.FrameType.NORMAL, 14, flags, out borders);
    assert (borders.visible.top > borders.visible.bottom);
    Polo.Marco.ButtonLayout layout;
    Polo.Marco.parse_button_layout ("menu:minimize,maximize,close", false, out layout);
    Polo.Marco.FrameGeometry geometry;
    theme.calc_geometry (Polo.Marco.FrameType.NORMAL, 14, flags, 400, 300, layout, out geometry);
    assert (geometry.width == 400 + geometry.borders.total.left + geometry.borders.total.right);
    assert (geometry.close_rect.visible.width > 0 && geometry.n_right_buttons == 1);
    assert (geometry.title_rect.width > 0);
}

void test_label () {
    int width, height;
    Polo.Marco.label_size ("Locked", null, out width, out height);
    assert (width > 0 && height > 0);
    var pixels = new uint8[width * height * 4];
    Polo.Marco.label_render ("Locked", null, width, height, pixels, width * 4);
    assert (pixels[3] == 0xff);
    Polo.Marco.release_fonts ();
}

int main () {
    assert (Polo.Marco.reference_version () == "1.28.2");
    test_rects ();
    test_regions ();
    test_theme ();
    test_label ();
    return 0;
}
