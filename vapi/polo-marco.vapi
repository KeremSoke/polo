/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Bindings for libpolo-marco (lib/marco/polo-marco.h).
 *
 * Region and edge lists contain g_new-allocated structs, as in Marco's
 * boxes.c. List<Rect?> frees them with g_free.
 */

[CCode (cheader_filename = "polo-marco.h")]
namespace Polo.Marco {
    [CCode (cname = "PoloDirection", cprefix = "POLO_DIRECTION_", has_type_id = false)]
    [Flags]
    public enum Direction {
        LEFT,
        RIGHT,
        TOP,
        BOTTOM,
        UP,
        DOWN,
        HORIZONTAL,
        VERTICAL
    }

    [CCode (cname = "PoloSide", cprefix = "POLO_SIDE_", has_type_id = false)]
    public enum Side {
        LEFT,
        RIGHT,
        TOP,
        BOTTOM
    }

    /* ICCCM gravity values. */
    [CCode (cname = "PoloGravity", cprefix = "POLO_GRAVITY_", has_type_id = false)]
    public enum Gravity {
        FORGET,
        NORTH_WEST,
        NORTH,
        NORTH_EAST,
        WEST,
        CENTER,
        EAST,
        SOUTH_WEST,
        SOUTH,
        SOUTH_EAST,
        STATIC
    }

    [CCode (cname = "PoloEdgeType", cprefix = "POLO_EDGE_", has_type_id = false)]
    public enum EdgeType {
        WINDOW,
        XINERAMA,
        SCREEN
    }

    [CCode (cname = "PoloFixedDirections", cprefix = "POLO_FIXED_DIRECTION_", has_type_id = false)]
    [Flags]
    public enum FixedDirections {
        NONE,
        X,
        Y
    }

    /* A rectangle in global logical coordinates. */
    [CCode (cname = "PoloRect", has_type_id = false)]
    public struct Rect {
        public int x;
        public int y;
        public int width;
        public int height;

        [CCode (cname = "polo_marco_rect_area")]
        public int area ();

        [CCode (cname = "polo_marco_rect_intersect")]
        public bool intersect (Rect other, out Rect dest);

        [CCode (cname = "polo_marco_rect_equal")]
        public bool equal (Rect other);

        [CCode (cname = "polo_marco_rect_union")]
        public void union (Rect other, out Rect dest);

        [CCode (cname = "polo_marco_rect_overlap")]
        public bool overlap (Rect other);

        [CCode (cname = "polo_marco_rect_vert_overlap")]
        public bool vert_overlap (Rect other);

        [CCode (cname = "polo_marco_rect_horiz_overlap")]
        public bool horiz_overlap (Rect other);

        /* This rectangle is the outer rectangle. */
        [CCode (cname = "polo_marco_rect_could_fit_rect")]
        public bool could_fit_rect (Rect inner);

        [CCode (cname = "polo_marco_rect_contains_rect")]
        public bool contains_rect (Rect inner);

        /* Resize rect from old_rect while keeping the gravity point fixed. */
        [CCode (cname = "polo_marco_rect_resize_with_gravity")]
        public static void resize_with_gravity (Rect old_rect, ref Rect rect, Gravity gravity,
                                                int new_width, int new_height);

        [CCode (cname = "polo_marco_rect_edge_aligns")]
        public bool edge_aligns (Edge edge);
    }

    [CCode (cname = "PoloStrut", has_type_id = false)]
    public struct Strut {
        public Rect rect;
        public Side side;
        public EdgeType edge;
    }

    [CCode (cname = "PoloEdge", has_type_id = false)]
    public struct Edge {
        /* An edge rectangle has zero width or height. */
        public Rect rect;
        public Side side_type;
        public EdgeType edge_type;
    }

