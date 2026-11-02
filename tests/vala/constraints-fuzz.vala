/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Random outputs, panels, size hints and requests through the
 * constraints.c port: no window the user did not drag may end with its
 * titlebar out of reach (Marco's require_titlebar_visible test: the
 * titlebar overlaps the usable screen region, and its top is inside it).
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

Marco.Rect rect (int x, int y, int width, int height) {
    return Marco.Rect () { x = x, y = y, width = width, height = height };
}

int checked = 0;

bool titlebar_reachable (Policy.Model model, Window w) {
    if (!w.has_frame () || w.has_state (WindowState.FULLSCREEN) || w.borders.top == 0) {
        return true;
    }
    checked++;
    var titlebar = rect (w.frame.x, w.frame.y, w.frame.width, w.borders.top);
    var top_line = rect (w.frame.x, w.frame.y, w.frame.width, 1);
    bool overlaps = false;
    bool top_inside = false;
    foreach (unowned Marco.Rect? r in model.work_areas ().screen_region) {
        Marco.Rect dest;
        overlaps |= titlebar.intersect (r, out dest);
        top_inside |= top_line.intersect (r, out dest);
    }
    return overlaps && top_inside;
}

Marco.SizeHints random_hints (GLib.Rand rand) {
    var h = Marco.SizeHints ();
    if (rand.boolean ()) {
        h.flags |= Marco.SizeHintFlags.P_MIN_SIZE;
        h.min_width = rand.int_range (0, 1400);
        h.min_height = rand.int_range (0, 900);
    }
    if (rand.boolean ()) {
        h.flags |= Marco.SizeHintFlags.P_MAX_SIZE;
        h.max_width = rand.int_range (0, 3000);
        h.max_height = rand.int_range (0, 2000);
    }
    if (rand.int_range (0, 4) == 0) {
        h.flags |= Marco.SizeHintFlags.P_RESIZE_INC;
        h.width_inc = rand.int_range (0, 40);
        h.height_inc = rand.int_range (0, 40);
    }
    if (rand.int_range (0, 4) == 0) {
        h.flags |= Marco.SizeHintFlags.P_BASE_SIZE;
        h.base_width = rand.int_range (0, 200);
        h.base_height = rand.int_range (0, 200);
    }
    if (rand.int_range (0, 5) == 0) {
        h.flags |= Marco.SizeHintFlags.P_ASPECT;
        h.min_aspect = Marco.Aspect () { x = rand.int_range (0, 5), y = rand.int_range (0, 5) };
        h.max_aspect = Marco.Aspect () { x = rand.int_range (0, 5), y = rand.int_range (0, 5) };
    }
    if (rand.int_range (0, 3) == 0) {
        h.flags |= Marco.SizeHintFlags.P_WIN_GRAVITY;
        h.win_gravity = (Marco.Gravity) rand.int_range (1, 11);
    }
    if (rand.boolean ()) {
        h.flags |= rand.boolean () ? Marco.SizeHintFlags.P_POSITION : Marco.SizeHintFlags.US_POSITION;
    }
    return h;
}

void run_case (uint32 seed) {
    var rand = new GLib.Rand.with_seed (seed);
    var model = new Policy.Model (MarcoPrefs.defaults ());

    int n_outputs = rand.int_range (1, 4);
    int x = 0;
    var output_rects = new Marco.Rect[n_outputs];
    for (int i = 0; i < n_outputs; i++) {
        output_rects[i] = rect (x, rand.int_range (0, 2) * rand.int_range (0, 400),
                                rand.int_range (800, 2600), rand.int_range (600, 1600));
        model.add_output (new Policy.Output ("out-%d".printf (i), output_rects[i]));
        x += output_rects[i].width;
    }

    int n_panels = rand.int_range (0, 4);
    for (int i = 0; i < n_panels; i++) {
        var o = output_rects[rand.int_range (0, n_outputs)];
        int t = rand.int_range (16, 60);
        Marco.Rect area;
        Marco.Side side;
        switch (rand.int_range (0, 4)) {
            case 0: area = rect (o.x, o.y, o.width, t); side = Marco.Side.TOP; break;
            case 1: area = rect (o.x, o.y + o.height - t, o.width, t); side = Marco.Side.BOTTOM; break;
            case 2: area = rect (o.x, o.y, t, o.height); side = Marco.Side.LEFT; break;
            default: area = rect (o.x + o.width - t, o.y, t, o.height); side = Marco.Side.RIGHT; break;
        }
        var dock = new Window (WindowKind.X11, new FakeAdapter ());
        dock.type_hint = Marco.WindowType.DOCK;
        dock.accepts_focus = false;
        dock.frame = area;
        dock.struts = { Marco.Strut () { rect = area, side = side, edge = Marco.EdgeType.XINERAMA } };
        model.manage (dock);
        model.map (dock);
    }

    Window[] windows = {};
    for (int i = 0; i < 8; i++) {
        var w = new Window (WindowKind.X11, new FakeAdapter ());
        w.label = "w%d".printf (i);
        w.borders = Marco.FrameBorders () {
            left = rand.int_range (0, 6), right = rand.int_range (0, 6),
            top = rand.int_range (18, 40), bottom = rand.int_range (0, 6)
        };
        int cw = rand.int_range (1, 3000);
        int ch = rand.int_range (1, 2000);
        w.frame = rect (rand.int_range (-3000, 6000), rand.int_range (-2000, 4000),
                        cw + w.borders.left + w.borders.right, ch + w.borders.top + w.borders.bottom);
        model.set_size_hints (w, random_hints (rand));
        model.manage (w);
        model.map (w);
        windows += w;
    }

    for (int step = 0; step < 60; step++) {
        var w = windows[rand.int_range (0, windows.length)];
        switch (rand.int_range (0, 7)) {
            case 0:
                model.configure_request (w, rand.boolean (), rand.int_range (-4000, 8000),
                                         rand.int_range (-3000, 5000), rand.boolean (),
                                         rand.int_range (1, 4000), rand.int_range (1, 3000));
                break;
            case 1:
                model.set_state (w, WindowState.MAXIMIZED, !w.has_state (WindowState.MAXIMIZED));
                break;
            case 2:
                model.set_state (w, rand.boolean () ? WindowState.MAXIMIZED_H : WindowState.MAXIMIZED_V,
                                 rand.boolean ());
                break;
            case 3:
                model.set_state (w, WindowState.FULLSCREEN, !w.has_state (WindowState.FULLSCREEN));
                break;
            case 4:
                model.set_size_hints (w, random_hints (rand));
                break;
            case 5:
                model.move (w, false, rand.int_range (-4000, 8000), rand.int_range (-3000, 5000));
                break;
            default:
                model.move (w, false, w.client_rect ().x, w.client_rect ().y);
                break;
        }
        foreach (var each in windows) {
            if (!titlebar_reachable (model, each)) {
                GLib.error ("seed %u step %d: %s titlebar unreachable at %d,%d %dx%d",
                            seed, step, each.label, each.frame.x, each.frame.y,
                            each.frame.width, each.frame.height);
            }
        }
    }
}

void test_fuzz () {
    for (uint32 seed = 1; seed <= 1000; seed++) {
        run_case (seed);
    }
    assert (checked > 300000);
}

int main (string[] args) {
    Test.init (ref args);
    Test.add_func ("/constraints/fuzz", test_fuzz);
    return Test.run ();
}
