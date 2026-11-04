/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * mate-session-manager 1.28's gsm-logout-dialog.c and gsm-inhibit-dialog.c,
 * in its translations. The payload is an a{sv}: "dialog" is logout,
 * shutdown or inhibit.
 */

namespace PoloShellUi {
    [CCode (cname = "g_dgettext", cheader_filename = "glib.h")]
    private extern unowned string msm_gettext (string domain, string msgid);

    [CCode (cname = "g_dngettext", cheader_filename = "glib.h")]
    private extern unowned string msm_ngettext (string domain, string msgid, string plural, ulong n);

    private unowned string msm (string msgid) {
        return msm_gettext ("mate-session-manager", msgid);
    }

    namespace EndSessionDialogs {
        private const int RESPONSE_LOGOUT = 0;
        private const int RESPONSE_SWITCH_USER = 1;
        private const int RESPONSE_SHUTDOWN = 2;
        private const int RESPONSE_REBOOT = 3;
        private const int RESPONSE_HIBERNATE = 4;
        private const int RESPONSE_SLEEP = 5;
        private const int RESPONSE_LOCK_SCREEN = 1;
        private const int ICON_SIZE = 32;

        public Gtk.Dialog? create (string? payload) {
            GLib.Variant data;
            try {
                data = GLib.Variant.parse (new GLib.VariantType ("a{sv}"), payload ?? "");
            } catch (GLib.VariantParseError e) {
                GLib.warning ("end-session payload: %s", e.message);
                return null;
            }
            var dict = new GLib.VariantDict (data);
            string kind;
            if (!dict.lookup ("dialog", "s", out kind)) {
                return null;
            }
            if (kind == "inhibit") {
                return inhibit (dict);
            }
            if (kind == "logout" || kind == "shutdown") {
                return logout (dict, kind == "shutdown");
            }
            return null;
        }

        private static void add_button (Gtk.Dialog dialog, string text, string icon_name, int response) {
            var button = new Gtk.Button.with_mnemonic (text);
            button.image = new Gtk.Image.from_icon_name (icon_name, Gtk.IconSize.BUTTON);
            button.use_underline = true;
            button.get_style_context ().add_class ("text-button");
            button.can_default = true;
            button.show ();
            dialog.add_action_widget (button, response);
        }

        private static bool offers (string[] actions, string name) {
            foreach (unowned string action in actions) {
                if (action == name) {
                    return true;
                }
            }
            return false;
        }

        private static string user_name () {
            string? name = GLib.Environment.get_real_name ();
            if (name == null || name == "" || name == "Unknown") {
                name = GLib.Environment.get_user_name ();
            }
            return name;
        }

