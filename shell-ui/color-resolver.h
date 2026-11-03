/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef POLO_SHELL_UI_COLOR_RESOLVER_H
#define POLO_SHELL_UI_COLOR_RESOLVER_H

#include "protocol-client.h"

G_BEGIN_DECLS

#define POLO_SHELL_UI_TYPE_COLOR_RESOLVER                                      \
	(polo_shell_ui_color_resolver_get_type())
G_DECLARE_FINAL_TYPE(PoloShellUiColorResolver, polo_shell_ui_color_resolver,
		     POLO_SHELL_UI, COLOR_RESOLVER, GObject)

/* Sends the colour table at startup and on GTK theme changes, then answers
 * lookup_color. */
PoloShellUiColorResolver *
polo_shell_ui_color_resolver_new(PoloShellUiClient *client);

G_END_DECLS

#endif
