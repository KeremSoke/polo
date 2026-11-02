/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Simple box operations */

/*
 * Copyright (C) 2005, 2006 Elijah Newren
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

#ifndef POLO_MARCO_BOXES_H
#define POLO_MARCO_BOXES_H

#include <glib.h>
/* Marco's common.h enums are in polo-marco.h. */

typedef enum
{
  POLO_EDGE_WINDOW,
  POLO_EDGE_XINERAMA,
  POLO_EDGE_SCREEN
} PoloEdgeType;

typedef struct _PoloRect PoloRect;
struct _PoloRect
{
  int x;
  int y;
  int width;
  int height;
};

typedef struct _PoloStrut PoloStrut;
struct _PoloStrut
{
  PoloRect rect;
  PoloSide side;
  PoloEdgeType edge;
};

#define POLO_BOX_LEFT(box)    ((box).x)                /* Leftmost pixel of rect */
#define POLO_BOX_RIGHT(box)   ((box).x + (box).width)  /* One pixel past right   */
#define POLO_BOX_TOP(box)     ((box).y)                /* Topmost pixel of rect  */
#define POLO_BOX_BOTTOM(box)  ((box).y + (box).height) /* One pixel past bottom  */

typedef enum
{
  POLO_FIXED_DIRECTION_NONE = 0,
  POLO_FIXED_DIRECTION_X    = 1 << 0,
  POLO_FIXED_DIRECTION_Y    = 1 << 1,
} PoloFixedDirections;

typedef struct _PoloEdge PoloEdge;
struct _PoloEdge
{
  PoloRect rect;      /* width or height should be 1 */
  PoloSide side_type;
  PoloEdgeType  edge_type;
};

/* Output functions -- note that the output buffer had better be big enough:
 *   rect_to_string:   POLO_RECT_LENGTH
 *   region_to_string: (POLO_RECT_LENGTH+strlen(separator_string)) *
 *                     g_list_length (region)
 *   edge_to_string:   POLO_EDGE_LENGTH
 *   edge_list_to_...: (POLO_EDGE_LENGTH+strlen(separator_string)) *
 *                     g_list_length (edge_list)
 */
#define POLO_RECT_LENGTH 27
#define POLO_EDGE_LENGTH 37
char* polo_marco_rect_to_string        (const PoloRect *rect,
                                       char                *output);
char* polo_marco_rect_region_to_string (GList               *region,
                                       const char          *separator_string,
                                       char                *output);
char* polo_marco_rect_edge_to_string   (const PoloEdge      *edge,
                                       char                *output);
char* polo_marco_rect_edge_list_to_string (
                                       GList               *edge_list,
                                       const char          *separator_string,
                                       char                *output);

/* Function to make initializing a rect with a single line of code easy */
PoloRect                 polo_marco_rect (int x, int y, int width, int height);

/* Basic comparison functions */
int      polo_marco_rect_area            (const PoloRect *rect);
gboolean polo_marco_rect_intersect       (const PoloRect *src1,
                                         const PoloRect *src2,
                                         PoloRect       *dest);
gboolean polo_marco_rect_equal           (const PoloRect *src1,
                                         const PoloRect *src2);

/* Find the bounding box of the union of two rectangles */
void     polo_marco_rect_union           (const PoloRect *rect1,
                                         const PoloRect *rect2,
                                         PoloRect       *dest);

/* overlap is similar to intersect but doesn't provide location of
 * intersection information.
 */
gboolean polo_marco_rect_overlap         (const PoloRect *rect1,
                                         const PoloRect *rect2);

/* vert_overlap means ignore the horizontal location and ask if the
 * vertical parts overlap.  An alternate way to think of it is "Does there
 * exist a way to shift either rect horizontally so that the two rects
 * overlap?"  horiz_overlap is similar.
 */
gboolean polo_marco_rect_vert_overlap    (const PoloRect *rect1,
                                         const PoloRect *rect2);
gboolean polo_marco_rect_horiz_overlap   (const PoloRect *rect1,
                                         const PoloRect *rect2);

/* could_fit_rect determines whether "outer_rect" is big enough to contain
 * inner_rect.  contains_rect checks whether it actually contains it.
 */
gboolean polo_marco_rect_could_fit_rect  (const PoloRect *outer_rect,
                                         const PoloRect *inner_rect);
gboolean polo_marco_rect_contains_rect   (const PoloRect *outer_rect,
                                         const PoloRect *inner_rect);

/* Resize old_rect to the given new_width and new_height, but store the
 * result in rect.  NOTE THAT THIS IS RESIZE ONLY SO IT CANNOT BE USED FOR
 * A MOVERESIZE OPERATION (that simplies the routine a little bit as it
 * means there's no difference between POLO_GRAVITY_NORTH_WEST and POLO_GRAVITY_STATIC.
 * Also, I lied a little bit--technically, you could use it in a MoveResize
 * operation if you muck with old_rect just right).
 */
void polo_marco_rect_resize_with_gravity (const PoloRect *old_rect,
                                         PoloRect       *rect,
                                         int                  gravity,
                                         int                  new_width,
                                         int                  new_height);

