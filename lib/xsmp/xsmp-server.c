/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <gio/gio.h>

#include <X11/ICE/ICEconn.h>
#include <X11/ICE/ICElib.h>
#include <X11/ICE/ICEutil.h>
#include <X11/SM/SMlib.h>

#include "xsmp-server.h"

#define ICE_AUTH_RETRIES 10
#define ICE_AUTH_INTERVAL 2
#define ICE_AUTH_LOCK_TIMEOUT 600
#define ICE_MAGIC_COOKIE_AUTH_NAME "MIT-MAGIC-COOKIE-1"
#define ICE_MAGIC_COOKIE_LEN 16
#define ICE_PROTOCOL_TIMEOUT_SECONDS 5
#define ICE_MESSAGE_MAX (64 * 1024)
#define ICE_MESSAGE_WAIT_MS 200

struct _PoloXsmpServer {
	GObject parent_instance;

	IceListenObj *sockets;
	int n_sockets;
	int n_local_sockets;
	GArray *watches;
	char *network_ids;
	gboolean accepting;
	GPtrArray *clients;
};

struct _PoloXsmpClient {
	GObject parent_instance;

	PoloXsmpServer *server;
	SmsConn conn;
	IceConn ice;
	guint watch_id;
	GPtrArray *props;
	char *reply_id;
};

typedef struct {
	guint watch_id;
	guint protocol_timeout;
} IceWatch;

enum {
	SERVER_NEW_CLIENT,
	N_SERVER_SIGNALS,
};

enum {
	CLIENT_REGISTER_REQUEST,
	CLIENT_REGISTERED,
	CLIENT_INTERACT_REQUEST,
	CLIENT_INTERACT_DONE,
	CLIENT_SAVE_YOURSELF_REQUEST,
	CLIENT_SAVE_YOURSELF_PHASE2_REQUEST,
	CLIENT_SAVE_YOURSELF_DONE,
	CLIENT_PROPERTIES_CHANGED,
	CLIENT_CLOSED,
	CLIENT_DISCONNECTED,
	N_CLIENT_SIGNALS,
};

static guint server_signals[N_SERVER_SIGNALS];
static guint client_signals[N_CLIENT_SIGNALS];
static PoloXsmpServer *the_server;

G_DEFINE_FINAL_TYPE(PoloXsmpServer, polo_xsmp_server, G_TYPE_OBJECT)
G_DEFINE_FINAL_TYPE(PoloXsmpClient, polo_xsmp_client, G_TYPE_OBJECT)

/* ---- Pending ICE connections ------------------------------------------- */

static void
ice_watch_free(IceWatch *watch)
{
	if (watch->watch_id)
		g_source_remove(watch->watch_id);
	if (watch->protocol_timeout)
		g_source_remove(watch->protocol_timeout);
	g_free(watch);
}

static void
disconnect_ice(IceConn ice)
{
	IceSetShutdownNegotiation(ice, False);
	IceCloseConnection(ice);
}

static gboolean
ice_protocol_timeout(gpointer data)
{
	IceConn ice = data;
	IceWatch *watch = ice->context;

	watch->protocol_timeout = 0;
	ice->context = NULL;
	ice_watch_free(watch);
	disconnect_ice(ice);
	return G_SOURCE_REMOVE;
}

static gboolean
ice_message_complete(IceConn ice)
{
	int fd = IceConnectionNumber(ice);
	gint64 deadline = g_get_monotonic_time() + ICE_MESSAGE_WAIT_MS * 1000;
	gint64 left;
	unsigned char header[8];
	guint32 units;
	guint64 size;
	int queued;
	ssize_t got;

	for (;;) {
		got = recv(fd, header, sizeof header, MSG_PEEK | MSG_DONTWAIT);
		if (got <= 0)
			return got == 0 || (errno != EAGAIN && errno != EINTR);
		if (got == sizeof header) {
			memcpy(&units, header + 4, sizeof units);
			if (ice->swap)
				units = GUINT32_SWAP_LE_BE(units);
			size = sizeof header + (guint64)units * 8;
			if (size > ICE_MESSAGE_MAX) {
				g_warning("XSMP: a client sent a message of "
					  "%" G_GUINT64_FORMAT
					  " bytes; disconnecting it",
					  size);
				return FALSE;
			}
			if (ioctl(fd, FIONREAD, &queued) < 0 ||
			    (guint64)queued >= size)
				return TRUE;
		}
		left = (deadline - g_get_monotonic_time()) / 1000;
		if (left <= 0) {
			g_warning("XSMP: a client left a message unfinished; "
				  "disconnecting it");
			return FALSE;
		}
		g_usleep(MIN(left, 5) * 1000);
	}
}