    [CCode (cname = "PoloWindowType", cprefix = "POLO_WINDOW_", has_type_id = false)]
    public enum WindowType {
        NORMAL,
        DESKTOP,
        DOCK,
        DIALOG,
        MODAL_DIALOG,
        TOOLBAR,
        MENU,
        UTILITY,
        SPLASHSCREEN
    }

    /* Marco's stack layers; TOP and DOCK share a value. */
    [CCode (cname = "PoloStackLayer", cprefix = "POLO_LAYER_", has_type_id = false)]
    public enum StackLayer {
        DESKTOP,
        BOTTOM,
        NORMAL,
        TOP,
        DOCK,
        FULLSCREEN,
        FOCUSED_WINDOW,
        LAST
    }

    [CCode (cname = "PoloFrameBorders", has_type_id = false)]
    public struct FrameBorders {
        public int left;
        public int right;
        public int top;
        public int bottom;
    }

    [CCode (cname = "PoloSizeHintFlags", cprefix = "POLO_SIZE_HINT_", has_type_id = false)]
    [Flags]
    public enum SizeHintFlags {
        US_POSITION,
        US_SIZE,
        P_POSITION,
        P_SIZE,
        P_MIN_SIZE,
        P_MAX_SIZE,
        P_RESIZE_INC,
        P_ASPECT,
        P_BASE_SIZE,
        P_WIN_GRAVITY
    }

    [CCode (cname = "PoloAspect", has_type_id = false)]
    public struct Aspect {
        public int x;
        public int y;
    }

    /* Matches the X11 XSizeHints layout. */
    [CCode (cname = "PoloSizeHints", has_type_id = false)]
    public struct SizeHints {
        public uint32 flags;
        public int x;
        public int y;
        public int width;
        public int height;
        public int min_width;
        public int min_height;
        public int max_width;
        public int max_height;
        public int width_inc;
        public int height_inc;
        public Aspect min_aspect;
        public Aspect max_aspect;
        public int base_width;
        public int base_height;
        public Gravity win_gravity;

        [CCode (cname = "polo_marco_normalize_size_hints")]
        public void normalize ();
    }

    [CCode (cname = "PoloPlacementMode", cprefix = "POLO_PLACEMENT_", has_type_id = false)]
    public enum PlacementMode {
        AUTOMATIC,
        POINTER,
        MANUAL
    }

    /* C algorithm settings populated from Polo.MarcoPrefs. */
    [CCode (cname = "PoloMarcoSettings", has_type_id = false)]
    public struct Prefs {
        public PlacementMode placement_mode;
        public bool center_new_windows;
        public bool allow_tiling;
        public bool allow_top_tiling;
        public bool allow_tile_cycling;
        public bool disable_workarounds;
    }

    /* Work-area regions and edges, from Marco's boxes.c. */
    namespace Region {
        [CCode (cname = "polo_marco_rect_get_minimal_spanning_set_for_region")]
        public GLib.List<Rect?> get_minimal_spanning_set (Rect basic_rect,
                                                         GLib.SList<Strut?> all_struts,
                                                         bool skip_middle_struts);

        [CCode (cname = "polo_marco_rect_expand_region")]
        public GLib.List<Rect?> expand (owned GLib.List<Rect?> region, int left_expand,
                                        int right_expand, int top_expand, int bottom_expand);

        [CCode (cname = "polo_marco_rect_expand_region_conditionally")]
        public GLib.List<Rect?> expand_conditionally (owned GLib.List<Rect?> region,
                                                      int left_expand, int right_expand,
                                                      int top_expand, int bottom_expand,
                                                      int min_x, int min_y);

        [CCode (cname = "polo_marco_rect_expand_to_avoiding_struts")]
        public void expand_to_avoiding_struts (ref Rect rect, Rect expand_to, Direction direction,
                                               GLib.SList<Strut?> all_struts);

        [CCode (cname = "polo_marco_rect_could_fit_in_region")]
        public bool could_fit (GLib.List<Rect?> spanning_rects, Rect rect);

