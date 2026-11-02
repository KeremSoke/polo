/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Edge resistance for move/resize operations */

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
 *
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Ported from Marco 1.28.2 src/core/edge-resistance.c. The display's grab
 * state is replaced by PoloEdgeResistanceData, built from the caller's
 * stacking list and the workspace's screen and xinerama edges, and the
 * window by its outer rect and size increments. Marco's resistance
 * timeouts are all 0 ms, so their timeout code can never run and is left
 * out.
 */

#include <limits.h>
#include <stdlib.h>

#include "polo-marco.h"

struct ResistanceDataForAnEdge
{
  int          keyboard_buildup;
};
typedef struct ResistanceDataForAnEdge ResistanceDataForAnEdge;

struct _PoloEdgeResistanceData
{
  GArray *left_edges;
  GArray *right_edges;
  GArray *top_edges;
  GArray *bottom_edges;

  ResistanceDataForAnEdge left_data;
  ResistanceDataForAnEdge right_data;
  ResistanceDataForAnEdge top_data;
  ResistanceDataForAnEdge bottom_data;

  GList *workspace_edges;
};

/* !WARNING!: this function can return invalid indices (namely, either -1 or
 * edges->len); this is by design, but you need to remember this.
 */
static int
find_index_of_edge_near_position (const GArray *edges,
                                  int           position,
                                  gboolean      want_interval_min,
                                  gboolean      horizontal)
{
  /* This is basically like a binary search, except that we're trying to
   * find a range instead of an exact value.  So, if we have in our array
   *   Value: 3  27 316 316 316 505 522 800 1213
   *   Index: 0   1   2   3   4   5   6   7    8
   * and we call this function with position=500 & want_interval_min=TRUE
   * then we should get 5 (because 505 is the first value bigger than 500).
   * If we call this function with position=805 and want_interval_min=FALSE
   * then we should get 7 (because 800 is the last value smaller than 800).
   * A couple more, to make things clear:
   *    position  want_interval_min  correct_answer
   *         316               TRUE               2
   *         316              FALSE               4
   *           2              FALSE              -1
   *        2000               TRUE               9
   */
  int low, high, mid;
  int compare;
  PoloEdge *edge;

  /* Initialize mid, edge, & compare in the off change that the array only
   * has one element.
   */
  mid  = 0;
  edge = g_array_index (edges, PoloEdge*, mid);
  compare = horizontal ? edge->rect.x : edge->rect.y;

  /* Begin the search... */
  low  = 0;
  high = edges->len - 1;
  while (low < high)
    {
      mid = low + (high - low)/2;
      edge = g_array_index (edges, PoloEdge*, mid);
      compare = horizontal ? edge->rect.x : edge->rect.y;

      if (compare == position)
        break;

      if (compare > position)
        high = mid - 1;
      else
        low = mid + 1;
    }

  /* mid should now be _really_ close to the index we want, so we start
   * linearly searching.  However, note that we don't know if mid is less
   * than or greater than what we need and it's possible that there are
   * several equal values equal to what we were searching for and we ended
   * up in the middle of them instead of at the end.  So we may need to
   * move mid multiple locations over.
   */
  if (want_interval_min)
    {
      while (compare >= position && mid > 0)
        {
          mid--;
          edge = g_array_index (edges, PoloEdge*, mid);
          compare = horizontal ? edge->rect.x : edge->rect.y;
        }
      while (compare < position && mid < (int)edges->len - 1)
        {
          mid++;
          edge = g_array_index (edges, PoloEdge*, mid);
          compare = horizontal ? edge->rect.x : edge->rect.y;
        }

      /* Special case for no values in array big enough */
      if (compare < position)
        return edges->len;

      /* Return the found value */
      return mid;
    }
  else
    {
      while (compare <= position && mid < (int)edges->len - 1)
        {
          mid++;
          edge = g_array_index (edges, PoloEdge*, mid);
          compare = horizontal ? edge->rect.x : edge->rect.y;
        }
      while (compare > position && mid > 0)
        {
          mid--;
          edge = g_array_index (edges, PoloEdge*, mid);
          compare = horizontal ? edge->rect.x : edge->rect.y;
        }

      /* Special case for no values in array small enough */
      if (compare > position)
        return -1;

      /* Return the found value */
      return mid;
    }
}

static gboolean
points_on_same_side (int ref, int pt1, int pt2)
{
  return (pt1 - ref) * (pt2 - ref) > 0;
}

