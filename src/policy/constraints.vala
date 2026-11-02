/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Snapshots a window and the active workspace for lib/marco/constraints.c
 * and writes back what meta_window_constrain () changes on the window.
 */

namespace Polo.Policy {
    public class Constraints {
        private static Marco.ConstraintWindow snapshot (Window window) {
            var cw = Marco.ConstraintWindow () {
                type = window.window_type,
                size_hints = window.hints,
                has_frame = window.has_frame (),
                decorated = window.has_feature (WindowFeatures.DECORATED),
                fullscreen = window.has_state (WindowState.FULLSCREEN),
                maximized_horizontally = window.has_state (WindowState.MAXIMIZED_H),
                maximized_vertically = window.has_state (WindowState.MAXIMIZED_V),
                shaded = window.has_state (WindowState.SHADED),
                tiled = window.tiled,
                tile_mode = window.tile_mode,
                tile_cycle = window.tile_cycle,
                tile_monitor_number = window.tile_monitor_number,
                tile_resized = window.tile_resized,
                require_fully_onscreen = window.require_fully_onscreen,
                require_on_single_xinerama = window.require_on_single_xinerama,
                require_titlebar_visible = window.require_titlebar_visible
            };
            for (int i = 0; i < 4; i++) {
                cw.fullscreen_monitors[i] = window.fullscreen_monitors[i];
            }
            return cw;
        }

        private static Marco.Xinerama[] xineramas (Model model, Window window) {
            var outputs = model.get_outputs ();
            var result = new Marco.Xinerama[outputs.length];
            for (int i = 0; i < outputs.length; i++) {
                result[i] = Marco.Xinerama () {
                    rect = outputs[i].rect,
                    work_area = model.work_area_for (outputs[i], window.workspace)
                };
            }
            return result;
        }

        /*
         * meta_window_constrain (): orig and new_rect are client areas.
         * Needs at least one output.
         */
        public static void constrain (Model model, Window window, Marco.MoveResizeFlags flags,
                                      Marco.Gravity gravity, Marco.Rect orig, ref Marco.Rect new_rect,
                                      bool grab_frame_action = false) {
            var outputs = model.get_outputs ();
            var areas = model.work_areas ();
            var cw = snapshot (window);
            var borders = window.borders;
            var xi = xineramas (model, window);
            var regions = new void*[outputs.length];
            for (int i = 0; i < outputs.length; i++) {
                regions[i] = (void*) areas.region_for (outputs[i]);
            }
            var input = Marco.ConstraintInput () {
                window = &cw,
                borders = window.has_frame () ? &borders : null,
                xineramas = xi,
                usable_screen_region = (void*) areas.screen_region,
                usable_xinerama_regions = (void**) regions,
                all_struts = (void*) areas.struts,
                grab_frame_action = grab_frame_action
            };
            Marco.constrain (input, flags, gravity, orig, ref new_rect);
            window.require_fully_onscreen = cw.require_fully_onscreen;
            window.require_on_single_xinerama = cw.require_on_single_xinerama;
            window.require_titlebar_visible = cw.require_titlebar_visible;
            window.tile_resized = cw.tile_resized;
        }

        /* meta_window_get_current_tile_area (), as a frame rectangle. */
        public static Marco.Rect tile_area (Model model, Window window) {
            var cw = snapshot (window);
            var xi = xineramas (model, window);
            var input = Marco.ConstraintInput () {
                window = &cw,
                xineramas = xi
            };
            Marco.Rect area;
            Marco.get_current_tile_area (input, out area);
            return area;
        }
    }
}
