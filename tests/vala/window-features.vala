/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Size-hint normalisation (meta_set_normal_hints), window-type inference
 * (recalc_window_type) and window features (recalc_window_features).
 */

using Polo;

class FakeAdapter : GLib.Object, WindowAdapter {
    public void configure (Marco.Rect frame) {}
    public void set_activated (bool activated) {}
    public void set_maximized (bool maximized) {}
    public void set_fullscreen (bool fullscreen) {}
    public void take_focus () {}
    public void close () {}
    public void ping () {}
}

const uint32 MIN = Marco.SizeHintFlags.P_MIN_SIZE;
const uint32 MAX = Marco.SizeHintFlags.P_MAX_SIZE;
const uint32 INC = Marco.SizeHintFlags.P_RESIZE_INC;
const uint32 BASE = Marco.SizeHintFlags.P_BASE_SIZE;
const uint32 ASPECT = Marco.SizeHintFlags.P_ASPECT;

Marco.SizeHints normalized (Marco.SizeHints raw) {
    var h = raw;
    h.normalize ();
    return h;
}

void test_normalize () {
    var h = normalized (Marco.SizeHints ());
    assert (h.min_width == 1 && h.min_height == 1);
    assert (h.max_width == int.MAX && h.max_height == int.MAX);
    assert (h.base_width == 0 && h.base_height == 0);
    assert (h.width_inc == 1 && h.height_inc == 1);
    assert (h.min_aspect.x == 1 && h.min_aspect.y == int.MAX);
    assert (h.max_aspect.x == int.MAX && h.max_aspect.y == 1);
    assert (h.win_gravity == Marco.Gravity.NORTH_WEST);
    assert ((h.flags & (MIN | MAX | INC | BASE | ASPECT | Marco.SizeHintFlags.P_WIN_GRAVITY)) ==
            (MIN | MAX | INC | BASE | ASPECT | Marco.SizeHintFlags.P_WIN_GRAVITY));

    h = normalized (Marco.SizeHints () { flags = MIN, min_width = 200, min_height = 100 });
    assert (h.base_width == 200 && h.base_height == 100);

    h = normalized (Marco.SizeHints () { flags = BASE, base_width = 30, base_height = 40 });
    assert (h.min_width == 30 && h.min_height == 40);

    h = normalized (Marco.SizeHints () { flags = MIN | MAX, min_width = 300, min_height = 0,
                                         max_width = 100, max_height = 0 });
    assert (h.min_width == 300 && h.max_width == 300);
    assert (h.min_height == 1 && h.max_height == 1);

    h = normalized (Marco.SizeHints () { flags = MIN | MAX | INC | BASE,
                                         base_width = 4, base_height = 4,
                                         width_inc = 10, height_inc = 20,
                                         min_width = 25, min_height = 4,
                                         max_width = 107, max_height = 100 });
    assert (h.min_width == 34 && h.max_width == 104);
    assert (h.min_height == 4 && h.max_height == 84);

    h = normalized (Marco.SizeHints () { flags = INC, width_inc = 0, height_inc = -3 });
    assert (h.width_inc == 1 && h.height_inc == 1);

    h = normalized (Marco.SizeHints () {
        flags = ASPECT, min_aspect = Marco.Aspect () { x = 2, y = 1 },
        max_aspect = Marco.Aspect () { x = 1, y = 1 } });
    assert (h.min_aspect.x == 1 && h.min_aspect.y == int.MAX);
    assert (h.max_aspect.x == int.MAX && h.max_aspect.y == 1);

    h = normalized (Marco.SizeHints () {
        flags = ASPECT | MIN | MAX, min_width = 100, min_height = 100, max_width = 100,
        max_height = 200, min_aspect = Marco.Aspect () { x = 2, y = 1 },
        max_aspect = Marco.Aspect () { x = 3, y = 0 } });
    assert (h.max_aspect.y == 1);
    assert (h.min_aspect.x == 1 && h.min_aspect.y == int.MAX);

    h = normalized (Marco.SizeHints () {
        flags = ASPECT | MIN, min_width = 400, min_height = 100,
        min_aspect = Marco.Aspect () { x = 1, y = 2 }, max_aspect = Marco.Aspect () { x = 1, y = 1 } });
    assert (h.min_aspect.x == 1 && h.min_aspect.y == 2);
    assert (h.max_aspect.x == 1 && h.max_aspect.y == 1);
}

