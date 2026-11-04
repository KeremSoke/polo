/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * libpolo-marco: exact-behaviour ports of Marco algorithms (boxes, place,
 * constraints, stack constraints, edge resistance, theme engine) as pure
 * functions on value structs.
 *
 * C side of vapi/polo-marco.vapi.
 */

#ifndef POLO_MARCO_H
#define POLO_MARCO_H

#include <glib.h>

G_BEGIN_DECLS

/*
 * Directions and sides, from Marco src/include/common.h (v1.28.2,
 * GPL-2.0-or-later). Values are Marco's.
 */
typedef enum {
	POLO_DIRECTION_LEFT = 1 << 0,
	POLO_DIRECTION_RIGHT = 1 << 1,
	POLO_DIRECTION_TOP = 1 << 2,
	POLO_DIRECTION_BOTTOM = 1 << 3,

	POLO_DIRECTION_UP = POLO_DIRECTION_TOP,
	POLO_DIRECTION_DOWN = POLO_DIRECTION_BOTTOM,

	POLO_DIRECTION_HORIZONTAL = POLO_DIRECTION_LEFT | POLO_DIRECTION_RIGHT,
	POLO_DIRECTION_VERTICAL = POLO_DIRECTION_UP | POLO_DIRECTION_DOWN,
} PoloDirection;

typedef enum {
	POLO_SIDE_LEFT = POLO_DIRECTION_LEFT,
	POLO_SIDE_RIGHT = POLO_DIRECTION_RIGHT,
	POLO_SIDE_TOP = POLO_DIRECTION_TOP,
	POLO_SIDE_BOTTOM = POLO_DIRECTION_BOTTOM,
} PoloSide;

/*
 * Window gravity, with the X11 (ICCCM WM_NORMAL_HINTS) values Marco uses,
 * so XWayland hints map across unchanged and no X header is needed.
 */
typedef enum {
	POLO_GRAVITY_FORGET = 0,
	POLO_GRAVITY_NORTH_WEST = 1,
	POLO_GRAVITY_NORTH = 2,
	POLO_GRAVITY_NORTH_EAST = 3,
	POLO_GRAVITY_WEST = 4,
	POLO_GRAVITY_CENTER = 5,
	POLO_GRAVITY_EAST = 6,
	POLO_GRAVITY_SOUTH_WEST = 7,
	POLO_GRAVITY_SOUTH = 8,
	POLO_GRAVITY_SOUTH_EAST = 9,
	POLO_GRAVITY_STATIC = 10,
} PoloGravity;

/*
 * Rectangles, struts, edges and work-area regions: the port of Marco
 * src/core/boxes.c. PoloRect is the one rectangle type of Polo.
 */
#include "boxes.h"

/*
 * Window types and stack layers, with Marco's values (src/core/
 * window-private.h MetaWindowType, src/core/stack.h MetaStackLayer).
 */
typedef enum {
	POLO_WINDOW_NORMAL,
	POLO_WINDOW_DESKTOP,
	POLO_WINDOW_DOCK,
	POLO_WINDOW_DIALOG,
	POLO_WINDOW_MODAL_DIALOG,
	POLO_WINDOW_TOOLBAR,
	POLO_WINDOW_MENU,
	POLO_WINDOW_UTILITY,
	POLO_WINDOW_SPLASHSCREEN,
} PoloWindowType;

typedef enum {
	POLO_LAYER_DESKTOP = 0,
	POLO_LAYER_BOTTOM = 1,
	POLO_LAYER_NORMAL = 2,
	POLO_LAYER_TOP = 4, /* same as DOCK; see EWMH and Marco bug 330717 */
	POLO_LAYER_DOCK = 4,
	POLO_LAYER_FULLSCREEN = 5,
	POLO_LAYER_FOCUSED_WINDOW = 6,
	POLO_LAYER_LAST = 7,
} PoloStackLayer;

/* Decoration extents around the client area (_NET_FRAME_EXTENTS). */
typedef struct {
	int left;
	int right;
	int top;
	int bottom;
} PoloFrameBorders;

