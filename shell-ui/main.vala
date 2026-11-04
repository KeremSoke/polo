/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * polo-shell-ui: Polo's GTK helper, started by Polo with WAYLAND_SOCKET.
 */

namespace PoloShellUi {
    public class Helper : GLib.Object {
        public Client client { get; construct; }
        private AltTab alt_tab;
        private WindowMenu window_menu;
        private Dialogs dialogs;
        private ColorResolver colors;
        private WindowIcons icons;
        private GLib.Settings? interface_settings = null;

        public Helper (Client client) {
            Object (client: client);
        }

        construct {
            follow_gtk_theme ();
            alt_tab = new AltTab (client);
            window_menu = new WindowMenu (client);
            dialogs = new Dialogs (client);
            colors = new ColorResolver (client);
            icons = new WindowIcons (client);
        }

        /* Marco's frames take their colours and icons from the GTK and icon themes MATE sets, which reached
         * it through XSETTINGS. */
        private void follow_gtk_theme () {
            var source = GLib.SettingsSchemaSource.get_default ();
            var schema = source != null ? source.lookup ("org.mate.interface", true) : null;
            if (schema == null || !schema.has_key ("gtk-theme")) {
                return;
            }
            interface_settings = new GLib.Settings.full (schema, null, null);
            interface_settings.changed["gtk-theme"].connect (apply_gtk_theme);
            interface_settings.changed["icon-theme"].connect (apply_icon_theme);
            apply_gtk_theme ();
            apply_icon_theme ();
        }

        private void apply_gtk_theme () {
            var name = interface_settings.get_string ("gtk-theme");
            if (name != "") {
                Gtk.Settings.get_default ().gtk_theme_name = name;
            }
        }

        private void apply_icon_theme () {
            var name = interface_settings.get_string ("icon-theme");
            if (name != "") {
                Gtk.Settings.get_default ().gtk_icon_theme_name = name;
            }
        }
    }

    int main (string[] args) {
        GLib.Environment.set_prgname ("polo-shell-ui");
        Gdk.set_allowed_backends ("wayland");
        if (!Gtk.init_check (ref args)) {
            printerr ("polo-shell-ui: cannot open the Wayland display\n");
            return 1;
        }
        /* gtk-layer-shell performs a GDK-queue roundtrip here. Do this before
         * creating the client so Polo cannot dispatch into a half-built popup. */
        if (!GtkLayerShell.is_supported ()) {
            printerr ("polo-shell-ui: the compositor has no layer shell\n");
            return 1;
        }
        Helper helper;
        try {
            helper = new Helper (new Client (Gdk.Display.get_default ()));
        } catch (ClientError e) {
            printerr ("polo-shell-ui: %s\n", e.message);
            return 1;
        }
        Gtk.main ();
        helper = null;
        return 0;
    }
}