Policy.Model new_model () {
    var model = new Policy.Model (MarcoPrefs.defaults ());
    model.add_output (new Policy.Output ("eDP-1", Marco.Rect () { width = 1920, height = 1080 }));
    return model;
}

Window xdg_window (Policy.Model model, int min_w = 0, int min_h = 0, int max_w = 0, int max_h = 0) {
    var window = new Window (WindowKind.XDG, new FakeAdapter ());
    window.mwm_decorated = false;
    window.frame = Marco.Rect () { width = 400, height = 300 };
    var hints = Marco.SizeHints ();
    if (min_w > 0 || min_h > 0) {
        hints.flags |= MIN;
        hints.min_width = min_w;
        hints.min_height = min_h;
    }
    if (max_w > 0 || max_h > 0) {
        hints.flags |= MAX;
        hints.max_width = max_w > 0 ? max_w : int.MAX;
        hints.max_height = max_h > 0 ? max_h : int.MAX;
    }
    model.set_size_hints (window, hints);
    model.manage (window);
    return window;
}

void test_window_type () {
    var model = new_model ();
    var parent = xdg_window (model);
    model.map (parent);
    assert (parent.window_type == Marco.WindowType.NORMAL);

    var child = xdg_window (model);
    model.set_transient_for (child, parent);
    model.map (child);
    assert (child.window_type == Marco.WindowType.DIALOG);
    assert (child.has_state (WindowState.SKIP_TASKBAR));
    assert (!child.has_state (WindowState.SKIP_PAGER));

    model.set_state (child, WindowState.MODAL, true);
    model.recalc_window_type (child);
    assert (child.window_type == Marco.WindowType.MODAL_DIALOG);

    model.set_transient_for (child, null);
    assert (child.window_type == Marco.WindowType.NORMAL);
    assert (!child.has_state (WindowState.SKIP_TASKBAR));

    model.set_transient_for (parent, child);
    model.set_transient_for (child, parent);
    assert (child.transient_for == null);

    var utility = xdg_window (model);
    utility.type_hint = Marco.WindowType.UTILITY;
    model.set_transient_for (utility, parent);
    model.map (utility);
    assert (utility.window_type == Marco.WindowType.UTILITY);
    assert (utility.has_state (WindowState.SKIP_TASKBAR | WindowState.SKIP_PAGER));
    assert (!utility.has_feature (WindowFeatures.MAXIMIZE));
    assert (!utility.has_feature (WindowFeatures.MINIMIZE));
    assert (utility.has_feature (WindowFeatures.MOVE | WindowFeatures.RESIZE | WindowFeatures.CLOSE));

    var plain = xdg_window (model);
    plain.wm_state_skip_pager = true;
    model.map (plain);
    assert (plain.has_state (WindowState.SKIP_PAGER) && !plain.has_state (WindowState.SKIP_TASKBAR));
}