static gboolean
pending_ice_ready(GIOChannel *source, GIOCondition condition, gpointer data)
{
	IceConn ice = data;
	IceWatch *watch = ice->context;

	if (!ice_message_complete(ice)) {
		watch->watch_id = 0;
		ice->context = NULL;
		ice_watch_free(watch);
		disconnect_ice(ice);
		return G_SOURCE_REMOVE;
	}
	switch (IceProcessMessages(ice, NULL, NULL)) {
	case IceProcessMessagesSuccess:
		return G_SOURCE_CONTINUE;
	case IceProcessMessagesIOError:
		watch->watch_id = 0;
		ice->context = NULL;
		ice_watch_free(watch);
		disconnect_ice(ice);
		return G_SOURCE_REMOVE;
	default:
		if (ice->context == watch) {
			watch->watch_id = 0;
			ice->context = NULL;
			ice_watch_free(watch);
		}
		return G_SOURCE_REMOVE;
	}
}

static gboolean
accept_ice(GIOChannel *source, GIOCondition condition, gpointer data)
{
	IceListenObj listener = data;
	IceAcceptStatus status;
	IceConn ice;
	IceWatch *watch;
	GIOChannel *channel;
	int fd;

	ice = IceAcceptConnection(listener, &status);
	if (status != IceAcceptSuccess)
		return G_SOURCE_CONTINUE;
	fd = IceConnectionNumber(ice);
	fcntl(fd, F_SETFD, fcntl(fd, F_GETFD, 0) | FD_CLOEXEC);
	watch = g_new0(IceWatch, 1);
	ice->context = watch;
	watch->protocol_timeout = g_timeout_add_seconds(
		ICE_PROTOCOL_TIMEOUT_SECONDS, ice_protocol_timeout, ice);
	channel = g_io_channel_unix_new(fd);
	watch->watch_id = g_io_add_watch(channel, G_IO_IN | G_IO_ERR,
					 pending_ice_ready, ice);
	g_io_channel_unref(channel);
	return G_SOURCE_CONTINUE;
}

/* ---- Client ------------------------------------------------------------ */

static SmProp *
find_property(PoloXsmpClient *client, const char *name, guint *index)
{
	for (guint i = 0; i < client->props->len; i++) {
		SmProp *prop = client->props->pdata[i];

		if (strcmp(prop->name, name) == 0) {
			if (index)
				*index = i;
			return prop;
		}
	}
	return NULL;
}

static void
delete_property(PoloXsmpClient *client, const char *name)
{
	guint index;
	SmProp *prop = find_property(client, name, &index);

	if (!prop)
		return;
	g_ptr_array_remove_index_fast(client->props, index);
	SmFreeProperty(prop);
}

static gboolean
client_ready(GIOChannel *source, GIOCondition condition, gpointer data)
{
	PoloXsmpClient *client = g_object_ref(data);
	gboolean keep = TRUE;

	if (!ice_message_complete(client->ice)) {
		client->watch_id = 0;
		g_signal_emit(client, client_signals[CLIENT_DISCONNECTED], 0);
		g_object_unref(client);
		return G_SOURCE_REMOVE;
	}
	switch (IceProcessMessages(client->ice, NULL, NULL)) {
	case IceProcessMessagesSuccess:
		break;
	case IceProcessMessagesIOError:
		client->watch_id = 0;
		keep = FALSE;
		g_signal_emit(client, client_signals[CLIENT_DISCONNECTED], 0);
		break;
	default:
		client->watch_id = 0;
		client->ice = NULL;
		keep = FALSE;
		g_signal_emit(client, client_signals[CLIENT_DISCONNECTED], 0);
		break;
	}
	g_object_unref(client);
	return keep;
}