static int
find_nearest_position (const GArray        *edges,
                       int                  position,
                       int                  old_position,
                       const PoloRect *new_rect,
                       gboolean             horizontal,
                       gboolean             only_forward)
{
  /* This is basically just a binary search except that we're looking
   * for the value closest to position, rather than finding that
   * actual value.  Also, we ignore any edges that aren't relevant
   * given the horizontal/vertical position of new_rect.
   */
  int low, high, mid;
  int compare;
  PoloEdge *edge;
  int best, best_dist, i;
  gboolean edges_align;

  /* Initialize mid, edge, & compare in the off change that the array only
   * has one element.
   */
  mid  = 0;
  edge = g_array_index (edges, PoloEdge*, mid);
  compare = horizontal ? edge->rect.x : edge->rect.y;

  /* Begin the search... */
  low  = 0;
  high = edges->len - 1;
  while (low < high)
    {
      mid = low + (high - low)/2;
      edge = g_array_index (edges, PoloEdge*, mid);
      compare = horizontal ? edge->rect.x : edge->rect.y;

      if (compare == position)
        break;

      if (compare > position)
        high = mid - 1;
      else
        low = mid + 1;
    }

  /* mid should now be _really_ close to the index we want, so we
   * start searching nearby for something that overlaps and is closer
   * than the original position.
   */
  best = old_position;
  best_dist = INT_MAX;

  /* Start the search at mid */
  edge = g_array_index (edges, PoloEdge*, mid);
  compare = horizontal ? edge->rect.x : edge->rect.y;
  edges_align = polo_marco_rect_edge_aligns (new_rect, edge);
  if (edges_align &&
      (!only_forward || !points_on_same_side (position, compare, old_position)))
    {
      int dist = ABS (compare - position);
      if (dist < best_dist)
        {
          best = compare;
          best_dist = dist;
        }
    }

  /* Now start searching higher than mid */
  for (i = mid + 1; i < (int)edges->len; i++)
    {
      edge = g_array_index (edges, PoloEdge*, i);
      compare = horizontal ? edge->rect.x : edge->rect.y;

      edges_align = horizontal ?
        polo_marco_rect_vert_overlap (&edge->rect, new_rect) :
        polo_marco_rect_horiz_overlap (&edge->rect, new_rect);

      if (edges_align &&
          (!only_forward ||
           !points_on_same_side (position, compare, old_position)))
        {
          int dist = ABS (compare - position);
          if (dist < best_dist)
            {
              best = compare;
              best_dist = dist;
            }
          break;
        }
    }

  /* Now start searching lower than mid */
  for (i = mid-1; i >= 0; i--)
    {
      edge = g_array_index (edges, PoloEdge*, i);
      compare = horizontal ? edge->rect.x : edge->rect.y;

      edges_align = horizontal ?
        polo_marco_rect_vert_overlap (&edge->rect, new_rect) :
        polo_marco_rect_horiz_overlap (&edge->rect, new_rect);

      if (edges_align &&
          (!only_forward ||
           !points_on_same_side (position, compare, old_position)))
        {
          int dist = ABS (compare - position);
          if (dist < best_dist)
            {
              best = compare;
            }
          break;
        }
    }

  /* Return the best one found */
  return best;
}

static gboolean
movement_towards_edge (PoloSide side, int increment)
{
  switch (side)
    {
    case POLO_SIDE_LEFT:
    case POLO_SIDE_TOP:
      return increment < 0;
    case POLO_SIDE_RIGHT:
    case POLO_SIDE_BOTTOM:
      return increment > 0;
    default:
      g_assert_not_reached ();
    }
}

