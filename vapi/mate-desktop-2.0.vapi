/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Bindings for the libmate-desktop image-menu item used by Polo's window menu.
 */

[CCode (cheader_filename = "libmate-desktop/mate-image-menu-item.h")]
namespace Mate {
    [CCode (cname = "MateImageMenuItem", type_id = "mate_image_menu_item_get_type ()")]
    public class ImageMenuItem : Gtk.MenuItem {
        [CCode (cname = "mate_image_menu_item_new", has_construct_function = false, type = "GtkWidget*")]
        public ImageMenuItem ();

        [CCode (cname = "mate_image_menu_item_set_image")]
        public void set_image (Gtk.Widget? image);
    }
}
