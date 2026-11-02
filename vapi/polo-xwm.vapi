/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Bindings for libpolo-xwm (lib/xwm/polo-xwm.h).
 */

[CCode (cheader_filename = "polo-xwm.h")]
namespace Polo.Xwm {
    /* Returns the upstream source of the vendored XWM. */
    [CCode (cname = "polo_xwm_vendored_from")]
    public unowned string vendored_from ();

    [CCode (cname = "PoloXWindow", cprefix = "polo_xwindow_", type_id = "polo_xwindow_get_type ()")]
    public class XWindow : GLib.Object {
        public uint32 get_id ();
        public uint32 get_frame_id ();
        public unowned string? get_net_wm_name ();
        public unowned string? get_wm_name ();
        public unowned string? get_res_name ();
        public unowned string? get_res_class ();
        public unowned string? get_sm_client_id ();
        public uint32 get_client_leader ();
        public bool get_input ();
        public bool get_initially_iconic ();
        public bool get_urgent ();
        public uint32 get_window_group ();
        public void get_motif_hints (out uint32 flags, out uint32 functions, out uint32 decorations);
        public bool get_transient_for_root ();
        public bool get_supports_ping ();
        public bool get_supports_sync_request ();
        public unowned string? get_machine ();
        public int get_pid ();
        public string? get_window_type ();
        public unowned XWindow? get_transient_for ();
        public bool get_override_redirect ();
        public bool get_fullscreen ();
        public bool get_maximized_vert ();
        public bool get_maximized_horz ();
        public bool get_supports_delete ();
        public bool get_supports_take_focus ();
        public int get_decorations ();
        public void get_geometry (out int x, out int y, out int width, out int height);
        public void get_size_hints (out uint32 flags, out int min_width, out int min_height,
                                    out int max_width, out int max_height, out int width_inc,
                                    out int height_inc, out int base_width, out int base_height,
                                    out int win_gravity);
        public void get_aspect (out int min_x, out int min_y, out int max_x, out int max_y);

        public string? name_of_atom (uint32 atom);
        [CCode (array_length_pos = 1.1, array_length_type = "int")]
        public uint32[] get_cardinals (string property);
        [CCode (array_length_pos = 1.1, array_length_type = "int", array_null_terminated = false)]
        public string[] get_atoms (string property);
        public string? get_text (string property);
        public void set_frame_inset (int left, int right, int top, int bottom);
        public void set_intercept_configure (bool intercept);
        public void set_net_wm_desktop (uint32 desktop);

        public void configure (int x, int y, int width, int height);
        public void set_input_focus (uint32 time);
        public void send_take_focus (uint32 time);
        public void send_delete (uint32 time);
        public void send_ping (uint32 time);
        public void set_visible_name (string name);
        public void set_wm_state (int state);
        public void set_net_wm_state ([CCode (array_length_type = "int")] string[] atoms);
        public void set_frame_extents (uint32 left, uint32 right, uint32 top, uint32 bottom);
        public void set_allowed_actions ([CCode (array_length_type = "int")] string[] atoms);
        public void kill_client ();

        public signal void destroyed ();
        public signal void property_changed (string atom);
        public signal void geometry_changed ();
        public signal void pong ();
        public signal void focus_in ();
        public signal void unmapped ();
        public signal void client_message (string type, uint d0, uint d1, uint d2, uint d3, uint d4);
        public signal void configure_requested (uint mask, int x, int y, int width, int height);
    }

    [CCode (cname = "PoloXwm", cprefix = "polo_xwm_", type_id = "polo_xwm_get_type ()")]
    public class Manager : GLib.Object {
        public Manager (Polo.Backend.Server server);

        public void set_eager (bool eager);
        public bool start () throws GLib.Error;
        public unowned string? get_display_name ();
        public bool is_running ();
        public unowned XWindow? window_for_toplevel (Polo.Backend.Toplevel toplevel);

        [CCode (array_length_pos = 2.1, array_length_type = "int")]
        public void clear_focus ();
        public uint32[] get_cardinals (uint32 xid, string property);
        public void set_cardinals (uint32 xid, string property, [CCode (array_length_type = "int")] uint32[] values);
        public void watch_window (uint32 xid);
        public void set_supported ([CCode (array_length_type = "int")] string[] atoms);
        public void set_gtk_workareas (int desktop, [CCode (array_length_type = "int")] uint32[] areas);
        [CCode (array_length_pos = 0.1, array_length_type = "int")]
        public uint32[] get_desktop_layout ();
        public void set_client_list ([CCode (array_length_type = "int")] uint32[] windows);
        public void set_client_list_stacking ([CCode (array_length_type = "int")] uint32[] windows);
        public void set_active_window (uint32 window);
        public void set_number_of_desktops (uint32 count);
        public void set_current_desktop (uint32 desktop);
        public void set_desktop_names ([CCode (array_length_type = "int")] string[] names);
        public void set_desktop_geometry (uint32 width, uint32 height);
        public void set_desktop_viewport ([CCode (array_length_type = "int")] uint32[] origins);
        public void set_workarea ([CCode (array_length_type = "int")] uint32[] areas);
        public void set_showing_desktop (bool showing);

        public signal void window_created (XWindow window);
        public signal void ready (string display_name);
        public signal void started ();
        public signal void focus_lost ();
        public signal void root_property_changed (string atom);
        public signal void root_message (string type, uint data);
        public signal void watched_property_changed (uint xid, string atom);
        public signal void exited (bool crashed);
    }
}
