/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Magic Lamp on the GL renderer: minimising moves keyboard focus at once,
 * keeps the window drawn and deformed while it funnels, then hides it;
 * restoring draws it deformed and ends with it whole where it was.
 */

#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "harness.h"
#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#define WINDOW_COLOUR 0x3070c0u
#define COVER_COLOUR 0x20a040u

static struct zwlr_foreign_toplevel_manager_v1 *manager;
static struct zwlr_foreign_toplevel_handle_v1 *handles[16];
static int n_handles;

static void
handle_noop(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle)
{
}

static void
handle_string(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle,
	      const char *value)
{
}

static void
handle_output(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle,
	      struct wl_output *output)
{
}

static void
handle_state(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle,
	     struct wl_array *state)
{
}

static void
handle_parent(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle,
	      struct zwlr_foreign_toplevel_handle_v1 *parent)
{
}

static const struct zwlr_foreign_toplevel_handle_v1_listener handle_listener = {
	handle_string, handle_string, handle_output, handle_output,
	handle_state,  handle_noop,   handle_noop,   handle_parent,
};

static void
manager_toplevel(void *data, struct zwlr_foreign_toplevel_manager_v1 *m,
		 struct zwlr_foreign_toplevel_handle_v1 *handle)
{
	CHECK(n_handles < 16, "too many toplevels");
	handles[n_handles++] = handle;
	zwlr_foreign_toplevel_handle_v1_add_listener(handle, &handle_listener,
						     NULL);
}

static void
manager_finished(void *data, struct zwlr_foreign_toplevel_manager_v1 *m)
{
}

static const struct zwlr_foreign_toplevel_manager_v1_listener manager_listener = {
	manager_toplevel,
	manager_finished,
};

static void
registry_global(void *data, struct wl_registry *registry, uint32_t name,
		const char *interface, uint32_t version)
{
	if (strcmp(interface, zwlr_foreign_toplevel_manager_v1_interface.name) ==
	    0) {
		manager = wl_registry_bind(
			registry, name, &zwlr_foreign_toplevel_manager_v1_interface,
			3);
		zwlr_foreign_toplevel_manager_v1_add_listener(
			manager, &manager_listener, NULL);
	}
}

static void
registry_remove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = {
	registry_global,
	registry_remove,
};

struct shape {
	int pixels;
	int x0, y0, x1, y1;
};

static struct shape
window_shape(struct harness *h)
{
	int width, height;
	uint32_t *pixels = harness_capture(h, &width, &height);
	struct shape s = { 0, width, height, -1, -1 };

	CHECK(pixels, "cannot capture");
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			if ((pixels[y * width + x] & 0xffffff) != WINDOW_COLOUR)
				continue;
			s.pixels++;
			s.x0 = x < s.x0 ? x : s.x0;
			s.y0 = y < s.y0 ? y : s.y0;
			s.x1 = x > s.x1 ? x : s.x1;
			s.y1 = y > s.y1 ? y : s.y1;
		}
	}
	free(pixels);
	return s;
}

static void
pause_ms(struct harness *h, int ms)
{
	struct timespec step = { 0, 10 * 1000000L };

	for (int i = 0; i < ms / 10; i++) {
		harness_roundtrip(h);
		nanosleep(&step, NULL);
	}
}

static void
paint(struct harness *h, struct harness_window *w, uint32_t colour)
{
	size_t size = (size_t)w->width * w->height * 4;
	int fd = memfd_create("lamp-cover", MFD_CLOEXEC);

	CHECK(fd >= 0 && ftruncate(fd, (off_t)size) == 0, "no shm");
	uint32_t *pixels =
		mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

	CHECK(pixels != MAP_FAILED, "mmap");
	for (size_t i = 0; i < size / 4; i++)
		pixels[i] = 0xff000000u | colour;
	munmap(pixels, size);
	struct wl_shm_pool *pool = wl_shm_create_pool(h->shm, fd, (int)size);
	struct wl_buffer *buffer = wl_shm_pool_create_buffer(
		pool, 0, w->width, w->height, w->width * 4,
		WL_SHM_FORMAT_ARGB8888);

	wl_shm_pool_destroy(pool);
	close(fd);
	wl_surface_attach(w->surface, buffer, 0, 0);
	wl_surface_damage_buffer(w->surface, 0, 0, w->width, w->height);
	if (w->buffer)
		wl_buffer_destroy(w->buffer);
	w->buffer = buffer;
	harness_wait_frame(h, w->surface);
}

