/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * PoloBackendClient: who a Wayland client is (credentials, executable) and
 * whether Polo trusts it. The trust decision is Vala's; this file only
 * records facts and the flag. It also keeps a client from holding more
 * objects than any real program needs: past a limit the client gets
 * no_memory and is disconnected, instead of exhausting Polo's memory.
 */

#include <sys/types.h>

#include <wayland-server-core.h>

#include "polo-backend-private.h"

struct _PoloBackendClient {
	GObject parent_instance;

	PoloBackendServer *server;
	struct wl_client *client; /* NULL once disconnected */
	struct wl_listener destroy_listener;
	struct wl_listener resource_listener;
	guint created_since_check;
	guint live_at_check;
	pid_t pid;
	uid_t uid;
	char *executable;
	gboolean trusted;
	gboolean xserver;
	char *sandbox_engine;
	char *sandbox_app_id;
	char *sandbox_instance_id;
};

enum {
	SIGNAL_DESTROYED,
	N_SIGNALS,
};

static guint signals[N_SIGNALS];

G_DEFINE_FINAL_TYPE(PoloBackendClient, polo_backend_client, G_TYPE_OBJECT)

#define MAX_OBJECTS 200000

static const struct {
	const char *interface;
	guint max;
} limits[] = {
	{ "wl_surface", 16384 },
	{ "wl_shm_pool", 1024 },
	{ "xdg_activation_token_v1", 1024 },
	{ "zwp_idle_inhibitor_v1", 256 },
	{ "zwp_keyboard_shortcuts_inhibitor_v1", 256 },
};

struct tally {
	guint total;
	guint counts[G_N_ELEMENTS(limits)];
};

static enum wl_iterator_result
count_resource(struct wl_resource *resource, void *data)
{
	struct tally *tally = data;
	const char *interface = wl_resource_get_class(resource);

	tally->total++;
	for (gsize i = 0; i < G_N_ELEMENTS(limits); i++) {
		if (g_strcmp0(interface, limits[i].interface) == 0) {
			tally->counts[i]++;
			break;
		}
	}
	return WL_ITERATOR_CONTINUE;
}

static void
refuse(PoloBackendClient *self, const char *what, guint count)
{
	polo_log_scope_warning(
		polo_backend_log_scope("polo-protocol"),
		"client pid %d (%s) holds %u %s: disconnected", (int)self->pid,
		self->executable ? self->executable : "?", count, what);
	wl_client_post_no_memory(self->client);
}

static void
resource_created(struct wl_listener *listener, void *data)
{
	PoloBackendClient *self =
		wl_container_of(listener, self, resource_listener);
	struct tally tally = { 0 };

	if (self->xserver)
		return;
	if (++self->created_since_check < MAX(64u, self->live_at_check / 16))
		return;
	self->created_since_check = 0;
	wl_client_for_each_resource(self->client, count_resource, &tally);
	self->live_at_check = tally.total;
	if (tally.total > MAX_OBJECTS) {
		refuse(self, "objects", tally.total);
		return;
	}
	for (gsize i = 0; i < G_N_ELEMENTS(limits); i++) {
		if (tally.counts[i] > limits[i].max) {
			refuse(self, limits[i].interface, tally.counts[i]);
			return;
		}
	}
}

static void
client_destroyed(struct wl_listener *listener, void *data)
{
	PoloBackendClient *self =
		wl_container_of(listener, self, destroy_listener);
	struct wl_client *client = self->client;
	PoloBackendServer *server = self->server;

	polo_backend_client_destroy(self);
	/* Drops the server's reference. */
	g_hash_table_remove(server->clients, client);
}

PoloBackendClient *
polo_backend_client_new(PoloBackendServer *server, struct wl_client *client)
{
	PoloBackendClient *self = g_object_new(POLO_BACKEND_TYPE_CLIENT, NULL);
	gid_t gid;

	self->server = server;
	self->client = client;
	wl_client_get_credentials(client, &self->pid, &self->uid, &gid);
	if (self->pid > 0) {
		g_autofree char *link =
			g_strdup_printf("/proc/%d/exe", (int)self->pid);

		self->executable = g_file_read_link(link, NULL);
	}

	self->destroy_listener.notify = client_destroyed;
	wl_client_add_destroy_listener(client, &self->destroy_listener);
	self->resource_listener.notify = resource_created;
	wl_client_add_resource_created_listener(client,
						&self->resource_listener);
	return self;
}

