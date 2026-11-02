/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A private in-process Wayland client supplies ordinary SHM cursor buffers.
 * It uses public APIs, so it works with both GL and Pixman renderers.
 * Images are cached at startup; pointer motion only moves existing views.
 */
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
#include <X11/Xcursor/Xcursor.h>
#include <wayland-client.h>
#include <glib-unix.h>
#include "polo-backend-private.h"

#define CURSOR_SCALES 8
struct cursor_image {
	struct wl_surface *proxy;
	/* libweston reads it at the first paint, which can be much later */
	struct wl_buffer *buffer;
	struct weston_surface *surface;
	int hotspot_x, hotspot_y;
};
struct polo_cursor {
	PoloBackendServer *server;
	struct wl_display *display;
	struct wl_client *client;
	struct wl_listener client_destroy;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct cursor_image images[CURSOR_SCALES];
	struct cursor_image shapes[37][CURSOR_SCALES];
	struct wl_list pointers;
	guint source;
};
struct cursor_pointer {
	struct polo_cursor *cursor;
	struct weston_pointer *pointer;
	struct weston_view *views[CURSOR_SCALES];
	struct wl_listener motion, focus, destroy;
	struct wl_list link;
	int visible;
	struct cursor_image *pending_image;
	struct wl_callback *pending_callback;
	int pending_index;
	uint32_t pending_serial;
};

static void
registry_global(void *data, struct wl_registry *registry, uint32_t name,
		const char *interface, uint32_t version)
{
	struct polo_cursor *cursor = data;
	if (strcmp(interface, "wl_compositor") == 0)
		cursor->compositor = wl_registry_bind(
			registry, name, &wl_compositor_interface, 4);
	else if (strcmp(interface, "wl_shm") == 0)
		cursor->shm =
			wl_registry_bind(registry, name, &wl_shm_interface, 1);
}
static void
registry_remove(void *data, struct wl_registry *registry, uint32_t name)
{
}
static const struct wl_registry_listener registry_listener = {
	registry_global, registry_remove
};

static int
output_index(struct cursor_pointer *cp)
{
	struct weston_pointer *pointer = cp->pointer;
	struct weston_output *output;

	wl_list_for_each(output, &cp->cursor->server->compositor->output_list,
			 link)
	{
		if (pixman_region32_contains_point(&output->region,
						   pointer->pos.c.x,
						   pointer->pos.c.y, NULL))
			return CLAMP(output->current_scale, 1, CURSOR_SCALES) -
				1;
	}
	return 0;
}

static void
update_pointer(struct cursor_pointer *cp)
{
	struct weston_pointer *pointer = cp->pointer;
	int index = output_index(cp);

	if (pointer->focus)
		index = -1; /* Clients own their cursor, including explicitly
			       hiding it. */
	if (index >= 0 && !cp->views[index] &&
	    cp->cursor->images[index].surface)
		cp->views[index] =
			weston_view_create(cp->cursor->images[index].surface);
	if (index >= 0 && !cp->views[index])
		index = -1;
	if (cp->visible >= 0 && cp->visible != index)
		weston_view_unmap(cp->views[cp->visible]);
	cp->visible = index;
	if (index >= 0) {
		struct weston_view *view = cp->views[index];
		struct cursor_image *image = &cp->cursor->images[index];
		struct weston_coord_global pos = pointer->pos;
		pos.c.x -= (double)image->hotspot_x / (index + 1);
		pos.c.y -= (double)image->hotspot_y / (index + 1);
		weston_view_set_position(view, pos);
		weston_view_move_to_layer(view,
					  &cp->cursor->server->compositor
						   ->cursor_layer.view_list);
		weston_surface_map(view->surface);
		weston_view_update_transform(view);
		weston_view_schedule_repaint(view);
	}
}
static void
cursor_motion(struct wl_listener *listener, void *data)
{
	struct cursor_pointer *cp = wl_container_of(listener, cp, motion);
	update_pointer(cp);
}
static void
cursor_focus(struct wl_listener *listener, void *data)
{
	struct cursor_pointer *cp = wl_container_of(listener, cp, focus);
	update_pointer(cp);
}
static void
cursor_destroy(struct wl_listener *listener, void *data)
{
	struct cursor_pointer *cp = wl_container_of(listener, cp, destroy);
	for (int i = 0; i < CURSOR_SCALES; i++)
		if (cp->views[i])
			weston_view_destroy(cp->views[i]);
	if (cp->pending_callback)
		wl_callback_destroy(cp->pending_callback);
	wl_list_remove(&cp->motion.link);
	wl_list_remove(&cp->focus.link);
	wl_list_remove(&cp->destroy.link);
	wl_list_remove(&cp->link);
	g_free(cp);
}
void
polo_backend_cursor_add_pointer(PoloBackendServer *server,
				struct weston_pointer *pointer)
{
	struct polo_cursor *cursor = server->cursor;
	struct cursor_pointer *cp;
	if (!cursor || !pointer)
		return;
	wl_list_for_each(cp, &cursor->pointers,
			 link) if (cp->pointer == pointer) return;
	cp = g_new0(struct cursor_pointer, 1);
	cp->cursor = cursor;
	cp->pointer = pointer;
	cp->visible = -1;
	cp->motion.notify = cursor_motion;
	cp->focus.notify = cursor_focus;
	cp->destroy.notify = cursor_destroy;
	wl_signal_add(&pointer->motion_signal, &cp->motion);
	wl_signal_add(&pointer->focus_signal, &cp->focus);
	wl_signal_add(&pointer->destroy_signal, &cp->destroy);
	wl_list_insert(&cursor->pointers, &cp->link);
	update_pointer(cp);
}

