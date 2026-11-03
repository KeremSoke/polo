/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Marco Theme Rendering */

/*
 * Copyright (C) 2001 Havoc Pennington
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#ifndef POLO_THEME_H
#define POLO_THEME_H

#include "theme-compat.h"

typedef struct _PoloFrameStyle PoloFrameStyle;
typedef struct _PoloFrameStyleSet PoloFrameStyleSet;
typedef struct _PoloDrawOp PoloDrawOp;
typedef struct _PoloDrawOpList PoloDrawOpList;
typedef struct _PoloGradientSpec PoloGradientSpec;
typedef struct _PoloAlphaGradientSpec PoloAlphaGradientSpec;
typedef struct _PoloColorSpec PoloColorSpec;
typedef struct _PoloFrameLayout PoloFrameLayout;
typedef struct _PoloButtonSpace PoloButtonSpace;
typedef struct _PoloFrameGeometry PoloFrameGeometry;
typedef struct _PoloTheme PoloTheme;
typedef struct _PoloPositionExprEnv PoloPositionExprEnv;
typedef struct _PoloDrawInfo PoloDrawInfo;

#define POLO_THEME_ERROR (g_quark_from_static_string("meta-theme-error"))

typedef enum {
	POLO_THEME_ERROR_FRAME_GEOMETRY,
	POLO_THEME_ERROR_BAD_CHARACTER,
	POLO_THEME_ERROR_BAD_PARENS,
	POLO_THEME_ERROR_UNKNOWN_VARIABLE,
	POLO_THEME_ERROR_DIVIDE_BY_ZERO,
	POLO_THEME_ERROR_MOD_ON_FLOAT,
	POLO_THEME_ERROR_FAILED
} PoloThemeError;

/**
 * Whether a button's size is calculated from the area around it (aspect
 * sizing) or is given as a fixed height and width in pixels (fixed sizing).
 *
 * \bug This could be done away with; see the comment at the top of
 * PoloFrameLayout.
 */
typedef enum {
	POLO_BUTTON_SIZING_ASPECT,
	POLO_BUTTON_SIZING_FIXED,
	POLO_BUTTON_SIZING_LAST
} PoloButtonSizing;

/**
 * Various parameters used to calculate the geometry of a frame.
 * They are used inside a PoloFrameStyle.
 * This corresponds closely to the <frame_geometry> tag in a theme file.
 *
 * \bug button_sizing isn't really necessary, because we could easily say
 * that if button_aspect is zero, the height and width are fixed values.
 * This would also mean that PoloButtonSizing didn't need to exist, and
 * save code.
 **/
struct _PoloFrameLayout {
	/** Reference count. */
	int refcount;

	/** Size of left side */
	int left_width;
	/** Size of right side */
	int right_width;
	/** Size of bottom side */
	int bottom_height;

	PoloBorder invisible_resize_border;

	/** Border of blue title region
	 * \bug (blue?!)
	 **/
	PoloBorder title_border;

	/** Extra height for inside of title region, above the font height */
	int title_vertical_pad;

	/** Right indent of buttons from edges of frame */
	int right_titlebar_edge;
	/** Left indent of buttons from edges of frame */
	int left_titlebar_edge;

	/**
	 * Sizing rule of buttons, either POLO_BUTTON_SIZING_ASPECT
	 * (in which case button_aspect will be honoured, and
	 * button_width and button_height set from it), or
	 * POLO_BUTTON_SIZING_FIXED (in which case we read the width
	 * and height directly).
	 */
	PoloButtonSizing button_sizing;

	/**
	 * Ratio of height/width. Honoured only if
	 * button_sizing==POLO_BUTTON_SIZING_ASPECT.
	 * Otherwise we figure out the height from the button_border.
	 */
	double button_aspect;

	/** Width of a button; set even when we are using aspect sizing */
	int button_width;

	/** Height of a button; set even when we are using aspect sizing */
	int button_height;

	/** Space around buttons */
	PoloBorder button_border;

	/** scale factor for title text */
	double title_scale;

	/** Whether title text will be displayed */
	guint has_title : 1;

	/** Whether we should hide the buttons */
	guint hide_buttons : 1;

	/** Radius of the top left-hand corner; 0 if not rounded */
	guint top_left_corner_rounded_radius;
	/** Radius of the top right-hand corner; 0 if not rounded */
	guint top_right_corner_rounded_radius;
	/** Radius of the bottom left-hand corner; 0 if not rounded */
	guint bottom_left_corner_rounded_radius;
	/** Radius of the bottom right-hand corner; 0 if not rounded */
	guint bottom_right_corner_rounded_radius;
};

/**
 * The computed size of a button (really just a way of tying its
 * visible and clickable areas together).
 * The reason for two different rectangles here is Fitts' law & maximized
 * windows; see bug #97703 for more details.
 */
