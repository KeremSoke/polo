/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * VAPI coverage test for polo-backend.vapi: uses every symbol,
 * built with --fatal-warnings and -Werror on the generated C. Runs a real
 * headless server; symbols that need a live client are compiled in
 * Coverage.with_client () without being called.
 */

namespace Coverage {
    /* Never called: makes valac and the C compiler check these symbols. */
    public static void with_client (Polo.Backend.Seat seat, Polo.Backend.Toplevel toplevel,
                                    Polo.Backend.Client client,
                                    Polo.Backend.LayerSurface layer_surface) {
        unowned Polo.Backend.View view = toplevel.get_view ();
        view.set_position (1.0, 2.0);
        double x, y;
        view.get_position (out x, out y);
        view.move_to_layer (null);
        unowned Polo.Backend.Toplevel? owner = view.get_toplevel ();
        unowned Polo.Backend.Client? viewer = view.get_client ();
        print ("%p\n", viewer);
        view.destroyed.connect (() => {});

        unowned string? title = toplevel.title;
        unowned string? app_id = toplevel.app_id;
        int pid = toplevel.get_pid ();
        unowned Polo.Backend.Toplevel? parent = toplevel.get_parent ();
        int gx, gy, gw, gh;
        toplevel.get_window_geometry (out gx, out gy, out gw, out gh);
        toplevel.get_min_size (out gw, out gh);
        toplevel.get_max_size (out gw, out gh);
        bool content = toplevel.has_content ();
        bool mapped = toplevel.is_mapped ();
        toplevel.map ();
        unowned Polo.Backend.Client? owner_client = toplevel.get_client ();
        toplevel.configure_size (10, 10);
        toplevel.set_activated (true);
        toplevel.set_maximized (false);
        toplevel.set_fullscreen (false);
        toplevel.set_resizing (false);
        toplevel.set_tiled (Polo.Backend.Edges.LEFT | Polo.Backend.Edges.TOP);
        toplevel.close ();
        toplevel.ping ();
        toplevel.committed.connect (() => {});
        toplevel.request_move.connect ((s, serial) => {});
        toplevel.request_resize.connect ((s, serial, edges) => {});
        toplevel.request_maximize.connect ((on) => {});
        toplevel.request_fullscreen.connect ((on) => {});
        toplevel.request_minimize.connect (() => {});
        toplevel.request_window_menu.connect ((s, mx, my) => {});
        toplevel.ping_timeout.connect (() => {});
        toplevel.pong.connect (() => {});
        toplevel.destroyed.connect (() => {});

        seat.set_keyboard_focus (view);
        Polo.Backend.PointerGrab? grab = seat.start_pointer_grab (1, view);
        if (grab != null) {
            grab.motion.connect ((gx2, gy2, time) => {});
            grab.button.connect ((button, pressed, held) => {});
            grab.ended.connect ((cancelled) => {});
            grab.end ();
        }

        int cpid = client.get_pid ();
        uint uid = client.get_uid ();
        unowned string? exe = client.get_executable ();
        client.set_trusted (!client.get_trusted ());
        client.destroyed.connect (() => {});

        unowned string ns = layer_surface.get_namespace ();
        unowned Polo.Backend.View lview = layer_surface.get_view ();
        unowned Polo.Backend.Output? loutput = layer_surface.get_output ();
        layer_surface.set_output (loutput);
        layer_surface.configure (10, 10);
        layer_surface.close ();
        bool lmapped = layer_surface.is_mapped ();
        int lsx, lsy;
        layer_surface.get_size (out lsx, out lsy);
        uint llayer = layer_surface.layer;
        uint lanchor = layer_surface.anchor;
        int lzone = layer_surface.exclusive_zone;
        uint ledge = layer_surface.exclusive_edge;
        int lmt = layer_surface.margin_top;
        int lmr = layer_surface.margin_right;
        int lmb = layer_surface.margin_bottom;
        int lml = layer_surface.margin_left;
        uint lkbd = layer_surface.keyboard_interactivity;
        uint ldw = layer_surface.desired_width;
        uint ldh = layer_surface.desired_height;
        bool lmapped2 = layer_surface.mapped;
        unowned Polo.Backend.Output? lassigned = layer_surface.output;
        layer_surface.output = lassigned;
        unowned Polo.Backend.Output? lrequested = layer_surface.requested_output;
        layer_surface.committed.connect (() => {});
        layer_surface.new_popup.connect ((view) => {});
        layer_surface.destroyed.connect (() => {});

        print ("%p %s %s %d %p %d %d %p %p %d %u %s\n", owner, title, app_id, pid, parent,
               (int) content, (int) mapped, owner_client, grab, cpid, uid, exe);
        print ("%p %s %p %p %d %d %d %u %u %d %u %d %d %d %d %u %u %u %d %p %p\n",
               lview, ns, loutput, lassigned, (int) lmapped, lsx, lsy, llayer, lanchor,
               lzone, ledge, lmt, lmr, lmb, lml, lkbd, ldw, ldh, (int) lmapped2, lrequested, grab);
    }
}