static gboolean
cursor_dispatch(gint fd, GIOCondition condition, gpointer data)
{
	struct polo_cursor *cursor = data;
	if ((condition & (G_IO_ERR | G_IO_HUP)) ||
	    wl_display_dispatch(cursor->display) < 0) {
		cursor->source = 0;
		return G_SOURCE_REMOVE;
	}
	wl_display_flush(cursor->display);
	return G_SOURCE_CONTINUE;
}

/* Only used during startup, before the compositor main loop is attached. */
static void
pump(struct polo_cursor *cursor)
{
	wl_display_flush(cursor->display);
	wl_event_loop_dispatch(
		wl_display_get_event_loop(cursor->server->display), 0);
	wl_display_flush_clients(cursor->server->display);
}

static void
client_destroyed(struct wl_listener *listener, void *data)
{
	struct polo_cursor *cursor =
		wl_container_of(listener, cursor, client_destroy);
	struct cursor_pointer *cp, *next;
	wl_list_for_each_safe(cp, next, &cursor->pointers, link)
		cursor_destroy(&cp->destroy, NULL);
	for (int i = 0; i < CURSOR_SCALES; i++) {
		cursor->images[i].surface = NULL;
		for (int shape = 0; shape < 37; shape++)
			cursor->shapes[shape][i].surface = NULL;
	}
	cursor->client = NULL;
	wl_list_remove(&cursor->client_destroy.link);
}

static XcursorImage *
find_image(struct polo_cursor *cursor, const char *name, const char *fallback,
	   int size)
{
	XcursorImage *xc = XcursorLibraryLoadImage(
		name, cursor->server->cursor_theme, size);

	if (!xc && fallback)
		xc = XcursorLibraryLoadImage(
			fallback, cursor->server->cursor_theme, size);
	return xc;
}

static void
load_image(struct polo_cursor *cursor, const char *name, const char *fallback,
	   int i, struct cursor_image *image)
{
	XcursorImage *xc = find_image(cursor, name, fallback,
				      cursor->server->cursor_size * (i + 1));
	struct wl_shm_pool *pool;
	struct wl_region *region;
	int fd, width, height;
	size_t size;
	void *pixels;

	if (!xc)
		return;
	width = ((xc->width + i) / (i + 1)) * (i + 1);
	height = ((xc->height + i) / (i + 1)) * (i + 1);
	size = (size_t)width * height * 4;
	fd = memfd_create("polo-cursor", MFD_CLOEXEC);
	if (fd < 0 || ftruncate(fd, size) < 0) {
		if (fd >= 0)
			close(fd);
		XcursorImageDestroy(xc);
		return;
	}
	pixels = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (pixels == MAP_FAILED) {
		close(fd);
		XcursorImageDestroy(xc);
		return;
	}
	for (unsigned row = 0; row < xc->height; row++)
		memcpy((char *)pixels + row * width * 4,
		       xc->pixels + row * xc->width, xc->width * 4);
	pool = wl_shm_create_pool(cursor->shm, fd, size);
	image->buffer = wl_shm_pool_create_buffer(
		pool, 0, width, height, width * 4, WL_SHM_FORMAT_ARGB8888);
	image->proxy = wl_compositor_create_surface(cursor->compositor);
	image->hotspot_x = xc->xhot;
	image->hotspot_y = xc->yhot;
	region = wl_compositor_create_region(cursor->compositor);
	wl_surface_set_input_region(image->proxy, region);
	wl_region_destroy(region);
	wl_surface_set_buffer_scale(image->proxy, i + 1);
	wl_surface_attach(image->proxy, image->buffer, 0, 0);
	wl_surface_damage(image->proxy, 0, 0, xc->width, xc->height);
	wl_surface_commit(image->proxy);
	wl_shm_pool_destroy(pool);
	munmap(pixels, size);
	close(fd);
	XcursorImageDestroy(xc);
}