static Status
register_client_cb(SmsConn conn, SmPointer data, char *previous_id)
{
	PoloXsmpClient *client = data;
	gboolean fresh = !previous_id || !previous_id[0];
	Status ok = False;

	g_clear_pointer(&client->reply_id, g_free);
	g_signal_emit(client, client_signals[CLIENT_REGISTER_REQUEST], 0,
		      previous_id);
	if (client->reply_id && client->reply_id[0]) {
		SmsRegisterClientReply(conn, client->reply_id);
		ok = True;
	}
	g_clear_pointer(&client->reply_id, g_free);
	free(previous_id);
	if (ok)
		g_signal_emit(client, client_signals[CLIENT_REGISTERED], 0,
			      fresh);
	return ok;
}

static void
interact_request_cb(SmsConn conn, SmPointer data, int dialog_type)
{
	g_signal_emit(data, client_signals[CLIENT_INTERACT_REQUEST], 0,
		      dialog_type);
}

static void
interact_done_cb(SmsConn conn, SmPointer data, Bool cancel_shutdown)
{
	g_signal_emit(data, client_signals[CLIENT_INTERACT_DONE], 0,
		      (gboolean)cancel_shutdown);
}

static void
save_yourself_request_cb(SmsConn conn, SmPointer data, int save_type,
			 Bool shutdown, int interact_style, Bool fast,
			 Bool global)
{
	g_signal_emit(data, client_signals[CLIENT_SAVE_YOURSELF_REQUEST], 0,
		      save_type, (gboolean)shutdown, interact_style,
		      (gboolean)fast, (gboolean)global);
}

static void
save_yourself_phase2_request_cb(SmsConn conn, SmPointer data)
{
	g_signal_emit(data, client_signals[CLIENT_SAVE_YOURSELF_PHASE2_REQUEST],
		      0);
}

static void
save_yourself_done_cb(SmsConn conn, SmPointer data, Bool success)
{
	g_signal_emit(data, client_signals[CLIENT_SAVE_YOURSELF_DONE], 0,
		      (gboolean)success);
}

static void
close_connection_cb(SmsConn conn, SmPointer data, int count, char **reasons)
{
	SmFreeReasons(count, reasons);
	g_signal_emit(data, client_signals[CLIENT_CLOSED], 0);
}

static void
set_properties_cb(SmsConn conn, SmPointer data, int n, SmProp **props)
{
	PoloXsmpClient *client = data;

	for (int i = 0; i < n; i++) {
		delete_property(client, props[i]->name);
		g_ptr_array_add(client->props, props[i]);
	}
	free(props);
	g_signal_emit(client, client_signals[CLIENT_PROPERTIES_CHANGED], 0);
}

static void
delete_properties_cb(SmsConn conn, SmPointer data, int n, char **names)
{
	PoloXsmpClient *client = data;

	for (int i = 0; i < n; i++) {
		delete_property(client, names[i]);
		free(names[i]);
	}
	free(names);
	g_signal_emit(client, client_signals[CLIENT_PROPERTIES_CHANGED], 0);
}

static void
get_properties_cb(SmsConn conn, SmPointer data)
{
	PoloXsmpClient *client = data;

	SmsReturnProperties(conn, (int)client->props->len,
			    (SmProp **)client->props->pdata);
}

