/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Snapshots the model for lib/marco/place.c and runs meta_window_place ()
 * on a window that is about to be shown.
 */

namespace Polo.Policy {
    public class Placement {
        /* Marco theme.c: the layout default no Metacity theme can change. */
        private const int INVISIBLE_RESIZE_BORDER = 10;

        /*
         * The invisible resize borders of Marco's frame window
         * (meta_frame_layout_get_borders () with the frame flags of
         * meta_frame_get_flags ()); zero for undecorated windows.
         */
        public static Marco.FrameBorders invisible_borders (Window w) {
            var b = Marco.FrameBorders ();
            if (!w.has_feature (WindowFeatures.DECORATED)) {
                return b;
            }
            bool maximized = w.has_state (WindowState.MAXIMIZED);
            bool tiled_left = w.has_state (WindowState.TILED_LEFT);
            bool tiled_right = w.has_state (WindowState.TILED_RIGHT);
            bool resizable = w.has_feature (WindowFeatures.RESIZE) && !maximized &&
                             !w.has_state (WindowState.FULLSCREEN) && !w.has_state (WindowState.SHADED);
            if (resizable && w.hints.min_width < w.hints.max_width) {
                b.left = INVISIBLE_RESIZE_BORDER;
                b.right = INVISIBLE_RESIZE_BORDER;
            }
            if (resizable && !tiled_left && !tiled_right && w.hints.min_height < w.hints.max_height) {
                b.top = INVISIBLE_RESIZE_BORDER;
                b.bottom = INVISIBLE_RESIZE_BORDER;
            }
            if (maximized || tiled_left || tiled_right) {
                b.top = 0;
                b.bottom = 0;
            }
            if (maximized || tiled_left) {
                b.left = 0;
            }
            if (maximized || tiled_right) {
                b.right = 0;
            }
            if (w.has_state (WindowState.SHADED)) {
                b.bottom = 0;
            }
            return b;
        }

        /* MetaWindow's rect: with a frame, x and y are the client's offset in the frame window. */
        private static Marco.Rect marco_rect (Window w) {
            var client = w.client_rect ();
            if (w.has_frame ()) {
                var frame = frame_rect (w);
                client.x -= frame.x;
                client.y -= frame.y;
            }
            return client;
        }

        public static Marco.Rect frame_rect (Window w) {
            var b = invisible_borders (w);
            return Marco.Rect () {
                x = w.frame.x - b.left,
                y = w.frame.y - b.top,
                width = w.frame.width + b.left + b.right,
                height = w.outer_rect ().height + b.top + b.bottom
            };
        }

        private static Marco.WindowInfo info (Model model, Window w) {
            return Marco.WindowInfo () {
                rect = marco_rect (w),
                position = w.client_rect (),
                outer = w.frame,
                frame = frame_rect (w),
                type = w.window_type,
                size_hints_flags = w.hints.flags,
                has_maximize_func = w.has_feature (WindowFeatures.MAXIMIZE),
                decorated = w.has_feature (WindowFeatures.DECORATED),
                fullscreen = w.has_state (WindowState.FULLSCREEN),
                wm_state_modal = w.has_state (WindowState.MODAL),
                denied_focus_and_not_transient = false,
                workspace = w.workspace,
                on_all_workspaces = w.workspace == -1,
                showing_on_its_workspace = w.has_state (WindowState.MAPPED) &&
                                           model.showing_on_its_workspace (w),
                group = w.group
            };
        }

        /* Marco meta_screen_get_xinerama_for_rect (). */
        public static int xinerama_for_rect (GLib.GenericArray<Output> outputs, Marco.Rect rect) {
            int best = 0;
            int best_area = 0;
            for (int i = 0; i < outputs.length; i++) {
                Marco.Rect dest;
                if (rect.intersect (outputs[i].rect, out dest)) {
                    int area = dest.width * dest.height;
                    if (area > best_area) {
                        best = i;
                        best_area = area;
                    }
                }
            }
            return best;
        }

        /*
         * The placed client position of window, whose frame is still where
         * the client asked. denied_focus is Marco's
         * denied_focus_and_not_transient.
         */
        public static Marco.PlaceResult place (Model model, Window window, bool denied_focus,
                                               double pointer_x, double pointer_y) {
            var outputs = model.get_outputs ();
            var client = window.client_rect ();
            var result = Marco.PlaceResult () { x = client.x, y = client.y };
            if (outputs.length == 0) {
                return result;
            }

            var xineramas = new Marco.Xinerama[outputs.length];
            int current = 0;
            for (int i = 0; i < outputs.length; i++) {
                xineramas[i] = Marco.Xinerama () {
                    rect = outputs[i].rect,
                    work_area = model.work_area_for (outputs[i], window.workspace)
                };
            }
            var pointer = model.output_at (pointer_x, pointer_y);
            for (int i = 0; i < outputs.length; i++) {
                if (outputs[i] == pointer) {
                    current = i;
                }
            }

            Marco.WindowInfo[] others = {};
            foreach (var w in model.get_windows ()) {
                if (w != window && w.has_state (WindowState.MAPPED)) {
                    others += info (model, w);
                }
            }

            var self = info (model, window);
            self.denied_focus_and_not_transient = denied_focus;
            var parent = window.transient_for != null ? info (model, window.transient_for)
                                                      : Marco.WindowInfo ();
            var focus = model.focused != null ? info (model, model.focused) : Marco.WindowInfo ();
            var borders = window.borders;

            var input = Marco.PlaceInput () {
                window = &self,
                borders = window.has_feature (WindowFeatures.DECORATED) ? &borders : null,
                parent = window.transient_for != null ? &parent : null,
                focus_window = model.focused != null ? &focus : null,
                focus_window_xinerama = model.focused != null
                    ? xinerama_for_rect (outputs, model.focused.frame) : 0,
                windows = others,
                xineramas = xineramas,
                current_xinerama = current,
                pointer_x = (int) pointer_x,
                pointer_y = (int) pointer_y,
                prefs = model.prefs.to_c ()
            };
            Marco.place (input, client.x, client.y, out result);
            return result;
        }
    }
}