        [CCode (cname = "polo_marco_rect_contained_in_region")]
        public bool contains (GLib.List<Rect?> spanning_rects, Rect rect);

        [CCode (cname = "polo_marco_rect_overlaps_with_region")]
        public bool overlaps (GLib.List<Rect?> spanning_rects, Rect rect);

        [CCode (cname = "polo_marco_rect_clamp_to_fit_into_region")]
        public void clamp_to_fit (GLib.List<Rect?> spanning_rects,
                                  FixedDirections fixed_directions, ref Rect rect,
                                  Rect min_size);

        [CCode (cname = "polo_marco_rect_clip_to_region")]
        public void clip (GLib.List<Rect?> spanning_rects, FixedDirections fixed_directions,
                          ref Rect rect);

        [CCode (cname = "polo_marco_rect_shove_into_region")]
        public void shove (GLib.List<Rect?> spanning_rects, FixedDirections fixed_directions,
                           ref Rect rect);

        [CCode (cname = "polo_marco_rect_find_onscreen_edges")]
        public GLib.List<Edge?> find_onscreen_edges (Rect basic_rect,
                                                     GLib.SList<Strut?> all_struts);

        [CCode (cname = "polo_marco_rect_find_nonintersected_xinerama_edges")]
        public GLib.List<Edge?> find_nonintersected_xinerama_edges (Rect screen_rect,
                                                                    GLib.List<Rect?> xinerama_rects,
                                                                    GLib.SList<Strut?> all_struts);

        [CCode (cname = "polo_marco_rect_remove_intersections_with_boxes_from_edges")]
        public GLib.List<Edge?> remove_intersections_with_boxes_from_edges (
            owned GLib.List<Edge?> edges, GLib.SList<Rect?> rectangles);

        /* qsort-compatible edge comparators. */
        [CCode (cname = "polo_marco_rect_edge_cmp")]
        public int edge_cmp (Edge? a, Edge? b);

        [CCode (cname = "polo_marco_rect_edge_cmp_ignore_type")]
        public int edge_cmp_ignore_type (Edge? a, Edge? b);

        [CCode (cname = "polo_marco_rect_find_linepoint_closest_to_point")]
        public void find_linepoint_closest_to_point (double x1, double y1, double x2, double y2,
                                                     double px, double py,
                                                     out double valx, out double valy);
    }

    [CCode (cname = "PoloMarcoWindowInfo", has_type_id = false)]
    public struct WindowInfo {
        public Rect rect;
        public Rect position;
        public Rect outer;
        public Rect frame;
        public WindowType type;
        public uint32 size_hints_flags;
        public bool has_maximize_func;
        public bool decorated;
        public bool fullscreen;
        public bool wm_state_modal;
        public bool denied_focus_and_not_transient;
        public int workspace;
        public bool on_all_workspaces;
        public bool showing_on_its_workspace;
        public uint64 group;
    }

    [CCode (cname = "PoloMarcoXinerama", has_type_id = false)]
    public struct Xinerama {
        public Rect rect;
        public Rect work_area;
    }

    [CCode (cname = "PoloMarcoPlaceInput", has_type_id = false, destroy_function = "")]
    public struct PlaceInput {
        public WindowInfo* window;
        public FrameBorders* borders;
        public WindowInfo* parent;
        public WindowInfo* focus_window;
        public int focus_window_xinerama;
        [CCode (array_length_cname = "n_windows", array_length_type = "int")]
        public unowned WindowInfo[] windows;
        [CCode (array_length_cname = "n_xineramas", array_length_type = "int")]
        public unowned Xinerama[] xineramas;
        public int current_xinerama;
        public int pointer_x;
        public int pointer_y;
        public Prefs prefs;
    }

    [CCode (cname = "PoloMarcoPlaceResult", has_type_id = false)]
    public struct PlaceResult {
        public int x;
        public int y;
        public bool maximize_after_placement;
        public bool move_after_placement;
    }

