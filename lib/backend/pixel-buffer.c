/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * PoloBackendPixelBuffer: a surface whose pixels Polo draws itself.
 *
 * The pixels travel the way the cursor's do (cursor.c): a private
 * in-process Wayland client attaches ordinary wl_shm buffers to its own
 * wl_surface. That uses only public API and works with both renderers.
 *
 * The server side of the client's surface appears asynchronously (the
 * client's requests are read on a later main-loop iteration), so "ready"
 * is emitted once the view exists; until then there is nothing to place.
 */

#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>

#include <glib-unix.h>
#include <libweston/libweston.h>
#include <wayland-client.h>

#include "polo-backend-private.h"

struct polo_internal_client {
	PoloBackendServer *server;
	struct wl_display *display; /* client side */
	struct wl_client *client;   /* server side */
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct wl_listener client_destroyed;
	struct wl_listener resource_created;
	guint source;
	GList *pending; /* PoloBackendPixelBuffer waiting for its surface */
	GList *pixel_buffers; /* every live PoloBackendPixelBuffer, unowned */
	GList *shm_buffers;   /* every struct pb_buffer not freed yet */
};

static void
internal_registry_global(void *data, struct wl_registry *registry,
			 uint32_t name, const char *interface, uint32_t version)
{
	struct polo_internal_client *ic = data;

	if (strcmp(interface, wl_compositor_interface.name) == 0)
		ic->compositor = wl_registry_bind(registry, name,
						  &wl_compositor_interface, 4);
	else if (strcmp(interface, wl_shm_interface.name) == 0)
		ic->shm =
			wl_registry_bind(registry, name, &wl_shm_interface, 1);
}

static void
internal_registry_remove(void *data, struct wl_registry *registry,
			 uint32_t name)
{
}

static const struct wl_registry_listener internal_registry_listener = {
	internal_registry_global,
	internal_registry_remove,
};

static gboolean
internal_dispatch(gint fd, GIOCondition condition, gpointer data)
{
	struct polo_internal_client *ic = data;

	if ((condition & (G_IO_ERR | G_IO_HUP)) ||
	    wl_display_dispatch(ic->display) < 0) {
		ic->source = 0;
		return G_SOURCE_REMOVE;
	}
	wl_display_flush(ic->display);
	return G_SOURCE_CONTINUE;
}

static void
internal_flush(struct polo_internal_client *ic)
{
	if (ic && ic->display)
		wl_display_flush(ic->display);
}

struct pb_buffer {
	struct polo_internal_client *ic;
	struct wl_buffer *buffer;
	void *pixels;
	size_t size;
	gboolean attached;
};

enum {
	SIGNAL_READY,
	N_SIGNALS,
};

static guint signals[N_SIGNALS];

struct _PoloBackendPixelBuffer {
	GObject parent_instance;

	PoloBackendServer *server;
	struct wl_surface *proxy; /* client side */
	uint32_t proxy_id;
	struct weston_surface *surface; /* server side, once known */
	struct weston_view *view;
	PoloBackendView *view_handle;
	struct wl_listener surface_destroyed;
	struct wl_listener committed;

	int width, height, scale; /* buffer pixels and buffer scale */
	struct pb_buffer *back;
	gboolean have_listeners;
};

G_DEFINE_FINAL_TYPE(PoloBackendPixelBuffer, polo_backend_pixel_buffer,
		    G_TYPE_OBJECT)

static void
pb_buffer_free(struct pb_buffer *b)
{
	if (b->ic)
		b->ic->shm_buffers = g_list_remove(b->ic->shm_buffers, b);
	if (b->buffer)
		wl_buffer_destroy(b->buffer);
	if (b->pixels)
		munmap(b->pixels, b->size);
	g_free(b);
}

static void
buffer_release(void *data, struct wl_buffer *buffer)
{
	pb_buffer_free(data);
}

static const struct wl_buffer_listener buffer_listener = {
	buffer_release,
};