/* Needs a bound polo-shell-ui: this only has to compile. */
void cover_thumbnail (Polo.Backend.ShellUi shell_ui, Polo.Backend.View view) {
    print ("%d\n", (int) shell_ui.switcher_thumbnail (1, view, 300.0, 300.0));
    print ("%u\n", shell_ui.get_dialog (view.get_toplevel ()));
}

/* Headless has no libinput devices: this only has to compile. */
void cover_input_device (Polo.Backend.InputDevice device) {
    device.removed.connect (() => {});
    print ("%s %d %d %u\n", device.get_name (), (int) device.has_capability (Polo.Backend.DeviceCapability.POINTER),
           (int) device.is_touchpad (), (uint) device.get_features ());
    device.set_left_handed (true);
    device.set_accel_speed (0.5);
    device.set_accel_profile (Polo.Backend.AccelProfile.FLAT);
    device.set_middle_emulation (true);
    device.set_dwt (true);
    device.set_tap (true);
    device.set_click_method (Polo.Backend.ClickMethod.CLICKFINGER);
    device.set_scroll_method (Polo.Backend.ScrollMethod.EDGE);
    device.set_natural_scroll (true);
    device.set_send_events (false);
}

int main () {
    assert (Polo.Backend.libweston_version ().has_prefix ("16."));

    try {
        assert (Polo.Backend.Log.init (null, "polo-vapi-test"));
    } catch (GLib.Error e) {
        GLib.error ("log init failed: %s", e.message);
    }

    unowned Polo.LogScope scope = Polo.Backend.Log.scope ("polo-vapi-test");
    assert (scope.name == "polo-vapi-test");
    assert (scope.enabled);
    assert (Polo.Backend.Log.scope ("polo-vapi-test") == scope);
    unowned Polo.LogScope other = Polo.Backend.Log.scope ("polo-vapi-other");
    assert (!other.enabled);
    scope.debug ("debug %s %d", "message", 1);
    scope.info ("info %s", "message");
    scope.warning ("warning %u", 2U);

    /* A real headless server. */
    var server = new Polo.Backend.Server ();
    int outputs = 0;
    int seats = 0;
    server.output_added.connect ((output) => {
        outputs++;
        assert (output.get_name () == "headless0");
        int x, y, w, h;
        output.get_geometry (out x, out y, out w, out h);
        assert (x == 0 && y == 0 && w == 640 && h == 480);
        assert (output.get_scale () == 1);
        assert (output.get_enabled ());
        assert (output.get_transform () == 0);
        assert (!output.get_internal ());
        string make = output.get_make ();
        string model = output.get_model ();
        string serial = output.get_serial ();
        assert (make != null && model != null && serial != null);
        assert (output.get_vendor () == "");
        assert (output.get_product () == "" && output.get_edid_serial () == "");
        int mm_width, mm_height;
        output.get_physical_size (out mm_width, out mm_height);
        assert (output.get_mode_count () > 0);
        Polo.Backend.Mode mode;
        assert (output.get_mode (0, out mode));
        assert (mode.width == 640 && mode.height == 480 && mode.current);
        assert (!output.get_mode (-1, out mode));
        assert (!output.get_mode (999, out mode));
        try {
            output.apply (640, 480, 0, 2, 1, -240, 100, true);
            output.get_geometry (out x, out y, out w, out h);
            assert (x == -240 && y == 100 && w == 240 && h == 320);
            assert (output.get_scale () == 2 && output.get_transform () == 1);
            output.apply (640, 480, 0, 1, 0, 0, 0, false);
            assert (!output.get_enabled ());
            output.apply (640, 480, 0, 1, 0, 0, 0, true);
            assert (output.get_enabled ());
            bool rejected = false;
            try { output.apply (123, 456, 0, 1, 0, 0, 0, true); }
            catch (GLib.Error e) { rejected = true; }
            assert (rejected);
        } catch (GLib.Error e) { GLib.error ("%s", e.message); }
        output.set_background_color (0.1, 0.2, 0.3);
        output.set_work_area (0, 24, 640, 456);
        output.set_primary (true);
        var toplevel = new Polo.Backend.ForeignToplevel (server);
        toplevel.set_title ("title");
        toplevel.set_app_id ("app");
        toplevel.set_state (Polo.Backend.ForeignToplevelState.ACTIVATED);
        toplevel.set_extra_state (Polo.Backend.ForeignToplevelExtraState.STICKY |
                                  Polo.Backend.ForeignToplevelExtraState.SKIP_PAGER);
        toplevel.set_outputs ({ output });
        toplevel.set_parent (null);
        toplevel.done ();
        int rx, ry, rw, rh;
        assert (!toplevel.get_rectangle (out rx, out ry, out rw, out rh));
        toplevel.request_maximize.connect ((on) => {});
        toplevel.request_minimize.connect ((on) => {});
        toplevel.request_activate.connect (() => {});
        toplevel.request_close.connect (() => {});
        toplevel.request_fullscreen.connect ((on, o) => {});
        toplevel.request_shade.connect ((on) => {});
        toplevel.request_sticky.connect ((on) => {});
        toplevel.request_above.connect ((on) => {});
        toplevel.request_below.connect ((on) => {});
        toplevel.request_move.connect (() => {});
        toplevel.request_resize.connect (() => {});
        toplevel.request_move_to_output.connect ((o) => {});
        toplevel.request_move_to_workspace.connect ((w) => {});
        toplevel.set_workspaces (new Polo.Backend.Workspace[0]);
        toplevel.close ();
        output.destroyed.connect (() => outputs--);
    });
    server.output_removed.connect ((output) => {});
    server.outputs_changed.connect (() => {});
    server.input_device_added.connect (cover_input_device);
    server.seat_added.connect ((seat) => {
        seats++;
        assert (seat.get_name () == "default");
        double px, py;
        seat.get_pointer_position (out px, out py);
        print ("%u\n", (uint) seat.get_modifiers ());
        seat.set_keyboard_focus (null);
        seat.set_button_filter ((s, event) => {
            print ("%u %d %f %f %u %u %p\n", event.time_msec, (int) event.pressed, event.x,
                   event.y, event.button, event.modifiers, event.view);
            return Polo.Backend.FilterResult.PASS;
        });
        seat.set_key_filter ((s, event) => {
            print ("%u %u %u %u %u %d\n", event.time_msec, event.keycode, event.keysym,
                   event.base_keysym, (uint) event.modifiers, (int) event.pressed);
            return Polo.Backend.Modifiers.LOCK in event.modifiers
                ? Polo.Backend.FilterResult.CONSUME : Polo.Backend.FilterResult.PASS;
        });
        seat.set_axis_filter ((s, event) => {
            print ("%u %u %f %d %d %f %f %u %p\n", event.time_msec, event.axis, event.value,
                   (int) event.has_discrete, event.discrete, event.x, event.y, event.modifiers,
                   event.view);
            return Polo.Backend.FilterResult.PASS;
        });
        seat.begin_pointer_grab ();
        seat.group_changed.connect ((group) => print ("%u\n", group));
        seat.numlock_changed.connect ((locked) => print ("%d\n", (int) locked));
        seat.modifiers_changed.connect ((mods) => print ("%u\n", (uint) mods));
        try {
            seat.set_keymap ("evdev", null, "us", null, null);
        } catch (GLib.Error e) {
            print ("%s\n", e.message);
        }
        seat.lock_group (seat.get_group ());
        seat.set_numlock (false);
        seat.motion.connect ((x, y, time, view) => {});
        seat.warp_pointer (px, py);
        seat.destroyed.connect (() => {
            seat.set_button_filter (null);
            seat.set_key_filter (null);
            seat.set_axis_filter (null);
        });
    });
    server.system_bell.connect ((toplevel) => print ("%s\n", toplevel != null ? toplevel.title : ""));
    server.new_toplevel.connect ((toplevel) => cover_thumbnail (server.get_shell_ui (), toplevel.get_view ()));
    server.new_layer_surface.connect ((surface) => {});
    server.shutdown.connect (() => {});

    assert (Polo.Backend.Keysym.from_name ("a") == 0x61);
    assert (Polo.Backend.Keysym.from_name ("F10") == 0xffc7);
    assert (Polo.Backend.Keysym.from_name ("no-such-key") == 0);
    assert (Polo.Backend.Keysym.to_lower (0x41) == 0x61);
    var all_mods = Polo.Backend.Modifiers.SHIFT | Polo.Backend.Modifiers.CONTROL |
                   Polo.Backend.Modifiers.MOD1 | Polo.Backend.Modifiers.MOD2 |
                   Polo.Backend.Modifiers.MOD3 | Polo.Backend.Modifiers.MOD4 |
                   Polo.Backend.Modifiers.MOD5;
    assert ((uint) all_mods == 0xfd);
    server.set_activation_mint ((request) => {
        print ("%s %d %p %d %u %d\n", request.app_id ?? "", (int) request.has_surface,
               request.toplevel, (int) request.input_valid, request.input_time_msec,
               (int) request.surface_focused);
        return null;
    });
    server.set_activation_activate ((token, toplevel) => {});

    bool filtered = false;
    server.set_global_filter ((client, name) => {
        filtered = true;
        return true;
    });
    server.set_test_inspector ((toplevel, ref info) => {
        info.x = info.y = info.width = info.height = 0;
        info.layer = (int) Polo.Backend.LayerKind.NORMAL;
        info.stack_index = 0;
        info.workspace = -1;
        info.state = 0;
        info.focused = false;
        return false;
    });

    var config = Polo.Backend.Config ();
    config.kind = Polo.Backend.Kind.HEADLESS;
    config.renderer = Polo.Backend.Renderer.PIXMAN;
    config.width = 640;
    config.height = 480;
    config.scale = 1;
    config.output_count = 1;
    config.fullscreen = false;
    /* Config only borrows its strings. */
    string socket_name = "polo-vapi-test-%d".printf ((int) Posix.getpid ());
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
    assert (outputs == 1 && seats == 1);
    assert (server.get_socket_name () == config.socket_name);
    assert (!server.get_idle_inhibited ());
    server.set_keyboard_repeat (25, 600);
    server.set_test_settings ((schema, key, value) => value);
    assert (Polo.Backend.Keymap.has_layout ("us", null));
    uint32 now = server.get_time_msec ();
    assert (server.get_time_msec () >= now);
    server.attach (GLib.MainContext.default ());

    unowned Polo.Backend.WorkspaceManager workspace_manager = server.get_workspace_manager ();
    workspace_manager.create_workspace.connect ((name) => {});
    Polo.Backend.Workspace workspace = workspace_manager.add ("id", "name", 0, 0, 1, 13);
    workspace.activate.connect (() => {});
    workspace.remove.connect (() => {});
    workspace.set_name ("other");
    workspace.set_coordinates (1, 0);
    workspace.set_state (0);
    workspace_manager.done ();
    workspace.remove_workspace ();

    unowned Polo.Backend.ShellUi shell_ui = server.get_shell_ui ();
    assert (!shell_ui.is_bound () && shell_ui.get_client () == null);
    shell_ui.bound.connect ((c) => {});
    shell_ui.unbound.connect (() => {});
    shell_ui.menu_action.connect ((id, op, workspace) => {});
    shell_ui.dialog_response.connect ((id, response, text) => {});
    shell_ui.color_table.connect ((colors) => {});
    shell_ui.switcher_entry (1, "title", null, Polo.Backend.SwitcherEntryFlags.HIDDEN);
    shell_ui.show_switcher (null, 1, 5, false, 48);
    shell_ui.switcher_select.connect ((id) => {});
    shell_ui.update_selected (1);
    shell_ui.hide_switcher ();
    shell_ui.workspace_switcher_entry (1, "Workspace 1", false);
    shell_ui.workspace_switcher_window (1, 0, 0, 10, 10, true, null);
    shell_ui.show_workspace_switcher (null, 1, 4, 1, 640, 480, 640, 480, 48);
    shell_ui.update_workspace_switcher (1, 1);
    shell_ui.window_menu_workspace (0, "Workspace 1");
    shell_ui.show_window_menu (1, null, 0, 0, Polo.Backend.MenuOp.DELETE | Polo.Backend.MenuOp.RECOVER,
                               Polo.Backend.MenuOp.NONE, 0, 1);
    shell_ui.hide_window_menu (1);
    shell_ui.show_osd (Polo.Backend.OsdKind.WORKSPACE, null, null, -1);
    shell_ui.show_dialog (1, Polo.Backend.DialogKind.NOT_RESPONDING, null, "title");
    shell_ui.hide_dialog (1);
    assert ((uint) Polo.Backend.MenuOp.RECOVER == 0x20000);
    var flag_kinds = new Polo.Backend.SwitcherEntryFlags[] {
        Polo.Backend.SwitcherEntryFlags.BLANK, Polo.Backend.SwitcherEntryFlags.DEMANDS_ATTENTION };
    var menu_ops = new Polo.Backend.MenuOp[] {
        Polo.Backend.MenuOp.MINIMIZE, Polo.Backend.MenuOp.UNMAXIMIZE, Polo.Backend.MenuOp.MAXIMIZE,
        Polo.Backend.MenuOp.UNSHADE, Polo.Backend.MenuOp.SHADE, Polo.Backend.MenuOp.UNSTICK,
        Polo.Backend.MenuOp.STICK, Polo.Backend.MenuOp.WORKSPACES, Polo.Backend.MenuOp.MOVE,
        Polo.Backend.MenuOp.RESIZE, Polo.Backend.MenuOp.ABOVE, Polo.Backend.MenuOp.UNABOVE,
        Polo.Backend.MenuOp.MOVE_LEFT, Polo.Backend.MenuOp.MOVE_RIGHT, Polo.Backend.MenuOp.MOVE_UP,
        Polo.Backend.MenuOp.MOVE_DOWN };
    var osd_kinds = new Polo.Backend.OsdKind[] {
        Polo.Backend.OsdKind.VOLUME, Polo.Backend.OsdKind.BRIGHTNESS, Polo.Backend.OsdKind.MESSAGE };
    var dialog_kinds = new Polo.Backend.DialogKind[] {
        Polo.Backend.DialogKind.RENAME_WORKSPACE, Polo.Backend.DialogKind.END_SESSION,
        Polo.Backend.DialogKind.MESSAGE };
    assert (flag_kinds.length + menu_ops.length + osd_kinds.length + dialog_kinds.length == 24);

    foreach (var kind in new Polo.Backend.LayerKind[] {
            Polo.Backend.LayerKind.BACKGROUND, Polo.Backend.LayerKind.BOTTOM,
            Polo.Backend.LayerKind.BELOW, Polo.Backend.LayerKind.NORMAL,
            Polo.Backend.LayerKind.ABOVE, Polo.Backend.LayerKind.DOCK,
            Polo.Backend.LayerKind.FULLSCREEN, Polo.Backend.LayerKind.OVERLAY,
            Polo.Backend.LayerKind.LOCK }) {
        server.get_layer (kind).restack (new Polo.Backend.View[0]);
    }

    /* A trusted in-process client over a socket pair. */
    unowned Polo.Backend.Client client;
    try {
        int fd = server.create_client_socket (out client);
        assert (fd >= 0);
        assert (client.get_pid () == (int) Posix.getpid ());
        client.set_trusted (true);
        assert (client.get_trusted ());
        client.set_process ((int) Posix.getpid ());
        assert (client.get_pid () == (int) Posix.getpid ());
        Posix.close (fd);
    } catch (GLib.Error e) {
        GLib.error ("client socket failed: %s", e.message);
    }

    /* Let the loop run once; the closed client disconnects. */
    var context = GLib.MainContext.default ();
    for (int i = 0; i < 5; i++) {
        context.iteration (false);
    }

    assert (!Polo.Backend.Server.has_test_protocol () || true);
    var no_backend = new Polo.Backend.Error.BACKEND ("x");
    assert (no_backend.domain == Polo.Backend.Error.quark ());
    assert (Polo.Backend.FilterResult.CONSUME != Polo.Backend.FilterResult.PASS);
    assert (Polo.Backend.Edges.NONE == 0);
    var other_kinds = new Polo.Backend.Kind[] { Polo.Backend.Kind.DRM, Polo.Backend.Kind.WAYLAND };
    var other_renderers = new Polo.Backend.Renderer[] {
        Polo.Backend.Renderer.AUTO, Polo.Backend.Renderer.GL };
    assert (other_kinds.length == 2 && other_renderers.length == 2);
    assert (new Polo.Backend.Error.FAILED ("x") is Polo.Backend.Error.FAILED);
    assert (new Polo.Backend.Error.SOCKET ("x") is Polo.Backend.Error.SOCKET);

    server.set_test_inspector (null);
    server.set_test_settings (null);
    server.set_global_filter (null);
    server.set_activation_mint (null);
    server.set_activation_activate (null);
    server.stop ();
    server.stop ();
    assert (outputs == 0);

    try {
        Polo.Backend.Log.init ("/nonexistent-polo-dir/polo.log", null);
        assert_not_reached ();
    } catch (GLib.FileError e) {
        assert (e is GLib.FileError.NOENT);
    } catch (GLib.Error e) {
        assert_not_reached ();
    }

    Polo.Backend.Log.shutdown ();
    print ("filtered globals: %s\n", filtered.to_string ());
    return 0;
}