static PoloXsmpClient *
client_new(PoloXsmpServer *server, SmsConn conn, IceConn ice,
	   unsigned long *mask, SmsCallbacks *callbacks)
{
	PoloXsmpClient *client = g_object_new(POLO_XSMP_TYPE_CLIENT, NULL);
	GIOChannel *channel;
	int fd = IceConnectionNumber(ice);

	client->server = server;
	client->conn = conn;
	client->ice = ice;
	fcntl(fd, F_SETFD, fcntl(fd, F_GETFD, 0) | FD_CLOEXEC);
	channel = g_io_channel_unix_new(fd);
	client->watch_id = g_io_add_watch(channel, G_IO_IN | G_IO_ERR,
					  client_ready, client);
	g_io_channel_unref(channel);

	*mask = SmsRegisterClientProcMask | SmsInteractRequestProcMask |
		SmsInteractDoneProcMask | SmsSaveYourselfRequestProcMask |
		SmsSaveYourselfP2RequestProcMask | SmsSaveYourselfDoneProcMask |
		SmsCloseConnectionProcMask | SmsSetPropertiesProcMask |
		SmsDeletePropertiesProcMask | SmsGetPropertiesProcMask;
	callbacks->register_client.callback = register_client_cb;
	callbacks->register_client.manager_data = client;
	callbacks->interact_request.callback = interact_request_cb;
	callbacks->interact_request.manager_data = client;
	callbacks->interact_done.callback = interact_done_cb;
	callbacks->interact_done.manager_data = client;
	callbacks->save_yourself_request.callback = save_yourself_request_cb;
	callbacks->save_yourself_request.manager_data = client;
	callbacks->save_yourself_phase2_request.callback =
		save_yourself_phase2_request_cb;
	callbacks->save_yourself_phase2_request.manager_data = client;
	callbacks->save_yourself_done.callback = save_yourself_done_cb;
	callbacks->save_yourself_done.manager_data = client;
	callbacks->close_connection.callback = close_connection_cb;
	callbacks->close_connection.manager_data = client;
	callbacks->set_properties.callback = set_properties_cb;
	callbacks->set_properties.manager_data = client;
	callbacks->delete_properties.callback = delete_properties_cb;
	callbacks->delete_properties.manager_data = client;
	callbacks->get_properties.callback = get_properties_cb;
	callbacks->get_properties.manager_data = client;
	return client;
}

void
polo_xsmp_client_close(PoloXsmpClient *client)
{
	g_return_if_fail(POLO_XSMP_IS_CLIENT(client));
	if (client->watch_id) {
		g_source_remove(client->watch_id);
		client->watch_id = 0;
	}
	if (client->conn) {
		SmsCleanUp(client->conn);
		client->conn = NULL;
	}
	if (client->ice) {
		disconnect_ice(client->ice);
		client->ice = NULL;
	}
	if (client->server) {
		PoloXsmpServer *server = client->server;

		client->server = NULL;
		g_ptr_array_remove(server->clients, client);
	}
}

gboolean
polo_xsmp_client_is_connected(PoloXsmpClient *client)
{
	g_return_val_if_fail(POLO_XSMP_IS_CLIENT(client), FALSE);
	return client->conn != NULL;
}

void
polo_xsmp_client_accept_registration(PoloXsmpClient *client, const char *id)
{
	g_return_if_fail(POLO_XSMP_IS_CLIENT(client));
	g_free(client->reply_id);
	client->reply_id = g_strdup(id);
}

void
polo_xsmp_client_save_yourself(PoloXsmpClient *client,
			       PoloXsmpSaveType save_type, gboolean shutdown,
			       PoloXsmpInteractStyle interact_style,
			       gboolean fast)
{
	g_return_if_fail(POLO_XSMP_IS_CLIENT(client));
	if (client->conn)
		SmsSaveYourself(client->conn, (int)save_type, shutdown,
				(int)interact_style, fast);
}

void
polo_xsmp_client_save_yourself_phase2(PoloXsmpClient *client)
{
	g_return_if_fail(POLO_XSMP_IS_CLIENT(client));
	if (client->conn)
		SmsSaveYourselfPhase2(client->conn);
}

void
polo_xsmp_client_interact(PoloXsmpClient *client)
{
	g_return_if_fail(POLO_XSMP_IS_CLIENT(client));
	if (client->conn)
		SmsInteract(client->conn);
}

