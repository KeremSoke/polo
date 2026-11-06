/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Marco 1.28's bell (core/bell.c meta_bell_notify ()) for every bell Polo
 * hears: XKB bells from Xwayland, xdg_system_bell_v1 and gtk_shell1's
 * system_bell, which GTK 3 terminals ring. visual-bell flashes every output
 * white for a frame, or with frame_flash the window's frame for 100 ms
 * (the focused window when the bell names none, the whole screen when that
 * has no frame). audible-bell plays libcanberra's bell-window-system with
 * the window's properties. There is no X server bell to fall back on.
 * Unlike Marco, bells closer than 100 ms are dropped, so a client cannot
 * strobe the screen, and no flash is drawn while the session is locked.
 */

namespace Polo.Shell {
    public class Bell : GLib.Object {
        private const uint FRAME_FLASH_MS = 100;
        private const int64 MIN_INTERVAL_USEC = 100 * 1000;

        [CCode (cname = "g_dgettext", cheader_filename = "glib.h")]
        private static extern unowned string dgettext (string domain, string msgid);

        private Backend.Server server;
        private Policy.Model model;
        private Canberra.Context? sounds = null;
        private int64 last_usec = 0;
        private GLib.HashTable<Window, uint> flashing =
            new GLib.HashTable<Window, uint> (GLib.direct_hash, GLib.direct_equal);

        public Bell (Backend.Server server, Policy.Model model) {
            this.server = server;
            this.model = model;
            if (!CommandLine.test_mode && Canberra.Context.create (out sounds) != Canberra.SUCCESS) {
                sounds = null;
            }
        }

        public void shutdown () {
            foreach (var window in flashing.get_keys ()) {
                GLib.Source.remove (flashing[window]);
                window.frame_flashing = false;
            }
            flashing.remove_all ();
        }

        public void ring (Window? window) {
            int64 now = GLib.get_monotonic_time ();
            if (now - last_usec < MIN_INTERVAL_USEC) {
                return;
            }
            last_usec = now;
            var prefs = model.prefs;
            if (prefs.visual_bell && server.get_lock_state () == Backend.LockState.UNLOCKED) {
                visual (window, prefs.visual_bell_type);
            }
            if (prefs.audible_bell) {
                audible (window);
            }
        }

        private void visual (Window? rang, VisualBellType type) {
            if (type == VisualBellType.FULLSCREEN) {
                server.flash_outputs ();
            } else if (type == VisualBellType.FRAME_FLASH) {
                var window = rang ?? model.focused;
                var xdg = window != null ? window.adapter as XdgWindow : null;
                if (xdg != null && xdg.frame_drawn ()) {
                    flash_frame (window, xdg);
                } else {
                    server.flash_outputs ();
                }
            }
        }

        private void flash_frame (Window window, XdgWindow xdg) {
            if (flashing.contains (window)) {
                GLib.Source.remove (flashing[window]);
            }
            window.frame_flashing = true;
            xdg.refresh_frame ();
            flashing[window] = GLib.Timeout.add (FRAME_FLASH_MS, () => {
                flashing.remove (window);
                window.frame_flashing = false;
                if (model.is_managed (window)) {
                    xdg.refresh_frame ();
                }
                return GLib.Source.REMOVE;
            });
        }

        private void audible (Window? rang) {
            if (sounds == null) {
                return;
            }
            Canberra.Proplist props;
            if (Canberra.Proplist.create (out props) != Canberra.SUCCESS) {
                return;
            }
            props.sets (Canberra.PROP_EVENT_ID, "bell-window-system");
            props.sets (Canberra.PROP_EVENT_DESCRIPTION, dgettext ("marco", "Bell event"));
            props.sets (Canberra.PROP_CANBERRA_CACHE_CONTROL, "permanent");
            var window = rang;
            if (window == null && model.focused != null && model.focused.has_frame ()) {
                window = model.focused;
            }
            if (window != null) {
                window_props (props, window);
            }
            sounds.play_full (1, props);
        }

        private void window_props (Canberra.Proplist props, Window window) {
            if (window.title != null) {
                props.sets (Canberra.PROP_WINDOW_NAME, window.title);
            }
            unowned string? name = window.res_class ?? window.app_id;
            if (name != null) {
                props.sets (Canberra.PROP_APPLICATION_NAME, name);
            }
            var xdg = window.adapter as XdgWindow;
            unowned Backend.Client? client = xdg != null ? xdg.toplevel.get_view ().get_client () : null;
            if (client != null) {
                props.sets (Canberra.PROP_APPLICATION_PROCESS_ID, client.get_pid ().to_string ());
            }
#if POLO_HAVE_XWAYLAND
            var x11 = window.adapter as X11Window;
            if (x11 != null) {
                props.sets (Canberra.PROP_WINDOW_X11_XID, x11.xwindow.get_id ().to_string ());
            }
#endif
            var rect = window.client_rect ();
            props.sets (Canberra.PROP_WINDOW_X, rect.x.to_string ());
            props.sets (Canberra.PROP_WINDOW_Y, rect.y.to_string ());
            props.sets (Canberra.PROP_WINDOW_WIDTH, rect.width.to_string ());
            props.sets (Canberra.PROP_WINDOW_HEIGHT, rect.height.to_string ());
            var outputs = model.get_outputs ();
            if (outputs.length == 0) {
                return;
            }
            int left = int.MAX, top = int.MAX, right = int.MIN, bottom = int.MIN;
            foreach (var output in outputs) {
                left = int.min (left, output.rect.x);
                top = int.min (top, output.rect.y);
                right = int.max (right, output.rect.x + output.rect.width);
                bottom = int.max (bottom, output.rect.y + output.rect.height);
            }
            var screen = Marco.Rect () { x = left, y = top, width = right - left, height = bottom - top };
            if (screen.width > 1) {
                int x = (rect.x - screen.x + rect.width / 2).clamp (0, screen.width - 1);
                props.sets (Canberra.PROP_WINDOW_HPOS, position (x, screen.width - 1));
            }
            if (screen.height > 1) {
                int y = (rect.y - screen.y + rect.height / 2).clamp (0, screen.height - 1);
                props.sets (Canberra.PROP_WINDOW_VPOS, position (y, screen.height - 1));
            }
        }

        private static string position (int value, int range) {
            return "%d.%03d".printf (value / range, (int) (1000.0 * value / range) % 1000);
        }
    }
}
