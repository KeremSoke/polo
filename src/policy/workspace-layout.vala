/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Marco screen.c meta_screen_calc_workspace_layout () and workspace.c
 * meta_workspace_get_neighbor ().
 */

namespace Polo.Policy {
    public enum Corner {
        TOPLEFT,
        TOPRIGHT,
        BOTTOMLEFT,
        BOTTOMRIGHT
    }

    public enum Motion {
        UP,
        DOWN,
        LEFT,
        RIGHT,
        PREV
    }

    public class WorkspaceLayout {
        public int rows;
        public int cols;
        public int[] grid;
        public int current_row;
        public int current_col;

        public WorkspaceLayout (int num_workspaces, int current_space, int rows_of_workspaces,
                                int columns_of_workspaces, bool vertical, Corner corner) {
            rows = rows_of_workspaces;
            cols = columns_of_workspaces;
            if (rows <= 0 && cols <= 0) {
                cols = num_workspaces;
            }
            if (rows <= 0) {
                rows = num_workspaces / cols + ((num_workspaces % cols) > 0 ? 1 : 0);
            }
            if (cols <= 0) {
                cols = num_workspaces / rows + ((num_workspaces % rows) > 0 ? 1 : 0);
            }
            if (rows < 1) {
                rows = 1;
            }
            if (cols < 1) {
                cols = 1;
            }

            grid = new int[rows * cols];
            bool from_right = corner == Corner.TOPRIGHT || corner == Corner.BOTTOMRIGHT;
            bool from_bottom = corner == Corner.BOTTOMLEFT || corner == Corner.BOTTOMRIGHT;
            int i = 0;
            if (vertical) {
                for (int k = 0; k < cols; k++) {
                    int c = from_right ? cols - 1 - k : k;
                    for (int l = 0; l < rows; l++) {
                        int r = from_bottom ? rows - 1 - l : l;
                        grid[r * cols + c] = i++;
                    }
                }
            } else {
                for (int l = 0; l < rows; l++) {
                    int r = from_bottom ? rows - 1 - l : l;
                    for (int k = 0; k < cols; k++) {
                        int c = from_right ? cols - 1 - k : k;
                        grid[r * cols + c] = i++;
                    }
                }
            }

            current_row = 0;
            current_col = 0;
            for (int r = 0; r < rows; r++) {
                for (int c = 0; c < cols; c++) {
                    if (grid[r * cols + c] == current_space) {
                        current_row = r;
                        current_col = c;
                    } else if (grid[r * cols + c] >= num_workspaces) {
                        grid[r * cols + c] = -1;
                    }
                }
            }
        }

        [CCode (cname = "g_dgettext", cheader_filename = "glib.h")]
        private static extern unowned string dgettext (string domain, string msgid);

        /* meta_ui_get_direction (): GTK's own test, the gtk30 translation of "default:LTR". */
        public static bool ltr () {
            return dgettext ("gtk30", "default:LTR") != "default:RTL";
        }

        /* Move from this layout's current cell. */
        public int neighbor (int num_workspaces, int current_space, Motion direction, WrapStyle wrap,
                             bool ltr = true) {
            unowned WorkspaceLayout layout = this;
            switch (direction) {
                case Motion.LEFT:
                    layout.current_col -= ltr ? 1 : -1;
                    break;
                case Motion.RIGHT:
                    layout.current_col += ltr ? 1 : -1;
                    break;
                case Motion.UP:
                    layout.current_row -= 1;
                    break;
                case Motion.DOWN:
                    layout.current_row += 1;
                    break;
                default:
                    break;
            }

            if (layout.current_col < 0) {
                if (wrap == WrapStyle.NO_WRAP) {
                    layout.current_col = 0;
                } else {
                    if (wrap == WrapStyle.CLASSIC) {
                        layout.current_row = layout.current_row > 0 ? layout.current_row - 1 : layout.rows - 1;
                    }
                    layout.current_col = layout.cols - 1;
                }
            }
            if (layout.current_col >= layout.cols) {
                if (wrap == WrapStyle.NO_WRAP) {
                    layout.current_col = layout.cols - 1;
                } else {
                    if (wrap == WrapStyle.CLASSIC) {
                        layout.current_row = layout.current_row < layout.rows - 1 ? layout.current_row + 1 : 0;
                    }
                    layout.current_col = 0;
                }
            }
            if (layout.current_row < 0) {
                if (wrap == WrapStyle.NO_WRAP) {
                    layout.current_row = 0;
                } else {
                    if (wrap == WrapStyle.CLASSIC) {
                        layout.current_col = layout.current_col > 0 ? layout.current_col - 1 : layout.cols - 1;
                    }
                    layout.current_row = layout.rows - 1;
                }
            }
            if (layout.current_row >= layout.rows) {
                if (wrap == WrapStyle.NO_WRAP) {
                    layout.current_row = layout.rows - 1;
                } else {
                    if (wrap == WrapStyle.CLASSIC) {
                        layout.current_col = layout.current_col < layout.cols - 1 ? layout.current_col + 1 : 0;
                    }
                    layout.current_row = 0;
                }
            }

            if (wrap != WrapStyle.NO_WRAP &&
                layout.current_row * layout.cols + layout.current_col >= num_workspaces) {
                switch (direction) {
                    case Motion.LEFT:
                        layout.current_col = num_workspaces - (layout.current_row * layout.cols + 1);
                        break;
                    case Motion.RIGHT:
                        layout.current_col = 0;
                        if (wrap == WrapStyle.CLASSIC) {
                            layout.current_row = 0;
                        }
                        break;
                    case Motion.UP:
                        layout.current_row -= 1;
                        break;
                    case Motion.DOWN:
                        layout.current_row = 0;
                        if (wrap == WrapStyle.CLASSIC) {
                            layout.current_col = layout.current_col < layout.cols - 1 ? layout.current_col + 1 : 0;
                        }
                        break;
                    default:
                        break;
                }
            }

            int cell = layout.current_row * layout.cols + layout.current_col;
            int i = cell >= 0 && cell < grid.length ? grid[cell] : -1;
            return i < 0 ? current_space : i;
        }
    }
}