static struct pb_buffer *
pb_buffer_new(struct polo_internal_client *ic, int width, int height)
{
	struct pb_buffer *b = g_new0(struct pb_buffer, 1);
	struct wl_shm_pool *pool;
	int stride = width * 4;
	int fd;

	b->size = (size_t)stride * height;
	fd = memfd_create("polo-pixel-buffer", MFD_CLOEXEC);
	if (fd < 0 || ftruncate(fd, b->size) < 0) {
		if (fd >= 0)
			close(fd);
		g_free(b);
		return NULL;
	}
	b->pixels =
		mmap(NULL, b->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (b->pixels == MAP_FAILED) {
		close(fd);
		g_free(b);
		return NULL;
	}
	b->ic = ic;
	ic->shm_buffers = g_list_prepend(ic->shm_buffers, b);
	pool = wl_shm_create_pool(ic->shm, fd, b->size);
	b->buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride,
					      WL_SHM_FORMAT_ARGB8888);
	wl_buffer_add_listener(b->buffer, &buffer_listener, b);
	wl_shm_pool_destroy(pool);
	close(fd);
	return b;
}

static void
pixel_buffer_surface_destroyed(struct wl_listener *listener, void *data)
{
	PoloBackendPixelBuffer *self =
		wl_container_of(listener, self, surface_destroyed);

	wl_list_remove(&self->surface_destroyed.link);
	wl_list_remove(&self->committed.link);
	self->have_listeners = FALSE;
	if (self->view_handle)
		polo_backend_view_destroy(self->view_handle);
	g_clear_object(&self->view_handle);
	self->view = NULL; /* destroyed with the surface */
	self->surface = NULL;
}

/* A role-less surface: map it once it has content. */
static void
pixel_buffer_committed(struct wl_listener *listener, void *data)
{
	PoloBackendPixelBuffer *self =
		wl_container_of(listener, self, committed);

	if (self->surface->buffer_ref.buffer &&
	    !weston_surface_is_mapped(self->surface)) {
		weston_surface_map(self->surface);
		/* Views may only be put in a layer once mapped. */
		if (self->view_handle)
			g_signal_emit(self, signals[SIGNAL_READY], 0);
	}
	if (self->view)
		weston_view_schedule_repaint(self->view);
}

static void
pixel_buffer_attach(PoloBackendPixelBuffer *self,
		    struct weston_surface *surface)
{
	self->surface = surface;
	self->surface_destroyed.notify = pixel_buffer_surface_destroyed;
	wl_signal_add(&surface->destroy_signal, &self->surface_destroyed);
	self->committed.notify = pixel_buffer_committed;
	wl_signal_add(&surface->commit_signal, &self->committed);
	self->have_listeners = TRUE;
	self->view = weston_view_create(surface);
	if (self->view)
		self->view_handle =
			polo_backend_view_new(self->server, self->view, NULL);
	if (surface->buffer_ref.buffer) {
		weston_surface_map(surface);
		g_signal_emit(self, signals[SIGNAL_READY], 0);
	}
}

/* Server side: a new resource of the internal client. Its user data is
 * set after this signal, so the match happens on the next idle. */
static void
internal_find_surfaces(void *data)
{
	struct polo_internal_client *ic = data;
	GList *l, *next;

	for (l = ic->pending; l; l = next) {
		PoloBackendPixelBuffer *self = l->data;
		struct wl_resource *resource;

		next = l->next;
		resource = wl_client_get_object(ic->client, self->proxy_id);
		if (!resource)
			continue;
		ic->pending = g_list_delete_link(ic->pending, l);
		pixel_buffer_attach(self, wl_resource_get_user_data(resource));
		g_object_unref(self);
	}
}

static void
internal_resource_created(struct wl_listener *listener, void *data)
{
	struct polo_internal_client *ic =
		wl_container_of(listener, ic, resource_created);
	struct wl_resource *resource = data;

	if (ic->pending &&
	    strcmp(wl_resource_get_class(resource),
		   wl_surface_interface.name) == 0)
		wl_event_loop_add_idle(
			wl_display_get_event_loop(ic->server->display),
			internal_find_surfaces, ic);
}

static void
internal_client_destroyed(struct wl_listener *listener, void *data)
{
	struct polo_internal_client *ic =
		wl_container_of(listener, ic, client_destroyed);

	wl_list_remove(&ic->client_destroyed.link);
	wl_list_remove(&ic->resource_created.link);
	ic->client = NULL;
}