void
polo_xsmp_client_save_complete(PoloXsmpClient *client)
{
	g_return_if_fail(POLO_XSMP_IS_CLIENT(client));
	if (client->conn)
		SmsSaveComplete(client->conn);
}

void
polo_xsmp_client_shutdown_cancelled(PoloXsmpClient *client)
{
	g_return_if_fail(POLO_XSMP_IS_CLIENT(client));
	if (client->conn)
		SmsShutdownCancelled(client->conn);
}

void
polo_xsmp_client_die(PoloXsmpClient *client)
{
	g_return_if_fail(POLO_XSMP_IS_CLIENT(client));
	if (client->conn)
		SmsDie(client->conn);
}

char *
polo_xsmp_client_get_string(PoloXsmpClient *client, const char *name)
{
	SmProp *prop;

	g_return_val_if_fail(POLO_XSMP_IS_CLIENT(client), NULL);
	prop = find_property(client, name, NULL);
	if (!prop || strcmp(prop->type, SmARRAY8) != 0 || prop->num_vals < 1)
		return NULL;
	return g_strndup(prop->vals[0].value, prop->vals[0].length);
}

/* gsm's prop_to_command (). */
char *
polo_xsmp_client_get_command(PoloXsmpClient *client, const char *name)
{
	SmProp *prop;
	GString *str;

	g_return_val_if_fail(POLO_XSMP_IS_CLIENT(client), NULL);
	prop = find_property(client, name, NULL);
	if (!prop || strcmp(prop->type, SmLISTofARRAY8) != 0)
		return NULL;
	str = g_string_new(NULL);
	for (int i = 0; i < prop->num_vals; i++) {
		const char *val = prop->vals[i].value;
		int length = prop->vals[i].length;
		gboolean quote = FALSE;

		for (int j = 0; j < length; j++) {
			if (!g_ascii_isalnum(val[j]) &&
			    !strchr("-_=:./", val[j])) {
				quote = TRUE;
				break;
			}
		}
		if (i > 0)
			g_string_append_c(str, ' ');
		if (!quote) {
			g_string_append_len(str, val, length);
			continue;
		}
		g_string_append_c(str, '\'');
		for (int j = 0; j < length; j++) {
			if (val[j] == '\'')
				g_string_append(str, "'\\''");
			else
				g_string_append_c(str, val[j]);
		}
		g_string_append_c(str, '\'');
	}
	return g_string_free(str, FALSE);
}

PoloXsmpRestartStyle
polo_xsmp_client_get_restart_style(PoloXsmpClient *client)
{
	SmProp *prop;
	unsigned char hint;

	g_return_val_if_fail(POLO_XSMP_IS_CLIENT(client),
			     POLO_XSMP_RESTART_IF_RUNNING);
	prop = find_property(client, SmRestartStyleHint, NULL);
	if (!prop || strcmp(prop->type, SmCARD8) != 0 || prop->num_vals < 1)
		return POLO_XSMP_RESTART_IF_RUNNING;
	hint = ((unsigned char *)prop->vals[0].value)[0];
	return hint <= SmRestartNever ? (PoloXsmpRestartStyle)hint
				      : POLO_XSMP_RESTART_IF_RUNNING;
}

static void
polo_xsmp_client_finalize(GObject *object)
{
	PoloXsmpClient *client = POLO_XSMP_CLIENT(object);

	client->server = NULL;
	polo_xsmp_client_close(client);
	for (guint i = 0; i < client->props->len; i++)
		SmFreeProperty(client->props->pdata[i]);
	g_ptr_array_free(client->props, TRUE);
	g_free(client->reply_id);
	G_OBJECT_CLASS(polo_xsmp_client_parent_class)->finalize(object);
}