struct _PoloButtonSpace {
	/** The screen area where the button's image is drawn */
	PoloRect visible;
	/** The screen area where the button can be activated by clicking */
	PoloRect clickable;
};

/**
 * Calculated actual geometry of the frame
 */
struct _PoloFrameGeometry {
	PoloThemeBorders borders;

	int width;
	int height;

	PoloRect title_rect;

	int left_titlebar_edge;
	int right_titlebar_edge;
	int top_titlebar_edge;
	int bottom_titlebar_edge;

	/* used for a memset hack */
#define ADDRESS_OF_BUTTON_RECTS(fgeom)                                         \
	(((char *)(fgeom)) + G_STRUCT_OFFSET(PoloFrameGeometry, close_rect))
#define LENGTH_OF_BUTTON_RECTS                                                 \
	(G_STRUCT_OFFSET(PoloFrameGeometry, right_single_background) +         \
	 sizeof(PoloRect) - G_STRUCT_OFFSET(PoloFrameGeometry, close_rect))

	/* The button rects (if changed adjust memset hack) */
	PoloButtonSpace close_rect;
	PoloButtonSpace max_rect;
	PoloButtonSpace min_rect;
	PoloButtonSpace menu_rect;
	PoloButtonSpace appmenu_rect;
	PoloButtonSpace shade_rect;
	PoloButtonSpace above_rect;
	PoloButtonSpace stick_rect;
	PoloButtonSpace unshade_rect;
	PoloButtonSpace unabove_rect;
	PoloButtonSpace unstick_rect;

#define MAX_MIDDLE_BACKGROUNDS (POLO_MAX_BUTTONS_PER_CORNER - 2)
	PoloRect left_left_background;
	PoloRect left_middle_backgrounds[MAX_MIDDLE_BACKGROUNDS];
	PoloRect left_right_background;
	PoloRect left_single_background;
	PoloRect right_left_background;
	PoloRect right_middle_backgrounds[MAX_MIDDLE_BACKGROUNDS];
	PoloRect right_right_background;
	PoloRect right_single_background;
	/* End of button rects (if changed adjust memset hack) */

	/* Saved button layout */
	PoloButtonLayout button_layout;
	int n_left_buttons;
	int n_right_buttons;

	/* Round corners */
	guint top_left_corner_rounded_radius;
	guint top_right_corner_rounded_radius;
	guint bottom_left_corner_rounded_radius;
	guint bottom_right_corner_rounded_radius;
};

typedef enum {
	POLO_IMAGE_FILL_SCALE, /* default, needs to be all-bits-zero for g_new0
				*/
	POLO_IMAGE_FILL_TILE
} PoloImageFillType;

typedef enum {
	POLO_COLOR_SPEC_BASIC,
	POLO_COLOR_SPEC_GTK,
	POLO_COLOR_SPEC_GTK_CUSTOM,
	POLO_COLOR_SPEC_BLEND,
	POLO_COLOR_SPEC_SHADE
} PoloColorSpecType;

typedef enum {
	POLO_GTK_COLOR_FG,
	POLO_GTK_COLOR_BG,
	POLO_GTK_COLOR_LIGHT,
	POLO_GTK_COLOR_DARK,
	POLO_GTK_COLOR_MID,
	POLO_GTK_COLOR_TEXT,
	POLO_GTK_COLOR_BASE,
	POLO_GTK_COLOR_TEXT_AA,
	POLO_GTK_COLOR_LAST
} PoloGtkColorComponent;

struct _PoloColorSpec {
	PoloColorSpecType type;
	union {
		struct {
			PoloRGBA color;
		} basic;
		struct {
			PoloGtkColorComponent component;
			PoloStateFlags state;
		} gtk;
		struct {
			char *color_name;
			PoloColorSpec *fallback;
		} gtkcustom;
		struct {
			PoloColorSpec *foreground;
			PoloColorSpec *background;
			double alpha;
			PoloRGBA color;
		} blend;
		struct {
			PoloColorSpec *base;
			double factor;
			PoloRGBA color;
		} shade;
	} data;
};

struct _PoloGradientSpec {
	PoloGradientType type;
	GSList *color_specs;
};

struct _PoloAlphaGradientSpec {
	PoloGradientType type;
	unsigned char *alphas;
	int n_alphas;
};

struct _PoloDrawInfo {
	GdkPixbuf *mini_icon;
	GdkPixbuf *icon;
	PangoLayout *title_layout;
	int title_layout_width;
	int title_layout_height;
	const PoloFrameGeometry *fgeom;
	PoloTheme *theme;
};

/**
 * A drawing operation in our simple vector drawing language.
 */
