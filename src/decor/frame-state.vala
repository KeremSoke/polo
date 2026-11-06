/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

namespace Polo.Decor {
    /* Maps Polo window state to what Marco's theme expects. */
    namespace FrameState {
        /* Based on Marco's META_CORE_GET_FRAME_TYPE. Marco 1.28 never attaches
         * modal dialogs. */
        public Marco.FrameType? type_of (Window window) {
            Marco.FrameType type;
            switch (window.window_type) {
                case Marco.WindowType.NORMAL: type = Marco.FrameType.NORMAL; break;
                case Marco.WindowType.DIALOG: type = Marco.FrameType.DIALOG; break;
                case Marco.WindowType.MODAL_DIALOG: type = Marco.FrameType.MODAL_DIALOG; break;
                case Marco.WindowType.MENU: type = Marco.FrameType.MENU; break;
                case Marco.WindowType.UTILITY: type = Marco.FrameType.UTILITY; break;
                default: return null;
            }
            return window.has_feature (WindowFeatures.BORDER_ONLY) ? Marco.FrameType.BORDER : type;
        }

        /* Matches Marco's meta_frame_get_flags(). */
        public Marco.FrameFlags flags_of (Window window) {
            Marco.FrameFlags flags = 0;
            bool fullscreen = window.has_state (WindowState.FULLSCREEN);
            bool maximized = window.has_state (WindowState.MAXIMIZED);
            bool shaded = window.has_state (WindowState.SHADED);
            bool side_tiled = window.tiled && (window.tile_mode == Marco.TileMode.LEFT ||
                                               window.tile_mode == Marco.TileMode.RIGHT);
            if (!window.has_feature (WindowFeatures.BORDER_ONLY)) {
                flags |= Marco.FrameFlags.ALLOWS_MENU;
                if (window.has_feature (WindowFeatures.CLOSE)) {
                    flags |= Marco.FrameFlags.ALLOWS_DELETE;
                }
                if (window.has_feature (WindowFeatures.MAXIMIZE)) {
                    flags |= Marco.FrameFlags.ALLOWS_MAXIMIZE;
                }
                if (window.has_feature (WindowFeatures.MINIMIZE)) {
                    flags |= Marco.FrameFlags.ALLOWS_MINIMIZE;
                }
                if (window.has_feature (WindowFeatures.SHADE)) {
                    flags |= Marco.FrameFlags.ALLOWS_SHADE;
                }
            }
            if (window.has_feature (WindowFeatures.MOVE) && !fullscreen) {
                flags |= Marco.FrameFlags.ALLOWS_MOVE;
            }
            bool resizable = window.has_feature (WindowFeatures.RESIZE) && !maximized && !fullscreen && !shaded;
            if (resizable && window.hints.min_width < window.hints.max_width) {
                flags |= Marco.FrameFlags.ALLOWS_HORIZONTAL_RESIZE;
            }
            if (resizable && !side_tiled && window.hints.min_height < window.hints.max_height) {
                flags |= Marco.FrameFlags.ALLOWS_VERTICAL_RESIZE;
            }
            if (window.has_state (WindowState.ACTIVE)) {
                flags |= Marco.FrameFlags.HAS_FOCUS;
            }
            if (shaded) {
                flags |= Marco.FrameFlags.SHADED;
            }
            if (window.workspace == -1) {
                flags |= Marco.FrameFlags.STUCK;
            }
            if (maximized) {
                flags |= Marco.FrameFlags.MAXIMIZED;
            }
            if (side_tiled && window.tile_mode == Marco.TileMode.LEFT) {
                flags |= Marco.FrameFlags.TILED_LEFT;
            }
            if (side_tiled && window.tile_mode == Marco.TileMode.RIGHT) {
                flags |= Marco.FrameFlags.TILED_RIGHT;
            }
            if (fullscreen) {
                flags |= Marco.FrameFlags.FULLSCREEN;
            }
            if (window.frame_flashing) {
                flags |= Marco.FrameFlags.IS_FLASHING;
            }
            if (window.has_state (WindowState.ABOVE)) {
                flags |= Marco.FrameFlags.ABOVE;
            }
            return flags;
        }
    }
}