static int
apply_edge_resistance (int                        old_pos,
                       int                        new_pos,
                       const PoloRect       *old_rect,
                       const PoloRect       *new_rect,
                       GArray                    *edges,
                       ResistanceDataForAnEdge   *resistance_data,
                       gboolean                   xdir,
                       gboolean                   keyboard_op)
{
  int i, begin, end;
  int last_edge;
  gboolean increasing = new_pos > old_pos;
  int      increment = increasing ? 1 : -1;

  const int PIXEL_DISTANCE_THRESHOLD_TOWARDS_WINDOW    = 16;
  const int PIXEL_DISTANCE_THRESHOLD_AWAYFROM_WINDOW   =  0;
  const int PIXEL_DISTANCE_THRESHOLD_TOWARDS_XINERAMA  = 32;
  const int PIXEL_DISTANCE_THRESHOLD_AWAYFROM_XINERAMA =  0;
  const int PIXEL_DISTANCE_THRESHOLD_TOWARDS_SCREEN    = 32;
  const int PIXEL_DISTANCE_THRESHOLD_AWAYFROM_SCREEN   =  0;

  /* Quit if no movement was specified */
  if (old_pos == new_pos)
    return new_pos;

  /* Get the range of indices in the edge array that we move past/to. */
  begin = find_index_of_edge_near_position (edges, old_pos,  increasing, xdir);
  end   = find_index_of_edge_near_position (edges, new_pos, !increasing, xdir);

  /* begin and end can be outside the array index, if the window is partially
   * off the screen
   */
  last_edge = edges->len - 1;
  begin = CLAMP (begin, 0, last_edge);
  end   = CLAMP (end,   0, last_edge);

  /* Loop over all these edges we're moving past/to. */
  i = begin;
  while ((increasing  && i <= end) ||
         (!increasing && i >= end))
    {
      gboolean  edges_align;
      PoloEdge *edge = g_array_index (edges, PoloEdge*, i);
      int       compare = xdir ? edge->rect.x : edge->rect.y;

      /* Find out if this edge is relevant */
      edges_align = polo_marco_rect_edge_aligns (new_rect, edge)  ||
                    polo_marco_rect_edge_aligns (old_rect, edge);

      /* Nothing to do unless the edges align */
      if (!edges_align)
        {
          /* Go to the next edge in the range */
          i += increment;
          continue;
        }

      /* Rest is easier to read if we split on keyboard vs. mouse op */
      if (keyboard_op)
        {
          if ((old_pos < compare && compare < new_pos) ||
              (old_pos > compare && compare > new_pos))
            return compare;
        }
      else /* mouse op */
        {
          int threshold;

          /* PIXEL DISTANCE MOUSE RESISTANCE: If the edge matters and the
           * user hasn't moved at least threshold pixels past this edge,
           * stop movement at this edge.  (Note that this is different from
           * keyboard resistance precisely because keyboard move ops are
           * relative to previous positions, whereas mouse move ops are
           * relative to differences in mouse position and mouse position
           * is an absolute quantity rather than a relative quantity)
           */

          /* First, determine the threshold */
          switch (edge->edge_type)
            {
            case POLO_EDGE_WINDOW:
              if (movement_towards_edge (edge->side_type, increment))
                threshold = PIXEL_DISTANCE_THRESHOLD_TOWARDS_WINDOW;
              else
                threshold = PIXEL_DISTANCE_THRESHOLD_AWAYFROM_WINDOW;
              break;
            case POLO_EDGE_XINERAMA:
              if (movement_towards_edge (edge->side_type, increment))
                threshold = PIXEL_DISTANCE_THRESHOLD_TOWARDS_XINERAMA;
              else
                threshold = PIXEL_DISTANCE_THRESHOLD_AWAYFROM_XINERAMA;
              break;
            case POLO_EDGE_SCREEN:
              if (movement_towards_edge (edge->side_type, increment))
                threshold = PIXEL_DISTANCE_THRESHOLD_TOWARDS_SCREEN;
              else
                threshold = PIXEL_DISTANCE_THRESHOLD_AWAYFROM_SCREEN;
              break;
            default:
              threshold = 0;
            }

          if (ABS (compare - new_pos) < threshold)
            return compare;
        }

      /* Go to the next edge in the range */
      i += increment;
    }

  return new_pos;
}

static int
apply_edge_snapping (int                  old_pos,
                     int                  new_pos,
                     const PoloRect *new_rect,
                     GArray              *edges,
                     gboolean             xdir,
                     gboolean             keyboard_op)
{
  int snap_to;

  if (old_pos == new_pos)
    return new_pos;

  snap_to = find_nearest_position (edges,
                                   new_pos,
                                   old_pos,
                                   new_rect,
                                   xdir,
                                   keyboard_op);

  /* If mouse snap-moving, the user could easily accidentally move just a
   * couple pixels in a direction they didn't mean to move; so ignore snap
   * movement in those cases unless it's only a small number of pixels
   * anyway.
   */
  if (!keyboard_op &&
      ABS (snap_to - old_pos) >= 8 &&
      ABS (new_pos - old_pos) < 8)
    return old_pos;
  else
    /* Otherwise, return the snapping position found */
    return snap_to;
}

