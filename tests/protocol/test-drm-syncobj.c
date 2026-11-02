/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * wp_linux_drm_syncobj with real kernel timelines under the GL renderer: a buffer
 * waits for its acquire point to have a fence, and the release point is signalled.
 * Skips without a render node with timeline syncobjs, or without GL.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <gbm.h>
#include <xf86drm.h>

#include "harness.h"
#include "linux-dmabuf-v1-client-protocol.h"
#include "linux-drm-syncobj-v1-client-protocol.h"

/* Connections die on purpose here with their proxies live. Polo is its own
 * process and is still checked. */
int
__lsan_is_turned_off(void);

int
__lsan_is_turned_off(void)
{
	return 1;
}

struct binds {
	struct wp_linux_drm_syncobj_manager_v1 *syncobj;
	struct zwp_linux_dmabuf_v1 *dmabuf;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct xdg_wm_base *wm_base;
};

static int drm_fd = -1;
static struct gbm_device *gbm;

static void
global(void *data, struct wl_registry *registry, uint32_t name,
       const char *interface, uint32_t version)
{
	struct binds *b = data;

	if (strcmp(interface, wp_linux_drm_syncobj_manager_v1_interface.name) == 0)
		b->syncobj = wl_registry_bind(
			registry, name, &wp_linux_drm_syncobj_manager_v1_interface, 1);
	else if (strcmp(interface, zwp_linux_dmabuf_v1_interface.name) == 0)
		b->dmabuf = wl_registry_bind(registry, name,
					     &zwp_linux_dmabuf_v1_interface, 3);
	else if (strcmp(interface, wl_compositor_interface.name) == 0)
		b->compositor = wl_registry_bind(registry, name,
						 &wl_compositor_interface, 4);
	else if (strcmp(interface, wl_shm_interface.name) == 0)
		b->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	else if (strcmp(interface, xdg_wm_base_interface.name) == 0)
		b->wm_base = wl_registry_bind(registry, name,
					      &xdg_wm_base_interface, 1);
}

static void
global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = { global,
							       global_remove };

static void
binds_get(struct wl_display *display, struct binds *b)
{
	memset(b, 0, sizeof *b);
	wl_registry_add_listener(wl_display_get_registry(display), &registry_listener,
				 b);
	wl_display_roundtrip(display);
}

static struct wl_buffer *
dmabuf_buffer(struct binds *b, int width, int height)
{
	struct gbm_bo *bo = gbm_bo_create(gbm, width, height, GBM_FORMAT_XRGB8888,
					  GBM_BO_USE_SCANOUT | GBM_BO_USE_LINEAR);

	CHECK(bo, "gbm_bo_create: %s", strerror(errno));
	int fd = gbm_bo_get_fd(bo);

	CHECK(fd >= 0, "gbm_bo_get_fd");
	struct zwp_linux_buffer_params_v1 *params =
		zwp_linux_dmabuf_v1_create_params(b->dmabuf);

	zwp_linux_buffer_params_v1_add(params, fd, 0, gbm_bo_get_offset(bo, 0),
				       gbm_bo_get_stride(bo), 0, 0);
	struct wl_buffer *buffer = zwp_linux_buffer_params_v1_create_immed(
		params, width, height, GBM_FORMAT_XRGB8888, 0);

	zwp_linux_buffer_params_v1_destroy(params);
	close(fd);
	return buffer;
}

static uint32_t
timeline_new(struct binds *b, struct wp_linux_drm_syncobj_timeline_v1 **timeline)
{
	uint32_t handle;
	int fd;

	CHECK(drmSyncobjCreate(drm_fd, 0, &handle) == 0, "drmSyncobjCreate");
	CHECK(drmSyncobjHandleToFD(drm_fd, handle, &fd) == 0, "drmSyncobjHandleToFD");
	*timeline = wp_linux_drm_syncobj_manager_v1_import_timeline(b->syncobj, fd);
	close(fd);
	return handle;
}

static void
signal_point(uint32_t handle, uint64_t point)
{
	CHECK(drmSyncobjTimelineSignal(drm_fd, &handle, &point, 1) == 0,
	      "signalling point %llu", (unsigned long long)point);
}

static bool
point_signalled(uint32_t handle, uint64_t point)
{
	return drmSyncobjTimelineWait(drm_fd, &handle, &point, 1, 0, 0, NULL) == 0;
}

struct window {
	struct wl_surface *surface;
	struct xdg_surface *xdg;
	struct xdg_toplevel *toplevel;
	uint32_t serial;
	struct harness_window info;
};

static void
window_configure(void *data, struct xdg_surface *s, uint32_t serial)
{
	((struct window *)data)->serial = serial;
}

static const struct xdg_surface_listener window_listener = { window_configure };