/* Which PoloSizeHints fields are set: the X11 XSizeHints flag values. */
typedef enum {
	POLO_SIZE_HINT_US_POSITION = 1 << 0,
	POLO_SIZE_HINT_US_SIZE = 1 << 1,
	POLO_SIZE_HINT_P_POSITION = 1 << 2,
	POLO_SIZE_HINT_P_SIZE = 1 << 3,
	POLO_SIZE_HINT_P_MIN_SIZE = 1 << 4,
	POLO_SIZE_HINT_P_MAX_SIZE = 1 << 5,
	POLO_SIZE_HINT_P_RESIZE_INC = 1 << 6,
	POLO_SIZE_HINT_P_ASPECT = 1 << 7,
	POLO_SIZE_HINT_P_BASE_SIZE = 1 << 8,
	POLO_SIZE_HINT_P_WIN_GRAVITY = 1 << 9,
} PoloSizeHintFlags;

typedef struct {
	int x;
	int y;
} PoloAspect;

/*
 * Size hints laid out like X11's XSizeHints, which Marco's constraints.c
 * reads; xdg-shell min/max sizes fill the same fields.
 */
typedef struct {
	guint32 flags; /* PoloSizeHintFlags */
	int x;
	int y;
	int width;
	int height;
	int min_width;
	int min_height;
	int max_width;
	int max_height;
	int width_inc;
	int height_inc;
	PoloAspect min_aspect;
	PoloAspect max_aspect;
	int base_width;
	int base_height;
	PoloGravity win_gravity;
} PoloSizeHints;

void
polo_marco_normalize_size_hints(PoloSizeHints *hints);

/* org.mate.Marco.general placement-mode, with the schema's values. */
typedef enum {
	POLO_PLACEMENT_AUTOMATIC = 0,
	POLO_PLACEMENT_POINTER = 1,
	POLO_PLACEMENT_MANUAL = 2,
} PoloPlacementMode;

/*
 * The preferences the C algorithms read, filled from Polo.MarcoPrefs,
 * which owns the full set.
 */
typedef struct {
	PoloPlacementMode placement_mode;
	gboolean center_new_windows;
	gboolean allow_tiling;
	gboolean allow_top_tiling;
	gboolean allow_tile_cycling;
	gboolean disable_workarounds;
} PoloMarcoSettings;

typedef enum {
	POLO_BUTTON_FUNCTION_MENU,
	POLO_BUTTON_FUNCTION_APPMENU,
	POLO_BUTTON_FUNCTION_MINIMIZE,
	POLO_BUTTON_FUNCTION_MAXIMIZE,
	POLO_BUTTON_FUNCTION_CLOSE,
	POLO_BUTTON_FUNCTION_SHADE,
	POLO_BUTTON_FUNCTION_ABOVE,
	POLO_BUTTON_FUNCTION_STICK,
	POLO_BUTTON_FUNCTION_UNSHADE,
	POLO_BUTTON_FUNCTION_UNABOVE,
	POLO_BUTTON_FUNCTION_UNSTICK,
	POLO_BUTTON_FUNCTION_LAST,
} PoloButtonFunction;

#define POLO_MAX_BUTTONS_PER_CORNER POLO_BUTTON_FUNCTION_LAST

/*
 * One slot more than Marco's MetaButtonLayout: Marco writes the terminating
 * LAST one past the end when a side lists all eight buttons.
 */
typedef struct {
	PoloButtonFunction left_buttons[POLO_MAX_BUTTONS_PER_CORNER + 1];
	gboolean left_buttons_has_spacer[POLO_MAX_BUTTONS_PER_CORNER + 1];
	PoloButtonFunction right_buttons[POLO_MAX_BUTTONS_PER_CORNER + 1];
	gboolean right_buttons_has_spacer[POLO_MAX_BUTTONS_PER_CORNER + 1];
} PoloButtonLayout;

typedef enum {
	POLO_VIRTUAL_SHIFT_MASK = 1 << 5,
	POLO_VIRTUAL_CONTROL_MASK = 1 << 6,
	POLO_VIRTUAL_ALT_MASK = 1 << 7,
	POLO_VIRTUAL_META_MASK = 1 << 8,
	POLO_VIRTUAL_SUPER_MASK = 1 << 9,
	POLO_VIRTUAL_HYPER_MASK = 1 << 10,
	POLO_VIRTUAL_MOD2_MASK = 1 << 11,
	POLO_VIRTUAL_MOD3_MASK = 1 << 12,
	POLO_VIRTUAL_MOD4_MASK = 1 << 13,
	POLO_VIRTUAL_MOD5_MASK = 1 << 14,
} PoloVirtualModifier;

