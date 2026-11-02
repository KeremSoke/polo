/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Work areas of one workspace, ported by semantics from Marco
 * 1.28.2 workspace.c ensure_work_areas_validated (), steps 1 to 5. The
 * regions and areas are computed with the boxes.c port in libpolo-marco,
 * exactly as Marco computes them.
 */

namespace Polo.Policy {
    /* One output's share of a workspace's work area. */
    public class OutputWorkArea : GLib.Object {
        public Output output { get; construct; }
        /* Marco work_area_xinerama[i]: the output rect clipped to the
         * region. Width and height are -1 when struts cover the output. */
        public Marco.Rect area;
        /* Marco xinerama_region[i]: the output minus all struts, as a
         * minimal spanning set. */
        public GLib.List<Marco.Rect?> region;

        public OutputWorkArea (Output output) {
            Object (output: output);
        }
    }

    public class WorkAreas : GLib.Object {
        /* Marco MIN_SANE_AREA. */
        private const int MIN_SANE_AREA = 100;

        /* Marco screen->rect: the bounding box of every output. */
        public Marco.Rect screen;
        /* Marco work_area_screen. */
        public Marco.Rect screen_area;
        /* Marco screen_region (never empty after step 4). */
        public GLib.List<Marco.Rect?> screen_region;
        /* Marco all_struts. */
        public GLib.SList<Marco.Strut?> struts;
        /* Marco screen_edges and xinerama_edges, for edge resistance. */
        public GLib.List<Marco.Edge?> screen_edges;
        public GLib.List<Marco.Edge?> xinerama_edges;

        private GLib.GenericArray<OutputWorkArea> per_output = new GLib.GenericArray<OutputWorkArea> ();

        /*
         * outputs in Marco's xinerama order; struts are every strut that
         * applies to the workspace (step 1 is the caller's: windows on the
         * workspace plus layer-shell surfaces).
         */
        public WorkAreas (GLib.GenericArray<Output> outputs, owned GLib.SList<Marco.Strut?> struts) {
            this.struts = (owned) struts;
            screen = bounding_box (outputs);

            /* Build the on-screen and single-output regions. */
            foreach (var output in outputs) {
                var share = new OutputWorkArea (output);
                share.region = Marco.Region.get_minimal_spanning_set (output.rect, this.struts, false);
                per_output.add (share);
            }
            screen_region = Marco.Region.get_minimal_spanning_set (screen, this.struts, true);

            /* Derive the screen and per-output work areas. */
            var work_area = screen;
            if (screen_region.length () == 0) {
                work_area = rect (0, 0, -1, -1);
            } else {
                Marco.Region.clip (screen_region, Marco.FixedDirections.NONE, ref work_area);
            }

            /* Keep the screen work area usable even with excessive struts. */
            if (work_area.width < MIN_SANE_AREA) {
                GLib.warning ("struts occupy an unusually large percentage of the screen; " +
                              "available remaining width = %d < %d", work_area.width, MIN_SANE_AREA);
                if (work_area.width < 1) {
                    work_area.x = (screen.width - MIN_SANE_AREA) / 2;
                    work_area.width = MIN_SANE_AREA;
                } else {
                    int amount = (MIN_SANE_AREA - work_area.width) / 2;
                    work_area.x -= amount;
                    work_area.width += 2 * amount;
                }
            }
            if (work_area.height < MIN_SANE_AREA) {
                GLib.warning ("struts occupy an unusually large percentage of the screen; " +
                              "available remaining height = %d < %d", work_area.height, MIN_SANE_AREA);
                if (work_area.height < 1) {
                    work_area.y = (screen.height - MIN_SANE_AREA) / 2;
                    work_area.height = MIN_SANE_AREA;
                } else {
                    int amount = (MIN_SANE_AREA - work_area.height) / 2;
                    work_area.y -= amount;
                    work_area.height += 2 * amount;
                }
            }
            screen_area = work_area;

            foreach (var share in per_output) {
                work_area = share.output.rect;
                if (share.region.length () == 0) {
                    work_area = rect (work_area.x, work_area.y, -1, -1);
                } else {
                    Marco.Region.clip (share.region, Marco.FixedDirections.NONE, ref work_area);
                }
                share.area = work_area;
            }

            if (screen_region.length () == 0) {
                screen_region.prepend (screen_area);
            }

            /* Cache screen and output edges for resistance and snapping. */
            screen_edges = Marco.Region.find_onscreen_edges (screen, this.struts);
            var xinerama_rects = new GLib.List<Marco.Rect?> ();
            foreach (var output in outputs) {
                xinerama_rects.prepend (output.rect);
            }
            xinerama_edges = Marco.Region.find_nonintersected_xinerama_edges (screen, xinerama_rects,
                                                                              this.struts);
        }

        public Marco.Rect area_for (Output output) {
            var share = share_for (output);
            return share != null ? share.area : output.rect;
        }

        public unowned GLib.List<Marco.Rect?>? region_for (Output output) {
            var share = share_for (output);
            return share != null ? share.region : null;
        }

        private OutputWorkArea? share_for (Output output) {
            foreach (var share in per_output) {
                if (share.output == output) {
                    return share;
                }
            }
            return null;
        }

        private static Marco.Rect rect (int x, int y, int width, int height) {
            return Marco.Rect () { x = x, y = y, width = width, height = height };
        }

        private static Marco.Rect bounding_box (GLib.GenericArray<Output> outputs) {
            if (outputs.length == 0) {
                return rect (0, 0, 0, 0);
            }
            int x1 = int.MAX, y1 = int.MAX, x2 = int.MIN, y2 = int.MIN;
            foreach (var output in outputs) {
                x1 = int.min (x1, output.rect.x);
                y1 = int.min (y1, output.rect.y);
                x2 = int.max (x2, output.rect.x + output.rect.width);
                y2 = int.max (y2, output.rect.y + output.rect.height);
            }
            return rect (x1, y1, x2 - x1, y2 - y1);
        }

        /*
         * A per-output strut, as Marco reads _GNOME_WM_STRUT_AREA
         * (window.c meta_window_update_struts ()): the rect must lie inside
         * one output and touch its edge; the side follows from its shape.
         * Layer-shell exclusive zones use this too.
         * Returns null for a rect Marco would ignore.
         */
        public static Marco.Strut? strut_from_area (Marco.Rect area, GLib.GenericArray<Output> outputs) {
            var side = Marco.Side.LEFT;
            bool valid = false;
            foreach (var output in outputs) {
                var monitor = output.rect;
                if (!monitor.contains_rect (area)) {
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
            if (!valid) {
                return null;
            }
            return Marco.Strut () { rect = area, side = side, edge = Marco.EdgeType.XINERAMA };
        }
    }
}