static void
window_new(struct harness *h, struct binds *b, struct window *w)
{
	memset(w, 0, sizeof *w);
	w->surface = wl_compositor_create_surface(b->compositor);
	w->xdg = xdg_wm_base_get_xdg_surface(b->wm_base, w->surface);
	xdg_surface_add_listener(w->xdg, &window_listener, w);
	w->toplevel = xdg_surface_get_toplevel(w->xdg);
	w->info.harness = h;
	w->info.surface = w->surface;
	wl_surface_set_user_data(w->surface, &w->info);
	wl_surface_commit(w->surface);
	harness_roundtrip(h);
	xdg_surface_ack_configure(w->xdg, w->serial);
}

static void
settle(struct harness *h, int ms)
{
	struct timespec tick = { 0, 10 * 1000000L };

	for (int i = 0; i < ms / 10; i++) {
		harness_roundtrip(h);
		nanosleep(&tick, NULL);
	}
}

static void
expect_error(struct wl_display *display, const struct wl_interface *interface,
	     uint32_t code)
{
	const struct wl_interface *got = NULL;

	for (int i = 0; i < 10 && !wl_display_get_error(display); i++)
		wl_display_roundtrip(display);
	CHECK(wl_display_get_error(display) == EPROTO, "no protocol error");
	CHECK(wl_display_get_protocol_error(display, &got, NULL) == code &&
		      got == interface,
	      "wrong protocol error: %s", got ? got->name : "none");
	wl_display_disconnect(display);
}

struct error_case {
	struct wl_display *display;
	struct binds b;
	struct wl_surface *surface;
	struct xdg_surface *xdg;
	struct wp_linux_drm_syncobj_surface_v1 *object;
	struct wp_linux_drm_syncobj_timeline_v1 *timeline;
	uint32_t serial;
};

static void
error_configure(void *data, struct xdg_surface *s, uint32_t serial)
{
	((struct error_case *)data)->serial = serial;
}

static const struct xdg_surface_listener error_listener = { error_configure };

static void
error_case_new(struct harness *h, struct error_case *c)
{
	memset(c, 0, sizeof *c);
	c->display = wl_display_connect(h->socket);
	CHECK(c->display, "connect");
	binds_get(c->display, &c->b);
	c->surface = wl_compositor_create_surface(c->b.compositor);
	c->xdg = xdg_wm_base_get_xdg_surface(c->b.wm_base, c->surface);
	xdg_surface_add_listener(c->xdg, &error_listener, c);
	xdg_surface_get_toplevel(c->xdg);
	wl_surface_commit(c->surface);
	wl_display_roundtrip(c->display);
	xdg_surface_ack_configure(c->xdg, c->serial);
	c->object = wp_linux_drm_syncobj_manager_v1_get_surface(c->b.syncobj,
								c->surface);
	timeline_new(&c->b, &c->timeline);
}

