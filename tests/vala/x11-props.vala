/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * set_title_text () and meta_window_compute_group ().
 */

using Polo;
using Polo.Policy;

void check_title (string? title, string? machine, string? owner, string expected, bool expected_modified) {
    bool modified;
    var result = X11Props.format_title (title, machine, "here", owner, out modified);
    if (result != expected || modified != expected_modified) {
        GLib.error ("got '%s' (%s), wanted '%s'", result, modified.to_string (), expected);
    }
}

void test_titles () {
    check_title (null, null, null, "", false);
    check_title ("", "elsewhere", null, "", false);
    check_title ("t", null, null, "t", false);
    check_title ("t", "here", null, "t", false);
    check_title ("t", "elsewhere", null, "t (on elsewhere)", true);
    check_title ("t", "elsewhere", "", "t (on elsewhere)", true);
    check_title ("t", "here", "", "t (as superuser)", true);
    check_title ("t", null, "bob", "t (as bob)", true);
    check_title ("t", null, "?", "t (as another user)", true);
}

void test_long_title () {
    var ascii = new GLib.StringBuilder ();
    var wide = new GLib.StringBuilder ();
    for (int i = 0; i < 600; i++) {
        ascii.append_c ('a');
        wide.append ("\xc3\xbc");
    }
    bool modified;
    var title = X11Props.format_title (ascii.str, "elsewhere", "here", null, out modified);
    assert (modified);
    assert (title.length == X11Props.MAX_TITLE_LENGTH);

    title = X11Props.format_title (wide.str, null, "here", null, out modified);
    assert (!modified && title == wide.str);

    ascii.truncate (X11Props.MAX_TITLE_LENGTH);
    title = X11Props.format_title (ascii.str, null, "here", null, out modified);
    assert (!modified && title == ascii.str);
}

void test_group () {
    assert (X11Props.compute_group (0, 0, 7) == 7);
    assert (X11Props.compute_group (0, 9, 7) == 9);
    assert (X11Props.compute_group (5, 9, 7) == 5);
}

void test_struts () {
    var outputs = new GLib.GenericArray<Output> ();
    outputs.add (new Output ("A", Marco.Rect () { width = 1000, height = 800 }));
    var screen = Marco.Rect () { width = 1000, height = 800 };
    var simple = X11Props.parse_struts ({}, {}, { 0, 10, 20, 0 }, screen, outputs);
    assert (simple.length == 2);
    assert (simple[0].side == Marco.Side.RIGHT && simple[0].rect.x == 990 && simple[0].rect.width == 10);
    assert (simple[1].side == Marco.Side.TOP && simple[1].rect.height == 20 && simple[1].rect.width == 1000);
    assert (simple[0].edge == Marco.EdgeType.SCREEN);

    var partial = X11Props.parse_struts ({}, { 30, 0, 0, 40, 100, 199, 0, 0, 0, 0, 300, 399 }, { 1, 1, 1, 1 },
                                         screen, outputs);
    assert (partial.length == 2);
    assert (partial[0].side == Marco.Side.LEFT && partial[0].rect.y == 100 && partial[0].rect.height == 100);
    assert (partial[1].side == Marco.Side.BOTTOM && partial[1].rect.y == 760 && partial[1].rect.x == 300 &&
            partial[1].rect.width == 100);

    var area = X11Props.parse_struts ({ 0, 0, 1000, 24 }, { 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0 }, {}, screen, outputs);
    assert (area.length == 1 && area[0].side == Marco.Side.TOP && area[0].edge == Marco.EdgeType.XINERAMA);

    var bad_area = X11Props.parse_struts ({ 5, 5, 10, 10 }, {}, { 0, 0, 7, 0 }, screen, outputs);
    assert (bad_area.length == 1 && bad_area[0].rect.height == 7);
    assert (X11Props.parse_struts ({}, { 1, 2 }, { 1 }, screen, outputs).length == 0);
}

