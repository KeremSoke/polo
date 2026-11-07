/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

namespace Polo.Effects {
    public abstract class Target : GLib.Object {
        public unowned Window window { get; construct; }
        public Polo.Fx.Grid? grid = null;

        public signal void gone ();

        public bool ensure_grid (uint cols, uint rows) {
            if (grid != null && grid.cols == cols && grid.rows == rows) {
                return true;
            }
            grid = Polo.Fx.Grid.create (cols, rows);
            return grid != null;
        }

        public abstract bool deform ();

        public abstract void undeform ();
    }

    /* One effect on one window. */
    public interface Effect : GLib.Object {
        public abstract int priority { get; }

        public abstract unowned string settings_key { get; }

        public abstract bool deforms { get; }

        public abstract void start (Target target);

        public abstract void update (double dt);

        public abstract bool apply (Target target);
        public abstract bool is_done ();
        public abstract void cancel ();
        public abstract void reverse ();
    }

    public class Engine : GLib.Object {
        private class Entry {
            public Target target;
            public Effect effect;
            public ulong gone_handler;
        }

        private GLib.HashTable<unowned Window, Entry> running =
            new GLib.HashTable<unowned Window, Entry> (GLib.direct_hash, GLib.direct_equal);
        private GLib.Settings? interface_settings;
        private GLib.Settings? effects_settings;
        private bool reduced_resources = false;
        private int64 last_usec = -1;

        public GLib.Settings? settings { get { return effects_settings; } }

        public bool deformation_supported { get; set; default = false; }
        public bool animating { get; private set; default = false; }

        public signal void finished (Window window, Effect effect);

        public Engine (GLib.Settings? interface_settings, GLib.Settings? effects_settings) {
            this.interface_settings = interface_settings;
            this.effects_settings = effects_settings;
            if (interface_settings != null) {
                interface_settings.changed["enable-animations"].connect (recheck);
            }
            if (effects_settings != null) {
                effects_settings.changed.connect (recheck);
            }
            notify["deformation-supported"].connect (recheck);
        }

        public void shutdown () {
            if (interface_settings != null) {
                interface_settings.changed["enable-animations"].disconnect (recheck);
            }
            if (effects_settings != null) {
                effects_settings.changed.disconnect (recheck);
            }
            stop_all ();
        }

        public void set_reduced_resources (bool reduced) {
            if (reduced_resources != reduced) {
                reduced_resources = reduced;
                recheck ();
            }
        }

        public bool allowed (Effect effect) {
            return permits (effect.settings_key, effect.deforms);
        }

        public bool permits (string settings_key, bool deforms) {
            if (reduced_resources || (deforms && !deformation_supported)) {
                return false;
            }
            if (interface_settings != null && !interface_settings.get_boolean ("enable-animations")) {
                return false;
            }
            if (effects_settings == null) {
                return false;
            }
            var schema = effects_settings.settings_schema;
            return schema.has_key (settings_key) && effects_settings.get_boolean (settings_key);
        }

        public bool run (Target target, Effect effect, bool apply_now = false) {
            if (!allowed (effect)) {
                return false;
            }
            unowned Entry? current = running[target.window];
            if (current != null) {
                if (current.effect.priority > effect.priority) {
                    return false;
                }
                stop (target.window);
            }
            var entry = new Entry ();
            entry.target = target;
            entry.effect = effect;
            entry.gone_handler = target.gone.connect (on_target_gone);
            effect.start (target);
            running[target.window] = entry;
            if (apply_now) {
                apply (target, effect);
            }
            update_animating ();
            return true;
        }

        public unowned Effect? effect_of (Window window) {
            unowned Entry? entry = running[window];
            return entry != null ? entry.effect : null;
        }

        public void reverse (Window window) {
            unowned Entry? entry = running[window];
            if (entry != null) {
                entry.effect.reverse ();
            }
        }

        public void stop (Window window) {
            Entry? entry;
            if (!running.steal_extended (window, null, out entry)) {
                return;
            }
            entry.effect.cancel ();
            finish (entry);
            update_animating ();
        }

        public void stop_all () {
            var iter = GLib.HashTableIter<unowned Window, Entry> (running);
            unowned Entry entry;
            while (iter.next (null, out entry)) {
                entry.effect.cancel ();
                finish (entry);
                iter.remove ();
            }
            update_animating ();
        }

        public void tick (int64 usec) {
            if (running.size () == 0 || usec <= last_usec) {
                return;
            }
            double dt = last_usec < 0 ? 0.0 : (usec - last_usec) / 1000000.0;
            last_usec = usec;
            var iter = GLib.HashTableIter<unowned Window, Entry> (running);
            unowned Entry entry;
            while (iter.next (null, out entry)) {
                entry.effect.update (dt);
                if (entry.effect.is_done ()) {
                    finish (entry);
                    iter.remove ();
                    continue;
                }
                apply (entry.target, entry.effect);
            }
            update_animating ();
        }

        private static void apply (Target target, Effect effect) {
            if (!effect.deforms) {
                effect.apply (target);
            } else if (!effect.apply (target) || !target.deform ()) {
                target.undeform ();
            }
        }

        private void finish (Entry entry) {
            entry.target.disconnect (entry.gone_handler);
            entry.target.undeform ();
            finished (entry.target.window, entry.effect);
        }

        private void on_target_gone (Target target) {
            Entry? entry;
            if (running.steal_extended (target.window, null, out entry)) {
                entry.effect.cancel ();
                entry.target.disconnect (entry.gone_handler);
                update_animating ();
                finished (entry.target.window, entry.effect);
            }
        }

        private void recheck () {
            var iter = GLib.HashTableIter<unowned Window, Entry> (running);
            unowned Entry entry;
            while (iter.next (null, out entry)) {
                if (!allowed (entry.effect)) {
                    entry.effect.cancel ();
                    finish (entry);
                    iter.remove ();
                }
            }
            update_animating ();
        }

        private void update_animating () {
            bool now = running.size () > 0;
            if (!now) {
                last_usec = -1;
            }
            if (animating != now) {
                animating = now;
            }
        }
    }
}
