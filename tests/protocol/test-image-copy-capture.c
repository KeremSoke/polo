/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * ext_image_copy_capture_manager_v1 into shm: an output capture shows the
 * window, a second one waits for damage and reports less than the whole
 * output, a toplevel capture shows the window's own pixels, keeps sending
 * frames while it is still, and stops when it goes, a cursor session
 * follows the pointer, and capturing without a buffer is no_buffer.
 */
#include <poll.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "ext-foreign-toplevel-list-v1-client-protocol.h"
#include "ext-image-capture-source-v1-client-protocol.h"
#include "ext-image-copy-capture-v1-client-protocol.h"
#include "harness.h"

#define FIRST_COLOUR 0xff3070c0u
#define SECOND_COLOUR 0xff20a040u

struct client {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_shm *shm;
	struct wl_output *output;
	struct wl_seat *seat;
	struct ext_image_copy_capture_manager_v1 *manager;
	struct ext_output_image_capture_source_manager_v1 *output_sources;
	struct ext_foreign_toplevel_image_capture_source_manager_v1
		*toplevel_sources;
	struct ext_foreign_toplevel_list_v1 *list;
	struct ext_foreign_toplevel_handle_v1 *handles[16];
	char *titles[16];
	int n_handles;
};

struct session {
	struct ext_image_copy_capture_session_v1 *proxy;
	int width, height;
	uint32_t format;
	bool done, stopped;
};

struct shm_buffer {
	struct wl_buffer *buffer;
	uint32_t *pixels;
	size_t size;
	int width, height;
};

struct frame {
	struct ext_image_copy_capture_frame_v1 *proxy;
	bool ready, failed;
	uint32_t reason;
	int damage_rects;
	int64_t damage_area;
};

struct cursor {
	struct ext_image_copy_capture_cursor_session_v1 *proxy;
	bool entered;
	int x, y;
	int hotspot_x, hotspot_y;
};

static void
handle_title(void *data, struct ext_foreign_toplevel_handle_v1 *handle,
	     const char *title)
{
	struct client *c = data;

	for (int i = 0; i < c->n_handles; i++) {
		if (c->handles[i] == handle) {
			free(c->titles[i]);
			c->titles[i] = strdup(title);
		}
	}
}

static void
handle_noop(void *data, struct ext_foreign_toplevel_handle_v1 *handle)
{
}

static void
handle_string(void *data, struct ext_foreign_toplevel_handle_v1 *handle,
	      const char *value)
{
}

static const struct ext_foreign_toplevel_handle_v1_listener handle_listener = {
	.closed = handle_noop,
	.done = handle_noop,
	.title = handle_title,
	.app_id = handle_string,
	.identifier = handle_string,
};

static void
list_toplevel(void *data, struct ext_foreign_toplevel_list_v1 *list,
	      struct ext_foreign_toplevel_handle_v1 *handle)
{
	struct client *c = data;

	CHECK(c->n_handles < 16, "too many toplevels");
	c->handles[c->n_handles++] = handle;
	ext_foreign_toplevel_handle_v1_add_listener(handle, &handle_listener, c);
}

static void
list_finished(void *data, struct ext_foreign_toplevel_list_v1 *list)
{
}

static const struct ext_foreign_toplevel_list_v1_listener list_listener = {
	.toplevel = list_toplevel,
	.finished = list_finished,
};

static void
global(void *data, struct wl_registry *registry, uint32_t name,
       const char *interface, uint32_t version)
{
	struct client *c = data;

	if (strcmp(interface, ext_image_copy_capture_manager_v1_interface.name) ==
	    0)
		c->manager = wl_registry_bind(
			registry, name,
			&ext_image_copy_capture_manager_v1_interface, 1);
	else if (strcmp(interface,
			ext_output_image_capture_source_manager_v1_interface
				.name) == 0)
		c->output_sources = wl_registry_bind(
			registry, name,
			&ext_output_image_capture_source_manager_v1_interface,
			1);
	else if (strcmp(interface,
			ext_foreign_toplevel_image_capture_source_manager_v1_interface
				.name) == 0)
		c->toplevel_sources = wl_registry_bind(
			registry, name,
			&ext_foreign_toplevel_image_capture_source_manager_v1_interface,
			1);
	else if (strcmp(interface, ext_foreign_toplevel_list_v1_interface.name) ==
		 0) {
		c->list = wl_registry_bind(
			registry, name, &ext_foreign_toplevel_list_v1_interface,
			1);
		ext_foreign_toplevel_list_v1_add_listener(c->list,
							  &list_listener, c);
	} else if (strcmp(interface, wl_shm_interface.name) == 0)
		c->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	else if (strcmp(interface, wl_seat_interface.name) == 0 && !c->seat)
		c->seat = wl_registry_bind(registry, name, &wl_seat_interface,
					   1);
	else if (strcmp(interface, wl_output_interface.name) == 0 && !c->output)
		c->output = wl_registry_bind(registry, name,
					     &wl_output_interface, 1);
}

