/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * VAPI coverage test for polo-xwm.vapi: uses every symbol against a real
 * Xwayland on a headless server. argv[1] is tests/xwm/x-test-client.c, which
 * reports what it sees to a file. Skips without Xwayland.
 */

const int SKIP = 77;

string log_path;
GLib.MainLoop loop;

string read_log () {
    string contents = "";
    try {
        GLib.FileUtils.get_contents (log_path, out contents);
    } catch (GLib.FileError e) {
        contents = "";
    }
    return contents;
}

/* Runs the loop until the client has written the line, or fails after 15 s. */
void wait_for_line (string line) {
    int64 deadline = GLib.get_monotonic_time () + 15 * 1000000;
    var context = loop.get_context ();
    while (!("\n" + read_log ()).contains ("\n" + line + "\n")) {
        if (GLib.get_monotonic_time () > deadline) {
            GLib.error ("no line \"%s\" in:\n%s", line, read_log ());
        }
        context.iteration (false);
        GLib.Thread.usleep (2000);
    }
}

void wait_until (owned GLib.SourceFunc done, string what) {
    int64 deadline = GLib.get_monotonic_time () + 15 * 1000000;
    var context = loop.get_context ();
    while (!done ()) {
        if (GLib.get_monotonic_time () > deadline) {
            GLib.error ("timed out waiting for %s", what);
        }
        context.iteration (false);
        GLib.Thread.usleep (2000);
    }
}