/* This function takes the position (including any frame) of the window and
 * a proposed new position (ignoring edge resistance/snapping), and then
 * applies edge resistance to EACH edge (separately) updating new_outer.
 * It returns true if new_outer is modified, false otherwise.
 *
 * edge_data MUST already be setup or calling this function will cause a
 * crash.
 */
static gboolean
apply_edge_resistance_to_each_side (PoloEdgeResistanceData *edge_data,
                                    int                  width_inc,
                                    int                  height_inc,
                                    const PoloRect *old_outer,
                                    PoloRect       *new_outer,
                                    gboolean             auto_snap,
                                    gboolean             keyboard_op,
                                    gboolean             is_resize)
{
  PoloRect           modified_rect;
  gboolean                modified;
  int new_left, new_right, new_top, new_bottom;

  if (auto_snap)
    {
      /* Do the auto snapping instead of normal edge resistance; in all
       * cases, we allow snapping to opposite kinds of edges (e.g. left
       * sides of windows to both left and right edges.
       */

      new_left   = apply_edge_snapping (POLO_BOX_LEFT (*old_outer),
                                        POLO_BOX_LEFT (*new_outer),
                                        new_outer,
                                        edge_data->left_edges,
                                        TRUE,
                                        keyboard_op);

      new_right  = apply_edge_snapping (POLO_BOX_RIGHT (*old_outer),
                                        POLO_BOX_RIGHT (*new_outer),
                                        new_outer,
                                        edge_data->right_edges,
                                        TRUE,
                                        keyboard_op);

      new_top    = apply_edge_snapping (POLO_BOX_TOP (*old_outer),
                                        POLO_BOX_TOP (*new_outer),
                                        new_outer,
                                        edge_data->top_edges,
                                        FALSE,
                                        keyboard_op);

      new_bottom = apply_edge_snapping (POLO_BOX_BOTTOM (*old_outer),
                                        POLO_BOX_BOTTOM (*new_outer),
                                        new_outer,
                                        edge_data->bottom_edges,
                                        FALSE,
                                        keyboard_op);
    }
  else
    {
      /* Disable edge resistance for resizes when windows have size
       * increment hints; see #346782.  For all other cases, apply
       * them.
       */
      if (!is_resize || width_inc == 1)
        {
          /* Now, apply the normal horizontal edge resistance */
          new_left   = apply_edge_resistance (POLO_BOX_LEFT (*old_outer),
                                              POLO_BOX_LEFT (*new_outer),
                                              old_outer,
                                              new_outer,
                                              edge_data->left_edges,
                                              &edge_data->left_data,
                                              TRUE,
                                              keyboard_op);
          new_right  = apply_edge_resistance (POLO_BOX_RIGHT (*old_outer),
                                              POLO_BOX_RIGHT (*new_outer),
                                              old_outer,
                                              new_outer,
                                              edge_data->right_edges,
                                              &edge_data->right_data,
                                              TRUE,
                                              keyboard_op);
        }
      else
        {
          new_left  = new_outer->x;
          new_right = new_outer->x + new_outer->width;
        }
      /* Same for vertical resizes... */
      if (!is_resize || height_inc == 1)
        {
          new_top    = apply_edge_resistance (POLO_BOX_TOP (*old_outer),
                                              POLO_BOX_TOP (*new_outer),
                                              old_outer,
                                              new_outer,
                                              edge_data->top_edges,
                                              &edge_data->top_data,
                                              FALSE,
                                              keyboard_op);
          new_bottom = apply_edge_resistance (POLO_BOX_BOTTOM (*old_outer),
                                              POLO_BOX_BOTTOM (*new_outer),
                                              old_outer,
                                              new_outer,
                                              edge_data->bottom_edges,
                                              &edge_data->bottom_data,
                                              FALSE,
                                              keyboard_op);
        }
      else
        {
          new_top    = new_outer->y;
          new_bottom = new_outer->y + new_outer->height;
        }
    }

  /* Determine whether anything changed, and save the changes */
  modified_rect = polo_marco_rect (new_left,
                             new_top,
                             new_right - new_left,
                             new_bottom - new_top);
  modified = !polo_marco_rect_equal (new_outer, &modified_rect);
  *new_outer = modified_rect;
  return modified;
}