static bool
same_shape(struct shape a, struct shape b)
{
	return a.pixels == b.pixels && a.x0 == b.x0 && a.y0 == b.y0 &&
	       a.x1 == b.x1 && a.y1 == b.y1;
}

int
main(int argc, char **argv)
{
	setenv("POLO_TEST_RENDERER", "--renderer=gl", 1);
	struct harness *h = harness_start(argc, argv);

	harness_set_setting(h, "org.mate.polo.effects", "magic-lamp", "true");
	harness_set_setting(h, "org.mate.polo.effects", "magic-lamp-duration-ms",
			    "uint32 600");
	harness_move_pointer(h, 1000, 740);
	struct harness_window *window =
		harness_window_create(h, 300, 200, "lamp");

	CHECK(h->keyboard_focus == window->surface, "the window is not focused");
	pause_ms(h, 100);
	struct shape whole = window_shape(h);

	CHECK(whole.pixels == 300 * 200, "the window shows %d pixels",
	      whole.pixels);

	xdg_toplevel_set_minimized(window->xdg_toplevel);
	harness_roundtrip(h);
	CHECK(h->keyboard_focus != window->surface,
	      "focus stayed on the minimising window");
	pause_ms(h, 150);
	struct shape funnel = window_shape(h);

	CHECK(funnel.pixels > 0, "the window vanished at once");
	CHECK(!same_shape(funnel, whole), "the minimising window is not bent");
	pause_ms(h, 800);
	CHECK(window_shape(h).pixels == 0, "the minimised window is still drawn");

	struct wl_registry *registry = wl_display_get_registry(h->display);

	wl_registry_add_listener(registry, &registry_listener, NULL);
	harness_roundtrip(h);
	harness_roundtrip(h);
	CHECK(manager && n_handles == 1, "no task-bar handle for the window");
	zwlr_foreign_toplevel_handle_v1_unset_minimized(handles[0]);
	harness_roundtrip(h);
	pause_ms(h, 150);
	struct shape opening = window_shape(h);

	CHECK(opening.pixels > 0, "the restoring window is not drawn");
	CHECK(!same_shape(opening, whole), "the restoring window is not bent");
	pause_ms(h, 800);
	struct shape back = window_shape(h);

	CHECK(same_shape(back, whole),
	      "the restored window is %d pixels at %d,%d-%d,%d", back.pixels,
	      back.x0, back.y0, back.x1, back.y1);

	xdg_toplevel_set_minimized(window->xdg_toplevel);
	harness_roundtrip(h);
	pause_ms(h, 150);
	zwlr_foreign_toplevel_handle_v1_unset_minimized(handles[0]);
	harness_roundtrip(h);
	pause_ms(h, 50);
	struct shape turning = window_shape(h);

	CHECK(turning.pixels > 0 && !same_shape(turning, whole),
	      "the interrupted window did not turn back from where it was");
	pause_ms(h, 400);
	back = window_shape(h);
	CHECK(same_shape(back, whole),
	      "after turning back the window is %d pixels at %d,%d-%d,%d",
	      back.pixels, back.x0, back.y0, back.x1, back.y1);

	struct harness_window *cover =
		harness_window_create(h, 1000, 700, "cover");

	paint(h, cover, COVER_COLOUR);
	CHECK(window_shape(h).pixels == 0, "the cover does not hide the window");
	xdg_toplevel_set_minimized(window->xdg_toplevel);
	harness_roundtrip(h);
	pause_ms(h, 150);
	CHECK(window_shape(h).pixels > 0,
	      "the minimising window is drawn behind the one in front");
	pause_ms(h, 800);
	CHECK(window_shape(h).pixels == 0, "the minimised window is still drawn");
	zwlr_foreign_toplevel_handle_v1_unset_minimized(handles[0]);
	pause_ms(h, 800);
	harness_window_destroy(cover);

	for (int i = 0; i < n_handles; i++)
		zwlr_foreign_toplevel_handle_v1_destroy(handles[i]);
	zwlr_foreign_toplevel_manager_v1_destroy(manager);
	wl_registry_destroy(registry);
	harness_window_destroy(window);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