void
polo_marco_parse_button_layout(const char *string, gboolean rtl,
			       PoloButtonLayout *layout);

gboolean
polo_marco_button_layout_equal(const PoloButtonLayout *a,
			       const PoloButtonLayout *b);

gboolean
polo_marco_parse_accelerator(const char *accel, char **key_name,
			     guint *keycode, PoloVirtualModifier *mask);

gboolean
polo_marco_parse_modifier(const char *accel, PoloVirtualModifier *mask);

char *
polo_marco_parse_font(const char *string);

/*
 * What place.c reads of a window: rect is MetaWindow's rect (the client
 * area, relative to the frame window for decorated windows), position the
 * client area in root coordinates (meta_window_get_position ()), outer the
 * visible frame (meta_window_get_outer_rect ()) and frame the frame window
 * with its invisible resize borders (frame->rect).
 */
typedef struct {
	PoloRect rect;
	PoloRect position;
	PoloRect outer;
	PoloRect frame;
	PoloWindowType type;
	guint32 size_hints_flags;
	gboolean has_maximize_func;
	gboolean decorated;
	gboolean fullscreen;
	gboolean wm_state_modal;
	gboolean denied_focus_and_not_transient;
	int workspace;
	gboolean on_all_workspaces;
	gboolean showing_on_its_workspace;
	guint64 group;
} PoloMarcoWindowInfo;

typedef struct {
	PoloRect rect;
	PoloRect work_area;
} PoloMarcoXinerama;

typedef struct {
	const PoloMarcoWindowInfo *window;
	const PoloFrameBorders *borders;
	const PoloMarcoWindowInfo *parent;
	const PoloMarcoWindowInfo *focus_window;
	int focus_window_xinerama;
	const PoloMarcoWindowInfo *windows;
	int n_windows;
	const PoloMarcoXinerama *xineramas;
	int n_xineramas;
	int current_xinerama;
	int pointer_x;
	int pointer_y;
	PoloMarcoSettings prefs;
} PoloMarcoPlaceInput;

typedef struct {
	int x;
	int y;
	gboolean maximize_after_placement;
	gboolean move_after_placement;
} PoloMarcoPlaceResult;

void
polo_marco_place(const PoloMarcoPlaceInput *input, int x, int y,
		 PoloMarcoPlaceResult *result);

typedef enum {
	POLO_TILE_NONE,
	POLO_TILE_LEFT,
	POLO_TILE_RIGHT,
	POLO_TILE_MAXIMIZED,
	POLO_TILE_TOP_LEFT,
	POLO_TILE_TOP_RIGHT,
	POLO_TILE_BOTTOM_LEFT,
	POLO_TILE_BOTTOM_RIGHT,
} PoloTileMode;

typedef enum {
	POLO_TILE_CYCLE_NONE,
	POLO_TILE_CYCLE_50,
	POLO_TILE_CYCLE_33,
	POLO_TILE_CYCLE_25,
	POLO_TILE_CYCLE_100,
	POLO_TILE_CYCLE_75,
	POLO_TILE_CYCLE_66,
} PoloTileCycle;

typedef enum {
	POLO_IS_CONFIGURE_REQUEST = 1 << 0,
	POLO_DO_GRAVITY_ADJUST = 1 << 1,
	POLO_IS_USER_ACTION = 1 << 2,
	POLO_IS_MOVE_ACTION = 1 << 3,
	POLO_IS_RESIZE_ACTION = 1 << 4,
} PoloMoveResizeFlags;

/*
 * The MetaWindow fields constraints.c reads; the require_* flags and
 * tile_resized are written back as Marco writes them.
 */
typedef struct {
	PoloWindowType type;
	PoloSizeHints size_hints;
	gboolean has_frame;
	gboolean decorated;
	gboolean fullscreen;
	int fullscreen_monitors[4];
	gboolean maximized_horizontally;
	gboolean maximized_vertically;
	gboolean shaded;
	gboolean tiled;
	PoloTileMode tile_mode;
	PoloTileCycle tile_cycle;
	int tile_monitor_number;
	gboolean tile_resized;
	gboolean require_fully_onscreen;
	gboolean require_on_single_xinerama;
	gboolean require_titlebar_visible;
} PoloMarcoConstraintWindow;

