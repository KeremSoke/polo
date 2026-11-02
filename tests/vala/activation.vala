/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * xdg_activation_v1 tokens and what activating with one does to the model.
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

void test_tokens () {
    var tokens = new Policy.ActivationTokens ();
    int64 time;

    var from_input = tokens.mint (true, 500, 1000);
    var worthless = tokens.mint (false, 0, 1000);
    var launch = tokens.mint_for_launch (1000);
    assert (from_input != worthless && worthless != launch && from_input.length == 32);

    assert (tokens.consume (from_input, 2000, out time) && time == 500);
    assert (!tokens.consume (from_input, 2000, out time));
    assert (!tokens.consume (worthless, 2000, out time));
    assert (tokens.consume (launch, 2000, out time) && time == 1000);
    assert (!tokens.consume ("no-such-token", 2000, out time));

    var old = tokens.mint (true, 100, 1000);
    assert (!tokens.consume (old, 1000 + Policy.ActivationTokens.LIFETIME_MSEC + 1, out time));
    var fresh = tokens.mint (true, 100, 1000);
    assert (tokens.consume (fresh, 1000 + Policy.ActivationTokens.LIFETIME_MSEC, out time));
    assert (tokens.size == 0);
}

Window new_window (Policy.Model model, int x, int width, int height) {
    var w = new Window (WindowKind.XDG, new FakeAdapter ());
    w.mwm_decorated = false;
    w.frame = Marco.Rect () { x = x, width = width, height = height };
    model.manage (w);
    return w;
}

void test_model () {
    var model = new Policy.Model (MarcoPrefs.defaults ());
    model.add_output (new Policy.Output ("eDP-1", Marco.Rect () { width = 1920, height = 1080 }));

    var below = new_window (model, 0, 300, 200);
    model.map (below);
    var top = new_window (model, 0, 800, 600);
    model.map (top);
    assert (model.focused == top);
    model.button_pressed (top, false, 5000);

    model.activate_with_token (below, false, 0);
    assert (model.focused == top);
    assert (below.has_state (WindowState.DEMANDS_ATTENTION));

    model.activate_with_token (below, true, 4000);
    assert (model.focused == top);

    model.activate_with_token (below, true, 6000);
    assert (model.focused == below);
    assert (model.stack_index (below) > model.stack_index (top));
    assert (!below.has_state (WindowState.DEMANDS_ATTENTION));

    var late = new_window (model, 0, 300, 200);
    model.activate_with_token (late, true, 5500);
    model.button_pressed (below, false, 7000);
    model.map (late);
    assert (model.focused == below);

    var launched = new_window (model, 0, 300, 200);
    model.activate_with_token (launched, true, 8000);
    model.map (launched);
    assert (model.focused == launched);
    assert (launched.initial_timestamp == Window.USER_TIME_UNSET);
}

int main (string[] args) {
    Test.init (ref args);
    Test.add_func ("/activation/tokens", test_tokens);
    Test.add_func ("/activation/model", test_model);
    return Test.run ();
}
