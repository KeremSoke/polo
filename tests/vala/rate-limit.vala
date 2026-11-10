/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A burst of requests passes, the rest of the window does not, and the next
 * window starts clean.
 */

using Polo.Policy;

const int64 SECOND = 1000000;

void test_burst_then_refusal () {
    var limit = new RateLimit (5, 10 * SECOND);
    for (int i = 0; i < 5; i++) {
        assert (limit.allow_at (100 * SECOND + i));
    }
    assert (!limit.allow_at (101 * SECOND));
    assert (!limit.allow_at (109 * SECOND));
}

void test_next_window () {
    var limit = new RateLimit (2, 10 * SECOND);
    assert (limit.allow_at (50 * SECOND));
    assert (limit.allow_at (51 * SECOND));
    assert (!limit.allow_at (52 * SECOND));
    assert (limit.allow_at (60 * SECOND));
    assert (limit.allow_at (61 * SECOND));
    assert (!limit.allow_at (62 * SECOND));
}

void test_refusals_do_not_extend_the_window () {
    var limit = new RateLimit (1, 10 * SECOND);
    assert (limit.allow_at (20 * SECOND));
    for (int i = 1; i < 10; i++) {
        assert (!limit.allow_at (20 * SECOND + i * SECOND));
    }
    assert (limit.allow_at (30 * SECOND));
}

void test_a_slow_user_never_waits () {
    var limit = new RateLimit (5, 10 * SECOND);
    for (int i = 0; i < 50; i++) {
        assert (limit.allow_at ((int64) i * 11 * SECOND));
    }
}

void test_clock_going_back () {
    var limit = new RateLimit (1, 10 * SECOND);
    assert (limit.allow_at (500 * SECOND));
    assert (limit.allow_at (10 * SECOND));
}

int main (string[] args) {
    GLib.Test.init (ref args);
    GLib.Test.add_func ("/rate-limit/burst", test_burst_then_refusal);
    GLib.Test.add_func ("/rate-limit/next-window", test_next_window);
    GLib.Test.add_func ("/rate-limit/refusals", test_refusals_do_not_extend_the_window);
    GLib.Test.add_func ("/rate-limit/slow-user", test_a_slow_user_never_waits);
    GLib.Test.add_func ("/rate-limit/clock-back", test_clock_going_back);
    return GLib.Test.run ();
}