    [CCode (cname = "polo_marco_place")]
    public void place (PlaceInput input, int x, int y, out PlaceResult result);

    [CCode (cname = "PoloEdgeWindow", has_type_id = false)]
    public struct EdgeWindow {
        public Rect outer;
        public bool relevant;
        public bool is_dock;
    }

    [Compact]
    [CCode (cname = "PoloEdgeResistanceData", free_function = "polo_marco_edge_resistance_free")]
    public class EdgeResistance {
        [CCode (cname = "polo_marco_edge_resistance_new")]
        public EdgeResistance (Rect screen_rect, EdgeWindow[] stacked_windows,
                               GLib.List<Edge?> xinerama_edges, GLib.List<Edge?> screen_edges);

        [CCode (cname = "polo_marco_edge_resistance_for_move")]
        public void for_move (Rect window_outer, int old_x, int old_y, ref int new_x, ref int new_y,
                              bool snap, bool is_keyboard_op);

        [CCode (cname = "polo_marco_edge_resistance_for_resize")]
        public void for_resize (Rect window_outer, int width_inc, int height_inc, int old_width,
                                int old_height, ref int new_width, ref int new_height, Gravity gravity,
                                bool snap, bool is_keyboard_op);
    }

    [CCode (cname = "PoloTileMode", cprefix = "POLO_TILE_", has_type_id = false)]
    public enum TileMode {
        NONE,
        LEFT,
        RIGHT,
        MAXIMIZED,
        TOP_LEFT,
        TOP_RIGHT,
        BOTTOM_LEFT,
        BOTTOM_RIGHT
    }

    [CCode (cname = "PoloTileCycle", cprefix = "POLO_TILE_CYCLE_", has_type_id = false)]
    public enum TileCycle {
        NONE,
        @50,
        @33,
        @25,
        @100,
        @75,
        @66
    }

    [Flags]
    [CCode (cname = "PoloMoveResizeFlags", cprefix = "POLO_", has_type_id = false)]
    public enum MoveResizeFlags {
        IS_CONFIGURE_REQUEST,
        DO_GRAVITY_ADJUST,
        IS_USER_ACTION,
        IS_MOVE_ACTION,
        IS_RESIZE_ACTION
    }

    [CCode (cname = "PoloMarcoConstraintWindow", has_type_id = false)]
    public struct ConstraintWindow {
        public WindowType type;
        public SizeHints size_hints;
        public bool has_frame;
        public bool decorated;
        public bool fullscreen;
        public int fullscreen_monitors[4];
        public bool maximized_horizontally;
        public bool maximized_vertically;
        public bool shaded;
        public bool tiled;
        public TileMode tile_mode;
        public TileCycle tile_cycle;
        public int tile_monitor_number;
        public bool tile_resized;
        public bool require_fully_onscreen;
        public bool require_on_single_xinerama;
        public bool require_titlebar_visible;
    }

    [CCode (cname = "PoloMarcoConstraintInput", has_type_id = false, destroy_function = "")]
    public struct ConstraintInput {
        public ConstraintWindow* window;
        public FrameBorders* borders;
        [CCode (array_length_cname = "n_xineramas", array_length_type = "int")]
        public unowned Xinerama[] xineramas;
        public void* usable_screen_region;
        public void** usable_xinerama_regions;
        public void* all_struts;
        public bool grab_frame_action;
    }

    [CCode (cname = "polo_marco_constrain")]
    public void constrain (ConstraintInput input, MoveResizeFlags flags, Gravity resize_gravity,
                           Rect orig, ref Rect new_rect);

    [CCode (cname = "polo_marco_get_current_tile_area")]
    public void get_current_tile_area (ConstraintInput input, out Rect tile_area);

    [CCode (cname = "polo_marco_adjust_for_gravity")]
    public void adjust_for_gravity (FrameBorders? borders, int border_width, Gravity gravity,
                                    ref Rect rect);