/*
 * The screen state: xineramas with the window's work areas, and the active
 * workspace's regions and struts (meta_workspace_get_onscreen_region (),
 * meta_workspace_get_onxinerama_region (), all_struts).
 */
typedef struct {
	PoloMarcoConstraintWindow *window;
	const PoloFrameBorders *borders;
	const PoloMarcoXinerama *xineramas;
	int n_xineramas;
	GList *usable_screen_region;
	GList **usable_xinerama_regions;
	GSList *all_struts;
	gboolean grab_frame_action;
} PoloMarcoConstraintInput;

void
polo_marco_constrain(const PoloMarcoConstraintInput *input,
		     PoloMoveResizeFlags flags, PoloGravity resize_gravity,
		     const PoloRect *orig, PoloRect *new_rect);

void
polo_marco_get_current_tile_area(const PoloMarcoConstraintInput *input,
				 PoloRect *tile_area);

int
polo_marco_xinerama_for_rect(const PoloMarcoXinerama *xineramas,
			     int n_xineramas, const PoloRect *rect);

void
polo_marco_adjust_for_gravity(const PoloFrameBorders *borders,
			      int border_width, PoloGravity gravity,
			      PoloRect *rect);

void
polo_marco_ensure_size_hints_satisfied(PoloRect *rect,
				       const PoloSizeHints *size_hints);

/*
 * A window as stack.c's constraint code sees it. stack_position and layer
 * are read and written; transient_for indexes the same array (-1 for none)
 * and group 0 means a group of its own.
 */
typedef struct {
	int stack_position;
	PoloStackLayer layer;
	PoloWindowType type;
	int transient_for;
	gboolean transient_parent_is_root_window;
	guint64 group;
} PoloMarcoStackWindow;

/*
 * stack_do_constrain (): create_constraints (), graph_constraints () and
 * apply_constraints () over windows in stack->sorted order (topmost first).
 * group_order lists, as indexes, every window in meta_group_list_windows ()
 * order; n_positions is stack->n_positions.
 */
void
polo_marco_stack_constrain(PoloMarcoStackWindow *windows, int n_windows,
			   const int *group_order, int n_positions);

void
polo_marco_set_stack_position(PoloMarcoStackWindow *windows, int n_windows,
			      int window, int position);

/*
 * edge-resistance.c: one window of compute_resistance_and_snapping_edges ()'s
 * stacking list. outer is meta_window_get_outer_rect (); relevant is
 * WINDOW_EDGES_RELEVANT (); is_dock is a META_WINDOW_DOCK.
 */
typedef struct {
	PoloRect outer;
	gboolean relevant;
	gboolean is_dock;
} PoloEdgeWindow;

/* display->grab_edge_resistance_data. */
typedef struct _PoloEdgeResistanceData PoloEdgeResistanceData;

/* stacked_windows go bottom to top. */
PoloEdgeResistanceData *
polo_marco_edge_resistance_new(const PoloRect *screen_rect,
			       const PoloEdgeWindow *stacked_windows,
			       int n_stacked_windows,
			       const GList *xinerama_edges,
			       const GList *screen_edges);

/* meta_display_cleanup_edges () */
void
polo_marco_edge_resistance_free(PoloEdgeResistanceData *edge_data);

/* meta_window_edge_resistance_for_move (); window_outer is its outer rect. */
void
polo_marco_edge_resistance_for_move(PoloEdgeResistanceData *edge_data,
				    const PoloRect *window_outer, int old_x,
				    int old_y, int *new_x, int *new_y,
				    gboolean snap, gboolean is_keyboard_op);

/* meta_window_edge_resistance_for_resize (); the increments are the size
 * hints'. */
void
polo_marco_edge_resistance_for_resize(PoloEdgeResistanceData *edge_data,
				      const PoloRect *window_outer,
				      int width_inc, int height_inc,
				      int old_width, int old_height,
				      int *new_width, int *new_height,
				      PoloGravity gravity, gboolean snap,
				      gboolean is_keyboard_op);

/* White text in font on black, centred with padding; the size to render it at. */
void
polo_marco_label_size(const char *text, const char *font, int *width,
		      int *height);

/* Into width x height ARGB32 pixels with the given stride. */
void
polo_marco_label_render(const char *text, const char *font, int width,
			int height, guint8 *pixels, int stride);

/* Marco release the ports follow. */
const char *
polo_marco_reference_version(void);

G_END_DECLS

#endif /* POLO_MARCO_H */
