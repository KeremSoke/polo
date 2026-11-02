/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * VAPI coverage test for polo-marco.vapi: uses every
 * symbol, and checks the boxes port through Vala with cases taken from
 * Marco's testboxes.c.
 */

using Polo.Marco;

Rect rect (int x, int y, int width, int height) {
    return Rect () { x = x, y = y, width = width, height = height };
}

/* A 1600x1200 screen with the struts of testboxes.c get_strut_list (1). */
GLib.SList<Strut?> struts () {
    var list = new GLib.SList<Strut?> ();
    list.prepend (Strut () {
        rect = rect (0, 0, 1600, 20), side = Side.TOP, edge = EdgeType.SCREEN
    });
    list.prepend (Strut () {
        rect = rect (400, 1160, 1600, 40), side = Side.BOTTOM, edge = EdgeType.SCREEN
    });
    return list;
}

void test_rects () {
    var a = rect (0, 0, 10, 10);
    var b = rect (5, 5, 10, 10);
    Rect dest;

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
    Rect.resize_with_gravity (old_rect, ref resized, Gravity.SOUTH_EAST, 20, 5);
    assert (resized.equal (rect (280, 695, 20, 5)));
    resized = old_rect;
    Rect.resize_with_gravity (old_rect, ref resized, Gravity.CENTER, 350, 300);
    assert (resized.equal (rect (0, 350, 350, 300)));
    foreach (var gravity in new Gravity[] {
            Gravity.FORGET, Gravity.NORTH_WEST, Gravity.NORTH, Gravity.NORTH_EAST,
            Gravity.WEST, Gravity.EAST, Gravity.SOUTH_WEST, Gravity.SOUTH,
            Gravity.STATIC }) {
        resized = old_rect;
        Rect.resize_with_gravity (old_rect, ref resized, gravity, 10, 10);
        assert (resized.width == 10 && resized.height == 10);
    }
}

void test_regions () {
    var screen = rect (0, 0, 1600, 1200);
    var all_struts = struts ();

    /* testboxes.c test_regions_okay, strut list 1. */
    var region = Region.get_minimal_spanning_set (screen, all_struts, false);
    assert (region.length () == 2);
    assert (Region.contains (region, rect (0, 20, 400, 1180)));
    assert (Region.could_fit (region, rect (0, 0, 1600, 1140)));
    assert (Region.overlaps (region, rect (0, 0, 10, 30)));
    assert (!Region.contains (region, rect (0, 0, 10, 10)));

    var clamped = rect (0, 0, 3000, 3000);
    Region.clamp_to_fit (region, FixedDirections.NONE, ref clamped, rect (0, 0, 10, 10));
    assert (clamped.width == 1600 && clamped.height == 1140);

    var clipped = rect (-50, -50, 100, 100);
    Region.clip (region, FixedDirections.NONE, ref clipped);
    assert (clipped.equal (rect (0, 20, 50, 30)));

    var shoved = rect (-50, 1190, 100, 100);
    Region.shove (region, FixedDirections.X | FixedDirections.Y, ref shoved);
    Region.shove (region, FixedDirections.NONE, ref shoved);
    assert (Region.contains (region, shoved));

    region = Region.expand ((owned) region, 1, 2, 3, 4);
    region = Region.expand_conditionally ((owned) region, 1, 1, 1, 1, 100, 100);
    assert (region.length () == 2);

    var grown = rect (100, 100, 10, 10);
    Region.expand_to_avoiding_struts (ref grown, screen, Direction.HORIZONTAL, all_struts);
    assert (grown.x == 0 && grown.width == 1600);
    Region.expand_to_avoiding_struts (ref grown, screen, Direction.VERTICAL, all_struts);
    assert (grown.y == 20 && grown.height == 1140);
    assert ((Direction.UP == Direction.TOP) && (Direction.DOWN == Direction.BOTTOM));
    assert ((Direction.LEFT | Direction.RIGHT) == Direction.HORIZONTAL);

    /* Edges. */
    var edges = Region.find_onscreen_edges (screen, all_struts);
    assert (edges.length () > 0);
    unowned Edge? first = edges.data;
    assert (first.rect.width == 0 || first.rect.height == 0);
    assert (Region.edge_cmp (first, first) == 0);
    assert (Region.edge_cmp_ignore_type (first, first) == 0);
    var aligned = rect (0, 20, 100, 100);
    bool any_aligned = false;
    foreach (unowned Edge? edge in edges) {
        any_aligned |= aligned.edge_aligns (edge);
        assert (edge.side_type in new Side[] { Side.LEFT, Side.RIGHT, Side.TOP, Side.BOTTOM });
        assert (edge.edge_type == EdgeType.SCREEN || edge.edge_type == EdgeType.WINDOW ||
                edge.edge_type == EdgeType.XINERAMA);
    }
    assert (any_aligned);

    var windows = new GLib.SList<Rect?> ();
    windows.prepend (rect (0, 20, 300, 300));
    edges = Region.remove_intersections_with_boxes_from_edges ((owned) edges, windows);
    assert (edges.length () > 0);

    var monitors = new GLib.List<Rect?> ();
    monitors.append (rect (0, 0, 800, 1200));
    monitors.append (rect (800, 0, 800, 1200));
    var xinerama = Region.find_nonintersected_xinerama_edges (screen, monitors, all_struts);
    assert (xinerama.length () == 2);

    double vx, vy;
    Region.find_linepoint_closest_to_point (0, 0, 10, 0, 5, 5, out vx, out vy);
    assert (vx == 5 && vy == 0);
}

int main () {
    assert (reference_version () == "1.28.2");
    test_rects ();
    test_regions ();
    return 0;
}