typedef enum {
	/** Basic drawing-- line */
	POLO_DRAW_LINE,
	/** Basic drawing-- rectangle */
	POLO_DRAW_RECTANGLE,
	/** Basic drawing-- arc */
	POLO_DRAW_ARC,

	/** Clip to a rectangle */
	POLO_DRAW_CLIP,

	/* Texture thingies */

	/** Just a filled rectangle with alpha */
	POLO_DRAW_TINT,
	POLO_DRAW_GRADIENT,
	POLO_DRAW_IMAGE,

	/** GTK theme engine stuff */
	POLO_DRAW_GTK_ARROW,
	POLO_DRAW_GTK_BOX,
	POLO_DRAW_GTK_VLINE,

	/** App's window icon */
	POLO_DRAW_ICON,
	/** App's window title */
	POLO_DRAW_TITLE,
	/** a draw op list */
	POLO_DRAW_OP_LIST,
	/** tiled draw op list */
	POLO_DRAW_TILE
} PoloDrawType;

typedef enum {
	POS_TOKEN_INT,
	POS_TOKEN_DOUBLE,
	POS_TOKEN_OPERATOR,
	POS_TOKEN_VARIABLE,
	POS_TOKEN_OPEN_PAREN,
	POS_TOKEN_CLOSE_PAREN
} PosTokenType;

typedef enum {
	POS_OP_NONE,
	POS_OP_ADD,
	POS_OP_SUBTRACT,
	POS_OP_MULTIPLY,
	POS_OP_DIVIDE,
	POS_OP_MOD,
	POS_OP_MAX,
	POS_OP_MIN
} PosOperatorType;

/**
 * A token, as output by the tokeniser.
 *
 * \ingroup tokenizer
 */
typedef struct {
	PosTokenType type;

	union {
		struct {
			int val;
		} i;

		struct {
			double val;
		} d;

		struct {
			PosOperatorType op;
		} o;

		struct {
			char *name;
			GQuark name_quark;
		} v;

	} d;
} PosToken;

/**
 * A computed expression in our simple vector drawing language.
 * While it appears to take the form of a tree, this is actually
 * merely a list; concerns such as precedence of operators are
 * currently recomputed on every recalculation.
 *
 * Created by polo_marco_draw_spec_new(), destroyed by
 * polo_marco_draw_spec_free(). pos_eval() fills this with ...FIXME. Are tokens
 * a tree or a list?
 * \ingroup parser
 */
typedef struct _PoloDrawSpec {
	/**
	 * If this spec is constant, this is the value of the constant;
	 * otherwise it is zero.
	 */
	int value;

	/** A list of tokens in the expression. */
	PosToken *tokens;

	/** How many tokens are in the tokens list. */
	int n_tokens;

	/** Does the expression contain any variables? */
	gboolean constant : 1;
} PoloDrawSpec;

/**
 * A single drawing operation in our simple vector drawing language.
 */
struct _PoloDrawOp {
	PoloDrawType type;

	/* Positions are strings because they can be expressions */
	union {
		struct {
			PoloColorSpec *color_spec;
			int dash_on_length;
			int dash_off_length;
			int width;
			PoloDrawSpec *x1;
			PoloDrawSpec *y1;
			PoloDrawSpec *x2;
			PoloDrawSpec *y2;
		} line;

		struct {
			PoloColorSpec *color_spec;
			gboolean filled;
			PoloDrawSpec *x;
			PoloDrawSpec *y;
			PoloDrawSpec *width;
			PoloDrawSpec *height;
		} rectangle;

		struct {
			PoloColorSpec *color_spec;
			gboolean filled;
			PoloDrawSpec *x;
			PoloDrawSpec *y;
			PoloDrawSpec *width;
			PoloDrawSpec *height;
			double start_angle;
			double extent_angle;
		} arc;

		struct {
			PoloDrawSpec *x;
			PoloDrawSpec *y;
			PoloDrawSpec *width;
			PoloDrawSpec *height;
		} clip;

		struct {
			PoloColorSpec *color_spec;
			PoloAlphaGradientSpec *alpha_spec;
			PoloDrawSpec *x;
			PoloDrawSpec *y;
			PoloDrawSpec *width;
			PoloDrawSpec *height;
		} tint;

		struct {
			PoloGradientSpec *gradient_spec;
			PoloAlphaGradientSpec *alpha_spec;
			PoloDrawSpec *x;
			PoloDrawSpec *y;
			PoloDrawSpec *width;
			PoloDrawSpec *height;
		} gradient;

		struct {
			PoloColorSpec *colorize_spec;
			PoloAlphaGradientSpec *alpha_spec;
			GdkPixbuf *pixbuf;
			PoloDrawSpec *x;
			PoloDrawSpec *y;
			PoloDrawSpec *width;
			PoloDrawSpec *height;

			guint32 colorize_cache_pixel;
			GdkPixbuf *colorize_cache_pixbuf;
			PoloImageFillType fill_type;
			unsigned int vertical_stripes : 1;
			unsigned int horizontal_stripes : 1;
		} image;

		struct {
			PoloStateFlags state;
			PoloShadowType shadow;
			PoloArrowType arrow;
			gboolean filled;

