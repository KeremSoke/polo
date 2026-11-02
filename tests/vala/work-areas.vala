/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/*
 * Work areas: the port of Marco's ensure_work_areas_validated ()
 * and how the policy model uses it. The single-output cases are also
 * covered against the real Marco by the oracle (place-strut-*,
 * constrain-maximize-struts); these add the multi-output cases the oracle
 * cannot record yet.
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

Marco.Strut strut (Marco.Rect r, Marco.Side side, Marco.EdgeType edge = Marco.EdgeType.XINERAMA) {
    return Marco.Strut () { rect = r, side = side, edge = edge };
}

bool same (Marco.Rect a, Marco.Rect b) {
    if (a.x != b.x || a.y != b.y || a.width != b.width || a.height != b.height) {
        printerr ("got %d,%d %dx%d, expected %d,%d %dx%d\n",
                  a.x, a.y, a.width, a.height, b.x, b.y, b.width, b.height);
        return false;
    }
    return true;
}

/* A 1920x1080 laptop panel with a 1280x1024 monitor to its right. */
GenericArray<Policy.Output> two_outputs () {
    var outputs = new GenericArray<Policy.Output> ();
    outputs.add (new Policy.Output ("eDP-1", rect (0, 0, 1920, 1080)));
    outputs.add (new Policy.Output ("HDMI-1", rect (1920, 0, 1280, 1024)));
    return outputs;
}

Policy.WorkAreas areas (GenericArray<Policy.Output> outputs, Marco.Strut[] struts) {
    var list = new SList<Marco.Strut?> ();
    foreach (var s in struts) {
        list.prepend (s);
    }
    return new Policy.WorkAreas (outputs, (owned) list);
}

void single_output_test () {
    var outputs = new GenericArray<Policy.Output> ();
    outputs.add (new Policy.Output ("eDP-1", rect (0, 0, 1920, 1080)));

    var a = areas (outputs, {});
    assert (same (a.area_for (outputs[0]), rect (0, 0, 1920, 1080)));
    assert (same (a.screen_area, rect (0, 0, 1920, 1080)));

    /* MATE's default: a 24 px panel at the top and at the bottom. */
    a = areas (outputs, { strut (rect (0, 0, 1920, 24), Marco.Side.TOP),
                          strut (rect (0, 1056, 1920, 24), Marco.Side.BOTTOM) });
    assert (same (a.area_for (outputs[0]), rect (0, 24, 1920, 1032)));
    assert (same (a.screen_area, rect (0, 24, 1920, 1032)));
}

/* Struts apply only to the output they touch. */
void per_output_test () {
    var outputs = two_outputs ();

    /* A bottom panel on the laptop only. */
    var a = areas (outputs, { strut (rect (0, 1056, 1920, 24), Marco.Side.BOTTOM) });
    assert (same (a.area_for (outputs[0]), rect (0, 0, 1920, 1056)));
    assert (same (a.area_for (outputs[1]), rect (1920, 0, 1280, 1024)));

    /* A left panel on the inner edge of the second monitor reserves on
     * that monitor, but not on the laptop next to it. */
    a = areas (outputs, { strut (rect (1920, 0, 30, 1024), Marco.Side.LEFT) });
    assert (same (a.area_for (outputs[0]), rect (0, 0, 1920, 1080)));
    assert (same (a.area_for (outputs[1]), rect (1950, 0, 1250, 1024)));

    /* A screen-edge strut (_NET_WM_STRUT_PARTIAL) along the top of the
     * laptop only. */
    a = areas (outputs, { strut (rect (0, 0, 1920, 24), Marco.Side.TOP, Marco.EdgeType.SCREEN) });
    assert (same (a.area_for (outputs[0]), rect (0, 24, 1920, 1056)));
    assert (same (a.area_for (outputs[1]), rect (1920, 0, 1280, 1024)));
}

/* Marco's paranoia checks on the screen work area. */
void sanity_test () {
    var outputs = new GenericArray<Policy.Output> ();
    outputs.add (new Policy.Output ("eDP-1", rect (0, 0, 1000, 800)));

    Test.expect_message ("Polo", LogLevelFlags.LEVEL_WARNING, "*unusually large*");
    var a = areas (outputs, { strut (rect (0, 0, 950, 800), Marco.Side.LEFT) });
    Test.assert_expected_messages ();
    /* 50 px left; widened to MIN_SANE_AREA around the remainder. */
    assert (same (a.screen_area, rect (925, 0, 100, 800)));
    assert (same (a.area_for (outputs[0]), rect (950, 0, 50, 800)));
}