void
polo_marco_edge_resistance_free (PoloEdgeResistanceData *edge_data)
{
  guint i,j;
  GHashTable *edges_to_be_freed;

  if (edge_data == NULL) /* Not currently cached */
    return;

  /* We first need to clean out any window edges */
  edges_to_be_freed = g_hash_table_new_full (g_direct_hash, g_direct_equal,
                                             g_free, NULL);
  for (i = 0; i < 4; i++)
    {
      GArray *tmp = NULL;
      PoloSide side;
      switch (i)
        {
        case 0:
          tmp = edge_data->left_edges;
          side = POLO_SIDE_LEFT;
          break;
        case 1:
          tmp = edge_data->right_edges;
          side = POLO_SIDE_RIGHT;
          break;
        case 2:
          tmp = edge_data->top_edges;
          side = POLO_SIDE_TOP;
          break;
        case 3:
          tmp = edge_data->bottom_edges;
          side = POLO_SIDE_BOTTOM;
          break;
        default:
          g_assert_not_reached ();
        }

      for (j = 0; j < tmp->len; j++)
        {
          PoloEdge *edge = g_array_index (tmp, PoloEdge*, j);
          if (edge->edge_type == POLO_EDGE_WINDOW &&
              edge->side_type == side)
            {
              /* The same edge will appear in two arrays, and we can't free
               * it yet we still need to compare edge->side_type for the other
               * array that it is in.  So store it in a hash table for later
               * freeing.  Could also do this in a simple linked list.
               */
              g_hash_table_insert (edges_to_be_freed, edge, edge);
            }
        }
    }

  /* Now free all the window edges (the key destroy function is g_free) */
  g_hash_table_destroy (edges_to_be_freed);

  /* Now free the arrays and data */
  g_array_free (edge_data->left_edges, TRUE);
  g_array_free (edge_data->right_edges, TRUE);
  g_array_free (edge_data->top_edges, TRUE);
  g_array_free (edge_data->bottom_edges, TRUE);
  edge_data->left_edges = NULL;
  edge_data->right_edges = NULL;
  edge_data->top_edges = NULL;
  edge_data->bottom_edges = NULL;

  g_list_free_full (edge_data->workspace_edges, g_free);
  g_free (edge_data);
}

static int
stupid_sort_requiring_extra_pointer_dereference (gconstpointer a,
                                                 gconstpointer b)
{
  const PoloEdge * const *a_edge = a;
  const PoloEdge * const *b_edge = b;
  return polo_marco_rect_edge_cmp_ignore_type (*a_edge, *b_edge);
}

static PoloEdgeResistanceData *
cache_edges (GList *window_edges,
             GList *xinerama_edges,
             GList *screen_edges)
{
  PoloEdgeResistanceData *edge_data;
  GList *tmp;
  int num_left, num_right, num_top, num_bottom;
  int i;

  /*
   * 1st: Get the total number of each kind of edge
   */
  num_left = num_right = num_top = num_bottom = 0;
  for (i = 0; i < 3; i++)
    {
      tmp = NULL;
      switch (i)
        {
        case 0:
          tmp = window_edges;
          break;
        case 1:
          tmp = xinerama_edges;
          break;
        case 2:
          tmp = screen_edges;
          break;
        default:
          g_assert_not_reached ();
        }

      while (tmp)
        {
          PoloEdge *edge = tmp->data;
          switch (edge->side_type)
            {
            case POLO_SIDE_LEFT:
              num_left++;
              break;
            case POLO_SIDE_RIGHT:
              num_right++;
              break;
            case POLO_SIDE_TOP:
              num_top++;
              break;
            case POLO_SIDE_BOTTOM:
              num_bottom++;
              break;
            default:
              g_assert_not_reached ();
            }
          tmp = tmp->next;
        }
    }

  /*
   * 2nd: Allocate the edges
   */
  edge_data = g_new (PoloEdgeResistanceData, 1);
  edge_data->left_edges   = g_array_sized_new (FALSE,
                                               FALSE,
                                               sizeof(PoloEdge*),
                                               num_left + num_right);
  edge_data->right_edges  = g_array_sized_new (FALSE,
                                               FALSE,
                                               sizeof(PoloEdge*),
                                               num_left + num_right);
  edge_data->top_edges    = g_array_sized_new (FALSE,
                                               FALSE,
                                               sizeof(PoloEdge*),
                                               num_top + num_bottom);
  edge_data->bottom_edges = g_array_sized_new (FALSE,
                                               FALSE,
                                               sizeof(PoloEdge*),
                                               num_top + num_bottom);

  /*
   * 3rd: Add the edges to the arrays
   */
  for (i = 0; i < 3; i++)
    {
      tmp = NULL;
      switch (i)
        {
        case 0:
          tmp = window_edges;
          break;
        case 1:
          tmp = xinerama_edges;
          break;
        case 2:
          tmp = screen_edges;
          break;
        default:
          g_assert_not_reached ();
        }

      while (tmp)
        {
          PoloEdge *edge = tmp->data;
          switch (edge->side_type)
            {
            case POLO_SIDE_LEFT:
            case POLO_SIDE_RIGHT:
              g_array_append_val (edge_data->left_edges, edge);
              g_array_append_val (edge_data->right_edges, edge);
              break;
            case POLO_SIDE_TOP:
            case POLO_SIDE_BOTTOM:
              g_array_append_val (edge_data->top_edges, edge);
              g_array_append_val (edge_data->bottom_edges, edge);
              break;
            default:
              g_assert_not_reached ();
            }
          tmp = tmp->next;
        }
    }

  g_array_sort (edge_data->left_edges,
                stupid_sort_requiring_extra_pointer_dereference);
  g_array_sort (edge_data->right_edges,
                stupid_sort_requiring_extra_pointer_dereference);
  g_array_sort (edge_data->top_edges,
                stupid_sort_requiring_extra_pointer_dereference);
  g_array_sort (edge_data->bottom_edges,
                stupid_sort_requiring_extra_pointer_dereference);

  return edge_data;
}

