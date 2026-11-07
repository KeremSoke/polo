/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Wayfire's open, close and minimise animations against their formulas in
 * basic_animations.hpp, spin.hpp and zap.hpp.
 */

using Polo;
using Polo.Policy;

bool near (double a, double b) {
    return (a - b).abs () < 1e-6;
}

void test_parse () {
    assert (WindowAnimationKind.parse ("fade") == WindowAnimationKind.FADE);
    assert (WindowAnimationKind.parse ("zoom") == WindowAnimationKind.ZOOM);
    assert (WindowAnimationKind.parse ("zap") == WindowAnimationKind.ZAP);
    assert (WindowAnimationKind.parse ("spin") == WindowAnimationKind.SPIN);
    assert (WindowAnimationKind.parse ("fire") == WindowAnimationKind.NONE);
    assert (WindowAnimationKind.parse (null) == WindowAnimationKind.NONE);
}

void test_fade () {
    var open = new WindowAnimation (WindowAnimationKind.FADE, 400, true);
    assert (open.showing && near (open.frame ().alpha, 0));
    open.advance (200);
    assert (near (open.frame ().alpha, Math.sqrt (0.75)));
    assert (near (open.frame ().scale_x, 1) && !open.done);
    open.advance (200);
    assert (open.done && near (open.frame ().alpha, 1));

    var close = new WindowAnimation (WindowAnimationKind.FADE, 400, false);
    assert (!close.showing && near (close.frame ().alpha, 1));
    close.advance (200);
    assert (near (close.frame ().alpha, 1 - Math.sqrt (0.75)));
    close.advance (300);
    assert (close.done && near (close.frame ().alpha, 0));
}

void test_zoom () {
    var open = new WindowAnimation (WindowAnimationKind.ZOOM, 400, true);
    var start = open.frame ();
    assert (near (start.alpha, 0) && near (start.scale_x, 1.0 / 3) && near (start.scale_y, 1.0 / 3));
    open.advance (400);
    var end = open.frame ();
    assert (near (end.alpha, 1) && near (end.scale_x, 1) && near (end.dx, 0));

    var minimize = new WindowAnimation (WindowAnimationKind.ZOOM, 400, false);
    minimize.zoom_into (Marco.Rect () { x = 100, y = 100, width = 400, height = 300 },
                        Marco.Rect () { x = 10, y = 700, width = 40, height = 30 });
    assert (near (minimize.frame ().scale_x, 1) && near (minimize.frame ().dx, 0));
    minimize.advance (400);
    var hidden = minimize.frame ();
    assert (near (hidden.scale_x, 0.1) && near (hidden.alpha, 0));
    assert (near (hidden.dx, 30 - 300) && near (hidden.dy, 715 - 250));

    var no_target = new WindowAnimation (WindowAnimationKind.ZOOM, 400, false);
    no_target.zoom_into (Marco.Rect () { x = 0, y = 0, width = 100, height = 100 }, Marco.Rect ());
    assert (near (no_target.zoom_from, WindowAnimation.ZOOM_FROM) && near (no_target.offset_x, 0));
}

void test_spin () {
    var open = new WindowAnimation (WindowAnimationKind.SPIN, 250, true);
    open.advance (125);
    var half = open.frame ();
    assert (near (half.alpha, 0.5) && near (half.angle, Math.PI) && near (half.scale_x, 0.505));
    open.rotations = -2;
    assert (near (open.frame ().angle, -2 * Math.PI));

    var close = new WindowAnimation (WindowAnimationKind.SPIN, 250, false);
    assert (near (close.frame ().alpha, 1) && near (close.frame ().scale_x, 1));
    close.advance (250);
    assert (close.done && near (close.frame ().alpha, 0) && near (close.frame ().scale_x, 0.01));
}

void test_zap () {
    var open = new WindowAnimation (WindowAnimationKind.ZAP, 300, true);
    open.advance (50);
    var first = open.frame ();
    assert (near (first.alpha, 0.5) && near (first.scale_x, 0.01) && near (first.scale_y, 0.01));
    open.advance (100);
    var middle = open.frame ();
    assert (near (middle.alpha, 1) && near (middle.scale_x, 0.505) && near (middle.scale_y, 0.01));
    open.advance (150);
    var end = open.frame ();
    assert (near (end.scale_x, 1) && near (end.scale_y, 1));
}

void test_reverse () {
    var open = new WindowAnimation (WindowAnimationKind.ZOOM, 400, true);
    open.advance (120);
    double reached = open.shown;
    open.reverse ();
    assert (!open.showing && near (open.shown, reached));
    open.advance (60);
    assert (open.shown < reached && !open.done);
    open.advance (60);
    assert (open.done && near (open.shown, 0));

    var spin = new WindowAnimation (WindowAnimationKind.SPIN, 250, false);
    spin.advance (100);
    spin.reverse ();
    assert (spin.showing);
    spin.advance (100);
    assert (spin.done && near (spin.shown, 1));
}

void test_start_from () {
    foreach (var kind in new WindowAnimationKind[] { WindowAnimationKind.FADE, WindowAnimationKind.SPIN }) {
        var close = new WindowAnimation (kind, 400, false);
        close.start_from (0.4);
        assert (near (close.shown, 0.4) && !close.done);
        var open = new WindowAnimation (kind, 400, true);
        open.start_from (0.4);
        assert (near (open.shown, 0.4));
    }
}

int main (string[] args) {
    Test.init (ref args);
    Test.add_func ("/window-animation/parse", test_parse);
    Test.add_func ("/window-animation/fade", test_fade);
    Test.add_func ("/window-animation/zoom", test_zoom);
    Test.add_func ("/window-animation/spin", test_spin);
    Test.add_func ("/window-animation/zap", test_zap);
    Test.add_func ("/window-animation/reverse", test_reverse);
    Test.add_func ("/window-animation/start-from", test_start_from);
    return Test.run ();
}
