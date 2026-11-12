/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * GTK implementation of Marco's window menu. It uses a one-pixel layer-shell
 * overlay at Polo's requested point and returns exactly one action, or none
 * when dismissed. Accelerators come from Marco's window-keybindings and are
 * drawn with GtkAccelLabel.
 */

namespace PoloShellUi {
    /* Marco's MetaMenuOp flags. */
    private const uint OP_NONE = 0;
    private const uint OP_DELETE = 1 << 0;
    private const uint OP_MINIMIZE = 1 << 1;
    private const uint OP_UNMAXIMIZE = 1 << 2;
    private const uint OP_MAXIMIZE = 1 << 3;
    private const uint OP_UNSHADE = 1 << 4;
    private const uint OP_SHADE = 1 << 5;
    private const uint OP_UNSTICK = 1 << 6;
    private const uint OP_STICK = 1 << 7;
    private const uint OP_WORKSPACES = 1 << 8;
    private const uint OP_MOVE = 1 << 9;
    private const uint OP_RESIZE = 1 << 10;
    private const uint OP_ABOVE = 1 << 11;
    private const uint OP_UNABOVE = 1 << 12;
    private const uint OP_MOVE_LEFT = 1 << 13;
    private const uint OP_MOVE_RIGHT = 1 << 14;
    private const uint OP_MOVE_UP = 1 << 15;
    private const uint OP_MOVE_DOWN = 1 << 16;
    private const uint OP_RECOVER = 1 << 17;
    private const uint ON_ALL_WORKSPACES = uint.MAX;

    private enum ItemType {
        SEPARATOR,
        NORMAL,
        IMAGE,
        CHECKBOX,
        RADIOBUTTON,
        WORKSPACE_LIST
    }

    private struct MenuItemSpec {
        public uint op;
        public ItemType type;
        public unowned string? icon;
        public bool checked;
        public unowned string? label;
    }

    private const MenuItemSpec[] MENU_ITEMS = {
        { OP_MINIMIZE, ItemType.IMAGE, "go-down", false, "Mi_nimize" },
        { OP_MAXIMIZE, ItemType.IMAGE, "view-fullscreen", false, "Ma_ximize" },
        { OP_UNMAXIMIZE, ItemType.IMAGE, "view-restore", false, "Unma_ximize" },
        { OP_SHADE, ItemType.NORMAL, null, false, "Roll _Up" },
        { OP_UNSHADE, ItemType.NORMAL, null, false, "_Unroll" },
        { OP_MOVE, ItemType.NORMAL, null, false, "_Move" },
        { OP_RESIZE, ItemType.NORMAL, null, false, "_Resize" },
        { OP_RECOVER, ItemType.NORMAL, null, false, "Move Titlebar On_screen" },
        { OP_WORKSPACES, ItemType.SEPARATOR, null, false, null },
        { OP_ABOVE, ItemType.CHECKBOX, null, false, "Always on _Top" },
        { OP_UNABOVE, ItemType.CHECKBOX, null, true, "Always on _Top" },
        { OP_STICK, ItemType.RADIOBUTTON, null, false, "_Always on Visible Workspace" },
        { OP_UNSTICK, ItemType.RADIOBUTTON, null, false, "_Only on This Workspace" },
        { OP_MOVE_LEFT, ItemType.NORMAL, null, false, "Move to Workspace _Left" },
        { OP_MOVE_RIGHT, ItemType.NORMAL, null, false, "Move to Workspace R_ight" },
        { OP_MOVE_UP, ItemType.NORMAL, null, false, "Move to Workspace _Up" },
        { OP_MOVE_DOWN, ItemType.NORMAL, null, false, "Move to Workspace _Down" },
        { OP_NONE, ItemType.WORKSPACE_LIST, null, false, null },
        { OP_NONE, ItemType.SEPARATOR, null, false, null },
        { OP_DELETE, ItemType.IMAGE, "window-close", false, "_Close" }
    };