    [CCode (cname = "polo_marco_ensure_size_hints_satisfied")]
    public void ensure_size_hints_satisfied (ref Rect rect, SizeHints size_hints);

    [CCode (cname = "PoloMarcoStackWindow", has_type_id = false)]
    public struct StackWindow {
        public int stack_position;
        public StackLayer layer;
        public WindowType type;
        public int transient_for;
        public bool transient_parent_is_root_window;
        public uint64 group;
    }

    [CCode (cname = "polo_marco_stack_constrain")]
    public void stack_constrain ([CCode (array_length_pos = 1.1)] StackWindow[] windows,
                                 [CCode (array_length = false)] int[] group_order,
                                 int n_positions);

    [CCode (cname = "polo_marco_set_stack_position")]
    public void set_stack_position ([CCode (array_length_pos = 1.1)] StackWindow[] windows,
                                    int window, int position);

    [CCode (cname = "PoloButtonFunction", cprefix = "POLO_BUTTON_FUNCTION_", has_type_id = false)]
    public enum ButtonFunction {
        MENU,
        APPMENU,
        MINIMIZE,
        MAXIMIZE,
        CLOSE,
        SHADE,
        ABOVE,
        STICK,
        UNSHADE,
        UNABOVE,
        UNSTICK,
        LAST
    }

    [CCode (cname = "PoloButtonLayout", has_type_id = false, destroy_function = "")]
    public struct ButtonLayout {
        [CCode (array_length = false)]
        public ButtonFunction left_buttons[12];
        [CCode (array_length = false)]
        public bool left_buttons_has_spacer[12];
        [CCode (array_length = false)]
        public ButtonFunction right_buttons[12];
        [CCode (array_length = false)]
        public bool right_buttons_has_spacer[12];

        [CCode (cname = "polo_marco_button_layout_equal")]
        public bool equal (ButtonLayout other);
    }

    [Flags]
    [CCode (cname = "PoloVirtualModifier", cprefix = "POLO_VIRTUAL_", has_type_id = false)]
    public enum VirtualModifier {
        SHIFT_MASK,
        CONTROL_MASK,
        ALT_MASK,
        META_MASK,
        SUPER_MASK,
        HYPER_MASK,
        MOD2_MASK,
        MOD3_MASK,
        MOD4_MASK,
        MOD5_MASK
    }

    [CCode (cname = "polo_marco_parse_button_layout")]
    public void parse_button_layout (string? str, bool rtl, out ButtonLayout layout);

    [CCode (cname = "polo_marco_parse_accelerator")]
    public bool parse_accelerator (string? accel, out string? key_name, out uint keycode,
                                   out VirtualModifier mask);

    [CCode (cname = "polo_marco_parse_modifier")]
    public bool parse_modifier (string? accel, out VirtualModifier mask);

    [CCode (cname = "polo_marco_parse_font")]
    public string? parse_font (string? str);

    /* Marco release used as the C-port reference. */
    [CCode (cname = "polo_marco_reference_version")]
    public unowned string reference_version ();

    [CCode (cname = "PoloFrameType", cprefix = "POLO_FRAME_TYPE_", has_type_id = false,
            cheader_filename = "theme-parser.h")]
    public enum FrameType {
        NORMAL,
        DIALOG,
        MODAL_DIALOG,
        UTILITY,
        MENU,
        BORDER,
        ATTACHED,
        LAST
    }

    [CCode (cname = "PoloFrameFlags", cprefix = "POLO_FRAME_", has_type_id = false,
            cheader_filename = "theme-parser.h")]
    [Flags]
    public enum FrameFlags {
        ALLOWS_DELETE,
        ALLOWS_MENU,
        ALLOWS_APPMENU,
        ALLOWS_MINIMIZE,
        ALLOWS_MAXIMIZE,
        ALLOWS_VERTICAL_RESIZE,
        ALLOWS_HORIZONTAL_RESIZE,
        HAS_FOCUS,
        SHADED,
        STUCK,
        MAXIMIZED,
        ALLOWS_SHADE,
        ALLOWS_MOVE,
        FULLSCREEN,
        IS_FLASHING,
        ABOVE,
        TILED_LEFT,
        TILED_RIGHT
    }

