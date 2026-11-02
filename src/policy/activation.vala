/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * xdg_activation_v1 tokens: the Wayland stand-in for startup notification
 * and _NET_WM_USER_TIME. A token is worth something when it was minted for
 * a button or key press the client really received, or by Polo itself; it
 * then carries that press's time into Marco's focus rules. Tokens expire
 * after ten seconds and work once.
 */

namespace Polo.Policy {
    public class ActivationTokens : GLib.Object {
        public const int64 LIFETIME_MSEC = 10000;

        private class Record {
            public int64 issued;
            public int64 user_time;
            public bool valid;
        }

        private GLib.HashTable<string, Record> records =
            new GLib.HashTable<string, Record> (GLib.str_hash, GLib.str_equal);

        private static string new_name () {
            return GLib.Uuid.string_random ().replace ("-", "");
        }

        private void expire (int64 now) {
            var stale = new GLib.GenericArray<string> ();
            foreach (unowned string name in records.get_keys ()) {
                if (now - records[name].issued > LIFETIME_MSEC) {
                    stale.add (name);
                }
            }
            foreach (unowned string name in stale) {
                records.remove (name);
            }
        }

        /*
         * A client's token. input_valid: the serial named a press the client
         * received, at input_time.
         */
        public string mint (bool input_valid, int64 input_time, int64 now) {
            expire (now);
            var name = new_name ();
            records.insert (name, new Record () {
                issued = now, user_time = input_valid ? input_time : 0, valid = input_valid
            });
            return name;
        }

        /* For a child Polo starts (XDG_ACTIVATION_TOKEN, DESKTOP_STARTUP_ID). */
        public string mint_for_launch (int64 now) {
            expire (now);
            var name = new_name ();
            records.insert (name, new Record () { issued = now, user_time = now, valid = true });
            return name;
        }

        /*
         * Use a token. Returns false for unknown, expired, already used or
         * worthless tokens; user_time is the time of the press behind it.
         */
        public bool consume (string name, int64 now, out int64 user_time) {
            user_time = 0;
            expire (now);
            var record = records.lookup (name);
            if (record == null) {
                return false;
            }
            records.remove (name);
            user_time = record.user_time;
            return record.valid;
        }

        public uint size { get { return records.size (); } }
    }
}