static void
resolve_image(struct polo_cursor *cursor, struct cursor_image *image)
{
	struct wl_resource *resource;

	if (!image->proxy || !cursor->client)
		return;
	resource = wl_client_get_object(
		cursor->client,
		wl_proxy_get_id((struct wl_proxy *)image->proxy));
	if (resource)
		image->surface = wl_resource_get_user_data(resource);
}

/* CSS names first, which current themes have, then the X names of old ones */
static const char *const shape_names[][2] = {
	[1] = { "default", "left_ptr" },
	[2] = { "context-menu", "left_ptr" },
	[3] = { "help", "question_arrow" },
	[4] = { "pointer", "hand2" },
	[5] = { "progress", "left_ptr_watch" },
	[6] = { "wait", "watch" },
	[7] = { "cell", "plus" },
	[8] = { "crosshair", "cross" },
	[9] = { "text", "xterm" },
	[10] = { "vertical-text", "xterm" },
	[11] = { "alias", "dnd-link" },
	[12] = { "copy", "dnd-copy" },
	[13] = { "move", "dnd-move" },
	[14] = { "no-drop", "dnd-none" },
	[15] = { "not-allowed", "crossed_circle" },
	[16] = { "grab", "hand1" },
	[17] = { "grabbing", "fleur" },
	[18] = { "e-resize", "right_side" },
	[19] = { "n-resize", "top_side" },
	[20] = { "ne-resize", "top_right_corner" },
	[21] = { "nw-resize", "top_left_corner" },
	[22] = { "s-resize", "bottom_side" },
	[23] = { "se-resize", "bottom_right_corner" },
	[24] = { "sw-resize", "bottom_left_corner" },
	[25] = { "w-resize", "left_side" },
	[26] = { "ew-resize", "sb_h_double_arrow" },
	[27] = { "ns-resize", "sb_v_double_arrow" },
	[28] = { "nesw-resize", "fd_double_arrow" },
	[29] = { "nwse-resize", "bd_double_arrow" },
	[30] = { "col-resize", "sb_h_double_arrow" },
	[31] = { "row-resize", "sb_v_double_arrow" },
	[32] = { "all-scroll", "fleur" },
	[33] = { "zoom-in", "plus" },
	[34] = { "zoom-out", "left_ptr" },
	[35] = { "dnd-ask", "dnd-copy" },
	[36] = { "all-resize", "fleur" },
};

static void
apply_shape(struct cursor_pointer *cp, struct cursor_image *image, int index)
{
	if (!image->surface)
		return;
	weston_pointer_set_sprite(cp->pointer, image->surface,
				  image->hotspot_x / (index + 1),
				  image->hotspot_y / (index + 1), NULL);
}

static void
shape_loaded(void *data, struct wl_callback *callback, uint32_t time)
{
	struct cursor_pointer *cp = data;
	struct polo_cursor *cursor = cp->cursor;
	struct cursor_image *image = cp->pending_image;

	wl_callback_destroy(callback);
	cp->pending_callback = NULL;
	if (!image)
		return;
	resolve_image(cursor, image);
	cp->pending_image = NULL;
	if (cp->pointer->focus &&
	    cp->pointer->focus_serial == cp->pending_serial)
		apply_shape(cp, image, cp->pending_index);
}

static const struct wl_callback_listener shape_loaded_listener = {
	shape_loaded,
};

int
polo_backend_cursor_n_shapes(void)
{
	return G_N_ELEMENTS(shape_names);
}