void test_features () {
    var model = new_model ();

    var normal = xdg_window (model, 200, 100);
    model.map (normal);
    var all = WindowFeatures.CLOSE | WindowFeatures.MINIMIZE | WindowFeatures.MAXIMIZE |
              WindowFeatures.MOVE | WindowFeatures.RESIZE | WindowFeatures.FULLSCREEN;
    assert (normal.features == all);

    var fixed_size = xdg_window (model, 300, 200, 300, 200);
    model.map (fixed_size);
    assert (!fixed_size.has_feature (WindowFeatures.RESIZE));
    assert (!fixed_size.has_feature (WindowFeatures.MAXIMIZE));
    assert (!fixed_size.has_feature (WindowFeatures.FULLSCREEN));
    model.request_state (fixed_size, WindowState.MAXIMIZED, true);
    model.request_state (fixed_size, WindowState.FULLSCREEN, true);
    assert (!fixed_size.has_state (WindowState.MAXIMIZED_H));
    assert (!fixed_size.has_state (WindowState.FULLSCREEN));

    var screen_sized = xdg_window (model, 1920, 1080, 1920, 1080);
    model.map (screen_sized);
    assert (screen_sized.has_feature (WindowFeatures.FULLSCREEN));
    assert (!screen_sized.has_feature (WindowFeatures.MAXIMIZE));

    var wide_only = xdg_window (model, 0, 0, 800, 0);
    model.map (wide_only);
    assert (wide_only.hints.max_width == 800 && wide_only.hints.max_height == int.MAX);
    assert (wide_only.has_feature (WindowFeatures.RESIZE | WindowFeatures.MAXIMIZE));

    model.request_state (normal, WindowState.SHADED, true);
    assert (!normal.has_state (WindowState.SHADED));
    assert (!normal.has_feature (WindowFeatures.SHADE));

    model.request_state (normal, WindowState.FULLSCREEN, true);
    assert (normal.has_state (WindowState.FULLSCREEN));
    assert (!normal.has_feature (WindowFeatures.MOVE));
    assert (!normal.has_feature (WindowFeatures.RESIZE));
    assert (!normal.has_feature (WindowFeatures.MAXIMIZE));
    model.request_state (normal, WindowState.FULLSCREEN, false);
    assert (normal.features == all);

    var decorated = new Window (WindowKind.X11, new FakeAdapter ());
    model.manage (decorated);
    model.map (decorated);
    assert (decorated.has_feature (WindowFeatures.SHADE | WindowFeatures.DECORATED));

    var no_max = new Window (WindowKind.X11, new FakeAdapter ());
    no_max.mwm_functions = WindowFeatures.CLOSE | WindowFeatures.MOVE;
    model.manage (no_max);
    model.map (no_max);
    assert (no_max.has_feature (WindowFeatures.RESIZE));
    assert (!no_max.has_feature (WindowFeatures.MAXIMIZE | WindowFeatures.MINIMIZE));

    var dock = new Window (WindowKind.X11, new FakeAdapter ());
    dock.type_hint = Marco.WindowType.DOCK;
    model.manage (dock);
    model.map (dock);
    assert (dock.features == WindowFeatures.ALWAYS_STICKY);
    assert (dock.has_state (WindowState.STICKY | WindowState.SKIP_TASKBAR | WindowState.SKIP_PAGER));

    var toolbar = new Window (WindowKind.X11, new FakeAdapter ());
    toolbar.type_hint = Marco.WindowType.TOOLBAR;
    model.manage (toolbar);
    model.map (toolbar);
    assert (!toolbar.has_feature (WindowFeatures.DECORATED));
    assert (!toolbar.has_feature (WindowFeatures.SHADE));
}

/* A shaded frame's outer rect is only its titlebar. */
void test_shaded_outer_rect () {
    var window = new Window (WindowKind.XDG, new FakeAdapter ());
    window.borders = Marco.FrameBorders () { left = 2, right = 2, top = 25, bottom = 3 };
    window.frame = Marco.Rect () { x = 100, y = 50, width = 304, height = 228 };
    var outer = window.outer_rect ();
    assert (outer.x == 100 && outer.y == 50 && outer.width == 304 && outer.height == 228);
    var full = Policy.Placement.frame_rect (window);

    window.set_state_flags (WindowState.SHADED, true);
    outer = window.outer_rect ();
    assert (outer.x == 100 && outer.y == 50 && outer.width == 304 && outer.height == 25);
    assert (window.client_rect ().height == 200);
    assert (Policy.Placement.frame_rect (window).height <= full.height - 200);

    window.features &= ~WindowFeatures.DECORATED;
    assert (window.outer_rect ().height == 228);
}

int main (string[] args) {
    Test.init (ref args);
    Test.add_func ("/window-features/normalize", test_normalize);
    Test.add_func ("/window-features/window-type", test_window_type);
    Test.add_func ("/window-features/features", test_features);
    Test.add_func ("/window-features/shaded-outer-rect", test_shaded_outer_rect);
    return Test.run ();
}