void
polo_backend_client_destroy(PoloBackendClient *self)
{
	if (!self->client)
		return;

	wl_list_remove(&self->destroy_listener.link);
	wl_list_remove(&self->resource_listener.link);
	self->client = NULL;
	g_signal_emit(self, signals[SIGNAL_DESTROYED], 0);
}

int
polo_backend_client_get_pid(PoloBackendClient *client)
{
	g_return_val_if_fail(POLO_BACKEND_IS_CLIENT(client), 0);
	return client->pid;
}

guint
polo_backend_client_get_uid(PoloBackendClient *client)
{
	g_return_val_if_fail(POLO_BACKEND_IS_CLIENT(client), 0);
	return client->uid;
}

const char *
polo_backend_client_get_executable(PoloBackendClient *client)
{
	g_return_val_if_fail(POLO_BACKEND_IS_CLIENT(client), NULL);
	return client->executable;
}

gboolean
polo_backend_client_get_trusted(PoloBackendClient *client)
{
	g_return_val_if_fail(POLO_BACKEND_IS_CLIENT(client), FALSE);
	return client->trusted;
}

void
polo_backend_client_set_process(PoloBackendClient *client, int pid)
{
	g_autofree char *link = NULL;

	g_return_if_fail(POLO_BACKEND_IS_CLIENT(client));
	g_return_if_fail(pid > 0);
	client->pid = pid;
	link = g_strdup_printf("/proc/%d/exe", pid);
	g_free(client->executable);
	client->executable = g_file_read_link(link, NULL);
}

void
polo_backend_client_set_sandbox(PoloBackendClient *client, const char *engine,
				const char *app_id, const char *instance_id)
{
	g_return_if_fail(POLO_BACKEND_IS_CLIENT(client));
	g_free(client->sandbox_engine);
	g_free(client->sandbox_app_id);
	g_free(client->sandbox_instance_id);
	/* Having no name for its engine does not make a client unsandboxed. */
	client->sandbox_engine = g_strdup(engine ? engine : "unknown");
	client->sandbox_app_id = g_strdup(app_id);
	client->sandbox_instance_id = g_strdup(instance_id);
}

const char *
polo_backend_client_get_sandbox_engine(PoloBackendClient *client)
{
	g_return_val_if_fail(POLO_BACKEND_IS_CLIENT(client), NULL);
	return client->sandbox_engine;
}

const char *
polo_backend_client_get_sandbox_app_id(PoloBackendClient *client)
{
	g_return_val_if_fail(POLO_BACKEND_IS_CLIENT(client), NULL);
	return client->sandbox_app_id;
}

const char *
polo_backend_client_get_sandbox_instance_id(PoloBackendClient *client)
{
	g_return_val_if_fail(POLO_BACKEND_IS_CLIENT(client), NULL);
	return client->sandbox_instance_id;
}

gboolean
polo_backend_client_get_xserver(PoloBackendClient *client)
{
	g_return_val_if_fail(POLO_BACKEND_IS_CLIENT(client), FALSE);
	return client->xserver;
}

void
polo_backend_client_set_xserver(PoloBackendClient *client)
{
	g_return_if_fail(POLO_BACKEND_IS_CLIENT(client));
	client->xserver = TRUE;
}

void
polo_backend_client_set_trusted(PoloBackendClient *client, gboolean trusted)
{
	g_return_if_fail(POLO_BACKEND_IS_CLIENT(client));
	client->trusted = trusted;
}

static void
polo_backend_client_finalize(GObject *object)
{
	PoloBackendClient *self = POLO_BACKEND_CLIENT(object);

	g_free(self->executable);
	g_free(self->sandbox_engine);
	g_free(self->sandbox_app_id);
	g_free(self->sandbox_instance_id);
	G_OBJECT_CLASS(polo_backend_client_parent_class)->finalize(object);
}

static void
polo_backend_client_class_init(PoloBackendClientClass *klass)
{
	G_OBJECT_CLASS(klass)->finalize = polo_backend_client_finalize;

	signals[SIGNAL_DESTROYED] = g_signal_new(
		"destroyed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0,
		NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
polo_backend_client_init(PoloBackendClient *self)
{
}
