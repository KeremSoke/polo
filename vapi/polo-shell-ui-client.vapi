/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Bindings for the polo-shell-ui protocol client.
 */

[CCode (cheader_filename = "protocol-client.h")]
namespace PoloShellUi {
    [CCode (cname = "PoloShellUiClientError", cprefix = "POLO_SHELL_UI_CLIENT_ERROR_")]
    public errordomain ClientError {
        NOT_WAYLAND,
        NO_GLOBAL
    }

    [CCode (cname = "PoloShellUiClient", type_id = "polo_shell_ui_client_get_type ()")]
    public class Client : GLib.Object {
        [CCode (cname = "polo_shell_ui_client_new", has_construct_function = false)]
        public Client (Gdk.Display display) throws ClientError;

        [CCode (cname = "polo_shell_ui_client_switcher_select")]
        public void switcher_select (uint id);

        [CCode (cname = "polo_shell_ui_client_menu_action")]
        public void menu_action (uint window_id, uint op, int workspace);

        [CCode (cname = "polo_shell_ui_client_dialog_surface")]
        public void dialog_surface (uint dialog_id, Gdk.Window window);

        [CCode (cname = "polo_shell_ui_client_dialog_response")]
        public void dialog_response (uint dialog_id, int response, string? text);

        [CCode (cname = "polo_shell_ui_client_color_table")]
        public void color_table ([CCode (array_length_type = "int")] float[] colors);

        [CCode (cname = "polo_shell_ui_client_custom_color")]
        public void custom_color (string name, [CCode (array_length = false)] float[]? rgba);

        public signal void lookup_color (string name);
        public signal void switcher_entry (uint id, string title, string? app_id, uint flags);
        public signal void switcher_thumbnail (uint id, Cairo.ImageSurface thumbnail);
        public signal void switcher_icon_name (uint id, string name);
        public signal void switcher_icon_pixels (uint id, Cairo.ImageSurface pixels);
        public signal void show_switcher (Gdk.Monitor? monitor, uint selected, uint columns, bool expand_to_fit,
                                          uint icon_size);
        public signal void update_selected (uint selected);
        public signal void hide_switcher ();
        public signal void workspace_switcher_entry (uint id, string title, bool blank);
        public signal void workspace_switcher_window (uint id, int x, int y, int width, int height, bool focused,
                                                      string? app_id);
        public signal void show_workspace_switcher (Gdk.Monitor? monitor, uint selected, uint columns, uint active,
                                                    int screen_width, int screen_height, int output_width,
                                                    int output_height, uint icon_size);
        public signal void update_workspace_switcher (uint selected, uint active);
        public signal void window_menu_workspace (uint index, string name);
        public signal void show_window_menu (uint window_id, Gdk.Monitor? monitor, int x, int y, uint ops,
                                             uint insensitive, uint active_workspace, uint n_workspaces, uint time);
        public signal void hide_window_menu (uint window_id);
        public signal void show_osd (uint kind, Gdk.Monitor? monitor, string? text, int value);
        public signal void show_dialog (uint dialog_id, uint kind, Gdk.Monitor? monitor, string? payload);
        public signal void hide_dialog (uint dialog_id);
    }

    /* Sends the colour table and responds to lookup_color. */
    [CCode (cname = "PoloShellUiColorResolver", cheader_filename = "color-resolver.h")]
    public class ColorResolver : GLib.Object {
        [CCode (cname = "polo_shell_ui_color_resolver_new")]
        public ColorResolver (Client client);
    }
}