void test_strut_abuse () {
    var outputs = new GLib.GenericArray<Output> ();
    outputs.add (new Output ("A", Marco.Rect () { width = 1000, height = 800 }));
    var screen = Marco.Rect () { width = 1000, height = 800 };
    uint32 max = uint32.MAX;
    assert (X11Props.parse_struts ({}, {}, { max, max, max, max }, screen, outputs).length == 0);
    assert (X11Props.parse_struts ({}, { max, 0, 0, 0, 0, 10, 0, 0, 0, 0, 0, 0 }, {}, screen, outputs).length == 0);
    assert (X11Props.parse_struts ({}, { 0x80000000u, 0x7fffffff, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, {}, screen,
                                   outputs).length == 0);
    assert (X11Props.parse_struts ({}, { 10, 0, 0, 0, 500, 100, 0, 0, 0, 0, 0, 0 }, {}, screen, outputs).length == 0);
    assert (X11Props.parse_struts ({}, { 10, 0, 0, 0, 0, max, 0, 0, 0, 0, 0, 0 }, {}, screen, outputs).length == 0);
    assert (X11Props.parse_struts ({ max, 0, 1000, 24 }, {}, {}, screen, outputs).length == 0);
    var whole = X11Props.parse_struts ({}, { 1000, 1000, 800, 800, 0, 799, 0, 799, 0, 999, 0, 999 }, {}, screen,
                                       outputs);
    assert (whole.length == 4);
    var huge_but_legal = X11Props.parse_struts ({}, {}, { 1 << 20, 0, 0, 0 }, screen, outputs);
    assert (huge_but_legal.length == 1);
    var one_more = X11Props.parse_struts ({}, {}, { (1 << 20) + 1, 0, 0, 0 }, screen, outputs);
    assert (one_more.length == 0);
}

void test_state_after () {
    assert (X11Props.state_after (1, false) && X11Props.state_after (1, true));
    assert (!X11Props.state_after (0, true) && !X11Props.state_after (0, false));
    assert (X11Props.state_after (2, false) && !X11Props.state_after (2, true));
}

void check_functions (uint32 flags, uint32 functions, WindowFeatures expected) {
    var result = X11Props.motif_functions (flags, functions);
    if (result != expected) {
        GLib.error ("flags %u functions %u: got %d, wanted %d", flags, functions, result, expected);
    }
}

void test_motif () {
    const WindowFeatures ALL = WindowFeatures.MWM_FUNCTIONS;
    check_functions (0, 0, ALL);
    check_functions (2, 0, ALL);
    check_functions (1, 0, 0);
    check_functions (1, 1, ALL);
    check_functions (1, 2, WindowFeatures.RESIZE);
    check_functions (1, 4, WindowFeatures.MOVE);
    check_functions (1, 8, WindowFeatures.MINIMIZE);
    check_functions (1, 16, WindowFeatures.MAXIMIZE);
    check_functions (1, 32, WindowFeatures.CLOSE);
    check_functions (1, 33, ALL & ~WindowFeatures.CLOSE);
    check_functions (1, 21, ALL & ~(WindowFeatures.MOVE | WindowFeatures.MAXIMIZE));

    bool decorated, border_only;
    X11Props.motif_decorations (0, 0, out decorated, out border_only);
    assert (decorated && !border_only);
    X11Props.motif_decorations (2, 0, out decorated, out border_only);
    assert (!decorated && !border_only);
    X11Props.motif_decorations (2, 1, out decorated, out border_only);
    assert (decorated && !border_only);
    X11Props.motif_decorations (2, 2, out decorated, out border_only);
    assert (decorated && border_only);
    X11Props.motif_decorations (2, 3, out decorated, out border_only);
    assert (decorated && !border_only);
    X11Props.motif_decorations (1, 2, out decorated, out border_only);
    assert (decorated && !border_only);
}

public static int main (string[] args) {
    GLib.Test.init (ref args);
    GLib.Test.add_func ("/x11-props/titles", test_titles);
    GLib.Test.add_func ("/x11-props/long-title", test_long_title);
    GLib.Test.add_func ("/x11-props/group", test_group);
    GLib.Test.add_func ("/x11-props/struts", test_struts);
    GLib.Test.add_func ("/x11-props/strut-abuse", test_strut_abuse);
    GLib.Test.add_func ("/x11-props/state-after", test_state_after);
    GLib.Test.add_func ("/x11-props/motif", test_motif);
    return GLib.Test.run ();
}
