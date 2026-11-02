/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The display.c tab chains: what each list holds, in which order, and where
 * Alt-Tab starts, with the stack save and restore the switcher relies on.
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

Policy.Model new_model () {
    var model = new Policy.Model (MarcoPrefs.defaults ());
    model.add_output (new Policy.Output ("eDP-1", Marco.Rect () { width = 1920, height = 1080 }));
    return model;
}

Window map (Policy.Model model, string label, uint64 group = 0, Marco.WindowType? type = null) {
    var w = new Window (WindowKind.X11, new FakeAdapter ());
    w.label = label;
    w.type_hint = type;
    w.group = group;
    w.frame = Marco.Rect () { x = 100, y = 100, width = 400, height = 300 };
    model.manage (w);
    model.map (w);
    model.activate (w);
    return w;
}

string names (GLib.GenericArray<Window> windows) {
    string[] labels = {};
    foreach (var w in windows) {
        labels += w.label;
    }
    return string.joinv (" ", labels);
}

string list (Policy.Model model, Policy.TabList type) {
    return names (Policy.TabChain.get_tab_list (model, type, model.current_workspace));
}

void test_normal () {
    var model = new_model ();
    var a = map (model, "a");
    var b = map (model, "b");
    var c = map (model, "c");
    assert (list (model, Policy.TabList.NORMAL) == "c b a");
    model.minimize (b);
    assert (list (model, Policy.TabList.NORMAL) == "c a b");

    var next = Policy.TabChain.get_tab_next (model, Policy.TabList.NORMAL, 0, null, false);
    assert (next == a);
    var previous = Policy.TabChain.get_tab_next (model, Policy.TabList.NORMAL, 0, null, true);
    assert (previous == model.get_mru (0)[model.get_mru (0).length - 1]);
    assert (Policy.TabChain.get_tab_next (model, Policy.TabList.NORMAL, 0, a, false) == b);
    assert (Policy.TabChain.get_tab_next (model, Policy.TabList.NORMAL, 0, b, false) == c);
    assert (Policy.TabChain.get_tab_current (model, Policy.TabList.NORMAL, 0) == c);

    c.accepts_focus = false;
    assert (list (model, Policy.TabList.NORMAL) == "a b");
}

void test_docks_and_skip_taskbar () {
    var model = new_model ();
    map (model, "panel", 0, Marco.WindowType.DOCK);
    var dialog = map (model, "dialog");
    dialog.wm_state_skip_taskbar = true;
    map (model, "app");
    assert (list (model, Policy.TabList.NORMAL) == "app");
    assert (list (model, Policy.TabList.DOCKS) == "dialog panel");
}

void test_group () {
    var model = new_model ();
    map (model, "one", 1);
    map (model, "other", 2);
    map (model, "two", 1);
    assert (list (model, Policy.TabList.GROUP) == "two one");
}

void test_all_workspaces () {
    var model = new_model ();
    var here = map (model, "here");
    var there = map (model, "there");
    model.move_to_workspace (there, 1);
    var everywhere = map (model, "everywhere");
    model.set_state (everywhere, WindowState.STICKY, true);
    assert (list (model, Policy.TabList.NORMAL) == "everywhere here");
    var all = list (model, Policy.TabList.NORMAL_ALL_WORKSPACES);
    assert (all.has_prefix ("everywhere here everywhere there everywhere"));
    assert (here != null);
}

void test_demands_attention () {
    var model = new_model ();
    map (model, "here");
    var there = map (model, "there");
    model.move_to_workspace (there, 1);
    model.focus_policy.set_demands_attention (there);
    assert (list (model, Policy.TabList.NORMAL) == "there here");
}

void test_stack_positions () {
    var model = new_model ();
    var a = map (model, "a");
    var b = map (model, "b");
    var saved = model.get_stack_positions ();
    model.raise (a);
    assert (model.stack_index (a) > model.stack_index (b));
    model.set_stack_positions (saved);
    assert (model.stack_index (b) > model.stack_index (a));
}

int main (string[] args) {
    Test.init (ref args);
    Test.add_func ("/tab-list/normal", test_normal);
    Test.add_func ("/tab-list/docks", test_docks_and_skip_taskbar);
    Test.add_func ("/tab-list/group", test_group);
    Test.add_func ("/tab-list/all-workspaces", test_all_workspaces);
    Test.add_func ("/tab-list/demands-attention", test_demands_attention);
    Test.add_func ("/tab-list/stack-positions", test_stack_positions);
    return Test.run ();
}