int
main(int argc, char **argv)
{
	drmDevicePtr devices[16];
	int count = drmGetDevices2(0, devices, 16);

	for (int i = 0; i < count && drm_fd < 0; i++) {
		uint64_t cap = 0;
		int fd;

		if (!(devices[i]->available_nodes & (1 << DRM_NODE_RENDER)))
			continue;
		fd = open(devices[i]->nodes[DRM_NODE_RENDER], O_RDWR | O_CLOEXEC);
		if (fd >= 0 && drmGetCap(fd, DRM_CAP_SYNCOBJ_TIMELINE, &cap) == 0 && cap)
			drm_fd = fd;
		else if (fd >= 0)
			close(fd);
	}
	if (drm_fd < 0) {
		printf("no render node with timeline syncobjs\n");
		return 77;
	}
	/* Buffers come from any node that can make them: a software or virtual GPU's
	 * render node may not. */
	for (int i = 0; i < count && !gbm; i++) {
		for (int node = 0; node < DRM_NODE_MAX && !gbm; node++) {
			int fd;

			if (!(devices[i]->available_nodes & (1 << node)))
				continue;
			fd = open(devices[i]->nodes[node], O_RDWR | O_CLOEXEC);
			if (fd < 0)
				continue;
			gbm = gbm_create_device(fd);
			struct gbm_bo *bo = gbm
				? gbm_bo_create(gbm, 16, 16, GBM_FORMAT_XRGB8888,
						GBM_BO_USE_SCANOUT | GBM_BO_USE_LINEAR)
				: NULL;

			if (bo) {
				gbm_bo_destroy(bo);
			} else {
				if (gbm)
					gbm_device_destroy(gbm);
				gbm = NULL;
				close(fd);
			}
		}
	}
	if (!gbm) {
		printf("nothing here can allocate a dmabuf\n");
		return 77;
	}

	setenv("POLO_TEST_RENDERER", "--renderer=gl", 1);
	struct harness *h = harness_start(argc, argv);
	struct binds b;

	binds_get(h->display, &b);
	if (!b.syncobj || !b.dmabuf) {
		printf("Polo offers no syncobj or dmabuf here\n");
		CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
		return 77;
	}

	struct wp_linux_drm_syncobj_timeline_v1 *acquire_timeline, *release_timeline;
	uint32_t acquire = timeline_new(&b, &acquire_timeline);
	uint32_t release = timeline_new(&b, &release_timeline);
	struct window w;

	window_new(h, &b, &w);
	struct wp_linux_drm_syncobj_surface_v1 *object =
		wp_linux_drm_syncobj_manager_v1_get_surface(b.syncobj, w.surface);

	/* No fence yet at the acquire point: the commit waits. */
	struct wl_buffer *first = dmabuf_buffer(&b, 200, 150);

	wl_surface_attach(w.surface, first, 0, 0);
	wl_surface_damage(w.surface, 0, 0, 200, 150);
	wp_linux_drm_syncobj_surface_v1_set_acquire_point(object, acquire_timeline, 0, 1);
	wp_linux_drm_syncobj_surface_v1_set_release_point(object, release_timeline, 0, 1);
	wl_surface_commit(w.surface);
	settle(h, 300);
	CHECK(!harness_window_info(&w.info)->known,
	      "the window mapped before its acquire point had a fence");

	/* A fence that is already signalled: it applies. */
	signal_point(acquire, 1);
	for (int i = 0; i < 100 && !harness_window_info(&w.info)->known; i++)
		settle(h, 20);
	CHECK(harness_window_info(&w.info)->known,
	      "the window did not map when its acquire point arrived");
	CHECK(!point_signalled(release, 1),
	      "the release point was signalled while the buffer is on screen");

	/* A later buffer replaces it, and the first one's release point comes. */
	struct wl_buffer *second = dmabuf_buffer(&b, 200, 150);

	signal_point(acquire, 2);
	wl_surface_attach(w.surface, second, 0, 0);
	wl_surface_damage(w.surface, 0, 0, 200, 150);
	wp_linux_drm_syncobj_surface_v1_set_acquire_point(object, acquire_timeline, 0, 2);
	wp_linux_drm_syncobj_surface_v1_set_release_point(object, release_timeline, 0, 2);
	wl_surface_commit(w.surface);
	for (int i = 0; i < 200 && !point_signalled(release, 1); i++)
		settle(h, 10);
	CHECK(point_signalled(release, 1), "the replaced buffer's release point never came");
	CHECK(!point_signalled(release, 2), "the buffer on screen was released");

	/* Updates after a waiting one stay in order behind it. */
	struct wl_buffer *third = dmabuf_buffer(&b, 200, 150);
	struct wl_buffer *fourth = dmabuf_buffer(&b, 200, 150);

	wl_surface_attach(w.surface, third, 0, 0);
	wl_surface_damage(w.surface, 0, 0, 200, 150);
	wp_linux_drm_syncobj_surface_v1_set_acquire_point(object, acquire_timeline, 0, 3);
	wp_linux_drm_syncobj_surface_v1_set_release_point(object, release_timeline, 0, 3);
	wl_surface_commit(w.surface);
	/* On a timeline of its own: a later point on the same one would complete
	 * the earlier point too. */
	struct wp_linux_drm_syncobj_timeline_v1 *other_timeline;
	uint32_t other = timeline_new(&b, &other_timeline);

	signal_point(other, 1);
	wl_surface_attach(w.surface, fourth, 0, 0);
	wl_surface_damage(w.surface, 0, 0, 200, 150);
	wp_linux_drm_syncobj_surface_v1_set_acquire_point(object, other_timeline, 0, 1);
	wp_linux_drm_syncobj_surface_v1_set_release_point(object, release_timeline, 0, 4);
	wl_surface_commit(w.surface);
	settle(h, 200);
	CHECK(!point_signalled(release, 2) && !point_signalled(release, 3),
	      "an update overtook one still waiting for its fence");
	signal_point(acquire, 3);
	for (int i = 0; i < 200 && !point_signalled(release, 3); i++)
		settle(h, 10);
	CHECK(point_signalled(release, 2) && point_signalled(release, 3),
	      "the queued updates did not follow in order");

	/* The window going with an update still waiting. */
	struct window gone;

	window_new(h, &b, &gone);
	struct wp_linux_drm_syncobj_surface_v1 *gone_object =
		wp_linux_drm_syncobj_manager_v1_get_surface(b.syncobj, gone.surface);

	wl_surface_attach(gone.surface, dmabuf_buffer(&b, 100, 100), 0, 0);
	wp_linux_drm_syncobj_surface_v1_set_acquire_point(gone_object, acquire_timeline, 0, 50);
	wp_linux_drm_syncobj_surface_v1_set_release_point(gone_object, release_timeline, 0, 50);
	wl_surface_commit(gone.surface);
	harness_roundtrip(h);
	xdg_toplevel_destroy(gone.toplevel);
	xdg_surface_destroy(gone.xdg);
	wl_surface_destroy(gone.surface);
	harness_roundtrip(h);
	signal_point(acquire, 50);
	settle(h, 100);
	wp_linux_drm_syncobj_surface_v1_destroy(gone_object);
	wp_linux_drm_syncobj_surface_v1_destroy(object);
	xdg_toplevel_destroy(w.toplevel);
	xdg_surface_destroy(w.xdg);
	wl_surface_destroy(w.surface);
	harness_roundtrip(h);

	/* Errors. */
	struct error_case c;

	error_case_new(h, &c);
	wl_surface_destroy(c.surface);
	wp_linux_drm_syncobj_surface_v1_set_acquire_point(c.object, c.timeline, 0, 1);
	expect_error(c.display, &wp_linux_drm_syncobj_surface_v1_interface,
		     WP_LINUX_DRM_SYNCOBJ_SURFACE_V1_ERROR_NO_SURFACE);

	error_case_new(h, &c);
	wp_linux_drm_syncobj_surface_v1_set_acquire_point(c.object, c.timeline, 0, 1);
	wp_linux_drm_syncobj_surface_v1_set_release_point(c.object, c.timeline, 0, 2);
	wl_surface_commit(c.surface);
	expect_error(c.display, &wp_linux_drm_syncobj_surface_v1_interface,
		     WP_LINUX_DRM_SYNCOBJ_SURFACE_V1_ERROR_NO_BUFFER);

	error_case_new(h, &c);
	wl_surface_attach(c.surface, dmabuf_buffer(&c.b, 50, 50), 0, 0);
	wp_linux_drm_syncobj_surface_v1_set_release_point(c.object, c.timeline, 0, 2);
	wl_surface_commit(c.surface);
	expect_error(c.display, &wp_linux_drm_syncobj_surface_v1_interface,
		     WP_LINUX_DRM_SYNCOBJ_SURFACE_V1_ERROR_NO_ACQUIRE_POINT);

	error_case_new(h, &c);
	wl_surface_attach(c.surface, dmabuf_buffer(&c.b, 50, 50), 0, 0);
	wp_linux_drm_syncobj_surface_v1_set_acquire_point(c.object, c.timeline, 0, 1);
	wl_surface_commit(c.surface);
	expect_error(c.display, &wp_linux_drm_syncobj_surface_v1_interface,
		     WP_LINUX_DRM_SYNCOBJ_SURFACE_V1_ERROR_NO_RELEASE_POINT);

	error_case_new(h, &c);
	wl_surface_attach(c.surface, dmabuf_buffer(&c.b, 50, 50), 0, 0);
	wp_linux_drm_syncobj_surface_v1_set_acquire_point(c.object, c.timeline, 0, 5);
	wp_linux_drm_syncobj_surface_v1_set_release_point(c.object, c.timeline, 0, 5);
	wl_surface_commit(c.surface);
	expect_error(c.display, &wp_linux_drm_syncobj_surface_v1_interface,
		     WP_LINUX_DRM_SYNCOBJ_SURFACE_V1_ERROR_CONFLICTING_POINTS);

	error_case_new(h, &c);
	wl_surface_attach(c.surface, harness_buffer_create(c.b.shm, 50, 50), 0, 0);
	wp_linux_drm_syncobj_surface_v1_set_acquire_point(c.object, c.timeline, 0, 1);
	wp_linux_drm_syncobj_surface_v1_set_release_point(c.object, c.timeline, 0, 2);
	wl_surface_commit(c.surface);
	expect_error(c.display, &wp_linux_drm_syncobj_surface_v1_interface,
		     WP_LINUX_DRM_SYNCOBJ_SURFACE_V1_ERROR_UNSUPPORTED_BUFFER);

	error_case_new(h, &c);
	wp_linux_drm_syncobj_manager_v1_get_surface(c.b.syncobj, c.surface);
	expect_error(c.display, &wp_linux_drm_syncobj_manager_v1_interface,
		     WP_LINUX_DRM_SYNCOBJ_MANAGER_V1_ERROR_SURFACE_EXISTS);

	error_case_new(h, &c);
	int pipes[2];

	CHECK(pipe(pipes) == 0, "pipe");
	wp_linux_drm_syncobj_manager_v1_import_timeline(c.b.syncobj, pipes[0]);
	expect_error(c.display, &wp_linux_drm_syncobj_manager_v1_interface,
		     WP_LINUX_DRM_SYNCOBJ_MANAGER_V1_ERROR_INVALID_TIMELINE);
	close(pipes[0]);
	close(pipes[1]);

	/* A plain window still works beside all that. */
	harness_roundtrip(h);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
