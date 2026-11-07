/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The key binding table, rebuilt from org.mate.Marco.global-keybindings and
 * window-keybindings whenever they change. A press matches the keysym at
 * the active level, then the base-level keysym (Marco grabs keycodes, so
 * its bindings work in any layout and with Shift held), then the keycode.
 * Only bindings with an action consume their key. Custom shortcuts come
 * after Marco's, which win a clash.
 */

namespace Polo.Input {
    public enum BindingKind {
        GLOBAL,
        WINDOW
    }

    public class Binding {
        public string name;
        public BindingKind kind;
        public Accelerator accelerator;
    }

    public delegate void BindingAction (Binding binding, Backend.Seat seat, Backend.KeyEvent event);

    public class Bindings : GLib.Object {
        private const uint64 KEYCODE_KEY = 1ULL << 56;

        private GLib.HashTable<void*, Binding> table =
            new GLib.HashTable<void*, Binding> (GLib.direct_hash, GLib.direct_equal);

        private class Action {
            public BindingAction run;

            public Action (owned BindingAction run) {
                this.run = (owned) run;
            }
        }

        private GLib.HashTable<string, Action> actions =
            new GLib.HashTable<string, Action> (GLib.str_hash, GLib.str_equal);

        private MarcoPrefs? prefs = null;
        private GLib.HashTable<string, GLib.GenericArray<Binding>> custom =
            new GLib.HashTable<string, GLib.GenericArray<Binding>> (GLib.str_hash, GLib.str_equal);

        private static void* key (uint64 value) {
            return (void*) (ulong) value;
        }

        private static void* keysym_key (uint32 keysym, Marco.VirtualModifier mask) {
            return key ((uint64) keysym << 16 | (uint64) mask);
        }

        private static void* keycode_key (uint32 keycode, Marco.VirtualModifier mask) {
            return key (KEYCODE_KEY | (uint64) keycode << 16 | (uint64) mask);
        }

        public void register (string name, owned BindingAction action) {
            actions.insert (name, new Action ((owned) action));
        }

        public void unregister (string name) {
            actions.remove (name);
        }

        public void set_custom (string group, GLib.GenericArray<Binding> bindings) {
            custom[group] = bindings;
            if (prefs != null) {
                rebuild (prefs);
            }
        }

        public uint size { get { return table.size (); } }

        public unowned Binding? lookup (string name) {
            foreach (unowned Binding binding in table.get_values ()) {
                if (binding.name == name) {
                    return binding;
                }
            }
            return null;
        }

        /* BINDING_REVERSES: also bound with Shift, which reverses them. */
        public static bool reverses (string name) {
            return name.has_prefix ("switch-group") || name.has_prefix ("switch-windows") ||
                   name.has_prefix ("switch-panels") || name.has_prefix ("cycle-group") ||
                   name.has_prefix ("cycle-windows") || name.has_prefix ("cycle-panels");
        }

        private static void insert (GLib.HashTable<void*, Binding> table, Binding binding,
                                    Marco.VirtualModifier mask, string text) {
            var accelerator = binding.accelerator;
            var k = accelerator.keysym != 0 ? keysym_key (accelerator.keysym, mask)
                                            : keycode_key (accelerator.keycode, mask);
            var existing = table.lookup (k);
            if (existing != null) {
                Logging.scope (LogDomain.INPUT).debug ("%s and %s share %s", existing.name, binding.name, text);
                return;
            }
            table.insert (k, binding);
        }

        /* Build the table for prefs and swap it in at once. */
        public void rebuild (MarcoPrefs prefs) {
            this.prefs = prefs;
            var fresh = new GLib.HashTable<void*, Binding> (GLib.direct_hash, GLib.direct_equal);
            unowned LogScope log = Logging.scope (LogDomain.INPUT);
            foreach (unowned string path in MarcoPrefs.KEYS) {
                BindingKind kind;
                if (path.has_prefix ("global-keybindings/")) {
                    kind = BindingKind.GLOBAL;
                } else if (path.has_prefix ("window-keybindings/")) {
                    kind = BindingKind.WINDOW;
                } else {
                    continue;
                }
                unowned string text = prefs.get_value (path).get_string ();
                Accelerator accelerator;
                if (!Accelerators.parse (text, out accelerator)) {
                    log.warning ("\"%s\" found in configuration database is not a valid value for keybinding \"%s\"",
                                 text, path);
                    continue;
                }
                if (accelerator.is_disabled ()) {
                    continue;
                }
                var name = path.substring (path.index_of_char ('/') + 1);
                bool add_shift = reverses (name);
                if (add_shift && accelerator.keysym != 0 &&
                    (accelerator.mask == Marco.VirtualModifier.SHIFT_MASK || accelerator.mask == 0)) {
                    log.warning ("Cannot bind \"%s\" to %s: it needs a modifier such as Ctrl or Alt.",
                                 name, text);
                    continue;
                }
                var binding = new Binding () {
                    name = name,
                    kind = kind,
                    accelerator = accelerator
                };
                insert (fresh, binding, accelerator.mask, text);
                if (add_shift && (accelerator.mask & Marco.VirtualModifier.SHIFT_MASK) == 0) {
                    insert (fresh, binding, accelerator.mask | Marco.VirtualModifier.SHIFT_MASK, text);
                }
            }
            foreach (var group in custom.get_values ()) {
                foreach (var binding in group) {
                    insert (fresh, binding, binding.accelerator.mask, binding.name);
                }
            }
            table = fresh;
            log.debug ("%u key bindings", table.size ());
        }

        /* The binding a key event triggers, if any. */
        public unowned Binding? match (Backend.KeyEvent event) {
            return match_mask (event, Accelerators.from_real (event.modifiers));
        }

        /* display_get_keybinding_action () with a given mask, as grabs look keys up. */
        public unowned Binding? match_mask (Backend.KeyEvent event, Marco.VirtualModifier mask) {
            unowned Binding? binding = table.lookup (
                keysym_key (Backend.Keysym.to_lower (event.keysym), mask));
            if (binding == null && event.base_keysym != event.keysym) {
                binding = table.lookup (keysym_key (Backend.Keysym.to_lower (event.base_keysym), mask));
            }
            if (binding == null) {
                binding = table.lookup (keycode_key (event.keycode + 8, mask));
            }
            return binding;
        }

        /* process_event () with on_window false: what a grab lets through. */
        public void run_global (Backend.Seat seat, Backend.KeyEvent event) {
            unowned Binding? binding = match (event);
            if (binding == null || binding.kind != BindingKind.GLOBAL) {
                return;
            }
            unowned Action? action = actions.lookup (binding.name);
            if (action != null) {
                action.run (binding, seat, event);
            }
        }

        /* The seat's key filter. */
        public Backend.FilterResult filter (Backend.Seat seat, Backend.KeyEvent event) {
            if (!event.pressed) {
                return Backend.FilterResult.PASS;
            }
            unowned Binding? binding = match (event);
            if (binding == null) {
                return Backend.FilterResult.PASS;
            }
            unowned Action? action = actions.lookup (binding.name);
            if (action == null) {
                return Backend.FilterResult.PASS;
            }
            action.run (binding, seat, event);
            return Backend.FilterResult.CONSUME;
        }
    }
}