			PoloDrawSpec *x;
			PoloDrawSpec *y;
			PoloDrawSpec *width;
			PoloDrawSpec *height;
		} gtk_arrow;

		struct {
			PoloStateFlags state;
			PoloShadowType shadow;
			PoloDrawSpec *x;
			PoloDrawSpec *y;
			PoloDrawSpec *width;
			PoloDrawSpec *height;
		} gtk_box;

		struct {
			PoloStateFlags state;
			PoloDrawSpec *x;
			PoloDrawSpec *y1;
			PoloDrawSpec *y2;
		} gtk_vline;

		struct {
			PoloAlphaGradientSpec *alpha_spec;
			PoloDrawSpec *x;
			PoloDrawSpec *y;
			PoloDrawSpec *width;
			PoloDrawSpec *height;
			PoloImageFillType fill_type;
		} icon;

		struct {
			PoloColorSpec *color_spec;
			PoloDrawSpec *x;
			PoloDrawSpec *y;
			PoloDrawSpec *ellipsize_width;
		} title;

		struct {
			PoloDrawOpList *op_list;
			PoloDrawSpec *x;
			PoloDrawSpec *y;
			PoloDrawSpec *width;
			PoloDrawSpec *height;
		} op_list;

		struct {
			PoloDrawOpList *op_list;
			PoloDrawSpec *x;
			PoloDrawSpec *y;
			PoloDrawSpec *width;
			PoloDrawSpec *height;
			PoloDrawSpec *tile_xoffset;
			PoloDrawSpec *tile_yoffset;
			PoloDrawSpec *tile_width;
			PoloDrawSpec *tile_height;
		} tile;

	} data;
};

/**
 * A list of PoloDrawOp objects. Maintains a reference count.
 * Grows as necessary and allows the allocation of unused spaces
 * to keep reallocations to a minimum.
 *
 * \bug Do we really win anything from not using the equivalent
 * GLib structures?
 */
struct _PoloDrawOpList {
	int refcount;
	PoloDrawOp **ops;
	int n_ops;
	int n_allocated;
};

typedef enum {
	POLO_BUTTON_STATE_NORMAL,
	POLO_BUTTON_STATE_PRESSED,
	POLO_BUTTON_STATE_PRELIGHT,
	POLO_BUTTON_STATE_LAST
} PoloButtonState;

typedef enum {
	/* Ordered so that background is drawn first */
	POLO_BUTTON_TYPE_LEFT_LEFT_BACKGROUND,
	POLO_BUTTON_TYPE_LEFT_MIDDLE_BACKGROUND,
	POLO_BUTTON_TYPE_LEFT_RIGHT_BACKGROUND,
	POLO_BUTTON_TYPE_LEFT_SINGLE_BACKGROUND,
	POLO_BUTTON_TYPE_RIGHT_LEFT_BACKGROUND,
	POLO_BUTTON_TYPE_RIGHT_MIDDLE_BACKGROUND,
	POLO_BUTTON_TYPE_RIGHT_RIGHT_BACKGROUND,
	POLO_BUTTON_TYPE_RIGHT_SINGLE_BACKGROUND,
	POLO_BUTTON_TYPE_CLOSE,
	POLO_BUTTON_TYPE_MAXIMIZE,
	POLO_BUTTON_TYPE_MINIMIZE,
	POLO_BUTTON_TYPE_MENU,
	POLO_BUTTON_TYPE_APPMENU,
	POLO_BUTTON_TYPE_SHADE,
	POLO_BUTTON_TYPE_ABOVE,
	POLO_BUTTON_TYPE_STICK,
	POLO_BUTTON_TYPE_UNSHADE,
	POLO_BUTTON_TYPE_UNABOVE,
	POLO_BUTTON_TYPE_UNSTICK,
	POLO_BUTTON_TYPE_LAST
} PoloButtonType;

typedef enum {
	POLO_MENU_ICON_TYPE_CLOSE,
	POLO_MENU_ICON_TYPE_MAXIMIZE,
	POLO_MENU_ICON_TYPE_UNMAXIMIZE,
	POLO_MENU_ICON_TYPE_MINIMIZE,
	POLO_MENU_ICON_TYPE_LAST
} PoloMenuIconType;