    [CCode (cname = "PoloBorder", has_type_id = false, cheader_filename = "theme-parser.h")]
    public struct Border {
        public int16 left;
        public int16 right;
        public int16 top;
        public int16 bottom;
    }

    /* Marco's MetaFrameBorders: what the theme draws, and around it. */
    [CCode (cname = "PoloThemeBorders", has_type_id = false, cheader_filename = "theme-parser.h")]
    public struct ThemeBorders {
        public Border visible;
        public Border shadow;
        public Border resize;
        public Border invisible;
        public Border total;
    }

    [CCode (cname = "PoloButtonSpace", has_type_id = false, cheader_filename = "theme-parser.h")]
    public struct ButtonSpace {
        public Rect visible;
        public Rect clickable;
    }

    [CCode (cname = "PoloFrameGeometry", has_type_id = false, destroy_function = "",
            cheader_filename = "theme-parser.h")]
    public struct FrameGeometry {
        public ThemeBorders borders;
        public int width;
        public int height;
        public Rect title_rect;
        public ButtonSpace close_rect;
        public ButtonSpace max_rect;
        public ButtonSpace min_rect;
        public ButtonSpace menu_rect;
        public ButtonSpace appmenu_rect;
        public ButtonSpace shade_rect;
        public ButtonSpace above_rect;
        public ButtonSpace stick_rect;
        public ButtonSpace unshade_rect;
        public ButtonSpace unabove_rect;
        public ButtonSpace unstick_rect;
        public int n_left_buttons;
        public int n_right_buttons;
    }

    [CCode (cname = "PoloButtonState", cprefix = "POLO_BUTTON_STATE_", has_type_id = false,
            cheader_filename = "theme-parser.h")]
    public enum ButtonState {
        NORMAL,
        PRESSED,
        PRELIGHT,
        LAST
    }

    [CCode (cname = "PoloButtonType", cprefix = "POLO_BUTTON_TYPE_", has_type_id = false,
            cheader_filename = "theme-parser.h")]
    public enum ButtonType {
        LEFT_LEFT_BACKGROUND,
        LEFT_MIDDLE_BACKGROUND,
        LEFT_RIGHT_BACKGROUND,
        LEFT_SINGLE_BACKGROUND,
        RIGHT_LEFT_BACKGROUND,
        RIGHT_MIDDLE_BACKGROUND,
        RIGHT_RIGHT_BACKGROUND,
        RIGHT_SINGLE_BACKGROUND,
        CLOSE,
        MAXIMIZE,
        MINIMIZE,
        MENU,
        APPMENU,
        SHADE,
        ABOVE,
        STICK,
        UNSHADE,
        UNABOVE,
        UNSTICK,
        LAST
    }

    /* GTK theme colours resolved by polo-shell-ui. */
    [Compact]
    [CCode (cname = "PoloThemeColors", free_function = "polo_marco_theme_colors_free",
            cheader_filename = "theme-parser.h")]
    public class ThemeColors {
        [CCode (cname = "polo_marco_theme_colors_new")]
        public ThemeColors ();

        [CCode (cname = "polo_marco_theme_colors_set_table")]
        public bool set_table ([CCode (array_length_type = "int")] float[] values);

        [CCode (cname = "polo_marco_theme_colors_set_custom")]
        public void set_custom (string name, RGBA? color);
    }

    [CCode (cname = "PoloRGBA", has_type_id = false, cheader_filename = "theme-parser.h")]
    public struct RGBA {
        public double red;
        public double green;
        public double blue;
        public double alpha;
    }