static void
polo_xsmp_client_class_init(PoloXsmpClientClass *klass)
{
	GType type = G_TYPE_FROM_CLASS(klass);

	G_OBJECT_CLASS(klass)->finalize = polo_xsmp_client_finalize;
	client_signals[CLIENT_REGISTER_REQUEST] =
		g_signal_new("register-request", type, G_SIGNAL_RUN_LAST, 0,
			     NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);
	client_signals[CLIENT_REGISTERED] =
		g_signal_new("registered", type, G_SIGNAL_RUN_LAST, 0, NULL,
			     NULL, NULL, G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
	client_signals[CLIENT_INTERACT_REQUEST] =
		g_signal_new("interact-request", type, G_SIGNAL_RUN_LAST, 0,
			     NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_INT);
	client_signals[CLIENT_INTERACT_DONE] =
		g_signal_new("interact-done", type, G_SIGNAL_RUN_LAST, 0, NULL,
			     NULL, NULL, G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
	client_signals[CLIENT_SAVE_YOURSELF_REQUEST] = g_signal_new(
		"save-yourself-request", type, G_SIGNAL_RUN_LAST, 0, NULL, NULL,
		NULL, G_TYPE_NONE, 5, G_TYPE_INT, G_TYPE_BOOLEAN, G_TYPE_INT,
		G_TYPE_BOOLEAN, G_TYPE_BOOLEAN);
	client_signals[CLIENT_SAVE_YOURSELF_PHASE2_REQUEST] = g_signal_new(
		"save-yourself-phase2-request", type, G_SIGNAL_RUN_LAST, 0,
		NULL, NULL, NULL, G_TYPE_NONE, 0);
	client_signals[CLIENT_SAVE_YOURSELF_DONE] =
		g_signal_new("save-yourself-done", type, G_SIGNAL_RUN_LAST, 0,
			     NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
	client_signals[CLIENT_PROPERTIES_CHANGED] =
		g_signal_new("properties-changed", type, G_SIGNAL_RUN_LAST, 0,
			     NULL, NULL, NULL, G_TYPE_NONE, 0);
	client_signals[CLIENT_CLOSED] =
		g_signal_new("closed", type, G_SIGNAL_RUN_LAST, 0, NULL, NULL,
			     NULL, G_TYPE_NONE, 0);
	client_signals[CLIENT_DISCONNECTED] =
		g_signal_new("disconnected", type, G_SIGNAL_RUN_LAST, 0, NULL,
			     NULL, NULL, G_TYPE_NONE, 0);
}

static void
polo_xsmp_client_init(PoloXsmpClient *client)
{
	client->props = g_ptr_array_new();
}

/* ---- Server ------------------------------------------------------------ */

static Status
accept_xsmp(SmsConn conn, SmPointer data, unsigned long *mask,
	    SmsCallbacks *callbacks, char **failure_reason)
{
	PoloXsmpServer *server = the_server;
	IceConn ice = SmsGetIceConnection(conn);
	IceWatch *watch = ice->context;
	PoloXsmpClient *client;

	(void)data;
	if (!server || !server->accepting) {
		*failure_reason = strdup(
			"Refusing new client connection because the session is "
			"currently being shut down\n");
		return False;
	}
	if (watch) {
		ice->context = NULL;
		ice_watch_free(watch);
	}
	client = client_new(server, conn, ice, mask, callbacks);
	g_ptr_array_add(server->clients, client);
	g_signal_emit(server, server_signals[SERVER_NEW_CLIENT], 0, client);
	return True;
}

static void
ice_error_handler(IceConn conn, Bool swap, int opcode, unsigned long sequence,
		  int error_class, int severity, IcePointer values)
{
	if (severity != IceCanContinue)
		IceCloseConnection(conn);
}

static void
ice_io_error_handler(IceConn conn)
{
}

static void
sms_error_handler(SmsConn conn, Bool swap, int opcode, unsigned long sequence,
		  int error_class, int severity, IcePointer values)
{
}

static IceAuthFileEntry *
auth_entry_new(const char *protocol, const char *network_id)
{
	IceAuthFileEntry *entry = malloc(sizeof *entry);
	IceAuthDataEntry data;

	entry->protocol_name = strdup(protocol);
	entry->protocol_data = NULL;
	entry->protocol_data_length = 0;
	entry->network_id = strdup(network_id);
	entry->auth_name = strdup(ICE_MAGIC_COOKIE_AUTH_NAME);
	entry->auth_data = IceGenerateMagicCookie(ICE_MAGIC_COOKIE_LEN);
	entry->auth_data_length = ICE_MAGIC_COOKIE_LEN;

	data.protocol_name = entry->protocol_name;
	data.network_id = entry->network_id;
	data.auth_name = entry->auth_name;
	data.auth_data = entry->auth_data;
	data.auth_data_length = entry->auth_data_length;
	IceSetPaAuthData(1, &data);
	return entry;
}

/* drops stale entries for our sockets and adds
 * fresh ICE and XSMP cookies. */
static gboolean
update_iceauthority(PoloXsmpServer *server, gboolean adding)
{
	char *filename = IceAuthFileName();
	char **ids;
	GSList *entries = NULL;
	IceAuthFileEntry *entry;
	gboolean ok = FALSE;
	FILE *fp;

	if (IceLockAuthFile(filename, ICE_AUTH_RETRIES, ICE_AUTH_INTERVAL,
			    ICE_AUTH_LOCK_TIMEOUT) != IceAuthLockSuccess)
		return FALSE;
	ids = g_new0(char *, server->n_local_sockets + 1);
	for (int i = 0; i < server->n_local_sockets; i++)
		ids[i] = IceGetListenConnectionString(server->sockets[i]);

	fp = fopen(filename, "r+");
	if (fp) {
		while ((entry = IceReadAuthFileEntry(fp)) != NULL) {
			gboolean ours = !entry->network_id;

			for (int i = 0; !ours && i < server->n_local_sockets;
			     i++)
				ours = strcmp(entry->network_id, ids[i]) == 0;
			if (ours) {
				IceFreeAuthFileEntry(entry);
				continue;
			}
			entries = g_slist_prepend(entries, entry);
		}
		if (fseek(fp, 0, SEEK_SET) != 0) {
			for (GSList *e = entries; e; e = e->next)
				IceFreeAuthFileEntry(e->data);
			g_slist_free(entries);
			fclose(fp);
			goto out;
		}
	} else {
		int fd;

		if (g_file_test(filename, G_FILE_TEST_EXISTS))
			goto out;
		fd = open(filename, O_CREAT | O_WRONLY, 0600);
		fp = fd >= 0 ? fdopen(fd, "w") : NULL;
		if (!fp) {
			if (fd >= 0)
				close(fd);
			goto out;
		}
	}
	if (adding) {
		for (int i = 0; i < server->n_local_sockets; i++) {
			entries = g_slist_append(entries,
						 auth_entry_new("ICE", ids[i]));
			entries = g_slist_prepend(
				entries, auth_entry_new("XSMP", ids[i]));
		}
	}
	for (GSList *e = entries; e; e = e->next) {
		IceWriteAuthFileEntry(fp, e->data);
		IceFreeAuthFileEntry(e->data);
	}
	g_slist_free(entries);
	fflush(fp);
	if (ftruncate(fileno(fp), ftell(fp)) != 0)
		g_warning("cannot truncate %s", filename);
	fclose(fp);
	ok = TRUE;
out:
	IceUnlockAuthFile(filename);
	for (int i = 0; i < server->n_local_sockets; i++)
		free(ids[i]);
	g_free(ids);
	return ok;
}

PoloXsmpServer *
polo_xsmp_server_new(const char *vendor, const char *release, GError **error)
{
	static gboolean initialized;
	PoloXsmpServer *server;
	char message[256];
	mode_t saved_umask;

	if (the_server) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_EXISTS,
			    "an XSMP server is already running");
		return NULL;
	}
	server = g_object_new(POLO_XSMP_TYPE_SERVER, NULL);
	IceSetErrorHandler(ice_error_handler);
	IceSetIOErrorHandler(ice_io_error_handler);
	SmsSetErrorHandler(sms_error_handler);
	if (!initialized &&
	    !SmsInitialize((char *)vendor, (char *)release, accept_xsmp, NULL,
			   NULL, sizeof message, message)) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
			    "Could not initialize libSM: %s", message);
		g_object_unref(server);
		return NULL;
	}
	initialized = TRUE;

	saved_umask = umask(0);
	umask(saved_umask);
	if (!IceListenForConnections(&server->n_sockets, &server->sockets,
				     sizeof message, message)) {
		umask(saved_umask);
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
			    "Could not create ICE listening socket: %s",
			    message);
		g_object_unref(server);
		return NULL;
	}
	umask(saved_umask);

	for (int i = 0; i < server->n_sockets; i++) {
		char *id = IceGetListenConnectionString(server->sockets[i]);

		if (g_str_has_prefix(id, "local/") ||
		    g_str_has_prefix(id, "unix/")) {
			IceListenObj tmp = server->sockets[i];

			server->sockets[i] =
				server->sockets[server->n_local_sockets];
			server->sockets[server->n_local_sockets++] = tmp;
		}
		free(id);
	}
	if (server->n_local_sockets == 0) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
			    "IceListenForConnections did not return a local "
			    "listener");
		g_object_unref(server);
		return NULL;
	}
	if (!update_iceauthority(server, TRUE)) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
			    "Could not update ICEauthority file %s",
			    IceAuthFileName());
		g_object_unref(server);
		return NULL;
	}
	{
		char *ids = IceComposeNetworkIdList(server->n_local_sockets,
						    server->sockets);

		server->network_ids = g_strdup(ids);
		free(ids);
	}
	for (int i = 0; i < server->n_local_sockets; i++) {
		GIOChannel *channel = g_io_channel_unix_new(
			IceGetListenConnectionNumber(server->sockets[i]));
		guint id =
			g_io_add_watch(channel, G_IO_IN | G_IO_HUP | G_IO_ERR,
				       accept_ice, server->sockets[i]);

		g_array_append_val(server->watches, id);
		g_io_channel_unref(channel);
	}
	server->accepting = TRUE;
	the_server = server;
	return server;
}