        private Gtk.Dialog logout (GLib.VariantDict dict, bool shutdown) {
            int timeout = 0;
            dict.lookup ("timeout", "i", out timeout);
            string[] actions = {};
            var offered = dict.lookup_value ("actions", new GLib.VariantType ("as"));
            if (offered != null) {
                actions = offered.dup_strv ();
            }

            var dialog = new Gtk.MessageDialog (null, 0, Gtk.MessageType.WARNING, Gtk.ButtonsType.NONE, null);
            dialog.get_style_context ().add_class ("logout-dialog");
            dialog.skip_taskbar_hint = true;
            dialog.set_keep_above (true);
            dialog.stick ();
            dialog.title = "";

            int default_response;
            if (shutdown) {
                dialog.icon_name = "system-shutdown";
                dialog.set_markup (msm ("Shut down this system now?"));
                default_response = RESPONSE_SHUTDOWN;
                if (offers (actions, "suspend")) {
                    add_button (dialog, msm ("S_uspend"), "battery", RESPONSE_SLEEP);
                }
                if (offers (actions, "hibernate")) {
                    add_button (dialog, msm ("_Hibernate"), "drive-harddisk", RESPONSE_HIBERNATE);
                }
                if (offers (actions, "reboot")) {
                    add_button (dialog, msm ("_Restart"), "view-refresh", RESPONSE_REBOOT);
                }
                add_button (dialog, msm ("_Cancel"), "process-stop", Gtk.ResponseType.CANCEL);
                if (offers (actions, "shutdown")) {
                    add_button (dialog, msm ("_Shut Down"), "system-shutdown", RESPONSE_SHUTDOWN);
                }
            } else {
                dialog.icon_name = "system-log-out";
                dialog.set_markup (msm ("Log out of this system now?"));
                default_response = RESPONSE_LOGOUT;
                if (offers (actions, "switch-user")) {
                    add_button (dialog, msm ("_Switch User"), "system-users", RESPONSE_SWITCH_USER);
                }
                add_button (dialog, msm ("_Cancel"), "process-stop", Gtk.ResponseType.CANCEL);
                add_button (dialog, msm ("_Log Out"), "system-log-out", RESPONSE_LOGOUT);
            }

            var hbox = new Gtk.Box (Gtk.Orientation.HORIZONTAL, 0);
            var progress = new Gtk.ProgressBar ();
            progress.show_text = true;
            progress.fraction = 1.0;
            hbox.pack_start (progress, true, true, 12);
            hbox.show_all ();
            dialog.get_content_area ().add (hbox);
            dialog.window_position = Gtk.WindowPosition.CENTER_ALWAYS;
            dialog.set_default_response (default_response);

            if (timeout <= 0) {
                progress.hide ();
                return dialog;
            }
            string secondary = msm ("You are currently logged in as \"%s\".").printf (user_name ());
            GLib.SourceFunc tick = () => {
                if (timeout == 0) {
                    dialog.response (default_response);
                    return GLib.Source.REMOVE;
                }
                unowned string warning = shutdown
                    ? msm_ngettext ("mate-session-manager", "This system will be automatically shut down in %d second",
                                    "This system will be automatically shut down in %d seconds", timeout)
                    : msm_ngettext ("mate-session-manager", "You will be automatically logged out in %d second",
                                    "You will be automatically logged out in %d seconds", timeout);
                progress.fraction = timeout / 60.0;
                progress.text = warning.printf (timeout);
                dialog.format_secondary_text ("%s", secondary);
                timeout--;
                return GLib.Source.CONTINUE;
            };
            tick ();
            uint timer = GLib.Timeout.add (1000, (owned) tick);
            dialog.destroy.connect (() => GLib.Source.remove (timer));
            return dialog;
        }

        private static string[] desktop_ids (string app_id) {
            string file = app_id.has_suffix (".desktop") ? app_id : app_id + ".desktop";
            return { GLib.Path.get_basename (file), "mate-%s.desktop".printf (app_id) };
        }

        private static Gdk.Pixbuf? load_icon (GLib.Icon? icon) {
            if (icon == null) {
                return null;
            }
            var info = Gtk.IconTheme.get_default ().lookup_by_gicon (icon, ICON_SIZE, Gtk.IconLookupFlags.FORCE_SIZE);
            if (info == null) {
                return null;
            }
            try {
                return info.load_icon ();
            } catch (GLib.Error e) {
                return null;
            }
        }

        private static void add_inhibitor (Gtk.ListStore store, string app_id, string reason) {
            string? name = null;
            Gdk.Pixbuf? pixbuf = null;
            if (app_id != "") {
                foreach (unowned string id in desktop_ids (app_id)) {
                    var info = new GLib.DesktopAppInfo (id);
                    if (info == null && GLib.Path.is_absolute (app_id)) {
                        info = new GLib.DesktopAppInfo.from_filename (app_id);
                    }
                    if (info != null) {
                        name = info.get_name ();
                        pixbuf = load_icon (info.get_icon ());
                        break;
                    }
                }
            }
            if (name == null) {
                name = app_id != "" ? app_id : msm ("Unknown");
            }
            if (pixbuf == null) {
                pixbuf = load_icon (new GLib.ThemedIcon ("mate-windows"));
            }
            Gtk.TreeIter iter;
            store.insert_with_values (out iter, 0, 0, pixbuf, 1, name, 2, reason);
        }

        private static void lock_screen () {
            foreach (unowned string command in new string[] { "mate-screensaver-command --lock",
                                                              "xscreensaver-command -lock" }) {
                string[] argv = command.split (" ");
                if (GLib.Environment.find_program_in_path (argv[0]) == null) {
                    continue;
                }
                try {
                    GLib.Process.spawn_async (null, argv, null, GLib.SpawnFlags.SEARCH_PATH, null, null);
                } catch (GLib.SpawnError e) {
                    GLib.warning ("Couldn't lock screen: %s", e.message);
                }
                return;
            }
            GLib.warning ("Couldn't find any screen locker");
        }

