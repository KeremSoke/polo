/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * meta_screen_calc_workspace_layout () and meta_workspace_get_neighbor ().
 */

using Polo;

Policy.WorkspaceLayout row (int n, int current) {
    return new Policy.WorkspaceLayout (n, current, 1, -1, false, Policy.Corner.TOPLEFT);
}

void test_grid () {
    var row = new Policy.WorkspaceLayout (4, 2, 1, -1, false, Policy.Corner.TOPLEFT);
    assert (row.rows == 1 && row.cols == 4);
    assert (row.current_row == 0 && row.current_col == 2);

    var ragged = new Policy.WorkspaceLayout (7, 6, 3, -1, false, Policy.Corner.TOPLEFT);
    assert (ragged.rows == 3 && ragged.cols == 3);
    assert (ragged.grid[7] == -1 && ragged.grid[8] == -1);
    assert (ragged.current_row == 2 && ragged.current_col == 0);

    var vertical = new Policy.WorkspaceLayout (4, 1, 2, -1, true, Policy.Corner.TOPLEFT);
    assert (vertical.grid[0] == 0 && vertical.grid[2] == 1 && vertical.grid[1] == 2);

    var corner = new Policy.WorkspaceLayout (4, 0, 2, -1, false, Policy.Corner.BOTTOMRIGHT);
    assert (corner.grid[3] == 0 && corner.grid[0] == 3);
}

void test_neighbor () {
    assert (row (4, 0).neighbor (4, 0, Policy.Motion.LEFT, WrapStyle.NO_WRAP) == 0);
    assert (row (4, 0).neighbor (4, 0, Policy.Motion.RIGHT, WrapStyle.NO_WRAP) == 1);
    assert (row (4, 0).neighbor (4, 0, Policy.Motion.LEFT, WrapStyle.TOROIDAL) == 3);
    assert (row (4, 3).neighbor (4, 3, Policy.Motion.RIGHT, WrapStyle.CLASSIC) == 0);
    assert (row (4, 1).neighbor (4, 1, Policy.Motion.UP, WrapStyle.NO_WRAP) == 1);
    assert (row (4, 1).neighbor (4, 1, Policy.Motion.LEFT, WrapStyle.NO_WRAP, false) == 2);
}

void test_pager_layout () {
    var two_rows = new Policy.WorkspaceLayout (6, 4, 2, -1, false, Policy.Corner.TOPLEFT);
    assert (two_rows.neighbor (6, 4, Policy.Motion.UP, WrapStyle.NO_WRAP) == 1);
    var wrapped = new Policy.WorkspaceLayout (7, 6, 3, -1, false, Policy.Corner.TOPLEFT);
    assert (wrapped.neighbor (7, 6, Policy.Motion.LEFT, WrapStyle.TOROIDAL) == 6);
}

int main (string[] args) {
    Test.init (ref args);
    Test.add_func ("/workspace-layout/grid", test_grid);
    Test.add_func ("/workspace-layout/neighbor", test_neighbor);
    Test.add_func ("/workspace-layout/pager", test_pager_layout);
    return Test.run ();
}