static void
global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = { global,
							       global_remove };

static void
session_buffer_size(void *data, struct ext_image_copy_capture_session_v1 *proxy,
		    uint32_t width, uint32_t height)
{
	struct session *s = data;

	s->width = (int)width;
	s->height = (int)height;
}

static void
session_shm_format(void *data, struct ext_image_copy_capture_session_v1 *proxy,
		   uint32_t format)
{
	((struct session *)data)->format = format;
}

static void
session_dmabuf_device(void *data,
		      struct ext_image_copy_capture_session_v1 *proxy,
		      struct wl_array *device)
{
}

static void
session_dmabuf_format(void *data,
		      struct ext_image_copy_capture_session_v1 *proxy,
		      uint32_t format, struct wl_array *modifiers)
{
}

static void
session_done(void *data, struct ext_image_copy_capture_session_v1 *proxy)
{
	((struct session *)data)->done = true;
}

static void
session_stopped(void *data, struct ext_image_copy_capture_session_v1 *proxy)
{
	((struct session *)data)->stopped = true;
}

static const struct ext_image_copy_capture_session_v1_listener
	session_listener = {
		.buffer_size = session_buffer_size,
		.shm_format = session_shm_format,
		.dmabuf_device = session_dmabuf_device,
		.dmabuf_format = session_dmabuf_format,
		.done = session_done,
		.stopped = session_stopped,
	};

static void
frame_transform(void *data, struct ext_image_copy_capture_frame_v1 *proxy,
		uint32_t transform)
{
}

static void
frame_damage(void *data, struct ext_image_copy_capture_frame_v1 *proxy,
	     int32_t x, int32_t y, int32_t width, int32_t height)
{
	struct frame *f = data;

	f->damage_rects++;
	f->damage_area += (int64_t)width * height;
}

static void
frame_presentation_time(void *data,
			struct ext_image_copy_capture_frame_v1 *proxy,
			uint32_t sec_hi, uint32_t sec_lo, uint32_t nsec)
{
}

static void
frame_ready(void *data, struct ext_image_copy_capture_frame_v1 *proxy)
{
	((struct frame *)data)->ready = true;
}

static void
frame_failed(void *data, struct ext_image_copy_capture_frame_v1 *proxy,
	     uint32_t reason)
{
	struct frame *f = data;

	f->failed = true;
	f->reason = reason;
}

static const struct ext_image_copy_capture_frame_v1_listener frame_listener = {
	.transform = frame_transform,
	.damage = frame_damage,
	.presentation_time = frame_presentation_time,
	.ready = frame_ready,
	.failed = frame_failed,
};

static void
cursor_enter(void *data,
	     struct ext_image_copy_capture_cursor_session_v1 *proxy)
{
	((struct cursor *)data)->entered = true;
}

static void
cursor_leave(void *data,
	     struct ext_image_copy_capture_cursor_session_v1 *proxy)
{
	((struct cursor *)data)->entered = false;
}

static void
cursor_position(void *data,
		struct ext_image_copy_capture_cursor_session_v1 *proxy,
		int32_t x, int32_t y)
{
	struct cursor *cur = data;

	cur->x = x;
	cur->y = y;
}

static void
cursor_hotspot(void *data,
	       struct ext_image_copy_capture_cursor_session_v1 *proxy,
	       int32_t x, int32_t y)
{
	struct cursor *cur = data;

	cur->hotspot_x = x;
	cur->hotspot_y = y;
}

static const struct ext_image_copy_capture_cursor_session_v1_listener
	cursor_listener = {
		.enter = cursor_enter,
		.leave = cursor_leave,
		.position = cursor_position,
		.hotspot = cursor_hotspot,
	};

