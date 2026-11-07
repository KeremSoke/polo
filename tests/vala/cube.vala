/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

using Polo;
using Polo.Policy;

const double DURATION = 350.0;
const double SPEED = 0.02;

bool near (double a, double b, double epsilon = 1e-6) {
    return (a - b).abs () < epsilon;
}

void assert_at_rest (CubeAnimation cube, double rotation) {
    assert (near (cube.rotation.value, rotation));
    assert (near (cube.zoom.value, 1.0));
    assert (near (cube.offset_y.value, 0.0));
    assert (near (cube.ease.value, 0.0));
    assert (near (cube.offset_z.value, CubeAnimation.rest_offset_z (cube.sides)));
}

void test_rest_offset () {
    for (int sides = 2; sides <= 8; sides++) {
        assert (near (CubeAnimation.rest_offset_z (sides), Fx.Cube.rest_offset_z (sides)));
    }
}

void test_easing () {
    var cube = new CubeAnimation (DURATION);
    assert (!cube.running ());
    cube.move (4, 1);
    assert (cube.running () && near (cube.progress (), 0.0));
    cube.tick (DURATION / 2);
    assert (near (cube.progress (), Math.sqrt (0.75)));
    assert (near (cube.rotation.value, -Math.sqrt (0.75) * Math.PI / 2));
    cube.tick (DURATION);
    assert (!cube.running () && near (cube.progress (), 1.0));
}

void test_moves_add_up () {
    var cube = new CubeAnimation (DURATION);
    assert (cube.move (4, 1));
    cube.tick (100);
    assert (!cube.done ());
    assert (cube.move (4, 1));
    assert (near (cube.rotation.end, -Math.PI));
    cube.tick (100 + DURATION);
    assert (cube.done ());
    assert_at_rest (cube, -Math.PI);
    assert (cube.finish () == 2);
    assert (!cube.active && near (cube.rotation.value, 0.0));
}

void test_wraps () {
    var cube = new CubeAnimation (DURATION);
    cube.move (4, -1);
    cube.tick (DURATION);
    assert (cube.finish () == 3);
    cube.move (3, 1);
    cube.tick (2 * DURATION);
    cube.move (3, 1);
    cube.tick (3 * DURATION);
    cube.move (3, 1);
    cube.tick (4 * DURATION);
    assert (cube.finish () == 0);
}

void test_grab_and_release () {
    var cube = new CubeAnimation (DURATION);
    assert (cube.grab (4, 0.1));
    assert (!cube.in_exit);
    cube.tick (DURATION);
    assert (near (cube.offset_z.value, 0.1 + CubeAnimation.rest_offset_z (4)));
    assert (near (cube.ease.value, 1.0));
    assert (!cube.done ());

    cube.rotate (0.6 * (Math.PI / 2) / SPEED, 0, SPEED, SPEED);
    cube.tick (2 * DURATION);
    assert (near (cube.rotation.value, 0.6 * Math.PI / 2));
    assert (cube.turned () == -1);

    cube.release ();
    assert (cube.in_exit);
    cube.rotate (1000, 0, SPEED, SPEED);
    cube.scroll (1, 0.07);
    assert (near (cube.rotation.end, Math.PI / 2));
    cube.tick (3 * DURATION);
    assert (cube.done ());
    assert_at_rest (cube, Math.PI / 2);
    assert (cube.finish () == 3);

    var fresh = new CubeAnimation (DURATION);
    assert (fresh.grab (4, 0.1));
    fresh.tick (DURATION / 3);
    cube.tick (DURATION * 10);
    assert (cube.grab (4, 0.1));
    cube.tick (DURATION * 10 + DURATION / 3);
    assert (near (cube.offset_z.value, fresh.offset_z.value));
    assert (near (cube.rotation.value, fresh.rotation.value));
}

void test_release_snaps_back () {
    var cube = new CubeAnimation (DURATION);
    cube.grab (4, 0.1);
    cube.rotate (-0.4 * (Math.PI / 2) / SPEED, 0, SPEED, SPEED);
    cube.tick (DURATION);
    cube.release ();
    cube.tick (2 * DURATION);
    assert_at_rest (cube, 0.0);
    assert (cube.finish () == 0);
}

