/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

namespace Polo.Decor {
    public delegate Marco.Icon? LocalIcon (int size);

    /* Marco's MetaIconCache for one window: the mini icon and icon its frame draws,
     * from the first source that has both, then Marco's default icon. */
    public class WindowIcon : GLib.Object {
        /* META_MINI_ICON_WIDTH */
        public const int MINI_SIZE = 16;

        [Compact]
        private class Step {
            public string? name;
            public string? desktop_id;
            public LocalIcon? local;
        }

        private Decorations decorations;
        private GLib.GenericArray<Step> steps = new GLib.GenericArray<Step> ();
        private uint next_step = 0;
        private bool fallback_asked = false;
        private uint waiting = 0;

        public Marco.Icon? mini_icon { get; private set; default = null; }
        public Marco.Icon? icon { get; private set; default = null; }
        public uint serial { get; private set; default = 0; }

        public signal void changed ();

        public WindowIcon (Decorations decorations) {
            this.decorations = decorations;
            decorations.icons_changed.connect (reload);
        }

        public override void dispose () {
            decorations.icons_changed.disconnect (reload);
            forget ();
            base.dispose ();
        }

        public void clear_sources () {
            steps = new GLib.GenericArray<Step> ();
        }

        public void add_theme (string? name, string? desktop_id) {
            if (name == null && desktop_id == null) {
                return;
            }
            var step = new Step ();
            step.name = name;
            step.desktop_id = desktop_id;
            steps.add ((owned) step);
        }

        public void add_local (owned LocalIcon local) {
            var step = new Step ();
            step.local = (owned) local;
            steps.add ((owned) step);
        }

        public void reload () {
            forget ();
            next_step = 0;
            fallback_asked = false;
            run ();
        }

        private void run () {
            int scale = decorations.image_scale;
            while (next_step < steps.length) {
                unowned Step step = steps[next_step++];
                if (step.local != null) {
                    var mini = step.local (MINI_SIZE * scale);
                    var full = step.local (decorations.icon_size * scale);
                    if (mini != null && full != null) {
                        set_icons ((owned) mini, (owned) full);
                        return;
                    }
                    continue;
                }
                fallback_asked = next_step == steps.length;
                waiting = decorations.request_icon (this, step.name, step.desktop_id, fallback_asked);
                if (waiting != 0) {
                    return;
                }
            }
            if (!fallback_asked) {
                fallback_asked = true;
                waiting = decorations.request_icon (this, null, null, true);
            }
        }

        internal void answer (uint serial, owned Marco.Icon? mini, owned Marco.Icon? full) {
            if (serial != waiting) {
                return;
            }
            waiting = 0;
            if (mini != null && full != null) {
                set_icons ((owned) mini, (owned) full);
            } else {
                run ();
            }
        }

        private void set_icons (owned Marco.Icon mini, owned Marco.Icon full) {
            mini_icon = (owned) mini;
            icon = (owned) full;
            serial++;
            changed ();
        }

        private void forget () {
            if (waiting != 0) {
                decorations.forget_icon (waiting);
                waiting = 0;
            }
        }
    }
}