    [CCode (cname = "g_dgettext", cheader_filename = "glib.h")]
    private extern unowned string marco_gettext (string domain, string msgid);

    private unowned string marco (string msgid) {
        return marco_gettext ("marco", msgid);
    }

    [CCode (cname = "sscanf", cheader_filename = "stdio.h")]
    private extern int sscanf (string str, string format, ...);

    private class Menu {
        public uint id;
        public Gtk.Window host;
        public Gtk.Menu menu;
        public bool answered = false;
    }

    public class WindowMenu : GLib.Object {
        private Client client;
        private GLib.Settings? keybindings = null;
        private string[] workspace_names = {};
        private Menu? current = null;

        public WindowMenu (Client client) {
            this.client = client;
            var source = GLib.SettingsSchemaSource.get_default ();
            var schema = source != null ? source.lookup ("org.mate.Marco.window-keybindings", true) : null;
            if (schema != null) {
                keybindings = new GLib.Settings.full (schema, null, null);
            }
            client.window_menu_workspace.connect (on_workspace);
            client.show_window_menu.connect (on_show);
            client.hide_window_menu.connect (on_hide);
        }

        private void on_workspace (uint index, string name) {
            if (index >= workspace_names.length) {
                workspace_names.resize ((int) index + 1);
            }
            workspace_names[index] = name;
        }

        private static string? binding_name (uint op, int workspace) {
            switch (op) {
                case OP_DELETE: return "close";
                case OP_MINIMIZE: return "minimize";
                case OP_UNMAXIMIZE: return "unmaximize";
                case OP_MAXIMIZE: return "maximize";
                case OP_UNSHADE:
                case OP_SHADE: return "toggle-shaded";
                case OP_UNSTICK:
                case OP_STICK: return "toggle-on-all-workspaces";
                case OP_ABOVE:
                case OP_UNABOVE: return "toggle-above";
                case OP_WORKSPACES:
                    return workspace >= 1 && workspace <= 12 ? "move-to-workspace-%d".printf (workspace) : null;
                case OP_MOVE: return "begin-move";
                case OP_RESIZE: return "begin-resize";
                case OP_MOVE_LEFT: return "move-to-workspace-left";
                case OP_MOVE_RIGHT: return "move-to-workspace-right";
                case OP_MOVE_UP: return "move-to-workspace-up";
                case OP_MOVE_DOWN: return "move-to-workspace-down";
                default: return null;
            }
        }

        private void accelerator (uint op, int workspace, out uint key, out Gdk.ModifierType mods) {
            key = 0;
            mods = 0;
            var name = binding_name (op, workspace);
            if (name == null || keybindings == null || keybindings.settings_schema.has_key (name) == false) {
                return;
            }
            Gtk.accelerator_parse (keybindings.get_string (name), out key, out mods);
        }

        private Gtk.MenuItem? menu_item_new (MenuItemSpec spec, string label, int workspace_id) {
            Gtk.MenuItem item;
            switch (spec.type) {
                case ItemType.NORMAL:
                    item = new Gtk.MenuItem ();
                    break;
                case ItemType.IMAGE:
                    var image_item = new Mate.ImageMenuItem ();
                    var image = new Gtk.Image.from_icon_name (spec.icon, Gtk.IconSize.MENU);
                    image_item.set_image (image);
                    image.show ();
                    item = image_item;
                    break;
                case ItemType.CHECKBOX:
                    var check = new Gtk.CheckMenuItem ();
                    check.active = spec.checked;
                    item = check;
                    break;
                case ItemType.RADIOBUTTON:
                    var radio = new Gtk.CheckMenuItem ();
                    radio.draw_as_radio = true;
                    radio.active = spec.checked;
                    item = radio;
                    break;
                case ItemType.WORKSPACE_LIST:
                    return null;
                default:
                    return new Gtk.SeparatorMenuItem ();
            }
            var accel_label = new Gtk.AccelLabel ("");
            accel_label.set_text_with_mnemonic (label);
            accel_label.halign = Gtk.Align.START;
            item.add (accel_label);
            accel_label.show ();
            uint key;
            Gdk.ModifierType mods;
            accelerator (spec.op, workspace_id, out key, out mods);
            accel_label.set_accel (key, mods);
            return item;
        }

