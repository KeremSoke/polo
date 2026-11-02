/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Closing pings the client (meta_window_delete ()); no pong within the
 * timeout makes the window not responding.
 */

using Polo;

class FakeAdapter : GLib.Object, WindowAdapter {
    public int pings = 0;
    public int closes = 0;
    public void configure (Marco.Rect frame) {}
    public void set_activated (bool activated) {}
    public void set_maximized (bool maximized) {}
    public void set_fullscreen (bool fullscreen) {}
    public void take_focus () {}
    public void close () { closes++; }
    public void ping () { pings++; }
}

Window map (Policy.Model model, FakeAdapter adapter) {
    var w = new Window (WindowKind.XDG, adapter);
    w.frame = Marco.Rect () { width = 300, height = 200 };
    model.manage (w);
    model.map (w);
    return w;
}

void test_ping () {
    var model = new Policy.Model (MarcoPrefs.defaults ());
    model.add_output (new Policy.Output ("eDP-1", Marco.Rect () { width = 1920, height = 1080 }));
    var adapter = new FakeAdapter ();
    var w = map (model, adapter);
    Window[] hung = {};
    model.not_responding.connect ((window) => { hung += window; });

    model.close (w);
    assert (adapter.closes == 1 && adapter.pings == 1);
    assert (model.ping_pending (w));
    model.close (w);
    assert (adapter.closes == 2 && adapter.pings == 1);

    model.pong (w);
    assert (!model.ping_pending (w) && !w.not_responding);
    model.expire_ping (w);
    assert (hung.length == 0);

    model.close (w);
    model.expire_ping (w);
    assert (hung.length == 1 && hung[0] == w && w.not_responding);
    model.close (w);
    model.expire_ping (w);
    assert (hung.length == 2);
    model.pong (w);
    assert (!w.not_responding);

    model.close (w);
    model.unmanage (w);
    assert (!model.ping_pending (w));
}

void test_timeout () {
    var model = new Policy.Model (MarcoPrefs.defaults ());
    model.add_output (new Policy.Output ("eDP-1", Marco.Rect () { width = 1920, height = 1080 }));
    var w = map (model, new FakeAdapter ());
    var loop = new GLib.MainLoop ();
    bool fired = false;
    model.not_responding.connect (() => { fired = true; loop.quit (); });
    int64 start = GLib.get_monotonic_time ();
    model.close (w);
    GLib.Timeout.add (Policy.Model.PING_TIMEOUT_DELAY + 2000, () => { loop.quit (); return false; });
    loop.run ();
    int64 waited = (GLib.get_monotonic_time () - start) / 1000;
    assert (fired);
    assert (waited >= Policy.Model.PING_TIMEOUT_DELAY - 50);
}

int main (string[] args) {
    Test.init (ref args);
    Test.add_func ("/ping/close-and-pong", test_ping);
    Test.add_func ("/ping/timeout", test_timeout);
    return Test.run ();
}