        private Gtk.Dialog inhibit (GLib.VariantDict dict) {
            string action = "logout";
            dict.lookup ("action", "s", out action);
            var store = new Gtk.ListStore (3, typeof (Gdk.Pixbuf), typeof (string), typeof (string));
            var listed = dict.lookup_value ("inhibitors", new GLib.VariantType ("a(ss)"));
            int count = 0;
            if (listed != null) {
                foreach (var entry in listed) {
                    string app_id, reason;
                    entry.get ("(ss)", out app_id, out reason);
                    add_inhibitor (store, app_id, reason);
                    count++;
                }
            }

            var dialog = new Gtk.Dialog ();
            dialog.border_width = 6;
            dialog.icon_name = "system-log-out";
            dialog.title = "";
            dialog.resizable = false;

            var box = new Gtk.Box (Gtk.Orientation.VERTICAL, 6);
            box.border_width = 6;
            var header = new Gtk.Label (null);
            header.xalign = 0;
            header.wrap = true;
            header.set_markup ("<b>%s</b>".printf (GLib.Markup.escape_text (
                count == 1 ? msm ("A program is still running:") : msm ("Some programs are still running:"))));
            box.pack_start (header, false, false, 0);

            var scrolled = new Gtk.ScrolledWindow (null, null);
            scrolled.set_policy (Gtk.PolicyType.AUTOMATIC, Gtk.PolicyType.AUTOMATIC);
            scrolled.shadow_type = Gtk.ShadowType.IN;
            var view = new Gtk.TreeView.with_model (store);
            view.headers_visible = false;
            view.enable_search = false;
            view.show_expanders = false;
            var image = new Gtk.CellRendererPixbuf ();
            image.xalign = 1.0f;
            var image_column = new Gtk.TreeViewColumn ();
            image_column.pack_start (image, false);
            image_column.add_attribute (image, "pixbuf", 0);
            view.append_column (image_column);
            var text = new Gtk.CellRendererText ();
            var text_column = new Gtk.TreeViewColumn ();
            text_column.pack_start (text, false);
            text_column.set_cell_data_func (text, (column, cell, model, iter) => {
                string name, reason;
                model.get (iter, 1, out name, 2, out reason);
                ((Gtk.CellRendererText) cell).markup = "<b>%s</b>\n<span size=\"small\">%s</span>".printf (
                    GLib.Markup.escape_text (name), GLib.Markup.escape_text (reason));
            });
            view.append_column (text_column);
            view.tooltip_column = 2;
            scrolled.add (view);
            box.pack_start (scrolled, false, false, 0);

            var description = new Gtk.Label (
                count == 1 ? msm ("Waiting for the program to finish.  Interrupting the program may cause you to lose work.")
                           : msm ("Waiting for programs to finish.  Interrupting these programs may cause you to lose work."));
            description.xalign = 0;
            description.yalign = 0;
            description.wrap = true;
            box.pack_start (description, true, true, 0);
            box.show_all ();
            dialog.get_content_area ().add (box);

            unowned string anyway;
            switch (action) {
                case "shutdown":
                    anyway = msm ("Shut Down Anyway");
                    break;
                case "reboot":
                    anyway = msm ("Reboot Anyway");
                    break;
                case "switch-user":
                    anyway = msm ("Switch User Anyway");
                    break;
                case "suspend":
                    anyway = msm ("Suspend Anyway");
                    break;
                case "hibernate":
                    anyway = msm ("Hibernate Anyway");
                    break;
                default:
                    anyway = msm ("Log Out Anyway");
                    break;
            }
            dialog.add_button (msm ("Lock Screen"), RESPONSE_LOCK_SCREEN);
            dialog.add_button (msm ("Cancel"), Gtk.ResponseType.CANCEL);
            dialog.add_button (anyway, Gtk.ResponseType.ACCEPT);
            dialog.response.connect ((response) => {
                if (response == RESPONSE_LOCK_SCREEN) {
                    GLib.Signal.stop_emission_by_name (dialog, "response");
                    lock_screen ();
                }
            });
            return dialog;
        }
    }
}