/* Only during start-up, before the server's loop is attached. */
static void
internal_pump(struct polo_internal_client *ic)
{
	wl_display_flush(ic->display);
	wl_event_loop_dispatch(wl_display_get_event_loop(ic->server->display),
			       0);
	wl_display_flush_clients(ic->server->display);
}

void
polo_backend_internal_client_init(PoloBackendServer *server)
{
	struct polo_internal_client *ic =
		g_new0(struct polo_internal_client, 1);
	int sockets[2];

	ic->server = server;
	server->internal_client = ic;
	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets) < 0)
		goto fail;
	ic->client = wl_client_create(server->display, sockets[0]);
	ic->display = wl_display_connect_to_fd(sockets[1]);
	if (!ic->client || !ic->display)
		goto fail;
	ic->client_destroyed.notify = internal_client_destroyed;
	wl_client_add_destroy_listener(ic->client, &ic->client_destroyed);
	ic->resource_created.notify = internal_resource_created;
	wl_client_add_resource_created_listener(ic->client,
						&ic->resource_created);
	ic->registry = wl_display_get_registry(ic->display);
	wl_registry_add_listener(ic->registry, &internal_registry_listener, ic);
	internal_pump(ic);
	if (wl_display_dispatch(ic->display) < 0 || !ic->compositor || !ic->shm)
		goto fail;
	ic->source = g_unix_fd_add(wl_display_get_fd(ic->display),
				   G_IO_IN | G_IO_ERR | G_IO_HUP,
				   internal_dispatch, ic);
	return;
fail:
	polo_log_scope_warning(polo_backend_log_scope("polo"),
			       "cannot start the internal client; "
			       "no Polo-drawn surfaces");
	polo_backend_internal_client_fini(server);
}

void
polo_backend_internal_client_fini(PoloBackendServer *server)
{
	struct polo_internal_client *ic = server->internal_client;

	if (!ic)
		return;
	if (ic->source)
		g_source_remove(ic->source);
	g_list_free_full(ic->pending, g_object_unref);
	ic->pending = NULL;
	/* Buffers outliving the server become inert. */
	for (GList *l = ic->pixel_buffers; l; l = l->next) {
		PoloBackendPixelBuffer *pb = l->data;

		if (pb->proxy)
			wl_surface_destroy(pb->proxy);
		pb->proxy = NULL;
		pb->back = NULL; /* freed below with the other shm buffers */
	}
	g_list_free(ic->pixel_buffers);
	while (ic->shm_buffers)
		pb_buffer_free(ic->shm_buffers->data);
	if (ic->registry)
		wl_registry_destroy(ic->registry);
	if (ic->shm)
		wl_shm_destroy(ic->shm);
	if (ic->compositor)
		wl_compositor_destroy(ic->compositor);
	if (ic->display)
		wl_display_disconnect(ic->display);
	if (ic->client)
		wl_client_destroy(ic->client);
	g_free(ic);
	server->internal_client = NULL;
}

PoloBackendPixelBuffer *
polo_backend_pixel_buffer_new(PoloBackendServer *server, int width, int height,
			      int scale)
{
	struct polo_internal_client *ic;
	PoloBackendPixelBuffer *self;
	struct wl_region *region;

	g_return_val_if_fail(POLO_BACKEND_IS_SERVER(server), NULL);
	g_return_val_if_fail(width > 0 && height > 0 && scale > 0, NULL);
	self = g_object_new(POLO_BACKEND_TYPE_PIXEL_BUFFER, NULL);
	self->server = server;
	self->width = width;
	self->height = height;
	self->scale = scale;

	ic = server->internal_client;
	if (!ic || !ic->compositor)
		return self; /* never ready */

	ic->pixel_buffers = g_list_prepend(ic->pixel_buffers, self);
	self->proxy = wl_compositor_create_surface(ic->compositor);
	self->proxy_id = wl_proxy_get_id((struct wl_proxy *)self->proxy);
	/* Input goes to whatever is below (nothing, for a wallpaper). */
	region = wl_compositor_create_region(ic->compositor);
	wl_surface_set_input_region(self->proxy, region);
	wl_region_destroy(region);
	wl_surface_set_buffer_scale(self->proxy, scale);
	wl_surface_commit(self->proxy);
	ic->pending = g_list_prepend(ic->pending, g_object_ref(self));
	internal_flush(ic);
	return self;
}