typedef enum {
	/* Listed in the order in which the textures are drawn.
	 * (though this only matters for overlaps of course.)
	 * Buttons are drawn after the frame textures.
	 *
	 * On the corners, horizontal pieces are arbitrarily given the
	 * corner area:
	 *
	 *   =====                 |====
	 *   |                     |
	 *   |       rather than   |
	 *
	 */

	/* entire frame */
	POLO_FRAME_PIECE_ENTIRE_BACKGROUND,
	/* entire titlebar background */
	POLO_FRAME_PIECE_TITLEBAR,
	/* portion of the titlebar background inside the titlebar
	 * background edges
	 */
	POLO_FRAME_PIECE_TITLEBAR_MIDDLE,
	/* left end of titlebar */
	POLO_FRAME_PIECE_LEFT_TITLEBAR_EDGE,
	/* right end of titlebar */
	POLO_FRAME_PIECE_RIGHT_TITLEBAR_EDGE,
	/* top edge of titlebar */
	POLO_FRAME_PIECE_TOP_TITLEBAR_EDGE,
	/* bottom edge of titlebar */
	POLO_FRAME_PIECE_BOTTOM_TITLEBAR_EDGE,
	/* render over title background (text area) */
	POLO_FRAME_PIECE_TITLE,
	/* left edge of the frame */
	POLO_FRAME_PIECE_LEFT_EDGE,
	/* right edge of the frame */
	POLO_FRAME_PIECE_RIGHT_EDGE,
	/* bottom edge of the frame */
	POLO_FRAME_PIECE_BOTTOM_EDGE,
	/* place over entire frame, after drawing everything else */
	POLO_FRAME_PIECE_OVERLAY,
	/* Used to get size of the enum */
	POLO_FRAME_PIECE_LAST
} PoloFramePiece;

/**
 * How to draw a frame in a particular state (say, a focussed, non-maximised,
 * resizable frame). This corresponds closely to the <frame_style> tag
 * in a theme file.
 */
struct _PoloFrameStyle {
	/** Reference count. */
	int refcount;
	/**
	 * Parent style.
	 * Settings which are unspecified here will be taken from there.
	 */
	PoloFrameStyle *parent;
	/** Operations for drawing each kind of button in each state. */
	PoloDrawOpList *buttons[POLO_BUTTON_TYPE_LAST][POLO_BUTTON_STATE_LAST];
	/** Operations for drawing each piece of the frame. */
	PoloDrawOpList *pieces[POLO_FRAME_PIECE_LAST];
	/**
	 * Details such as the height and width of each edge, the corner
	 * rounding, and the aspect ratio of the buttons.
	 */
	PoloFrameLayout *layout;
	/**
	 * Background colour of the window. Only present in theme formats
	 * 2 and above. Can be NULL to use the standard GTK theme engine.
	 */
	PoloColorSpec *window_background_color;
	/**
	 * Transparency of the window background. 0=transparent; 255=opaque.
	 */
	guint8 window_background_alpha;
};

/* Kinds of frame...
 *
 *  normal ->   noresize / vert only / horz only / both
 *              focused / unfocused
 *  max    ->   focused / unfocused
 *  shaded ->   focused / unfocused
 *  max/shaded -> focused / unfocused
 *
 *  so 4 states with 8 sub-states in one, 2 sub-states in the other 3,
 *  meaning 14 total
 *
 * 14 window states times 7 or 8 window types. Except some
 * window types never get a frame so that narrows it down a bit.
 *
 */
typedef enum {
	POLO_FRAME_STATE_NORMAL,
	POLO_FRAME_STATE_MAXIMIZED,
	POLO_FRAME_STATE_TILED_LEFT,
	POLO_FRAME_STATE_TILED_RIGHT,
	POLO_FRAME_STATE_SHADED,
	POLO_FRAME_STATE_MAXIMIZED_AND_SHADED,
	POLO_FRAME_STATE_TILED_LEFT_AND_SHADED,
	POLO_FRAME_STATE_TILED_RIGHT_AND_SHADED,
	POLO_FRAME_STATE_LAST
} PoloFrameState;

typedef enum {
	POLO_FRAME_RESIZE_NONE,
	POLO_FRAME_RESIZE_VERTICAL,
	POLO_FRAME_RESIZE_HORIZONTAL,
	POLO_FRAME_RESIZE_BOTH,
	POLO_FRAME_RESIZE_LAST
} PoloFrameResize;

typedef enum {
	POLO_FRAME_FOCUS_NO,
	POLO_FRAME_FOCUS_YES,
	POLO_FRAME_FOCUS_LAST
} PoloFrameFocus;

/**
 * How to draw frames at different times: when it's maximised or not, shaded
 * or not, when it's focussed or not, and (for non-maximised windows), when
 * it can be horizontally or vertically resized, both, or neither.
 * Not all window types actually get a frame.
 *
 * A theme contains one of these objects for each type of window (each
 * PoloFrameType), that is, normal, dialogue (modal and non-modal), etc.
 *
 * This corresponds closely to the <frame_style_set> tag in a theme file.
 */
struct _PoloFrameStyleSet {
	int refcount;
	PoloFrameStyleSet *parent;
	PoloFrameStyle
		*normal_styles[POLO_FRAME_RESIZE_LAST][POLO_FRAME_FOCUS_LAST];
	PoloFrameStyle *maximized_styles[POLO_FRAME_FOCUS_LAST];
	PoloFrameStyle *tiled_left_styles[POLO_FRAME_FOCUS_LAST];
	PoloFrameStyle *tiled_right_styles[POLO_FRAME_FOCUS_LAST];
	PoloFrameStyle
		*shaded_styles[POLO_FRAME_RESIZE_LAST][POLO_FRAME_FOCUS_LAST];
	PoloFrameStyle *maximized_and_shaded_styles[POLO_FRAME_FOCUS_LAST];
	PoloFrameStyle *tiled_left_and_shaded_styles[POLO_FRAME_FOCUS_LAST];
	PoloFrameStyle *tiled_right_and_shaded_styles[POLO_FRAME_FOCUS_LAST];
};