static bool
wait_for(struct wl_display *display, bool *flag, int ms)
{
	struct timespec start, now;
	struct pollfd pfd = { .fd = wl_display_get_fd(display),
			      .events = POLLIN };

	clock_gettime(CLOCK_MONOTONIC, &start);
	while (!*flag) {
		long elapsed;

		wl_display_dispatch_pending(display);
		wl_display_flush(display);
		if (*flag)
			break;
		clock_gettime(CLOCK_MONOTONIC, &now);
		elapsed = (now.tv_sec - start.tv_sec) * 1000 +
			  (now.tv_nsec - start.tv_nsec) / 1000000;
		if (elapsed >= ms)
			break;
		if (poll(&pfd, 1, (int)(ms - elapsed)) > 0 &&
		    wl_display_dispatch(display) < 0) {
			const struct wl_interface *interface = NULL;
			uint32_t code = wl_display_get_protocol_error(
				display, &interface, NULL);

			CHECK(false, "the capture client got error %u on %s",
			      code, interface ? interface->name : "-");
		}
	}
	return *flag;
}

static struct shm_buffer
buffer_new(struct wl_shm *shm, int width, int height, uint32_t format)
{
	struct shm_buffer b = { .width = width, .height = height };
	int fd = memfd_create("capture", MFD_CLOEXEC);
	struct wl_shm_pool *pool;

	b.size = (size_t)width * height * 4;
	CHECK(fd >= 0 && ftruncate(fd, (off_t)b.size) == 0, "no shm");
	b.pixels = mmap(NULL, b.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
			0);
	CHECK(b.pixels != MAP_FAILED, "mmap");
	pool = wl_shm_create_pool(shm, fd, (int32_t)b.size);
	b.buffer = wl_shm_pool_create_buffer(pool, 0, width, height, width * 4,
					     format);
	wl_shm_pool_destroy(pool);
	close(fd);
	return b;
}

static void
session_open(struct client *c, struct session *s,
	     struct ext_image_capture_source_v1 *source)
{
	memset(s, 0, sizeof *s);
	s->proxy = ext_image_copy_capture_manager_v1_create_session(
		c->manager, source,
		EXT_IMAGE_COPY_CAPTURE_MANAGER_V1_OPTIONS_PAINT_CURSORS);
	ext_image_copy_capture_session_v1_add_listener(s->proxy,
						       &session_listener, s);
	wl_display_roundtrip(c->display);
}

static void
frame_start(struct frame *f, struct session *s, struct shm_buffer *b)
{
	memset(f, 0, sizeof *f);
	f->proxy = ext_image_copy_capture_session_v1_create_frame(s->proxy);
	ext_image_copy_capture_frame_v1_add_listener(f->proxy, &frame_listener,
						     f);
	ext_image_copy_capture_frame_v1_attach_buffer(f->proxy, b->buffer);
	ext_image_copy_capture_frame_v1_damage_buffer(f->proxy, 0, 0, b->width,
						      b->height);
	ext_image_copy_capture_frame_v1_capture(f->proxy);
}

static void
redraw(struct harness *h, struct harness_window *w, uint32_t colour)
{
	struct shm_buffer b = buffer_new(h->shm, w->width, w->height,
					 WL_SHM_FORMAT_ARGB8888);

	for (int i = 0; i < w->width * w->height; i++)
		b.pixels[i] = colour;
	munmap(b.pixels, b.size);
	wl_surface_attach(w->surface, b.buffer, 0, 0);
	wl_surface_damage_buffer(w->surface, 0, 0, w->width, w->height);
	if (w->buffer)
		wl_buffer_destroy(w->buffer);
	w->buffer = b.buffer;
	harness_wait_frame(h, w->surface);
}

static struct ext_foreign_toplevel_handle_v1 *
find_handle(struct client *c, const char *title)
{
	for (int i = 0; i < c->n_handles; i++) {
		if (c->titles[i] && strcmp(c->titles[i], title) == 0)
			return c->handles[i];
	}
	return NULL;
}

