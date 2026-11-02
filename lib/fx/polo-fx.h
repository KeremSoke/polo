/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * libpolo-fx: per-frame effect mathematics (Wobbly Windows, Magic Lamp,
 * deformation grids), ported from my picom wobbly branch.
 *
 * C side of vapi/polo-fx.vapi. Must not include libweston, Wayland or X
 * headers.
 */

#ifndef POLO_FX_H
#define POLO_FX_H

#include <glib.h>

G_BEGIN_DECLS

const char *
polo_fx_source(void);

G_END_DECLS

#endif /* POLO_FX_H */