/* find a list of rectangles with the property that a window is contained
 * in the given region if and only if it is contained in one of the
 * rectangles in the list.
 *
 * In this case, the region is given by taking basic_rect, removing from
 * it the intersections with all the rectangles in the all_struts list,
 * then expanding all the rectangles in the resulting list by the given
 * amounts on each side.
 *
 * See boxes.c for more details.
 */
GList*   polo_marco_rect_get_minimal_spanning_set_for_region (
                                         const PoloRect *basic_rect,
                                         const GSList        *all_struts,
                                         gboolean             skip_middle_struts);

/* Expand all rectangles in region by the given amount on each side */
GList*   polo_marco_rect_expand_region   (GList               *region,
                                         const int            left_expand,
                                         const int            right_expand,
                                         const int            top_expand,
                                         const int            bottom_expand);
/* Same as for polo_marco_rect_expand_region except that rectangles not at
 * least min_x or min_y in size are not expanded in that direction
 */
GList*   polo_marco_rect_expand_region_conditionally (
                                         GList                *region,
                                         const int            left_expand,
                                         const int            right_expand,
                                         const int            top_expand,
                                         const int            bottom_expand,
                                         const int            min_x,
                                         const int            min_y);

/* Expand rect in direction to the size of expand_to, and then clip out any
 * overlapping struts oriented orthognal to the expansion direction.  (Think
 * horizontal or vertical maximization)
 */
void     polo_marco_rect_expand_to_avoiding_struts (
                                         PoloRect       *rect,
                                         const PoloRect *expand_to,
                                         const PoloDirection  direction,
                                         const GSList        *all_struts);

/* could_fit_in_region determines whether one of the spanning_rects is
 * big enough to contain rect.  contained_in_region checks whether one
 * actually contains it.
 */
gboolean polo_marco_rect_could_fit_in_region (
                                         const GList         *spanning_rects,
                                         const PoloRect *rect);
gboolean polo_marco_rect_contained_in_region (
                                         const GList         *spanning_rects,
                                         const PoloRect *rect);
gboolean polo_marco_rect_overlaps_with_region (
                                         const GList         *spanning_rects,
                                         const PoloRect *rect);

/* Make the rectangle small enough to fit into one of the spanning_rects,
 * but make it no smaller than min_size.
 */
void     polo_marco_rect_clamp_to_fit_into_region (
                                         const GList         *spanning_rects,
                                         PoloFixedDirections      fixed_directions,
                                         PoloRect       *rect,
                                         const PoloRect *min_size);

/* Clip the rectangle so that it fits into one of the spanning_rects, assuming
 * it overlaps with at least one of them
 */
void     polo_marco_rect_clip_to_region  (const GList         *spanning_rects,
                                         PoloFixedDirections      fixed_directions,
                                         PoloRect       *rect);

/* Shove the rectangle into one of the spanning_rects, assuming it fits in
 * one of them.
 */
void     polo_marco_rect_shove_into_region(
                                         const GList         *spanning_rects,
                                         PoloFixedDirections      fixed_directions,
                                         PoloRect       *rect);

/* Finds the point on the line connecting (x1,y1) to (x2,y2) which is closest
 * to (px, py).  Useful for finding an optimal rectangle size when given a
 * range between two sizes that are all candidates.
 */
void polo_marco_rect_find_linepoint_closest_to_point (double x1,    double y1,
                                                     double x2,    double y2,
                                                     double px,    double py,
                                                     double *valx, double *valy);

/***************************************************************************/
/*                                                                         */
/* Switching gears to code for edges instead of just rectangles            */
/*                                                                         */
/***************************************************************************/

/* Return whether an edge overlaps or is adjacent to the rectangle in the
 * nonzero-width dimension of the edge.
 */
gboolean polo_marco_rect_edge_aligns (const PoloRect *rect,
                                     const PoloEdge      *edge);

/* Compare two edges, so that sorting functions can put a list of edges in
 * canonical order.
 */
gint   polo_marco_rect_edge_cmp (gconstpointer a, gconstpointer b);

/* Compare two edges, so that sorting functions can put a list of edges in
 * order.  This function doesn't separate left edges first, then right edges,
 * etc., but rather compares only upon location.
 */
gint   polo_marco_rect_edge_cmp_ignore_type (gconstpointer a, gconstpointer b);

/* Removes an parts of edges in the given list that intersect any box in the
 * given rectangle list.  Returns the result.
 */
GList* polo_marco_rect_remove_intersections_with_boxes_from_edges (
                                           GList *edges,
                                           const GSList *rectangles);

/* Finds all the edges of an onscreen region, returning a GList* of
 * MetaEdgeRect's.
 */
GList* polo_marco_rect_find_onscreen_edges (const PoloRect *basic_rect,
                                           const GSList        *all_struts);

/* Finds edges between adjacent xineramas which are not covered by the given
 * struts.
 */
GList* polo_marco_rect_find_nonintersected_xinerama_edges (
                                           const PoloRect *screen_rect,
					   const GList         *xinerama_rects,
                                           const GSList        *all_struts);

#endif /* POLO_MARCO_BOXES_H */