int main (string[] args) {
    assert (Polo.Xwm.vendored_from () == "Weston 16.0.1 xwayland/");
    if (GLib.Environment.find_program_in_path ("Xwayland") == null) {
        return SKIP;
    }

    loop = new GLib.MainLoop ();
    var server = new Polo.Backend.Server ();
    var config = Polo.Backend.Config ();
    config.kind = Polo.Backend.Kind.HEADLESS;
    config.renderer = Polo.Backend.Renderer.PIXMAN;
    config.width = 640;
    config.height = 480;
    config.scale = 1;
    config.output_count = 1;
    config.fullscreen = false;
    string socket_name = "polo-vapi-xwm-%d".printf ((int) Posix.getpid ());
    config.socket_name = socket_name;
    config.seat = null;
    config.xkb_layout = null;
    config.xkb_variant = null;
    config.xkb_options = null;
    config.test_mode = false;
    config.debug_kill = false;
    config.cursor_theme = "default";
    config.cursor_size = 24;
    try {
        assert (server.start (config));
    } catch (GLib.Error e) {
        GLib.error ("server start failed: %s", e.message);
    }

    server.attach (loop.get_context ());

    var xwm = new Polo.Xwm.Manager (server);
    assert (!xwm.is_running ());
    string? ready_name = null;
    xwm.ready.connect ((name) => {
        ready_name = name;
    });
    bool started = false;
    xwm.started.connect (() => {
        started = true;
    });
    xwm.exited.connect ((crashed) => {
        assert (!crashed);
    });
    xwm.set_eager (false);
    Polo.Xwm.XWindow? seen = null;
    bool destroyed = false;
    var changed = new GLib.GenericArray<string> ();
    bool geometry_seen = false;
    xwm.window_created.connect ((window) => {
        if (window.get_override_redirect ()) {
            return;
        }
        if (seen == null && window.get_res_name () == "poloclient") {
            seen = window;
            window.destroyed.connect (() => {
                destroyed = true;
            });
            window.property_changed.connect ((atom) => {
                changed.add (atom);
            });
            window.geometry_changed.connect (() => {
                geometry_seen = true;
            });
        }
    });
    Polo.Xwm.XWindow? first = null;
    xwm.window_created.connect ((window) => {
        if (first == null) {
            first = window;
        }
    });
    try {
        assert (xwm.start ());
    } catch (GLib.Error e) {
        GLib.error ("xwm start failed: %s", e.message);
    }
    assert (ready_name != null && ready_name == xwm.get_display_name ());
    assert (GLib.Environment.get_variable ("DISPLAY") == ready_name);

    /* Doing nothing while the X server is not running is the contract. */
    xwm.set_active_window (1);

    try {
        int fd = GLib.FileUtils.open_tmp ("polo-xwm-XXXXXX", out log_path);
        Posix.close (fd);
        var launcher = new GLib.SubprocessLauncher (GLib.SubprocessFlags.NONE);
        launcher.setenv ("X_TEST_RETITLE", "1", true);
        launcher.setenv ("X_TEST_TYPE", "_NET_WM_WINDOW_TYPE_UTILITY", true);
        launcher.spawnv ({ args[1], log_path });
    } catch (GLib.Error e) {
        GLib.error ("cannot start the client: %s", e.message);
    }
    wait_until (() => xwm.is_running (), "the X server");
    wait_until (() => started, "started");
    wait_until (() => seen != null, "the window");
    var w = seen;

    assert (w.get_id () != 0);
    wait_until (() => w.get_frame_id () != 0, "the frame window");
    assert (w.get_res_name () == "poloclient");
    assert (w.get_res_class () == "PoloClient");
    assert (w.get_wm_name () == "old title");
    assert (w.get_net_wm_name () == "ünï");
    assert (!w.get_input ());
    assert (w.get_initially_iconic () == false);
    assert (w.get_urgent ());
    assert (w.get_window_group () == 0x1234);
    assert (w.get_client_leader () == w.get_id ());
    assert (w.get_sm_client_id () == "session-1");
    assert (!w.get_transient_for_root ());
    assert (w.get_supports_ping ());
    assert (!w.get_supports_sync_request ());
    assert (w.get_machine () == null);
    assert (w.get_pid () == 0);
    assert (w.get_window_type () == "_NET_WM_WINDOW_TYPE_UTILITY");
    assert (w.get_transient_for () == null);
    assert (!w.get_override_redirect ());
    assert (!w.get_fullscreen ());
    assert (!w.get_maximized_vert () && !w.get_maximized_horz ());
    assert (w.get_supports_delete ());
    assert (w.get_supports_take_focus ());
    assert (w.get_decorations () != 0);
    int x, y, width, height;
    w.get_geometry (out x, out y, out width, out height);
    assert (width == 300 && height == 200);
    uint32 flags;
    int min_w, min_h, max_w, max_h, inc_w, inc_h, base_w, base_h, gravity;
    w.get_size_hints (out flags, out min_w, out min_h, out max_w, out max_h, out inc_w, out inc_h,
                      out base_w, out base_h, out gravity);
    assert (min_w == 100 && min_h == 80 && max_w == 900 && max_h == 700);
    assert (inc_w == 7 && inc_h == 13 && base_w == 10 && base_h == 20 && gravity == 5);
    assert (flags != 0);
    int asp_a, asp_b, asp_c, asp_d;
    w.get_aspect (out asp_a, out asp_b, out asp_c, out asp_d);
    assert (asp_a == 1 && asp_b == 2 && asp_c == 3 && asp_d == 1);

    w.set_wm_state (1);
    wait_for_line ("prop WM_STATE 1 0");
    w.set_net_wm_state ({ "_NET_WM_STATE_ABOVE", "_NET_WM_STATE_STICKY" });
    wait_for_line ("prop _NET_WM_STATE _NET_WM_STATE_ABOVE _NET_WM_STATE_STICKY");
    w.set_frame_extents (1, 2, 3, 4);
    wait_for_line ("prop _NET_FRAME_EXTENTS 1 2 3 4");
    w.set_allowed_actions ({ "_NET_WM_ACTION_MOVE", "_NET_WM_ACTION_CLOSE" });
    wait_for_line ("prop _NET_WM_ALLOWED_ACTIONS _NET_WM_ACTION_MOVE _NET_WM_ACTION_CLOSE");

    w.configure (30, 40, 320, 210);
    wait_for_line ("size 320x210");
    wait_until (() => geometry_seen, "geometry-changed");

    w.set_input_focus (0);
    wait_for_line ("focus");

    xwm.set_supported ({ "_NET_WM_NAME", "_NET_WM_STATE" });
    wait_for_line ("prop _NET_SUPPORTED _NET_WM_NAME _NET_WM_STATE");
    xwm.set_gtk_workareas (1, { 1, 2, 3, 4 });
    wait_for_line ("prop _GTK_WORKAREAS_D1 1 2 3 4");
    assert (xwm.get_desktop_layout ().length == 0);
    xwm.root_property_changed.connect ((atom) => {});
    xwm.set_client_list ({ 11, 22 });
    wait_for_line ("prop _NET_CLIENT_LIST 11 22");
    xwm.set_client_list_stacking ({ 22, 11 });
    wait_for_line ("prop _NET_CLIENT_LIST_STACKING 22 11");
    xwm.set_active_window (w.get_id ());
    wait_for_line ("prop _NET_ACTIVE_WINDOW %u".printf (w.get_id ()));
    xwm.set_number_of_desktops (4);
    wait_for_line ("prop _NET_NUMBER_OF_DESKTOPS 4");
    xwm.set_current_desktop (2);
    wait_for_line ("prop _NET_CURRENT_DESKTOP 2");
    xwm.set_desktop_names ({ "one", "two" });
    wait_for_line ("prop _NET_DESKTOP_NAMES one|two|");
    xwm.set_desktop_geometry (640, 480);
    wait_for_line ("prop _NET_DESKTOP_GEOMETRY 640 480");
    xwm.set_desktop_viewport ({ 0, 0 });
    wait_for_line ("prop _NET_DESKTOP_VIEWPORT 0 0");
    xwm.set_workarea ({ 0, 24, 640, 456 });
    wait_for_line ("prop _NET_WORKAREA 0 24 640 456");
    xwm.set_showing_desktop (true);
    wait_for_line ("prop _NET_SHOWING_DESKTOP 1");

    w.send_take_focus (5);
    wait_for_line ("message WM_TAKE_FOCUS");
    w.send_delete (6);
    wait_for_line ("message WM_DELETE_WINDOW");

    wait_until (() => changed.find_with_equal_func ("WM_NAME", GLib.str_equal), "property-changed");
    wait_until (() => w.get_wm_name () == "new title", "the new title");
    bool ponged = false;
    w.pong.connect (() => {
        ponged = true;
    });
    w.send_ping (7);
    wait_for_line ("message _NET_WM_PING");
    wait_until (() => ponged, "the pong");
    w.set_visible_name ("shown");
    wait_for_line ("prop _NET_WM_VISIBLE_NAME shown");

    w.kill_client ();
    wait_until (() => destroyed, "destroyed");
    assert (w.get_id () == 0);
    wait_for_line ("disconnected");

    GLib.FileUtils.unlink (log_path);
    server.stop ();
    return 0;
}
