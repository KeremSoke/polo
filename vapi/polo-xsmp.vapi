/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Bindings for libpolo-xsmp (lib/xsmp/xsmp-server.h).
 */

[CCode (cheader_filename = "xsmp-server.h")]
namespace Polo.Xsmp {
    [CCode (cname = "PoloXsmpSaveType", cprefix = "POLO_XSMP_SAVE_", has_type_id = false)]
    public enum SaveType {
        GLOBAL,
        LOCAL,
        BOTH
    }

    [CCode (cname = "PoloXsmpInteractStyle", cprefix = "POLO_XSMP_INTERACT_", has_type_id = false)]
    public enum InteractStyle {
        NONE,
        ERRORS,
        ANY
    }

    [CCode (cname = "PoloXsmpRestartStyle", cprefix = "POLO_XSMP_RESTART_", has_type_id = false)]
    public enum RestartStyle {
        IF_RUNNING,
        ANYWAY,
        IMMEDIATELY,
        NEVER
    }

    [CCode (cname = "PoloXsmpServer", type_id = "polo_xsmp_server_get_type ()")]
    public class Server : GLib.Object {
        [CCode (cname = "polo_xsmp_server_new")]
        public Server (string vendor, string release) throws GLib.Error;

        /* The value for SESSION_MANAGER. */
        [CCode (cname = "polo_xsmp_server_get_network_ids")]
        public unowned string get_network_ids ();

        [CCode (cname = "polo_xsmp_server_set_accepting")]
        public void set_accepting (bool accepting);

        public signal void new_client (Client client);
    }

    [CCode (cname = "PoloXsmpClient", type_id = "polo_xsmp_client_get_type ()")]
    public class Client : GLib.Object {
        /* Inside register_request only: the id to reply with; none refuses. */
        [CCode (cname = "polo_xsmp_client_accept_registration")]
        public void accept_registration (string id);

        [CCode (cname = "polo_xsmp_client_save_yourself")]
        public void save_yourself (SaveType save_type, bool shutdown, InteractStyle interact_style, bool fast);

        [CCode (cname = "polo_xsmp_client_save_yourself_phase2")]
        public void save_yourself_phase2 ();

        [CCode (cname = "polo_xsmp_client_interact")]
        public void interact ();

        [CCode (cname = "polo_xsmp_client_save_complete")]
        public void save_complete ();

        [CCode (cname = "polo_xsmp_client_shutdown_cancelled")]
        public void shutdown_cancelled ();

        [CCode (cname = "polo_xsmp_client_die")]
        public void die ();

        [CCode (cname = "polo_xsmp_client_close")]
        public void close ();

        [CCode (cname = "polo_xsmp_client_is_connected")]
        public bool is_connected ();

        [CCode (cname = "polo_xsmp_client_get_string")]
        public string? get_string (string name);

        [CCode (cname = "polo_xsmp_client_get_command")]
        public string? get_command (string name);

        [CCode (cname = "polo_xsmp_client_get_restart_style")]
        public RestartStyle get_restart_style ();

        public signal void register_request (string? previous_id);
        public signal void interact_request (int dialog_type);
        public signal void interact_done (bool cancel_shutdown);
        public signal void save_yourself_request (int save_type, bool shutdown, int interact_style, bool fast,
                                                  bool global);
        public signal void save_yourself_phase2_request ();
        public signal void save_yourself_done (bool success);
        public signal void properties_changed ();
        public signal void closed ();
        public signal void disconnected ();
    }
}