        private string workspace_name_with_accel (int index) {
            string name = index < workspace_names.length && workspace_names[index] != null
                ? workspace_names[index] : marco ("Workspace %d").printf (index + 1);
            int number = 0;
            int charcount = 0;
            if (sscanf (name, marco ("Workspace %d%n"), &number, &charcount) != 0 &&
                charcount == name.length) {
                if (number == 10) {
                    return marco ("Workspace 1_0");
                }
                return marco ("Workspace %s%d").printf (number < 10 ? "_" : "", number);
            }
            var escaped = name.replace ("_", "__");
            if (index < 9) {
                escaped += " (_%d)".printf (index + 1);
            } else if (index == 9) {
                escaped += " (_0)";
            }
            return escaped;
        }

        private void answer (Menu menu, uint op, int workspace) {
            if (menu.answered) {
                return;
            }
            menu.answered = true;
            client.menu_action (menu.id, op, workspace);
            if (current == menu) {
                current = null;
            }
            GLib.Idle.add (() => {
                menu.menu.destroy ();
                /* Keep the GDK window alive until its surface is released; otherwise
                 * GDK can finalize it during surface teardown and crash. */
                Gdk.Window? held = menu.host.get_window ();
                menu.host.destroy ();
                GLib.Timeout.add_seconds (1, () => {
                    held = null;
                    return GLib.Source.REMOVE;
                });
                return GLib.Source.REMOVE;
            });
        }

        private void connect_activate (Menu menu, Gtk.MenuItem item, uint op, int workspace) {
            item.activate.connect (() => answer (menu, op, workspace));
        }

        private Gtk.Menu build (Menu menu, uint ops, uint insensitive, uint active_workspace, int n_workspaces) {
            if (n_workspaces < 2) {
                ops &= ~(OP_STICK | OP_UNSTICK | OP_WORKSPACES);
            } else if (n_workspaces == 2) {
                ops &= ~OP_WORKSPACES;
            }
            var gtk_menu = new Gtk.Menu ();
            foreach (var spec in MENU_ITEMS) {
                if ((ops & spec.op) == 0 && spec.op != 0) {
                    continue;
                }
                var item = menu_item_new (spec, spec.label != null ? marco (spec.label) : "", -1);
                if (spec.op == OP_STICK) {
                    ((Gtk.CheckMenuItem) item).active = active_workspace == ON_ALL_WORKSPACES;
                } else if (spec.op == OP_UNSTICK) {
                    ((Gtk.CheckMenuItem) item).active = active_workspace != ON_ALL_WORKSPACES;
                }

                if (spec.type == ItemType.WORKSPACE_LIST) {
                    if ((ops & OP_WORKSPACES) != 0) {
                        var to_another = MenuItemSpec () {
                            op = OP_NONE, type = ItemType.NORMAL, label = "Move to Another _Workspace"
                        };
                        var submenu = new Gtk.Menu ();
                        item = menu_item_new (to_another, marco (to_another.label), -1);
                        item.set_submenu (submenu);
                        for (int j = 0; j < n_workspaces; j++) {
                            var move_item = MenuItemSpec () { op = OP_WORKSPACES, type = ItemType.NORMAL };
                            var sub = menu_item_new (move_item, workspace_name_with_accel (j), j + 1);
                            if (active_workspace == (uint) j && (ops & OP_UNSTICK) != 0) {
                                sub.sensitive = false;
                            }
                            connect_activate (menu, sub, OP_WORKSPACES, j);
                            submenu.append (sub);
                            sub.show ();
                        }
                    }
                } else if (spec.type != ItemType.SEPARATOR) {
                    if ((insensitive & spec.op) != 0) {
                        item.sensitive = false;
                    }
                    connect_activate (menu, item, spec.op, 0);
                }

                if (item != null) {
                    gtk_menu.append (item);
                    item.show ();
                }
            }
            return gtk_menu;
        }

