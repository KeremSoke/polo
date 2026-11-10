/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * At most a burst of requests in a window of time, so a client that asks again
 * and again cannot keep a dialog on the screen.
 */

namespace Polo.Policy {
    public class RateLimit : GLib.Object {
        private int burst;
        private int64 window_usec;
        private int64 start = 0;
        private int count = 0;

        public RateLimit (int burst, int64 window_usec) {
            this.burst = burst;
            this.window_usec = window_usec;
        }

        public bool allow () {
            return allow_at (GLib.get_monotonic_time ());
        }

        public bool allow_at (int64 now) {
            if (count == 0 || now < start || now - start >= window_usec) {
                start = now;
                count = 0;
            }
            if (count >= burst) {
                return false;
            }
            count++;
            return true;
        }
    }
}
