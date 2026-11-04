/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * mate-session 1.28's gsm-xsmp-client.c and its use in gsm-manager.c, on
 * lib/xsmp's libSM server. An XSMP client is a session client like a D-Bus
 * one, so the end of session drives both.
 */

namespace Polo {
    public class XsmpSessionClient : Dbus.Client {
        private const string SM_PROGRAM = "Program";
        private const string SM_PROCESS_ID = "ProcessID";
        private const string SM_RESTART_COMMAND = "RestartCommand";
        private const string SM_DISCARD_COMMAND = "DiscardCommand";
        private const string GSM_DESKTOP_FILE = "_GSM_DesktopFile";
        private const uint FLAG_FORCEFUL = 1;
        private const uint FLAG_SAVE = 2;
        private const uint FLAG_LAST = 4;
        private const int NONE = -1;

        [CCode (cname = "g_dgettext", cheader_filename = "glib.h")]
        private static extern unowned string dgettext (string domain, string msgid);

        public Xsmp.Client connection { get; construct; }
        private Dbus.Manager manager;
        private int current_save = NONE;
        private int next_save = NONE;
        private bool next_interact = false;

        public XsmpSessionClient (Xsmp.Client connection, Dbus.Manager manager) {
            Object (startup_id: "", bus_name: "", connection: connection);
            this.manager = manager;
            restart_style_hint = 1;
            connection.register_request.connect (on_register_request);
            connection.interact_request.connect (on_interact_request);
            connection.interact_done.connect (on_interact_done);
            connection.save_yourself_request.connect (on_save_yourself_request);
            connection.save_yourself_phase2_request.connect (on_save_yourself_phase2_request);
            connection.save_yourself_done.connect (on_save_yourself_done);
            connection.properties_changed.connect (on_properties_changed);
            connection.closed.connect (on_closed);
            connection.disconnected.connect (on_disconnected);
        }

        private void forget () {
            connection.register_request.disconnect (on_register_request);
            connection.interact_request.disconnect (on_interact_request);
            connection.interact_done.disconnect (on_interact_done);
            connection.save_yourself_request.disconnect (on_save_yourself_request);
            connection.save_yourself_phase2_request.disconnect (on_save_yourself_phase2_request);
            connection.save_yourself_done.disconnect (on_save_yourself_done);
            connection.properties_changed.disconnect (on_properties_changed);
            connection.closed.disconnect (on_closed);
            connection.disconnected.disconnect (on_disconnected);
            connection.close ();
            manager.xsmp_client_gone (this);
        }

        private void on_register_request (string? previous_id) {
            string? id = manager.register_xsmp_client (this, previous_id);
            if (id == null) {
                return;
            }
            startup_id = id;
            connection.accept_registration (id);
            status = Dbus.ClientStatus.REGISTERED;
            if (previous_id == null || previous_id == "") {
                connection.save_yourself (Xsmp.SaveType.LOCAL, false, Xsmp.InteractStyle.NONE, false);
                current_save = Xsmp.SaveType.LOCAL;
            }
        }

        private void on_properties_changed () {
            string? process = connection.get_string (SM_PROCESS_ID);
            uint value = 0;
            pid = process != null && uint.try_parse (process, out value) ? value : 0;
            switch (connection.get_restart_style ()) {
                case Xsmp.RestartStyle.NEVER:
                    restart_style_hint = 0;
                    break;
                case Xsmp.RestartStyle.ANYWAY:
                    restart_style_hint = 2;
                    break;
                case Xsmp.RestartStyle.IMMEDIATELY:
                    restart_style_hint = 3;
                    break;
                default:
                    restart_style_hint = 1;
                    break;
            }
        }

        /* gsm's do_save_yourself (). */
        private void save_yourself (int save_type, bool allow_interact) {
            if (next_save != NONE) {
                return;
            }
            if (current_save != NONE) {
                next_save = save_type;
                next_interact = allow_interact;
                return;
            }
            current_save = save_type;
            next_save = NONE;
            next_interact = false;
            if (save_type == Xsmp.SaveType.LOCAL) {
                connection.save_yourself (Xsmp.SaveType.LOCAL, false, Xsmp.InteractStyle.NONE, false);
            } else if (!allow_interact) {
                connection.save_yourself ((Xsmp.SaveType) save_type, true, Xsmp.InteractStyle.NONE, true);
            } else {
                connection.save_yourself ((Xsmp.SaveType) save_type, true, Xsmp.InteractStyle.ANY, false);
            }
        }

        public override void query_end_session (uint flags) {
            save_yourself (Xsmp.SaveType.GLOBAL, (flags & FLAG_FORCEFUL) == 0);
        }

        public override void end_session (uint flags) {
            if ((flags & FLAG_LAST) != 0) {
                connection.save_yourself_phase2 ();
                return;
            }
            save_yourself ((flags & FLAG_SAVE) != 0 ? Xsmp.SaveType.BOTH : Xsmp.SaveType.GLOBAL, false);
        }

        public override void cancel_end_session () {
            connection.shutdown_cancelled ();
            current_save = NONE;
            next_save = NONE;
            next_interact = false;
        }

        public override void stop () {
            connection.die ();
        }

        private void on_save_yourself_request (int save_type, bool shutdown, int interact_style, bool fast,
                                               bool global) {
            if (shutdown && global) {
                manager.request_logout (fast ? Dbus.LogoutMode.NO_CONFIRMATION : Dbus.LogoutMode.NORMAL);
            } else if (!shutdown && !global) {
                save_yourself (Xsmp.SaveType.LOCAL, true);
            }
        }