/**
 * A theme. This is a singleton class which groups all settings from a theme
 * on disk together.
 *
 * \bug It is rather useless to keep the metadata fields in core, I think.
 */
struct _PoloTheme {
	/** Name of the theme (on disk), e.g. "Crux" */
	char *name;
	/** Path to the files associated with the theme */
	char *dirname;
	/**
	 * Filename of the XML theme file.
	 * \bug Kept lying around for no discernable reason.
	 */
	char *filename;
	/** Metadata: Human-readable name of the theme. */
	char *readable_name;
	/** Metadata: Author of the theme. */
	char *author;
	/** Metadata: Copyright holder. */
	char *copyright;
	/** Metadata: Date of the theme. */
	char *date;
	/** Metadata: Description of the theme. */
	char *description;
	/** Version of the theme format. Older versions cannot use the features
	 * of newer versions even if they think they can (this is to allow
	 * forward and backward compatibility.
	 */
	guint format_version;

	/** Symbol table of integer constants. */
	GHashTable *integer_constants;
	/** Symbol table of float constants. */
	GHashTable *float_constants;
	/**
	 * Symbol table of colour constants (hex triples, and triples
	 * plus alpha).
	 * */
	GHashTable *color_constants;
	GHashTable *images_by_filename;
	GHashTable *layouts_by_name;
	GHashTable *draw_op_lists_by_name;
	GHashTable *styles_by_name;
	GHashTable *style_sets_by_name;
	PoloFrameStyleSet *style_sets_by_type[POLO_FRAME_TYPE_LAST];

	GQuark quark_width;
	GQuark quark_height;
	GQuark quark_object_width;
	GQuark quark_object_height;
	GQuark quark_left_width;
	GQuark quark_right_width;
	GQuark quark_top_height;
	GQuark quark_bottom_height;
	GQuark quark_mini_icon_width;
	GQuark quark_mini_icon_height;
	GQuark quark_icon_width;
	GQuark quark_icon_height;
	GQuark quark_title_width;
	GQuark quark_title_height;
	GQuark quark_frame_x_center;
	GQuark quark_frame_y_center;
};

struct _PoloPositionExprEnv {
	PoloRect rect;
	/* size of an object being drawn, if it has a natural size */
	int object_width;
	int object_height;
	/* global object sizes, always available */
	int left_width;
	int right_width;
	int top_height;
	int bottom_height;
	int title_width;
	int title_height;
	int frame_x_center;
	int frame_y_center;
	int mini_icon_width;
	int mini_icon_height;
	int icon_width;
	int icon_height;
	/* Theme so we can look up constants */
	PoloTheme *theme;
};

PoloFrameLayout *
polo_marco_frame_layout_new(void);
PoloFrameLayout *
polo_marco_frame_layout_copy(const PoloFrameLayout *src);
void
polo_marco_frame_layout_ref(PoloFrameLayout *layout);
void
polo_marco_frame_layout_unref(PoloFrameLayout *layout);
void
polo_marco_frame_layout_get_borders(const PoloFrameLayout *layout,
				    int text_height, PoloFrameFlags flags,
				    PoloFrameType type,
				    PoloThemeBorders *borders);
void
polo_marco_frame_layout_calc_geometry(const PoloFrameLayout *layout,
				      int text_height, PoloFrameFlags flags,
				      int client_width, int client_height,
				      const PoloButtonLayout *button_layout,
				      PoloFrameGeometry *fgeom,
				      PoloTheme *theme);

gboolean
polo_marco_frame_layout_validate(const PoloFrameLayout *layout, GError **error);

gboolean
polo_marco_parse_position_expression(PoloDrawSpec *spec,
				     const PoloPositionExprEnv *env,
				     int *x_return, int *y_return,
				     GError **err);
gboolean
polo_marco_parse_size_expression(PoloDrawSpec *spec,
				 const PoloPositionExprEnv *env,
				 int *val_return, GError **err);

PoloDrawSpec *
polo_marco_draw_spec_new(PoloTheme *theme, const char *expr, GError **error);
void
polo_marco_draw_spec_free(PoloDrawSpec *spec);

PoloColorSpec *
polo_marco_color_spec_new(PoloColorSpecType type);
PoloColorSpec *
polo_marco_color_spec_new_from_string(const char *str, GError **err);
PoloColorSpec *
polo_marco_color_spec_new_gtk(PoloGtkColorComponent component,
			      PoloStateFlags state);
