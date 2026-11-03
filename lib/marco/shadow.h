/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Marco's compositor shadows around a frame.
 */

#ifndef POLO_MARCO_SHADOW_H
#define POLO_MARCO_SHADOW_H

#include "theme.h"

G_BEGIN_DECLS

/* MetaShadowType */
typedef enum {
	POLO_WINDOW_SHADOW_SMALL,
	POLO_WINDOW_SHADOW_MEDIUM,
	POLO_WINDOW_SHADOW_LARGE,
	POLO_WINDOW_SHADOW_LAST
} PoloWindowShadow;

typedef enum {
	POLO_SHADOW_PIECE_TOP,
	POLO_SHADOW_PIECE_BOTTOM,
	POLO_SHADOW_PIECE_LEFT,
	POLO_SHADOW_PIECE_RIGHT
} PoloShadowPiece;

/* compositor-xrender.c SHADOW_OPACITY */
#define POLO_SHADOW_OPACITY 0.66

typedef struct _PoloShadowMask PoloShadowMask;

/* The shadow of a frame, less what the frame covers; NULL for an empty one. */
PoloShadowMask *
polo_marco_shadow_mask_new(PoloWindowShadow type, double opacity,
			   const PoloFrameGeometry *fgeom, PoloFrameFlags flags);

void
polo_marco_shadow_mask_free(PoloShadowMask *mask);

/* x and y are relative to the frame window's corner. */
void
polo_marco_shadow_mask_get_bounds(const PoloShadowMask *mask, int *x, int *y,
				  int *width, int *height);

/* A piece of the ring around the frame, in mask coordinates. */
void
polo_marco_shadow_mask_get_piece(const PoloShadowMask *mask,
				 PoloShadowPiece piece, int *x, int *y,
				 int *width, int *height);

/* Copies that part of the mask into premultiplied ARGB32 pixels. */
void
polo_marco_shadow_mask_render(const PoloShadowMask *mask, int piece_x,
			      int piece_y, guint8 *pixels, int width,
			      int height, int stride);

G_END_DECLS

#endif