const char *
polo_xsmp_server_get_network_ids(PoloXsmpServer *server)
{
	g_return_val_if_fail(POLO_XSMP_IS_SERVER(server), NULL);
	return server->network_ids;
}

void
polo_xsmp_server_set_accepting(PoloXsmpServer *server, gboolean accepting)
{
	g_return_if_fail(POLO_XSMP_IS_SERVER(server));
	server->accepting = accepting;
}

static void
polo_xsmp_server_finalize(GObject *object)
{
	PoloXsmpServer *server = POLO_XSMP_SERVER(object);

	if (the_server == server) {
		update_iceauthority(server, FALSE);
		the_server = NULL;
	}
	for (guint i = 0; i < server->watches->len; i++)
		g_source_remove(g_array_index(server->watches, guint, i));
	g_array_free(server->watches, TRUE);
	while (server->clients->len > 0) {
		PoloXsmpClient *client =
			g_object_ref(server->clients->pdata[0]);

		polo_xsmp_client_close(client);
		g_object_unref(client);
	}
	g_ptr_array_free(server->clients, TRUE);
	if (server->sockets)
		IceFreeListenObjs(server->n_sockets, server->sockets);
	g_free(server->network_ids);
	G_OBJECT_CLASS(polo_xsmp_server_parent_class)->finalize(object);
}

static void
polo_xsmp_server_class_init(PoloXsmpServerClass *klass)
{
	G_OBJECT_CLASS(klass)->finalize = polo_xsmp_server_finalize;
	server_signals[SERVER_NEW_CLIENT] = g_signal_new(
		"new-client", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0,
		NULL, NULL, NULL, G_TYPE_NONE, 1, POLO_XSMP_TYPE_CLIENT);
}

static void
polo_xsmp_server_init(PoloXsmpServer *server)
{
	server->watches = g_array_new(FALSE, FALSE, sizeof(guint));
	server->clients = g_ptr_array_new_with_free_func(g_object_unref);
}