static void
initialize_grab_edge_resistance_data (PoloEdgeResistanceData *edge_data)
{
  edge_data->left_data.keyboard_buildup   = 0;
  edge_data->right_data.keyboard_buildup  = 0;
  edge_data->top_data.keyboard_buildup    = 0;
  edge_data->bottom_data.keyboard_buildup = 0;
}

PoloEdgeResistanceData *
polo_marco_edge_resistance_new (const PoloRect       *screen_rect,
                                const PoloEdgeWindow *stacked_windows,
                                int                   n_stacked_windows,
                                const GList          *xinerama_edges,
                                const GList          *screen_edges)
{
  PoloEdgeResistanceData *edge_data;
  GList *xinerama_copy, *screen_copy;
  const GList *iter;
  int cur_window_iter;
  GList *edges;
  /* Lists of window positions (rects) and their relative stacking positions */
  int stack_position;
  GSList *obscuring_windows, *window_stacking;
  /* The portions of the above lists that still remain at the stacking position
   * in the layer that we are working on
   */
  GSList *rem_windows, *rem_win_stacking;

  /*
   * 1st: Get the list of relevant windows, from bottom to top (the
   * caller's stacked_windows)
   */

  /*
   * 2nd: we need to separate that stacked list into a list of windows that
   * can obscure other edges.  To make sure we only have windows obscuring
   * those below it instead of going both ways, we also need to keep a
   * counter list.  Messy, I know.
   */
  obscuring_windows = window_stacking = NULL;
  cur_window_iter = 0;
  stack_position = 0;
  while (cur_window_iter < n_stacked_windows)
    {
      const PoloEdgeWindow *cur_window = &stacked_windows[cur_window_iter];
      if (cur_window->relevant)
        {
          PoloRect *new_rect;
          new_rect = g_new (PoloRect, 1);
          *new_rect = cur_window->outer;
          obscuring_windows = g_slist_prepend (obscuring_windows, new_rect);
          window_stacking =
            g_slist_prepend (window_stacking, GINT_TO_POINTER (stack_position));
        }

      stack_position++;
      cur_window_iter++;
    }
  /* Put 'em in bottom to top order */
  rem_windows = obscuring_windows = g_slist_reverse (obscuring_windows);
  rem_win_stacking = window_stacking = g_slist_reverse (window_stacking);

  /*
   * 3rd: loop over the windows again, this time getting the edges from
   * them and removing intersections with the relevant obscuring_windows &
   * obscuring_docks.
   */
  edges = NULL;
  stack_position = 0;
  cur_window_iter = 0;
  while (cur_window_iter < n_stacked_windows)
    {
      PoloRect  cur_rect;
      const PoloEdgeWindow *cur_window = &stacked_windows[cur_window_iter];
      cur_rect = cur_window->outer;

      /* Check if we want to use this window's edges for edge
       * resistance (note that dock edges are considered screen edges
       * which are handled separately
       */
      if (cur_window->relevant &&
          !cur_window->is_dock)
        {
          GList *new_edges;
          PoloEdge *new_edge;
          PoloRect reduced;

          /* We don't care about snapping to any portion of the window that
           * is offscreen (we also don't care about parts of edges covered
           * by other windows or DOCKS, but that's handled below).
           */
          polo_marco_rect_intersect (&cur_rect,
                                    screen_rect,
                                    &reduced);

          new_edges = NULL;

          /* Left side of this window is resistance for the right edge of
           * the window being moved.
           */
          new_edge = g_new (PoloEdge, 1);
          new_edge->rect = reduced;
          new_edge->rect.width = 0;
          new_edge->side_type = POLO_SIDE_RIGHT;
          new_edge->edge_type = POLO_EDGE_WINDOW;
          new_edges = g_list_prepend (new_edges, new_edge);

          /* Right side of this window is resistance for the left edge of
           * the window being moved.
           */
          new_edge = g_new (PoloEdge, 1);
          new_edge->rect = reduced;
          new_edge->rect.x += new_edge->rect.width;
          new_edge->rect.width = 0;
          new_edge->side_type = POLO_SIDE_LEFT;
          new_edge->edge_type = POLO_EDGE_WINDOW;
          new_edges = g_list_prepend (new_edges, new_edge);

          /* Top side of this window is resistance for the bottom edge of
           * the window being moved.
           */
          new_edge = g_new (PoloEdge, 1);
          new_edge->rect = reduced;
          new_edge->rect.height = 0;
          new_edge->side_type = POLO_SIDE_BOTTOM;
          new_edge->edge_type = POLO_EDGE_WINDOW;
          new_edges = g_list_prepend (new_edges, new_edge);

          /* Top side of this window is resistance for the bottom edge of
           * the window being moved.
           */
          new_edge = g_new (PoloEdge, 1);
          new_edge->rect = reduced;
          new_edge->rect.y += new_edge->rect.height;
          new_edge->rect.height = 0;
          new_edge->side_type = POLO_SIDE_TOP;
          new_edge->edge_type = POLO_EDGE_WINDOW;
          new_edges = g_list_prepend (new_edges, new_edge);

          /* Update the remaining windows to only those at a higher
           * stacking position than this one.
           */
          while (rem_win_stacking &&
                 stack_position >= GPOINTER_TO_INT (rem_win_stacking->data))
            {
              rem_windows      = rem_windows->next;
              rem_win_stacking = rem_win_stacking->next;
            }

          /* Remove edge portions overlapped by rem_windows and rem_docks */
          new_edges =
            polo_marco_rect_remove_intersections_with_boxes_from_edges (
              new_edges,
              rem_windows);

          /* Save the new edges */
          edges = g_list_concat (new_edges, edges);
        }

      stack_position++;
      cur_window_iter++;
    }

  /*
   * 4th: Free the extra memory not needed and sort the list
   */
  /* Free the memory used by the obscuring windows/docks lists */
  g_slist_free (window_stacking);
  g_slist_free_full (obscuring_windows, g_free);

  edges = g_list_sort (edges, polo_marco_rect_edge_cmp);

  /*
   * 5th: Cache the combination of these edges with the onscreen and
   * xinerama edges in an array for quick access.  Free the edges since
   * they've been cached elsewhere.
   */
  xinerama_copy = NULL;
  for (iter = xinerama_edges; iter; iter = iter->next)
    xinerama_copy = g_list_prepend (xinerama_copy,
                                    g_memdup2 (iter->data, sizeof (PoloEdge)));
  xinerama_copy = g_list_reverse (xinerama_copy);
  screen_copy = NULL;
  for (iter = screen_edges; iter; iter = iter->next)
    screen_copy = g_list_prepend (screen_copy,
                                  g_memdup2 (iter->data, sizeof (PoloEdge)));
  screen_copy = g_list_reverse (screen_copy);
  edge_data = cache_edges (edges, xinerama_copy, screen_copy);
  edge_data->workspace_edges = g_list_concat (xinerama_copy, screen_copy);
  g_list_free (edges);

  /*
   * 6th: Initialize the resistance timeouts and buildups
   */
  initialize_grab_edge_resistance_data (edge_data);

  return edge_data;
}

