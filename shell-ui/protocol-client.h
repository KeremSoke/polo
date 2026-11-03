/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Client binding for polo_shell_ui_v1 on GDK's Wayland connection. Protocol
 * events become GObject signals; wl_output arguments become their GdkMonitor
 * when one is available.
 */

#ifndef POLO_SHELL_UI_PROTOCOL_CLIENT_H
#define POLO_SHELL_UI_PROTOCOL_CLIENT_H

#include <gdk/gdk.h>

G_BEGIN_DECLS

#define POLO_SHELL_UI_CLIENT_ERROR (polo_shell_ui_client_error_quark())
GQuark
polo_shell_ui_client_error_quark(void);

typedef enum {
	POLO_SHELL_UI_CLIENT_ERROR_NOT_WAYLAND,
	POLO_SHELL_UI_CLIENT_ERROR_NO_GLOBAL,
} PoloShellUiClientError;

#define POLO_SHELL_UI_TYPE_CLIENT (polo_shell_ui_client_get_type())
G_DECLARE_FINAL_TYPE(PoloShellUiClient, polo_shell_ui_client, POLO_SHELL_UI,
		     CLIENT, GObject)

/* Binds polo_shell_ui_v1 on @display. */
PoloShellUiClient *
polo_shell_ui_client_new(GdkDisplay *display, GError **error);

void
polo_shell_ui_client_switcher_select(PoloShellUiClient *client, guint id);

void
polo_shell_ui_client_menu_action(PoloShellUiClient *client, guint window_id,
				 guint op, int workspace);

/* Call before showing @window. */
void
polo_shell_ui_client_dialog_surface(PoloShellUiClient *client, guint dialog_id,
				    GdkWindow *window);

void
polo_shell_ui_client_dialog_response(PoloShellUiClient *client,
				     guint dialog_id, int response,
				     const char *text);

/* Pass 160 floats in the protocol's color_table order. */
void
polo_shell_ui_client_color_table(PoloShellUiClient *client,
				 const float *colors, int n_colors);

/* Pass four RGBA floats, or NULL if the GTK theme has no value for name. */
void
polo_shell_ui_client_custom_color(PoloShellUiClient *client, const char *name,
				  const float *rgba);

G_END_DECLS

#endif