void
polo_backend_cursor_set_shape(PoloBackendServer *server,
			      struct weston_pointer *pointer, uint32_t shape)
{
	struct polo_cursor *cursor = server->cursor;
	struct cursor_pointer *cp, *found = NULL;
	struct cursor_image *image;
	int index;

	if (!cursor || !cursor->client || shape == 0 ||
	    shape >= G_N_ELEMENTS(shape_names))
		return;
	wl_list_for_each(cp, &cursor->pointers, link)
	{
		if (cp->pointer == pointer)
			found = cp;
	}
	if (!found)
		return;
	index = output_index(found);
	image = &cursor->shapes[shape][index];
	found->pending_image = NULL;
	if (image->surface) {
		apply_shape(found, image, index);
		return;
	}
	if (!image->proxy)
		load_image(cursor, shape_names[shape][0], shape_names[shape][1],
			   index, image);
	if (!image->proxy)
		load_image(cursor, "left_ptr", NULL, index, image);
	if (!image->proxy)
		return;
	found->pending_image = image;
	found->pending_index = index;
	found->pending_serial = pointer->focus_serial;
	if (!found->pending_callback) {
		found->pending_callback = wl_display_sync(cursor->display);
		wl_callback_add_listener(found->pending_callback,
					 &shape_loaded_listener, found);
	}
	wl_display_flush(cursor->display);
}

void
polo_backend_cursor_init(PoloBackendServer *server)
{
	struct polo_cursor *cursor = g_new0(struct polo_cursor, 1);
	struct weston_seat *seat;
	int sockets[2];
	cursor->server = server;
	wl_list_init(&cursor->pointers);
	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets) < 0) {
		g_free(cursor);
		return;
	}
	cursor->client = wl_client_create(server->display, sockets[0]);
	cursor->display = wl_display_connect_to_fd(sockets[1]);
	server->cursor = cursor;
	if (!cursor->client || !cursor->display)
		goto fail;
	cursor->client_destroy.notify = client_destroyed;
	wl_client_add_destroy_listener(cursor->client, &cursor->client_destroy);
	cursor->registry = wl_display_get_registry(cursor->display);
	wl_registry_add_listener(cursor->registry, &registry_listener, cursor);
	pump(cursor);
	if (wl_display_dispatch(cursor->display) < 0 || !cursor->compositor ||
	    !cursor->shm)
		goto fail;
	for (int i = 0; i < CURSOR_SCALES; i++)
		load_image(cursor, "left_ptr", NULL, i, &cursor->images[i]);
	pump(cursor);
	if (!cursor->client)
		goto fail;
	for (int i = 0; i < CURSOR_SCALES; i++)
		resolve_image(cursor, &cursor->images[i]);
	cursor->source = g_unix_fd_add(wl_display_get_fd(cursor->display),
				       G_IO_IN | G_IO_ERR | G_IO_HUP,
				       cursor_dispatch, cursor);
	wl_list_for_each(seat, &server->compositor->seat_list, link)
		polo_backend_cursor_add_pointer(server,
						weston_seat_get_pointer(seat));
	return;
fail:
	polo_backend_cursor_fini(server);
}

static void
release_image(struct cursor_image *image)
{
	if (image->proxy)
		wl_proxy_destroy((struct wl_proxy *)image->proxy);
	if (image->buffer)
		wl_buffer_destroy(image->buffer);
}

void
polo_backend_cursor_fini(PoloBackendServer *server)
{
	struct polo_cursor *cursor = server->cursor;
	struct cursor_pointer *cp, *next;
	if (!cursor)
		return;
	if (cursor->source)
		g_source_remove(cursor->source);
	wl_list_for_each_safe(cp, next, &cursor->pointers, link)
		cursor_destroy(&cp->destroy, NULL);
	for (int i = 0; i < CURSOR_SCALES; i++) {
		release_image(&cursor->images[i]);
		for (int shape = 0; shape < 37; shape++)
			release_image(&cursor->shapes[shape][i]);
	}
	if (cursor->registry)
		wl_registry_destroy(cursor->registry);
	if (cursor->shm)
		wl_shm_destroy(cursor->shm);
	if (cursor->compositor)
		wl_compositor_destroy(cursor->compositor);
	if (cursor->display)
		wl_display_disconnect(cursor->display);
	if (cursor->client)
		wl_client_destroy(cursor->client);
	g_free(cursor);
	server->cursor = NULL;
}