        private void on_show (uint id, Gdk.Monitor? monitor, int x, int y, uint ops, uint insensitive,
                              uint active_workspace, uint n_workspaces, uint time) {
            if (current != null) {
                current.menu.popdown ();
                answer (current, OP_NONE, 0);
            }
            var menu = new Menu () { id = id };
            current = menu;
            menu.menu = build (menu, ops, insensitive, active_workspace, (int) n_workspaces);
            menu.menu.deactivate.connect (() => {
                GLib.Idle.add (() => {
                    answer (menu, OP_NONE, 0);
                    return GLib.Source.REMOVE;
                });
            });

            Gtk.Requisition req;
            menu.menu.get_preferred_size (out req, null);
            if (monitor != null) {
                var geometry = monitor.get_geometry ();
                x = x.clamp (0, int.max (0, geometry.width - req.width));
                y = y.clamp (0, int.max (0, geometry.height - req.height));
            }

            var host = new Gtk.Window (Gtk.WindowType.TOPLEVEL);
            menu.host = host;
            GtkLayerShell.init_for_window (host);
            GtkLayerShell.set_layer (host, GtkLayerShell.Layer.OVERLAY);
            GtkLayerShell.set_namespace (host, "polo-window-menu");
            GtkLayerShell.set_keyboard_mode (host, GtkLayerShell.KeyboardMode.NONE);
            if (monitor != null) {
                GtkLayerShell.set_monitor (host, monitor);
            }
            GtkLayerShell.set_anchor (host, GtkLayerShell.Edge.TOP, true);
            GtkLayerShell.set_anchor (host, GtkLayerShell.Edge.LEFT, true);
            GtkLayerShell.set_margin (host, GtkLayerShell.Edge.TOP, y);
            GtkLayerShell.set_margin (host, GtkLayerShell.Edge.LEFT, x);
            host.app_paintable = true;
            var visual = host.get_screen ().get_rgba_visual ();
            if (visual != null) {
                host.set_visual (visual);
            }
            host.set_default_size (1, 1);
            /* The parent needs a buffer before GTK maps the popup. A childless
             * window never draws, so keep a 1×1 child. */
            bool painted = false;
            host.draw.connect ((cr) => {
                cr.set_operator (Cairo.Operator.CLEAR);
                cr.paint ();
                if (!painted) {
                    painted = true;
                    GLib.Idle.add (() => {
                        if (current == menu && !menu.answered) {
                            var rect = Gdk.Rectangle () { x = 0, y = 0, width = 1, height = 1 };
                            var trigger = new Gdk.Event (Gdk.EventType.KEY_PRESS);
                            trigger.key.window = host.get_window ();
                            trigger.key.time = time;
                            trigger.set_device (host.get_display ().get_default_seat ().get_keyboard ());
                            menu.menu.popup_at_rect (host.get_window (), rect, Gdk.Gravity.NORTH_WEST,
                                                     Gdk.Gravity.NORTH_WEST, trigger);
                            release_window (menu.menu.get_data<Gdk.Window> ("gtk-menu-transfer-window"));
                        }
                        return GLib.Source.REMOVE;
                    });
                }
                return true;
            });
            host.realize.connect (() => {
                host.input_shape_combine_region (new Cairo.Region ());
            });
            var filler = new Gtk.Box (Gtk.Orientation.VERTICAL, 0);
            filler.set_size_request (1, 1);
            host.add (filler);
            host.show_all ();
        }

        private void on_hide (uint id) {
            if (current != null && current.id == id) {
                current.menu.popdown ();
                answer (current, OP_NONE, 0);
            }
        }
    }
}
