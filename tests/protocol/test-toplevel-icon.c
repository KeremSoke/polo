/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A toplevel's icon reaches polo-shell-ui with its Alt-Tab entry. argv[2]:
 * tests/protocol/fake-shell-ui.c.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <linux/input.h>

#include "harness.h"
#include "xdg-toplevel-icon-v1-client-protocol.h"

/* Connections die on purpose here with their proxies live. Polo is its own
 * process and is still checked. */
int
__lsan_is_turned_off(void);

int
__lsan_is_turned_off(void)
{
	return 1;
}

static char log_path[300];
static struct xdg_toplevel_icon_manager_v1 *manager;
static int icon_sizes[4], n_sizes, dones;

static void
pause_ms(int ms)
{
	struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };

	nanosleep(&ts, NULL);
}

static bool
logged(const char *line)
{
	FILE *log = fopen(log_path, "r");
	char buf[512];
	bool found = false;

	CHECK(log, "cannot read %s", log_path);
	while (!found && fgets(buf, sizeof buf, log)) {
		buf[strcspn(buf, "\n")] = '\0';
		found = strcmp(buf, line) == 0;
	}
	fclose(log);
	return found;
}

static void
clear_log(void)
{
	FILE *log = fopen(log_path, "w");

	CHECK(log, "cannot truncate %s", log_path);
	fclose(log);
}

static void
on_icon_size(void *data, struct xdg_toplevel_icon_manager_v1 *m, int32_t size)
{
	if (n_sizes < 4)
		icon_sizes[n_sizes++] = size;
}

static void
on_done(void *data, struct xdg_toplevel_icon_manager_v1 *m)
{
	dones++;
}

static const struct xdg_toplevel_icon_manager_v1_listener manager_listener = {
	on_icon_size,
	on_done,
};

static void
global(void *data, struct wl_registry *registry, uint32_t name,
       const char *interface, uint32_t version)
{
	if (strcmp(interface, xdg_toplevel_icon_manager_v1_interface.name) == 0) {
		manager = wl_registry_bind(
			registry, name, &xdg_toplevel_icon_manager_v1_interface, 1);
		xdg_toplevel_icon_manager_v1_add_listener(manager, &manager_listener,
							  NULL);
	}
}

static void
global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = { global,
							       global_remove };

static struct wl_buffer *
solid(struct wl_shm *shm, int width, int height, uint32_t format, uint32_t color)
{
	size_t size = (size_t)width * height * 4;
	int fd = memfd_create("icon", MFD_CLOEXEC);

	CHECK(fd >= 0 && ftruncate(fd, size) == 0, "icon file");
	uint32_t *pixels = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

	CHECK(pixels != MAP_FAILED, "icon mmap");
	for (size_t i = 0; i < size / 4; i++)
		pixels[i] = color;
	munmap(pixels, size);
	struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
	struct wl_buffer *buffer = wl_shm_pool_create_buffer(
		pool, 0, width, height, width * 4, format);

	wl_shm_pool_destroy(pool);
	close(fd);
	return buffer;
}

/* The popup for what is on screen now, then closed again. */
static void
show(struct harness *h)
{
	clear_log();
	harness_key(h, KEY_LEFTALT, true);
	harness_key(h, KEY_TAB, true);
	harness_key(h, KEY_TAB, false);
	for (int i = 0; i < 300 && !logged("switcher_entry 1 win org.mate.PoloTest 0");
	     i++) {
		harness_roundtrip(h);
		pause_ms(10);
	}
	for (int i = 0; i < 20; i++) {
		harness_roundtrip(h);
		pause_ms(5);
	}
	harness_key(h, KEY_ESC, true);
	harness_key(h, KEY_ESC, false);
	harness_key(h, KEY_LEFTALT, false);
	harness_roundtrip(h);
}

static void
commit(struct harness *h, struct harness_window *w)
{
	wl_surface_commit(w->surface);
	harness_roundtrip(h);
}

