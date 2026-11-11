/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <string.h>

#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbregistry.h>

#include "polo-backend.h"

static struct rxkb_context *
registry(void)
{
	static struct rxkb_context *ctx;
	static gboolean tried;

	if (!tried) {
		tried = TRUE;
		ctx = rxkb_context_new(RXKB_CONTEXT_LOAD_EXOTIC_RULES);
		if (ctx && !rxkb_context_parse(ctx, "evdev"))
			ctx = rxkb_context_unref(ctx);
	}
	return ctx;
}

gboolean
polo_backend_keymap_has_layout(const char *layout, const char *variant)
{
	struct rxkb_context *ctx = registry();
	struct rxkb_layout *l;

	g_return_val_if_fail(layout != NULL, FALSE);

	if (!ctx)
		return TRUE;
	for (l = rxkb_layout_first(ctx); l; l = rxkb_layout_next(l)) {
		const char *v = rxkb_layout_get_variant(l);

		if (strcmp(rxkb_layout_get_name(l), layout) != 0)
			continue;
		if (variant ? v && strcmp(v, variant) == 0 : v == NULL)
			return TRUE;
	}
	return FALSE;
}

guint32
polo_backend_keysym_to_unicode(guint32 keysym)
{
	return xkb_keysym_to_utf32(keysym);
}
