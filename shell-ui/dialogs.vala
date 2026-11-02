/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * GTK versions of Marco's non-responsive and workspace-rename dialogs. They
 * reuse the relevant Zenity 3.44 layouts and return one GtkResponseType;
 * Polo's hide_dialog returns -1.
 */

namespace PoloShellUi {
    private const uint DIALOG_NOT_RESPONDING = 0;
    private const uint DIALOG_RENAME_WORKSPACE = 1;
    private const uint DIALOG_MESSAGE = 3;
    private const string ZENITY_UI = "/org/mate/polo/shell-ui/zenity.ui";

    public class Dialogs : GLib.Object {
        private Client client;
        private GLib.HashTable<uint, Gtk.Dialog> open = new GLib.HashTable<uint, Gtk.Dialog> (null, null);

        public Dialogs (Client client) {
            this.client = client;
            client.show_dialog.connect (on_show);
            client.hide_dialog.connect (on_hide);
        }

        private static Gtk.Builder builder (string id) {
            var builder = new Gtk.Builder ();
            builder.set_translation_domain ("zenity");
            try {
                builder.add_objects_from_resource (ZENITY_UI, { id });
            } catch (GLib.Error e) {
                GLib.error ("%s: %s", ZENITY_UI, e.message);
            }
            return builder;
        }

        private void on_show (uint id, uint kind, Gdk.Monitor? monitor, string? payload) {
            Gtk.Dialog? dialog = null;
            if (kind == DIALOG_NOT_RESPONDING) {
                dialog = not_responding (id, payload ?? "");
            } else if (kind == DIALOG_RENAME_WORKSPACE) {
                dialog = rename_workspace (id, payload);
            } else if (kind == DIALOG_MESSAGE) {
                dialog = message (id, payload ?? "");
            }
            if (dialog == null) {
                client.dialog_response (id, -1, null);
                return;
            }
            dialog.title = marco ("Marco");
            open.insert (id, dialog);
            dialog.realize ();
            client.dialog_surface (id, dialog.get_window ());
            dialog.show ();
        }

        private void answer (uint id, int response, string? text) {
            var dialog = open.lookup (id);
            if (dialog == null) {
                return;
            }
            open.remove (id);
            client.dialog_response (id, response, text);
            dialog.destroy ();
        }

        private void on_hide (uint id) {
            answer (id, -1, null);
        }

        private Gtk.Dialog not_responding (uint id, string title) {
            var builder = builder ("zenity_question_dialog");
            var dialog = (Gtk.Dialog) builder.get_object ("zenity_question_dialog");
            var text = (Gtk.Label) builder.get_object ("zenity_question_text");

            dialog.set_icon_name ("dialog-question");
            var cancel_button = dialog.add_button ("_No", Gtk.ResponseType.CANCEL);
            var ok_button = dialog.add_button ("_Yes", Gtk.ResponseType.OK);
            ok_button.grab_focus ();
            ((Gtk.Button) cancel_button).label = marco ("_Force Quit");
            ((Gtk.Button) ok_button).label = marco ("_Wait");

            text.width_chars = 10;
            text.max_width_chars = 60;
            var tmp = marco ("<tt>%s</tt> is not responding.").printf (GLib.Markup.escape_text (title));
            text.set_markup (("<big><b>%s</b></big>\n\n<i>%s</i>".printf (
                tmp, marco ("You may choose to wait a short while for it to " +
                            "continue or force the application to quit entirely."))).compress ());
            text.get_settings ().gtk_label_select_on_focus = false;

            dialog.response.connect ((response) => answer (id, response, null));
            return dialog;
        }

        private Gtk.Dialog message (uint id, string text) {
            var builder = builder ("zenity_question_dialog");
            var dialog = (Gtk.Dialog) builder.get_object ("zenity_question_dialog");
            ((Gtk.Label) builder.get_object ("zenity_question_text")).set_text (text);
            dialog.set_icon_name ("dialog-error");
            dialog.add_button (marco ("OK"), Gtk.ResponseType.OK).grab_focus ();
            dialog.response.connect ((response) => answer (id, response, null));
            return dialog;
        }

        private Gtk.Dialog rename_workspace (uint id, string? name) {
            var builder = builder ("zenity_entry_dialog");
            var dialog = (Gtk.Dialog) builder.get_object ("zenity_entry_dialog");
            ((Gtk.Button) builder.get_object ("zenity_entry_ok_button")).label = marco ("OK");
            ((Gtk.Button) builder.get_object ("zenity_entry_cancel_button")).label = marco ("Cancel");
            var text = (Gtk.Label) builder.get_object ("zenity_entry_text");
            text.set_text_with_mnemonic (marco ("New Workspace Name:").compress ());

            var entry = new Gtk.Entry ();
            entry.activates_default = true;
            if (name != null) {
                entry.text = name;
            }
            entry.show ();
            ((Gtk.Box) builder.get_object ("vbox4")).pack_end (entry, false, false, 0);
            text.mnemonic_widget = entry;

            dialog.response.connect ((response) => {
                answer (id, response, response == Gtk.ResponseType.OK ? entry.text : null);
            });
            return dialog;
        }
    }
}