void
polo_marco_color_spec_free(PoloColorSpec *spec);

PoloDrawOp *
polo_marco_draw_op_new(PoloDrawType type);
void
polo_marco_draw_op_free(PoloDrawOp *op);

PoloDrawOpList *
polo_marco_draw_op_list_new(int n_preallocs);
void
polo_marco_draw_op_list_ref(PoloDrawOpList *op_list);
void
polo_marco_draw_op_list_unref(PoloDrawOpList *op_list);
void
polo_marco_draw_op_list_append(PoloDrawOpList *op_list, PoloDrawOp *op);
gboolean
polo_marco_draw_op_list_validate(PoloDrawOpList *op_list, GError **error);
gboolean
polo_marco_draw_op_list_contains(PoloDrawOpList *op_list,
				 PoloDrawOpList *child);

PoloGradientSpec *
polo_marco_gradient_spec_new(PoloGradientType type);
void
polo_marco_gradient_spec_free(PoloGradientSpec *desc);
gboolean
polo_marco_gradient_spec_validate(PoloGradientSpec *spec, GError **error);

PoloAlphaGradientSpec *
polo_marco_alpha_gradient_spec_new(PoloGradientType type, int n_alphas);
void
polo_marco_alpha_gradient_spec_free(PoloAlphaGradientSpec *spec);

PoloFrameStyle *
polo_marco_frame_style_new(PoloFrameStyle *parent);
void
polo_marco_frame_style_ref(PoloFrameStyle *style);
void
polo_marco_frame_style_unref(PoloFrameStyle *style);

gboolean
polo_marco_frame_style_validate(PoloFrameStyle *style,
				guint current_theme_version, GError **error);

PoloFrameStyleSet *
polo_marco_frame_style_set_new(PoloFrameStyleSet *parent);
void
polo_marco_frame_style_set_ref(PoloFrameStyleSet *style_set);
void
polo_marco_frame_style_set_unref(PoloFrameStyleSet *style_set);

gboolean
polo_marco_frame_style_set_validate(PoloFrameStyleSet *style_set,
				    GError **error);

PoloTheme *
polo_marco_theme_new(void);
void
polo_marco_theme_free(PoloTheme *theme);

void
polo_marco_color_spec_render(PoloColorSpec *spec, PoloThemeColors *style,
			     PoloRGBA *color);
void
polo_marco_draw_op_draw_with_style(const PoloDrawOp *op,
				   PoloThemeColors *style_gtk, cairo_t *cr,
				   const PoloDrawInfo *info,
				   PoloRect logical_region);
void
polo_marco_draw_op_list_draw_with_style(const PoloDrawOpList *op_list,
					PoloThemeColors *style_gtk, cairo_t *cr,
					const PoloDrawInfo *info,
					PoloRect rect);
void
polo_marco_frame_style_draw_with_style(
	PoloTheme *theme, PoloFrameStyle *style, PoloThemeColors *style_gtk,
	cairo_t *cr, const PoloFrameGeometry *fgeom, int client_width,
	int client_height, PangoLayout *title_layout, int text_height,
	PoloButtonState button_states[POLO_BUTTON_TYPE_LAST],
	GdkPixbuf *mini_icon, GdkPixbuf *icon);
void
polo_marco_theme_draw_frame(
	PoloTheme *theme, PoloThemeColors *style_gtk, cairo_t *cr,
	PoloFrameType type, PoloFrameFlags flags, int client_width,
	int client_height, PangoLayout *title_layout, int text_height,
	const PoloButtonLayout *button_layout,
	PoloButtonState button_states[POLO_BUTTON_TYPE_LAST],
	GdkPixbuf *mini_icon, GdkPixbuf *icon);
double
polo_marco_theme_get_title_scale(PoloTheme *theme, PoloFrameType type,
				 PoloFrameFlags flags);
void
polo_marco_gradient_spec_render(const PoloGradientSpec *spec,
				const PoloAlphaGradientSpec *alpha_spec,
				cairo_t *cr, PoloThemeColors *context, gint x,
				gint y, gint width, gint height);
cairo_pattern_t *
polo_marco_alpha_gradient_spec_get_mask(const PoloAlphaGradientSpec *spec);
gboolean
polo_marco_theme_validate(PoloTheme *theme, GError **error);
GdkPixbuf *
polo_marco_theme_load_image(PoloTheme *theme, const char *filename,
			    guint size_of_theme_icons, GError **error);

PoloFrameStyle *
polo_marco_theme_get_frame_style(PoloTheme *theme, PoloFrameType type,
				 PoloFrameFlags flags);

void
polo_marco_theme_get_frame_borders(PoloTheme *theme, PoloFrameType type,
				   int text_height, PoloFrameFlags flags,
				   PoloThemeBorders *borders);
void
polo_marco_theme_calc_geometry(PoloTheme *theme, PoloFrameType type,
			       int text_height, PoloFrameFlags flags,
			       int client_width, int client_height,
			       const PoloButtonLayout *button_layout,
			       PoloFrameGeometry *fgeom);