gboolean
polo_backend_pixel_buffer_is_ready(PoloBackendPixelBuffer *self)
{
	g_return_val_if_fail(POLO_BACKEND_IS_PIXEL_BUFFER(self), FALSE);
	return self->view_handle != NULL && self->surface &&
		weston_surface_is_mapped(self->surface);
}

PoloBackendView *
polo_backend_pixel_buffer_get_view(PoloBackendPixelBuffer *self)
{
	g_return_val_if_fail(POLO_BACKEND_IS_PIXEL_BUFFER(self), NULL);
	return polo_backend_pixel_buffer_is_ready(self) ? self->view_handle
							: NULL;
}

void
polo_backend_pixel_buffer_get_size(PoloBackendPixelBuffer *self, int *width,
				   int *height, int *scale)
{
	g_return_if_fail(POLO_BACKEND_IS_PIXEL_BUFFER(self));
	*width = self->width;
	*height = self->height;
	*scale = self->scale;
}

/*
 * Memory for the next frame: width × height ARGB32 pixels, premultiplied,
 * native endian (cairo's CAIRO_FORMAT_ARGB32), stride width * 4. It may be
 * written from another thread until commit (). NULL without an internal
 * client.
 */
guint8 *
polo_backend_pixel_buffer_begin(PoloBackendPixelBuffer *self, int *length,
				int *stride)
{
	struct polo_internal_client *ic;

	g_return_val_if_fail(POLO_BACKEND_IS_PIXEL_BUFFER(self), NULL);
	*length = 0;
	*stride = self->width * 4;
	ic = self->server->internal_client;
	if (!self->proxy || !ic || !ic->shm)
		return NULL;
	if (!self->back)
		self->back = pb_buffer_new(ic, self->width, self->height);
	if (!self->back)
		return NULL;
	*length = (int)self->back->size;
	return self->back->pixels;
}

void
polo_backend_pixel_buffer_commit(PoloBackendPixelBuffer *self)
{
	struct pb_buffer *b;

	g_return_if_fail(POLO_BACKEND_IS_PIXEL_BUFFER(self));
	b = self->back;
	if (!b || !self->proxy)
		return;
	self->back = NULL;
	b->attached = TRUE;
	wl_surface_attach(self->proxy, b->buffer, 0, 0);
	wl_surface_damage_buffer(self->proxy, 0, 0, self->width, self->height);
	wl_surface_commit(self->proxy);
	internal_flush(self->server->internal_client);
}

static void
polo_backend_pixel_buffer_dispose(GObject *object)
{
	PoloBackendPixelBuffer *self = POLO_BACKEND_PIXEL_BUFFER(object);
	struct polo_internal_client *ic =
		self->server ? self->server->internal_client : NULL;

	if (ic)
		ic->pixel_buffers = g_list_remove(ic->pixel_buffers, self);

	if (self->have_listeners) {
		wl_list_remove(&self->surface_destroyed.link);
		wl_list_remove(&self->committed.link);
		self->have_listeners = FALSE;
	}
	if (self->view_handle)
		polo_backend_view_destroy(self->view_handle);
	g_clear_object(&self->view_handle);
	if (self->view)
		weston_view_destroy(self->view);
	self->view = NULL;
	self->surface = NULL;
	if (self->back) {
		pb_buffer_free(self->back);
		self->back = NULL;
	}
	if (self->proxy) {
		/* Attached buffers free themselves on release, or with the
		 * client at shutdown. */
		wl_surface_destroy(self->proxy);
		self->proxy = NULL;
		internal_flush(self->server->internal_client);
	}
	G_OBJECT_CLASS(polo_backend_pixel_buffer_parent_class)->dispose(object);
}

static void
polo_backend_pixel_buffer_class_init(PoloBackendPixelBufferClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->dispose = polo_backend_pixel_buffer_dispose;
	signals[SIGNAL_READY] = g_signal_new("ready", G_TYPE_FROM_CLASS(klass),
					     G_SIGNAL_RUN_LAST, 0, NULL, NULL,
					     NULL, G_TYPE_NONE, 0);
}

static void
polo_backend_pixel_buffer_init(PoloBackendPixelBuffer *self)
{
}