    [CCode (cname = "PoloWindowShadow", cprefix = "POLO_WINDOW_SHADOW_", has_type_id = false,
            cheader_filename = "shadow.h")]
    public enum ShadowType {
        SMALL,
        MEDIUM,
        LARGE
    }

    [CCode (cname = "PoloShadowPiece", cprefix = "POLO_SHADOW_PIECE_", has_type_id = false,
            cheader_filename = "shadow.h")]
    public enum ShadowPiece {
        TOP,
        BOTTOM,
        LEFT,
        RIGHT;

        [CCode (cname = "4")]
        public const int COUNT;
    }

    [CCode (cname = "POLO_SHADOW_OPACITY", cheader_filename = "shadow.h")]
    public const double SHADOW_OPACITY;

    /* Marco's frame shadow, excluding the area covered by the frame. */
    [Compact]
    [CCode (cname = "PoloShadowMask", free_function = "polo_marco_shadow_mask_free", cheader_filename = "shadow.h")]
    public class ShadowMask {
        [CCode (cname = "polo_marco_shadow_mask_new")]
        public static ShadowMask? create (ShadowType type, double opacity, FrameGeometry geometry,
                                          FrameFlags flags);

        /* x and y are relative to the frame window's corner. */
        [CCode (cname = "polo_marco_shadow_mask_get_bounds")]
        public void get_bounds (out int x, out int y, out int width, out int height);

        [CCode (cname = "polo_marco_shadow_mask_get_piece")]
        public void get_piece (ShadowPiece piece, out int x, out int y, out int width, out int height);

        [CCode (cname = "polo_marco_shadow_mask_render")]
        public void render (int piece_x, int piece_y, [CCode (array_length = false)] uint8[] pixels,
                            int width, int height, int stride);
    }

    /* Releases Pango's cached fonts before exit. */
    [CCode (cname = "polo_marco_frame_release_fonts", cheader_filename = "theme-parser.h")]
    public void release_fonts ();

    /* Owned by its theme. */
    [Compact]
    [CCode (cname = "PoloFrameStyle", free_function = "", cheader_filename = "theme-parser.h")]
    public class FrameStyle {
    }

    /* A Marco (Metacity) window theme, as meta_theme_load () finds it. */
    [Compact]
    [CCode (cname = "PoloTheme", free_function = "polo_marco_theme_free", cheader_filename = "theme-parser.h")]
    public class Theme {
        [CCode (cname = "polo_marco_theme_load")]
        public static Theme? load (string name) throws GLib.Error;

        [CCode (cname = "polo_marco_theme_get_frame_style")]
        public unowned FrameStyle? get_frame_style (FrameType type, FrameFlags flags);

        [CCode (cname = "polo_marco_theme_get_frame_borders")]
        public void get_frame_borders (FrameType type, int text_height, FrameFlags flags,
                                       out ThemeBorders borders);

        [CCode (cname = "polo_marco_theme_get_custom_color_names", array_length = false,
                array_null_terminated = true)]
        public (unowned string)[] get_custom_color_names ();

        [CCode (cname = "polo_marco_frame_text_height")]
        public int text_height (FrameType type, FrameFlags flags, string? system_font,
                                string? titlebar_font, double dpi);

        /* Renders the specified frame piece into a width × height ARGB32 buffer. */
        [CCode (cname = "polo_marco_frame_render")]
        public void render (ThemeColors colors, FrameType type, FrameFlags flags,
                            int client_width, int client_height, string? title,
                            string? system_font, string? titlebar_font, double dpi,
                            ButtonLayout button_layout,
                            [CCode (array_length = false)] ButtonState[] button_states,
                            int piece_x, int piece_y, [CCode (array_length = false)] uint8[] pixels,
                            int width, int height, int stride);

        [CCode (cname = "polo_marco_theme_calc_geometry")]
        public void calc_geometry (FrameType type, int text_height, FrameFlags flags,
                                   int client_width, int client_height, ButtonLayout button_layout,
                                   out FrameGeometry geometry);
    }
}