static void
expect_error(struct harness *h, const struct wl_interface *interface, uint32_t code)
{
	const struct wl_interface *got = NULL;

	for (int i = 0; i < 10 && !wl_display_get_error(h->display); i++)
		wl_display_roundtrip(h->display);
	CHECK(wl_display_get_error(h->display) == EPROTO, "no protocol error");
	CHECK(wl_display_get_protocol_error(h->display, &got, NULL) == code &&
		      got == interface,
	      "wrong protocol error");
}

/* Each error ends its connection, so each gets a fresh Polo. */
static struct harness *
start(int argc, char **argv)
{
	struct harness *h = harness_start(argc, argv);
	struct wl_registry *registry = wl_display_get_registry(h->display);

	manager = NULL;
	wl_registry_add_listener(registry, &registry_listener, NULL);
	harness_roundtrip(h);
	CHECK(manager, "no xdg_toplevel_icon_manager_v1");
	return h;
}

int
main(int argc, char **argv)
{
	int fd;

	snprintf(log_path, sizeof log_path, "%s/polo-icon-XXXXXX",
		 getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
	fd = mkstemp(log_path);
	CHECK(fd >= 0, "mkstemp: %s", strerror(errno));
	close(fd);
	setenv("FAKE_SHELL_UI_LOG", log_path, 1);
	setenv("POLO_SHELL_UI", argv[2], 1);

	struct harness *h = start(argc, argv);

	for (int i = 0; i < 300 && h->shell_ui_binds == 0; i++) {
		harness_roundtrip(h);
		pause_ms(10);
	}
	CHECK(h->shell_ui_binds == 1, "fake-shell-ui did not bind");
	harness_roundtrip(h);
	CHECK(n_sizes == 1 && icon_sizes[0] == 48 && dones == 1,
	      "preferred sizes: %d, done %d", n_sizes, dones);

	struct harness_window *w = harness_window_create(h, 200, 150, "win");

	show(h);
	CHECK(logged("switcher_entry 1 win org.mate.PoloTest 0"), "no popup");
	CHECK(!logged("switcher_icon_name 1 stock") ,
	      "an icon before any was set");

	/* A name: double-buffered until the toplevel commits. */
	struct xdg_toplevel_icon_v1 *icon =
		xdg_toplevel_icon_manager_v1_create_icon(manager);

	xdg_toplevel_icon_v1_set_name(icon, "stock");
	xdg_toplevel_icon_manager_v1_set_icon(manager, w->xdg_toplevel, icon);
	xdg_toplevel_icon_v1_destroy(icon);
	harness_roundtrip(h);
	show(h);
	CHECK(!logged("switcher_icon_name 1 stock"), "applied before the commit");
	commit(h, w);
	show(h);
	CHECK(logged("switcher_icon_name 1 stock"), "no name after the commit");

	/* Pixels: the largest kept, whatever the order, and it survives the client
	 * dropping everything. */
	struct wl_buffer *big =
		solid(h->shm, 64, 64, WL_SHM_FORMAT_ARGB8888, 0xff102030);
	struct wl_buffer *small =
		solid(h->shm, 16, 16, WL_SHM_FORMAT_ARGB8888, 0xff405060);

	icon = xdg_toplevel_icon_manager_v1_create_icon(manager);
	xdg_toplevel_icon_v1_add_buffer(icon, big, 1);
	xdg_toplevel_icon_v1_add_buffer(icon, small, 1);
	xdg_toplevel_icon_manager_v1_set_icon(manager, w->xdg_toplevel, icon);
	xdg_toplevel_icon_v1_destroy(icon);
	wl_buffer_destroy(big);
	wl_buffer_destroy(small);
	commit(h, w);
	show(h);
	CHECK(logged("switcher_icon_pixels 1 64 ff102030"),
	      "the largest buffer's pixels did not arrive");
	CHECK(!logged("switcher_icon_name 1 stock"), "the old name stayed");

	/* A format Polo cannot use leaves what the icon has. */
	struct wl_buffer *xrgb =
		solid(h->shm, 96, 96, WL_SHM_FORMAT_XRGB8888, 0xff708090);

	icon = xdg_toplevel_icon_manager_v1_create_icon(manager);
	xdg_toplevel_icon_v1_set_name(icon, "other");
	xdg_toplevel_icon_v1_add_buffer(icon, xrgb, 1);
	xdg_toplevel_icon_manager_v1_set_icon(manager, w->xdg_toplevel, icon);
	commit(h, w);
	xdg_toplevel_icon_v1_destroy(icon);
	wl_buffer_destroy(xrgb);
	show(h);
	CHECK(logged("switcher_icon_name 1 other"), "no name with an unusable buffer");
	CHECK(!logged("switcher_icon_pixels 1 96 ff708090"),
	      "an unusable format was sent on");

	/* Null resets it. */
	xdg_toplevel_icon_manager_v1_set_icon(manager, w->xdg_toplevel, NULL);
	commit(h, w);
	show(h);
	CHECK(!logged("switcher_icon_name 1 other"), "null did not reset the icon");

	/* An empty icon resets it too. */
	icon = xdg_toplevel_icon_manager_v1_create_icon(manager);
	xdg_toplevel_icon_v1_set_name(icon, "again");
	xdg_toplevel_icon_manager_v1_set_icon(manager, w->xdg_toplevel, icon);
	commit(h, w);
	xdg_toplevel_icon_v1_destroy(icon);
	icon = xdg_toplevel_icon_manager_v1_create_icon(manager);
	xdg_toplevel_icon_manager_v1_set_icon(manager, w->xdg_toplevel, icon);
	commit(h, w);
	xdg_toplevel_icon_v1_destroy(icon);
	show(h);
	CHECK(!logged("switcher_icon_name 1 again"), "an empty icon did not reset");

	/* The toplevel going with a change pending. */
	struct harness_window *gone = harness_window_create(h, 100, 100, "gone");

	icon = xdg_toplevel_icon_manager_v1_create_icon(manager);
	xdg_toplevel_icon_v1_set_name(icon, "x");
	xdg_toplevel_icon_manager_v1_set_icon(manager, gone->xdg_toplevel, icon);
	xdg_toplevel_icon_v1_destroy(icon);
	harness_window_destroy(gone);
	harness_roundtrip(h);

	/* Errors. */
	icon = xdg_toplevel_icon_manager_v1_create_icon(manager);
	xdg_toplevel_icon_v1_set_name(icon, "x");
	xdg_toplevel_icon_manager_v1_set_icon(manager, w->xdg_toplevel, icon);
	xdg_toplevel_icon_v1_set_name(icon, "y");
	expect_error(h, &xdg_toplevel_icon_v1_interface,
		     XDG_TOPLEVEL_ICON_V1_ERROR_IMMUTABLE);
	harness_stop(h);

	h = start(argc, argv);
	w = harness_window_create(h, 100, 100, "win");
	icon = xdg_toplevel_icon_manager_v1_create_icon(manager);
	xdg_toplevel_icon_manager_v1_set_icon(manager, w->xdg_toplevel, icon);
	big = solid(h->shm, 8, 8, WL_SHM_FORMAT_ARGB8888, 0);
	xdg_toplevel_icon_v1_add_buffer(icon, big, 1);
	expect_error(h, &xdg_toplevel_icon_v1_interface,
		     XDG_TOPLEVEL_ICON_V1_ERROR_IMMUTABLE);
	harness_stop(h);

	h = start(argc, argv);
	icon = xdg_toplevel_icon_manager_v1_create_icon(manager);
	big = solid(h->shm, 16, 8, WL_SHM_FORMAT_ARGB8888, 0);
	xdg_toplevel_icon_v1_add_buffer(icon, big, 1);
	expect_error(h, &xdg_toplevel_icon_v1_interface,
		     XDG_TOPLEVEL_ICON_V1_ERROR_INVALID_BUFFER);
	harness_stop(h);

	h = start(argc, argv);
	icon = xdg_toplevel_icon_manager_v1_create_icon(manager);
	big = solid(h->shm, 8, 8, WL_SHM_FORMAT_ARGB8888, 0);
	xdg_toplevel_icon_v1_add_buffer(icon, big, 1);
	wl_buffer_destroy(big);
	expect_error(h, &xdg_toplevel_icon_v1_interface,
		     XDG_TOPLEVEL_ICON_V1_ERROR_NO_BUFFER);
	harness_stop(h);

	unlink(log_path);
	return 0;
}