PoloFrameLayout *
polo_marco_theme_lookup_layout(PoloTheme *theme, const char *name);
void
polo_marco_theme_insert_layout(PoloTheme *theme, const char *name,
			       PoloFrameLayout *layout);
PoloDrawOpList *
polo_marco_theme_lookup_draw_op_list(PoloTheme *theme, const char *name);
void
polo_marco_theme_insert_draw_op_list(PoloTheme *theme, const char *name,
				     PoloDrawOpList *op_list);
PoloFrameStyle *
polo_marco_theme_lookup_style(PoloTheme *theme, const char *name);
void
polo_marco_theme_insert_style(PoloTheme *theme, const char *name,
			      PoloFrameStyle *style);
PoloFrameStyleSet *
polo_marco_theme_lookup_style_set(PoloTheme *theme, const char *name);
void
polo_marco_theme_insert_style_set(PoloTheme *theme, const char *name,
				  PoloFrameStyleSet *style_set);
gboolean
polo_marco_theme_define_int_constant(PoloTheme *theme, const char *name,
				     int value, GError **error);
gboolean
polo_marco_theme_lookup_int_constant(PoloTheme *theme, const char *name,
				     int *value);
gboolean
polo_marco_theme_define_float_constant(PoloTheme *theme, const char *name,
				       double value, GError **error);
gboolean
polo_marco_theme_lookup_float_constant(PoloTheme *theme, const char *name,
				       double *value);

gboolean
polo_marco_theme_define_color_constant(PoloTheme *theme, const char *name,
				       const char *value, GError **error);
gboolean
polo_marco_theme_lookup_color_constant(PoloTheme *theme, const char *name,
				       char **value);

gboolean
polo_marco_theme_replace_constants(PoloTheme *theme, PosToken *tokens,
				   int n_tokens, GError **err);
int
polo_marco_pango_font_desc_get_text_height(
	const PangoFontDescription *font_desc, PangoContext *context);

/* Enum converters */
PoloGtkColorComponent
polo_marco_color_component_from_string(const char *str);
const char *
polo_marco_color_component_to_string(PoloGtkColorComponent component);
PoloButtonState
polo_marco_button_state_from_string(const char *str);
const char *
polo_marco_button_state_to_string(PoloButtonState state);
PoloButtonType
polo_marco_button_type_from_string(const char *str, PoloTheme *theme);
const char *
polo_marco_button_type_to_string(PoloButtonType type);
PoloFramePiece
polo_marco_frame_piece_from_string(const char *str);
const char *
polo_marco_frame_piece_to_string(PoloFramePiece piece);
PoloFrameState
polo_marco_frame_state_from_string(const char *str);
const char *
polo_marco_frame_state_to_string(PoloFrameState state);
PoloFrameResize
polo_marco_frame_resize_from_string(const char *str);
const char *
polo_marco_frame_resize_to_string(PoloFrameResize resize);
PoloFrameFocus
polo_marco_frame_focus_from_string(const char *str);
const char *
polo_marco_frame_focus_to_string(PoloFrameFocus focus);
PoloFrameType
polo_marco_frame_type_from_string(const char *str);
const char *
polo_marco_frame_type_to_string(PoloFrameType type);
PoloGradientType
polo_marco_gradient_type_from_string(const char *str);
const char *
polo_marco_gradient_type_to_string(PoloGradientType type);
PoloStateFlags
polo_marco_gtk_state_from_string(const char *str);
PoloShadowType
polo_marco_gtk_shadow_from_string(const char *str);
const char *
polo_marco_gtk_shadow_to_string(PoloShadowType shadow);
PoloArrowType
polo_marco_gtk_arrow_from_string(const char *str);
const char *
polo_marco_gtk_arrow_to_string(PoloArrowType arrow);
PoloImageFillType
polo_marco_image_fill_type_from_string(const char *str);
const char *
polo_marco_image_fill_type_to_string(PoloImageFillType fill_type);

guint
polo_marco_theme_earliest_version_with_button(PoloButtonType type);

#define POLO_THEME_ALLOWS(theme, feature) (theme->format_version >= feature)

/* What version of the theme file format were various features introduced in? */
#define POLO_THEME_SHADE_STICK_ABOVE_BUTTONS 2
#define POLO_THEME_UBIQUITOUS_CONSTANTS 2
#define POLO_THEME_VARIED_ROUND_CORNERS 2
#define POLO_THEME_IMAGES_FROM_ICON_THEMES 2
#define POLO_THEME_UNRESIZABLE_SHADED_STYLES 2
#define POLO_THEME_DEGREES_IN_ARCS 2
#define POLO_THEME_HIDDEN_BUTTONS 2
#define POLO_THEME_COLOR_CONSTANTS 2
#define POLO_THEME_FRAME_BACKGROUNDS 2

#endif
