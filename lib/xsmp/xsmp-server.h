/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * An XSMP server on libSM/ICE, as mate-session 1.28's gsm-xsmp-server.c:
 * local ICE sockets only, MIT-MAGIC-COOKIE-1 entries in .ICEauthority, and
 * one PoloXsmpClient per connection. Policy stays in Vala.
 */

#ifndef POLO_XSMP_SERVER_H
#define POLO_XSMP_SERVER_H

#include <glib-object.h>

G_BEGIN_DECLS

#define POLO_XSMP_TYPE_SERVER (polo_xsmp_server_get_type())
G_DECLARE_FINAL_TYPE(PoloXsmpServer, polo_xsmp_server, POLO_XSMP, SERVER,
		     GObject)

#define POLO_XSMP_TYPE_CLIENT (polo_xsmp_client_get_type())
G_DECLARE_FINAL_TYPE(PoloXsmpClient, polo_xsmp_client, POLO_XSMP, CLIENT,
		     GObject)

/* XSMP's SmSave* and SmInteractStyle* values. */
typedef enum {
	POLO_XSMP_SAVE_GLOBAL = 0,
	POLO_XSMP_SAVE_LOCAL = 1,
	POLO_XSMP_SAVE_BOTH = 2,
} PoloXsmpSaveType;

typedef enum {
	POLO_XSMP_INTERACT_NONE = 0,
	POLO_XSMP_INTERACT_ERRORS = 1,
	POLO_XSMP_INTERACT_ANY = 2,
} PoloXsmpInteractStyle;

/* SmRestartIfRunning... SmRestartNever. */
typedef enum {
	POLO_XSMP_RESTART_IF_RUNNING = 0,
	POLO_XSMP_RESTART_ANYWAY = 1,
	POLO_XSMP_RESTART_IMMEDIATELY = 2,
	POLO_XSMP_RESTART_NEVER = 3,
} PoloXsmpRestartStyle;

/* Listens on the local ICE sockets and updates .ICEauthority. The returned
 * server emits "new-client" (PoloXsmpClient) for each XSMP connection. */
PoloXsmpServer *
polo_xsmp_server_new(const char *vendor, const char *release,
		     GError **error);

/* The value for SESSION_MANAGER. */
const char *
polo_xsmp_server_get_network_ids(PoloXsmpServer *server);

/* FALSE refuses new XSMP connections, as during shutdown. */
void
polo_xsmp_server_set_accepting(PoloXsmpServer *server, gboolean accepting);

void
polo_xsmp_client_accept_registration(PoloXsmpClient *client, const char *id);

void
polo_xsmp_client_save_yourself(PoloXsmpClient *client,
			       PoloXsmpSaveType save_type, gboolean shutdown,
			       PoloXsmpInteractStyle interact_style,
			       gboolean fast);

void
polo_xsmp_client_save_yourself_phase2(PoloXsmpClient *client);

void
polo_xsmp_client_interact(PoloXsmpClient *client);

void
polo_xsmp_client_save_complete(PoloXsmpClient *client);

void
polo_xsmp_client_shutdown_cancelled(PoloXsmpClient *client);

void
polo_xsmp_client_die(PoloXsmpClient *client);

void
polo_xsmp_client_close(PoloXsmpClient *client);

gboolean
polo_xsmp_client_is_connected(PoloXsmpClient *client);

/* An ARRAY8 property (SmProgram, SmProcessID, _GSM_DesktopFile...), or NULL. */
char *
polo_xsmp_client_get_string(PoloXsmpClient *client, const char *name);

/* A LISTofARRAY8 property as a shell command, quoted as gsm's
 * prop_to_command () quotes it, or NULL. */
char *
polo_xsmp_client_get_command(PoloXsmpClient *client, const char *name);

/* SmRestartStyleHint; IF_RUNNING when unset, as in gsm. */
PoloXsmpRestartStyle
polo_xsmp_client_get_restart_style(PoloXsmpClient *client);

G_END_DECLS

#endif /* POLO_XSMP_SERVER_H */
