/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Marco's reading of ICCCM values. window-props.c set_title_text (),
 * group.c meta_window_compute_group ().
 */

namespace Polo.Policy {
    namespace X11Props {
        public const int MAX_TITLE_LENGTH = 512;
        private const uint32 MWM_HINTS_FUNCTIONS = 1 << 0;
        private const uint32 MWM_HINTS_DECORATIONS = 1 << 1;
        private const uint32 MWM_FUNC_ALL = 1 << 0;
        private const uint32 MWM_FUNC_RESIZE = 1 << 1;
        private const uint32 MWM_FUNC_MOVE = 1 << 2;
        private const uint32 MWM_FUNC_MINIMIZE = 1 << 3;
        private const uint32 MWM_FUNC_MAXIMIZE = 1 << 4;
        private const uint32 MWM_FUNC_CLOSE = 1 << 5;
        private const uint32 MWM_DECOR_BORDER = 1 << 1;

        /*
         * The title to show. owner: null for the user's own window or an
         * unknown one, "" for root, otherwise the owner's name when known,
         * "?" when another user's name is not.
         */
        public string format_title (string? title, string? machine, string hostname, string? owner,
                                    out bool modified) {
            modified = false;
            if (title == null || title == "") {
                return "";
            }
            /* g_utf8_strlen () with a byte limit, as Marco calls it: only a title of
             * single-byte characters can pass. */
            if (title.char_count (MAX_TITLE_LENGTH + 1) > MAX_TITLE_LENGTH) {
                modified = true;
                return title.substring (0, MAX_TITLE_LENGTH);
            }
            if (machine != null && machine != hostname) {
                modified = true;
                return "%s (on %s)".printf (title, machine);
            }
            if (owner != null) {
                modified = true;
                if (owner == "") {
                    return "%s (as superuser)".printf (title);
                }
                if (owner == "?") {
                    return "%s (as another user)".printf (title);
                }
                return "%s (as %s)".printf (title, owner);
            }
            return title;
        }

        /* With MWM_FUNC_ALL, named bits disable functions; otherwise they enable them. */
        public WindowFeatures motif_functions (uint32 flags, uint32 functions) {
            if ((flags & MWM_HINTS_FUNCTIONS) == 0) {
                return WindowFeatures.MWM_FUNCTIONS;
            }
            WindowFeatures named = 0;
            if ((functions & MWM_FUNC_CLOSE) != 0) {
                named |= WindowFeatures.CLOSE;
            }
            if ((functions & MWM_FUNC_MINIMIZE) != 0) {
                named |= WindowFeatures.MINIMIZE;
            }
            if ((functions & MWM_FUNC_MAXIMIZE) != 0) {
                named |= WindowFeatures.MAXIMIZE;
            }
            if ((functions & MWM_FUNC_MOVE) != 0) {
                named |= WindowFeatures.MOVE;
            }
            if ((functions & MWM_FUNC_RESIZE) != 0) {
                named |= WindowFeatures.RESIZE;
            }
            return (functions & MWM_FUNC_ALL) != 0 ? WindowFeatures.MWM_FUNCTIONS & ~named : named;
        }

        /* Only 0 and exactly MWM_DECOR_BORDER have distinct meanings. */
        public void motif_decorations (uint32 flags, uint32 decorations, out bool decorated, out bool border_only) {
            bool named = (flags & MWM_HINTS_DECORATIONS) != 0;
            decorated = !named || decorations != 0;
            border_only = named && decorations == MWM_DECOR_BORDER;
        }

        /*
         * meta_window_update_struts (): _NET_WM_STRUT_PARTIAL, or _NET_WM_STRUT with
         * the whole screen edge. strut_area (_GNOME_WM_STRUT_AREA) comes first.
         */
        public Marco.Strut[] parse_struts (uint32[] strut_area, uint32[] partial, uint32[] simple,
                                           Marco.Rect screen, GLib.GenericArray<Output> outputs) {
            Marco.Strut[] struts = {};
            if (strut_area.length == 4) {
                var area = Marco.Rect () {
                    x = (int) strut_area[0], y = (int) strut_area[1],
                    width = (int) strut_area[2], height = (int) strut_area[3]
                };
                Marco.Side side = Marco.Side.LEFT;
                bool valid = false;
                foreach (var output in outputs) {
                    var monitor = output.rect;
                    if (!(area.x >= monitor.x && area.y >= monitor.y &&
                          area.x + area.width <= monitor.x + monitor.width &&
                          area.y + area.height <= monitor.y + monitor.height)) {
                        continue;
                    }
                    if (area.height > area.width) {
                        if (area.x == monitor.x) {
                            side = Marco.Side.LEFT;
                            valid = true;
                        } else if (area.x + area.width == monitor.x + monitor.width) {
                            side = Marco.Side.RIGHT;
                            valid = true;
                        }
                    } else {
                        if (area.y == monitor.y) {
                            side = Marco.Side.TOP;
                            valid = true;
                        } else if (area.y + area.height == monitor.y + monitor.height) {
                            side = Marco.Side.BOTTOM;
                            valid = true;
                        }
                    }
                }
                if (valid) {
                    struts += Marco.Strut () { rect = area, side = side, edge = Marco.EdgeType.XINERAMA };
                    return struts;
                }
            }
            unowned uint32[] values = partial.length == 12 ? partial : simple;
            if (values.length != 12 && values.length != 4) {
                return struts;
            }
            Marco.Side[] sides = { Marco.Side.LEFT, Marco.Side.RIGHT, Marco.Side.TOP, Marco.Side.BOTTOM };
            for (int i = 0; i < 4; i++) {
                int thickness = (int) values[i];
                if (thickness == 0) {
                    continue;
                }
                var rect = screen;
                bool has_range = values.length == 12;
                int begin = has_range ? (int) values[4 + i * 2] : 0;
                int end = has_range ? (int) values[5 + i * 2] : 0;
                if (sides[i] == Marco.Side.RIGHT) {
                    rect.x = screen.x + screen.width - thickness;
                }
                if (sides[i] == Marco.Side.BOTTOM) {
                    rect.y = screen.y + screen.height - thickness;
                }
                if (i < 2) {
                    rect.width = thickness;
                    if (has_range) {
                        rect.y = begin;
                        rect.height = end - begin + 1;
                    }
                } else {
                    rect.height = thickness;
                    if (has_range) {
                        rect.x = begin;
                        rect.width = end - begin + 1;
                    }
                }
                struts += Marco.Strut () { rect = rect, side = sides[i], edge = Marco.EdgeType.SCREEN };
            }
            return struts;
        }

        /* _NET_WM_STATE add (1), remove (0) or toggle (2) of a value now current. */
        public bool state_after (uint action, bool current) {
            return action == 1 || (action == 2 && !current);
        }

        /* An ancestor's group wins; otherwise the group leader, or the window itself. */
        public uint64 compute_group (uint64 ancestor_group, uint32 group_leader, uint32 own_id) {
            if (ancestor_group != 0) {
                return ancestor_group;
            }
            return group_leader != 0 ? group_leader : own_id;
        }
    }
}