int
main(int argc, char **argv)
{
	struct harness *h = harness_start(argc, argv);
	struct harness_window *w = harness_window_create(h, 200, 150, "capture");
	const struct harness_window_info *info = harness_window_info(w);
	struct client c = { 0 };
	struct session out, top, cur_session;
	struct frame f;
	struct shm_buffer screen, window, sprite;

	CHECK(info->known, "no window geometry");
	int cx = info->x + info->width / 2, cy = info->y + info->height / 2;

	c.display = wl_display_connect(h->socket);
	CHECK(c.display, "cannot connect");
	c.registry = wl_display_get_registry(c.display);
	wl_registry_add_listener(c.registry, &registry_listener, &c);
	wl_display_roundtrip(c.display);
	wl_display_roundtrip(c.display);
	CHECK(c.manager && c.output_sources && c.toplevel_sources && c.list &&
		      c.shm && c.output && c.seat,
	      "missing globals");

	struct ext_image_capture_source_v1 *output_source =
		ext_output_image_capture_source_manager_v1_create_source(
			c.output_sources, c.output);

	session_open(&c, &out, output_source);
	CHECK(out.done && !out.stopped && out.width == HARNESS_OUTPUT_WIDTH &&
		      out.height == HARNESS_OUTPUT_HEIGHT,
	      "output constraints %dx%d done %d", out.width, out.height,
	      out.done);
	CHECK(out.format == WL_SHM_FORMAT_XRGB8888 ||
		      out.format == WL_SHM_FORMAT_ARGB8888,
	      "output shm format 0x%x", out.format);
	screen = buffer_new(c.shm, out.width, out.height, out.format);
	frame_start(&f, &out, &screen);
	CHECK(wait_for(c.display, &f.ready, 3000) && !f.failed,
	      "the first output frame did not come (failed %d, reason %u)",
	      f.failed, f.reason);
	CHECK(f.damage_area == (int64_t)out.width * out.height,
	      "the first frame is not fully damaged");
	CHECK((screen.pixels[cy * out.width + cx] & 0xffffff) ==
		      (FIRST_COLOUR & 0xffffff),
	      "the window is not in the capture: 0x%08x",
	      screen.pixels[cy * out.width + cx]);
	ext_image_copy_capture_frame_v1_destroy(f.proxy);

	frame_start(&f, &out, &screen);
	CHECK(!wait_for(c.display, &f.ready, 300) && !f.failed,
	      "an output frame came without damage");
	redraw(h, w, SECOND_COLOUR);
	CHECK(wait_for(c.display, &f.ready, 3000), "a damaged frame did not come");
	CHECK(f.damage_area > 0 &&
		      f.damage_area < (int64_t)out.width * out.height,
	      "the damaged frame reported %lld pixels",
	      (long long)f.damage_area);
	CHECK((screen.pixels[cy * out.width + cx] & 0xffffff) ==
		      (SECOND_COLOUR & 0xffffff),
	      "the redraw is not in the capture");
	ext_image_copy_capture_frame_v1_destroy(f.proxy);

	wl_display_roundtrip(c.display);
	struct ext_foreign_toplevel_handle_v1 *handle =
		find_handle(&c, "capture");

	CHECK(handle, "the window is not listed");
	struct ext_image_capture_source_v1 *toplevel_source =
		ext_foreign_toplevel_image_capture_source_manager_v1_create_source(
			c.toplevel_sources, handle);

	session_open(&c, &top, toplevel_source);
	CHECK(top.done && !top.stopped && top.width == w->width &&
		      top.height == w->height &&
		      top.format == WL_SHM_FORMAT_ARGB8888,
	      "toplevel constraints %dx%d format 0x%x", top.width,
	      top.height, top.format);
	window = buffer_new(c.shm, top.width, top.height, top.format);
	frame_start(&f, &top, &window);
	CHECK(wait_for(c.display, &f.ready, 3000) && !f.failed,
	      "the toplevel frame did not come");
	CHECK(window.pixels[0] == SECOND_COLOUR &&
		      window.pixels[top.width * top.height - 1] ==
			      SECOND_COLOUR,
	      "the toplevel capture is 0x%08x", window.pixels[0]);
	ext_image_copy_capture_frame_v1_destroy(f.proxy);

	frame_start(&f, &top, &window);
	CHECK(!wait_for(c.display, &f.ready, 200), "a toplevel frame came "
						    "without a commit");
	redraw(h, w, FIRST_COLOUR);
	CHECK(wait_for(c.display, &f.ready, 3000) &&
		      window.pixels[0] == FIRST_COLOUR,
	      "a committed toplevel frame did not come");

	struct ext_image_copy_capture_frame_v1 *finished = f.proxy;

	frame_start(&f, &top, &window);
	ext_image_copy_capture_frame_v1_destroy(finished);
	CHECK(wait_for(c.display, &f.ready, 3000) && !f.failed,
	      "a still window got no frame");
	ext_image_copy_capture_frame_v1_destroy(f.proxy);

	struct wl_pointer *pointer = wl_seat_get_pointer(c.seat);
	struct cursor cur = { 0 };

	cur.proxy = ext_image_copy_capture_manager_v1_create_pointer_cursor_session(
		c.manager, output_source, pointer);
	ext_image_copy_capture_cursor_session_v1_add_listener(
		cur.proxy, &cursor_listener, &cur);
	harness_move_pointer(h, 40, 50);
	CHECK(wait_for(c.display, &cur.entered, 3000), "the cursor never entered");
	wl_display_roundtrip(c.display);
	CHECK(cur.x == 40 && cur.y == 50, "the cursor is at %d,%d", cur.x,
	      cur.y);
	memset(&cur_session, 0, sizeof cur_session);
	cur_session.proxy =
		ext_image_copy_capture_cursor_session_v1_get_capture_session(
			cur.proxy);
	ext_image_copy_capture_session_v1_add_listener(
		cur_session.proxy, &session_listener, &cur_session);
	wl_display_roundtrip(c.display);
	CHECK(cur_session.done && cur_session.width > 1 &&
		      cur_session.height > 1,
	      "cursor constraints %dx%d", cur_session.width,
	      cur_session.height);
	sprite = buffer_new(c.shm, cur_session.width, cur_session.height,
			    cur_session.format);
	frame_start(&f, &cur_session, &sprite);
	CHECK(wait_for(c.display, &f.ready, 3000), "no cursor frame");
	bool painted = false;

	for (int i = 0; i < sprite.width * sprite.height; i++)
		painted |= (sprite.pixels[i] >> 24) != 0;
	CHECK(painted, "the cursor image is empty");
	ext_image_copy_capture_frame_v1_destroy(f.proxy);
	harness_move_pointer(h, HARNESS_OUTPUT_WIDTH + 50, 50);
	wl_display_roundtrip(c.display);

	harness_window_destroy(w);
	harness_roundtrip(h);
	CHECK(wait_for(c.display, &top.stopped, 3000),
	      "the toplevel session did not stop");

	memset(&f, 0, sizeof f);
	f.proxy = ext_image_copy_capture_session_v1_create_frame(out.proxy);
	ext_image_copy_capture_frame_v1_capture(f.proxy);
	wl_display_roundtrip(c.display);
	const struct wl_interface *interface = NULL;
	uint32_t code =
		wl_display_get_protocol_error(c.display, &interface, NULL);

	CHECK(interface == &ext_image_copy_capture_frame_v1_interface &&
		      code == EXT_IMAGE_COPY_CAPTURE_FRAME_V1_ERROR_NO_BUFFER,
	      "a capture without a buffer was accepted");

	struct wl_proxy *proxies[] = {
		(struct wl_proxy *)f.proxy,
		(struct wl_proxy *)cur_session.proxy,
		(struct wl_proxy *)cur.proxy,
		(struct wl_proxy *)top.proxy,
		(struct wl_proxy *)out.proxy,
		(struct wl_proxy *)toplevel_source,
		(struct wl_proxy *)output_source,
		(struct wl_proxy *)pointer,
		(struct wl_proxy *)c.manager,
		(struct wl_proxy *)c.output_sources,
		(struct wl_proxy *)c.toplevel_sources,
		(struct wl_proxy *)c.list,
		(struct wl_proxy *)c.shm,
		(struct wl_proxy *)c.seat,
		(struct wl_proxy *)c.output,
		(struct wl_proxy *)c.registry,
	};

	munmap(screen.pixels, screen.size);
	munmap(window.pixels, window.size);
	munmap(sprite.pixels, sprite.size);
	wl_proxy_destroy((struct wl_proxy *)screen.buffer);
	wl_proxy_destroy((struct wl_proxy *)window.buffer);
	wl_proxy_destroy((struct wl_proxy *)sprite.buffer);
	for (size_t i = 0; i < sizeof proxies / sizeof proxies[0]; i++)
		wl_proxy_destroy(proxies[i]);
	for (int i = 0; i < c.n_handles; i++) {
		wl_proxy_destroy((struct wl_proxy *)c.handles[i]);
		free(c.titles[i]);
	}
	wl_display_disconnect(c.display);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