/* Note that old_[xy] and new_[xy] are with respect to inner positions of
 * the window.
 */
void
polo_marco_edge_resistance_for_move (PoloEdgeResistanceData *edge_data,
                                     const PoloRect *window_outer,
                                     int          old_x,
                                     int          old_y,
                                     int         *new_x,
                                     int         *new_y,
                                     gboolean     snap,
                                     gboolean     is_keyboard_op)
{
  PoloRect old_outer, proposed_outer, new_outer;
  gboolean is_resize;

  old_outer = *window_outer;
  proposed_outer = old_outer;
  proposed_outer.x += (*new_x - old_x);
  proposed_outer.y += (*new_y - old_y);
  new_outer = proposed_outer;

  is_resize = FALSE;
  if (apply_edge_resistance_to_each_side (edge_data,
                                          1,
                                          1,
                                          &old_outer,
                                          &new_outer,
                                          snap,
                                          is_keyboard_op,
                                          is_resize))
    {
      /* apply_edge_resistance_to_each_side independently applies
       * resistance to both the right and left edges of new_outer as both
       * could meet areas of resistance.  But we don't want a resize, so we
       * just have both edges move according to the stricter of the
       * resistances.  Same thing goes for top & bottom edges.
       */
      PoloRect *reference;
      int left_change, right_change, smaller_x_change;
      int top_change, bottom_change, smaller_y_change;

      if (snap && !is_keyboard_op)
        reference = &proposed_outer;
      else
        reference = &old_outer;

      left_change  = POLO_BOX_LEFT (new_outer)  - POLO_BOX_LEFT (*reference);
      right_change = POLO_BOX_RIGHT (new_outer) - POLO_BOX_RIGHT (*reference);
      if (     snap && is_keyboard_op && left_change == 0)
        smaller_x_change = right_change;
      else if (snap && is_keyboard_op && right_change == 0)
        smaller_x_change = left_change;
      else if (ABS (left_change) < ABS (right_change))
        smaller_x_change = left_change;
      else
        smaller_x_change = right_change;

      top_change    = POLO_BOX_TOP (new_outer)    - POLO_BOX_TOP (*reference);
      bottom_change = POLO_BOX_BOTTOM (new_outer) - POLO_BOX_BOTTOM (*reference);
      if (     snap && is_keyboard_op && top_change == 0)
        smaller_y_change = bottom_change;
      else if (snap && is_keyboard_op && bottom_change == 0)
        smaller_y_change = top_change;
      else if (ABS (top_change) < ABS (bottom_change))
        smaller_y_change = top_change;
      else
        smaller_y_change = bottom_change;

      *new_x = old_x + smaller_x_change +
              (POLO_BOX_LEFT (*reference) - POLO_BOX_LEFT (old_outer));
      *new_y = old_y + smaller_y_change +
              (POLO_BOX_TOP (*reference) - POLO_BOX_TOP (old_outer));

    }
}