void test_interactive_keys_and_cancel () {
    var cube = new CubeAnimation (DURATION);
    assert (cube.grab (4, 0.1));
    cube.tick (DURATION);
    cube.turn_while_open (1);
    cube.tick (2 * DURATION);
    assert (!cube.in_exit && !cube.done ());
    assert (near (cube.rotation.value, -Math.PI / 2));
    cube.turn_while_open (1);
    cube.tick (3 * DURATION);
    assert (near (cube.rotation.value, -Math.PI));
    cube.release ();
    cube.tick (4 * DURATION);
    assert (cube.finish () == 2);

    assert (cube.grab (4, 0.1));
    cube.tick (5 * DURATION);
    cube.turn_while_open (1);
    cube.tick (6 * DURATION);
    cube.release ();
    cube.cancel ();
    cube.tick (7 * DURATION);
    assert (cube.done () && cube.finish () == 0);

    assert (cube.grab (4, 0.1));
    cube.tick (8 * DURATION);
    cube.rotate (-100, -30, SPEED, SPEED);
    cube.tick (9 * DURATION);
    cube.scroll (1, 0.07);
    cube.tick (10 * DURATION);
    cube.cancel ();
    cube.tick (11 * DURATION);
    assert (cube.done ());
    assert_at_rest (cube, 0);
    assert (cube.finish () == 0);
}

void test_clamps () {
    var cube = new CubeAnimation (DURATION);
    cube.grab (4, 0.1);
    cube.rotate (0, -1000, SPEED, SPEED);
    cube.tick (DURATION);
    assert (near (cube.offset_y.value, CubeAnimation.OFFSET_Y_MAX));
    cube.rotate (0, 1000, SPEED, SPEED);
    cube.tick (2 * DURATION);
    assert (near (cube.offset_y.value, -CubeAnimation.OFFSET_Y_MAX));
    cube.scroll (1000, 0.07);
    cube.tick (3 * DURATION);
    assert (near (cube.zoom.value, CubeAnimation.ZOOM_MAX));
    cube.scroll (-1000, 0.07);
    cube.tick (4 * DURATION);
    assert (near (cube.zoom.value, CubeAnimation.ZOOM_MIN));
    cube.scroll (1, 0.07);
    cube.tick (5 * DURATION);
    assert (near (cube.zoom.value, 0.1 + Math.pow (0.1, 1.5) * 0.07));
}

void test_shortest_turn () {
    for (int sides = 3; sides <= 4; sides++) {
        for (int from = 0; from < sides; from++) {
            for (int to = 0; to < sides; to++) {
                int turn = CubeAnimation.shortest_turn (from, to, sides);
                assert (((from + turn) % sides + sides) % sides == to);
                assert (turn.abs () * 2 <= sides);
            }
        }
    }
    assert (CubeAnimation.shortest_turn (0, 2, 4) == 2);
    assert (CubeAnimation.shortest_turn (0, 3, 4) == -1);
    assert (CubeAnimation.shortest_turn (2, 0, 3) == 1);
    assert (CubeAnimation.shortest_turn (0, 0, 1) == 0);
}

void test_one_side () {
    var cube = new CubeAnimation (DURATION);
    assert (!cube.move (1, 1));
    assert (!cube.grab (1, 0.1));
    assert (!cube.active && !cube.running ());
    cube.release ();
    cube.rotate (10, 10, SPEED, SPEED);
    assert (near (cube.rotation.value, 0.0));
    assert (cube.finish () == 0);
}

int main (string[] args) {
    Test.init (ref args);
    Test.add_func ("/cube/rest-offset", test_rest_offset);
    Test.add_func ("/cube/easing", test_easing);
    Test.add_func ("/cube/moves-add-up", test_moves_add_up);
    Test.add_func ("/cube/wraps", test_wraps);
    Test.add_func ("/cube/grab-and-release", test_grab_and_release);
    Test.add_func ("/cube/release-snaps-back", test_release_snaps_back);
    Test.add_func ("/cube/interactive-keys-and-cancel", test_interactive_keys_and_cancel);
    Test.add_func ("/cube/clamps", test_clamps);
    Test.add_func ("/cube/shortest-turn", test_shortest_turn);
    Test.add_func ("/cube/one-side", test_one_side);
    return Test.run ();
}
