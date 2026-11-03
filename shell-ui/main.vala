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

        public Helper (Client client) {
            Object (client: client);
        }

        construct {
            alt_tab = new AltTab (client);
            window_menu = new WindowMenu (client);
            dialogs = new Dialogs (client);
            colors = new ColorResolver (client);
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