/* Note that old_(width|height) and new_(width|height) are with respect to
 * sizes of the inner window.
 */
void
polo_marco_edge_resistance_for_resize (PoloEdgeResistanceData *edge_data,
                                       const PoloRect *window_outer,
                                       int          width_inc,
                                       int          height_inc,
                                       int          old_width,
                                       int          old_height,
                                       int         *new_width,
                                       int         *new_height,
                                       PoloGravity  gravity,
                                       gboolean     snap,
                                       gboolean     is_keyboard_op)
{
  PoloRect old_outer, new_outer;
  int proposed_outer_width, proposed_outer_height;
  gboolean is_resize;

  old_outer = *window_outer;
  proposed_outer_width  = old_outer.width  + (*new_width  - old_width);
  proposed_outer_height = old_outer.height + (*new_height - old_height);
  polo_marco_rect_resize_with_gravity (&old_outer,
                                      &new_outer,
                                      gravity,
                                      proposed_outer_width,
                                      proposed_outer_height);

  is_resize = TRUE;
  if (apply_edge_resistance_to_each_side (edge_data,
                                          width_inc,
                                          height_inc,
                                          &old_outer,
                                          &new_outer,
                                          snap,
                                          is_keyboard_op,
                                          is_resize))
    {
      *new_width  = old_width  + (new_outer.width  - old_outer.width);
      *new_height = old_height + (new_outer.height - old_outer.height);

    }
}