/* _GNOME_WM_STRUT_AREA validation (window.c meta_window_update_struts ()). */
void strut_from_area_test () {
    var outputs = two_outputs ();

    Marco.Strut? s = Policy.WorkAreas.strut_from_area (rect (0, 0, 1920, 24), outputs);
    assert (s != null && s.side == Marco.Side.TOP && s.edge == Marco.EdgeType.XINERAMA);
    s = Policy.WorkAreas.strut_from_area (rect (0, 1056, 1920, 24), outputs);
    assert (s != null && s.side == Marco.Side.BOTTOM);
    s = Policy.WorkAreas.strut_from_area (rect (1920, 0, 30, 1024), outputs);
    assert (s != null && s.side == Marco.Side.LEFT);
    s = Policy.WorkAreas.strut_from_area (rect (3170, 0, 30, 1024), outputs);
    assert (s != null && s.side == Marco.Side.RIGHT);

    /* Not touching an edge, or not inside one output. */
    assert (Policy.WorkAreas.strut_from_area (rect (100, 100, 400, 24), outputs) == null);
    assert (Policy.WorkAreas.strut_from_area (rect (0, 0, 3200, 24), outputs) == null);
}

Window dock (Policy.Model model, Marco.Rect area, Marco.Side side) {
    var window = new Window (WindowKind.X11, new FakeAdapter ());
    window.type_hint = Marco.WindowType.DOCK;
    window.accepts_focus = false;
    window.frame = area;
    window.hints.flags |= Marco.SizeHintFlags.P_POSITION;
    window.struts = { strut (area, side, Marco.EdgeType.SCREEN) };
    model.manage (window);
    model.map (window);
    return window;
}

Window normal (Policy.Model model) {
    var window = new Window (WindowKind.X11, new FakeAdapter ());
    window.frame = rect (0, 0, 400, 300);
    model.manage (window);
    model.map (window);
    return window;
}

/* The model: docks are sticky, maximised windows follow the work area. */
void model_test () {
    var model = new Policy.Model (MarcoPrefs.defaults ());
    var output = new Policy.Output ("eDP-1", rect (0, 0, 1920, 1080));
    model.add_output (output);

    var window = normal (model);
    model.set_state (window, WindowState.MAXIMIZED, true);
    assert (same (window.frame, rect (0, 0, 1920, 1080)));

    var panel = dock (model, rect (0, 0, 1920, 24), Marco.Side.TOP);
    assert (panel.workspace == -1 && panel.has_state (WindowState.STICKY));
    assert (panel.has_state (WindowState.SKIP_TASKBAR | WindowState.SKIP_PAGER));
    assert (same (output.work_area, rect (0, 24, 1920, 1056)));
    assert (same (window.frame, rect (0, 24, 1920, 1056)));

    /* Layer-shell struts arrive through the delegate. */
    model.layer_struts = () => {
        var list = new SList<Marco.Strut?> ();
        list.prepend (strut (rect (0, 1050, 1920, 30), Marco.Side.BOTTOM));
        return list;
    };
    model.invalidate_work_areas ();
    assert (same (window.frame, rect (0, 24, 1920, 1026)));

    model.unmanage (panel);
    assert (same (window.frame, rect (0, 0, 1920, 1050)));
}

/* A strut window on one workspace only reserves space there. */
void workspace_test () {
    var model = new Policy.Model (MarcoPrefs.defaults ());
    var output = new Policy.Output ("eDP-1", rect (0, 0, 1920, 1080));
    model.add_output (output);
    assert (model.n_workspaces > 1);

    var panel = dock (model, rect (0, 0, 1920, 24), Marco.Side.TOP);
    model.move_to_workspace (panel, 1);
    assert (same (model.work_area_for (output, 0), rect (0, 0, 1920, 1080)));
    assert (same (model.work_area_for (output, 1), rect (0, 24, 1920, 1056)));
    assert (same (output.work_area, rect (0, 0, 1920, 1080)));

    model.switch_workspace (1);
    assert (same (output.work_area, rect (0, 24, 1920, 1056)));
}

int main (string[] args) {
    Test.init (ref args);
    Test.add_func ("/work-areas/single-output", single_output_test);
    Test.add_func ("/work-areas/per-output", per_output_test);
    Test.add_func ("/work-areas/sanity", sanity_test);
    Test.add_func ("/work-areas/strut-from-area", strut_from_area_test);
    Test.add_func ("/work-areas/model", model_test);
    Test.add_func ("/work-areas/workspace", workspace_test);
    return Test.run ();
}