        private void on_save_yourself_phase2_request () {
            current_save = NONE;
            end_session_response (true, true, false, "");
        }

        private void on_interact_request (int dialog_type) {
            end_session_response (false, false, false,
                                  dgettext ("mate-session-manager", "This program is blocking logout."));
            connection.interact ();
        }

        private void on_interact_done (bool cancel_shutdown) {
            end_session_response (true, false, cancel_shutdown, "");
        }

        /* gsm tests next_save_yourself for truth, and its "none" is -1, so it
         * would queue a bogus SaveYourself after every SaveYourselfDone. */
        private void on_save_yourself_done (bool success) {
            if (current_save != NONE) {
                connection.save_complete ();
                current_save = NONE;
            }
            end_session_response (true, false, false, "");
            if (next_save != NONE) {
                int save_type = next_save;
                bool allow_interact = next_interact;
                next_save = NONE;
                next_interact = false;
                save_yourself (save_type, allow_interact);
            }
        }

        private void on_closed () {
            status = Dbus.ClientStatus.FINISHED;
            forget ();
        }

        private void on_disconnected () {
            status = Dbus.ClientStatus.FAILED;
            forget ();
        }

        private string? desktop_file () {
            string? uri = connection.get_string (GSM_DESKTOP_FILE);
            if (uri != null) {
                return GLib.File.new_for_uri (uri).get_path ();
            }
            string? program = connection.get_string (SM_PROGRAM);
            if (program == null) {
                return null;
            }
            string[] app_dirs = { GLib.Path.build_filename (GLib.Environment.get_user_data_dir (), "applications") };
            foreach (unowned string dir in GLib.Environment.get_system_data_dirs ()) {
                app_dirs += GLib.Path.build_filename (dir, "applications");
            }
            string[] search = app_dirs;
            foreach (unowned string dir in Autostart.directories ()) {
                search += dir;
            }
            search += Autostart.saved_session_directory ();
            foreach (unowned string name in new string[] { program + ".desktop", "mate-" + program + ".desktop" }) {
                foreach (unowned string dir in search) {
                    var path = GLib.Path.build_filename (dir, name);
                    if (GLib.FileUtils.test (path, GLib.FileTest.IS_REGULAR)) {
                        return path;
                    }
                }
            }
            return null;
        }

        /* gsm's xsmp_save (). */
        public override GLib.KeyFile? save () {
            if (connection.get_restart_style () == Xsmp.RestartStyle.NEVER) {
                return null;
            }
            string? restart = connection.get_command (SM_RESTART_COMMAND);
            if (restart == null) {
                return null;
            }
            var keyfile = new GLib.KeyFile ();
            string? path = desktop_file ();
            bool loaded = false;
            if (path != null) {
                try {
                    loaded = keyfile.load_from_file (path, GLib.KeyFileFlags.KEEP_COMMENTS |
                                                     GLib.KeyFileFlags.KEEP_TRANSLATIONS);
                } catch (GLib.Error e) {
                    Logging.scope (LogDomain.POLO).info ("saving %s: %s", path, e.message);
                }
            }
            if (!loaded) {
                string group = GLib.KeyFileDesktop.GROUP;
                keyfile.set_string (group, GLib.KeyFileDesktop.KEY_NAME,
                                    connection.get_string (SM_PROGRAM) ??
                                    dgettext ("mate-session-manager", "Remembered Application"));
                keyfile.set_string (group, GLib.KeyFileDesktop.KEY_COMMENT,
                                    "Client %s which was automatically saved".printf (startup_id));
                keyfile.set_string (group, GLib.KeyFileDesktop.KEY_ICON, "system-run");
                keyfile.set_string (group, GLib.KeyFileDesktop.KEY_TYPE, "Application");
                keyfile.set_boolean (group, GLib.KeyFileDesktop.KEY_STARTUP_NOTIFY, true);
            }
            keyfile.set_string (GLib.KeyFileDesktop.GROUP, Autostart.STARTUP_ID_KEY, startup_id);
            keyfile.set_string (GLib.KeyFileDesktop.GROUP, GLib.KeyFileDesktop.KEY_EXEC, restart);
            string? discard = connection.get_command (SM_DISCARD_COMMAND);
            if (discard != null) {
                keyfile.set_string (GLib.KeyFileDesktop.GROUP, Autostart.DISCARD_KEY, discard);
            }
            return keyfile;
        }
    }

    public class XsmpSession : GLib.Object {
        private Xsmp.Server server;
        private Dbus.Manager manager;

        /* SESSION_MANAGER is set for everything started after this. */
        public XsmpSession (Dbus.Manager manager) throws GLib.Error {
            this.manager = manager;
            server = new Xsmp.Server ("Polo", Config.VERSION);
            server.new_client.connect (on_new_client);
            SessionEnv.setenv ("SESSION_MANAGER", server.get_network_ids ());
        }

        public void shutdown () {
            server.set_accepting (false);
            server.new_client.disconnect (on_new_client);
        }

        private void on_new_client (Xsmp.Client connection) {
            var client = new XsmpSessionClient (connection, manager);
            if (!manager.add_xsmp_client (client)) {
                connection.close ();
            }
        }
    }
}
