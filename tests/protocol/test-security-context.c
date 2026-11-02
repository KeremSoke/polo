/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A client that arrives through a security context carries its metadata and sees
 * no privileged global, and the socket lives until close_fd hangs up.
 */

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include "harness.h"
#include "security-context-v1-client-protocol.h"

/* Connections die on purpose here with their proxies live. Polo is its own
 * process and is still checked. */
int
__lsan_is_turned_off(void);

int
__lsan_is_turned_off(void)
{
	return 1;
}

struct seen {
	struct wp_security_context_manager_v1 *manager;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct xdg_wm_base *wm_base;
	bool layer_shell, foreign, test, capture, shell_ui, security, virtual_keyboard,
		input_method;
};

static void
global(void *data, struct wl_registry *registry, uint32_t name,
       const char *interface, uint32_t version)
{
	struct seen *s = data;

	if (strcmp(interface, wp_security_context_manager_v1_interface.name) == 0) {
		s->security = true;
		s->manager = wl_registry_bind(
			registry, name, &wp_security_context_manager_v1_interface, 1);
	} else if (strcmp(interface, wl_compositor_interface.name) == 0) {
		s->compositor = wl_registry_bind(registry, name,
						 &wl_compositor_interface, 4);
	} else if (strcmp(interface, wl_shm_interface.name) == 0) {
		s->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	} else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
		s->wm_base = wl_registry_bind(registry, name,
					      &xdg_wm_base_interface, 1);
	} else if (strcmp(interface, "zwlr_layer_shell_v1") == 0) {
		s->layer_shell = true;
	} else if (strcmp(interface, "zwlr_foreign_toplevel_manager_v1") == 0) {
		s->foreign = true;
	} else if (strcmp(interface, "polo_test_v1") == 0) {
		s->test = true;
	} else if (strcmp(interface, "weston_capture_v1") == 0) {
		s->capture = true;
	} else if (strcmp(interface, "polo_shell_ui_v1") == 0) {
		s->shell_ui = true;
	} else if (strcmp(interface, "zwp_virtual_keyboard_manager_v1") == 0) {
		s->virtual_keyboard = true;
	} else if (strcmp(interface, "zwp_input_method_manager_v2") == 0) {
		s->input_method = true;
	}
}

static void
global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = { global,
							       global_remove };

static char socket_path[108];

static int
listening_socket(void)
{
	struct sockaddr_un address = { .sun_family = AF_UNIX };
	static int counter;
	int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);

	snprintf(socket_path, sizeof socket_path, "/tmp/polo-sc-%d-%d",
		 (int)getpid(), counter++);
	unlink(socket_path);
	strcpy(address.sun_path, socket_path);
	CHECK(fd >= 0 && bind(fd, (struct sockaddr *)&address, sizeof address) == 0 &&
		      listen(fd, 8) == 0,
	      "listening socket: %s", strerror(errno));
	return fd;
}

static int
connect_socket(void)
{
	struct sockaddr_un address = { .sun_family = AF_UNIX };
	int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);

	strcpy(address.sun_path, socket_path);
	CHECK(fd >= 0 && connect(fd, (struct sockaddr *)&address, sizeof address) == 0,
	      "connect: %s", strerror(errno));
	return fd;
}

static const char *
sandbox_clients(struct harness *h)
{
	return harness_get_setting(h, "polo-test", "sandbox-clients");
}

static void
wait_sandbox(struct harness *h, const char *wanted)
{
	struct timespec tick = { 0, 10 * 1000000L };

	for (int i = 0; i < 300 && strcmp(sandbox_clients(h), wanted) != 0; i++)
		nanosleep(&tick, NULL);
	CHECK(strcmp(sandbox_clients(h), wanted) == 0, "sandbox clients \"%s\", wanted \"%s\"",
	      sandbox_clients(h), wanted);
}

static uint32_t peer_serial;

static void
peer_configure(void *data, struct xdg_surface *s, uint32_t serial)
{
	peer_serial = serial;
}

static const struct xdg_surface_listener peer_listener = { peer_configure };

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
	      "wrong protocol error");
	wl_display_disconnect(display);
}

static struct wl_display *
fresh(struct harness *h, struct seen *seen)
{
	struct wl_display *display = wl_display_connect(h->socket);

	CHECK(display != NULL, "cannot connect");
	memset(seen, 0, sizeof *seen);
	wl_registry_add_listener(wl_display_get_registry(display),
				 &registry_listener, seen);
	wl_display_roundtrip(display);
	return display;
}

static struct wp_security_context_v1 *
new_context(struct wl_display *display, struct seen *seen, int *close_end)
{
	int pipes[2];

	CHECK(pipe(pipes) == 0, "pipe");
	int listen_fd = listening_socket();
	struct wp_security_context_v1 *context =
		wp_security_context_manager_v1_create_listener(seen->manager, listen_fd,
							       pipes[0]);

	wl_display_flush(display);
	close(listen_fd);
	close(pipes[0]);
	*close_end = pipes[1];
	return context;
}

int
main(int argc, char **argv)
{
	struct harness *h = harness_start(argc, argv);
	struct seen creator, normal, boxed;
	struct wl_display *maker = fresh(h, &creator);

	CHECK(creator.manager, "no wp_security_context_manager_v1");
	CHECK(strcmp(sandbox_clients(h), "") == 0, "sandboxed clients already");

	struct wl_display *plain = fresh(h, &normal);

	CHECK(normal.layer_shell && normal.foreign && normal.security,
	      "an ordinary client lost its globals");
	CHECK(strcmp(sandbox_clients(h), "") == 0, "an ordinary client was marked");
	wl_display_disconnect(plain);

	/* A committed context, whose maker then goes. */
	int close_end;
	struct wp_security_context_v1 *context =
		new_context(maker, &creator, &close_end);

	wp_security_context_v1_set_sandbox_engine(context, "org.flatpak");
	wp_security_context_v1_set_app_id(context, "org.example.App");
	wp_security_context_v1_set_instance_id(context, "1234");
	wp_security_context_v1_commit(context);
	wl_display_roundtrip(maker);
	wp_security_context_v1_destroy(context);
	wl_display_roundtrip(maker);
	wl_display_disconnect(maker);
	harness_roundtrip(h);

	struct wl_display *inside = wl_display_connect_to_fd(connect_socket());

	CHECK(inside != NULL, "cannot connect through the context");
	memset(&boxed, 0, sizeof boxed);
	wl_registry_add_listener(wl_display_get_registry(inside), &registry_listener,
				 &boxed);
	wl_display_roundtrip(inside);
	wl_display_roundtrip(inside);
	wait_sandbox(h, "org.flatpak org.example.App 1234;");

	CHECK(!boxed.layer_shell && !boxed.foreign && !boxed.test && !boxed.capture &&
		      !boxed.shell_ui && !boxed.virtual_keyboard && !boxed.input_method,
	      "a sandboxed client sees a privileged global");
	CHECK(!boxed.security, "a sandboxed client sees the security context manager");
	CHECK(boxed.compositor && boxed.wm_base && boxed.shm,
	      "a sandboxed client lost the ordinary globals");

	/* It can be an ordinary window. */
	struct wl_surface *surface = wl_compositor_create_surface(boxed.compositor);
	struct xdg_surface *xdg = xdg_wm_base_get_xdg_surface(boxed.wm_base, surface);

	xdg_surface_add_listener(xdg, &peer_listener, NULL);
	xdg_surface_get_toplevel(xdg);
	wl_surface_commit(surface);
	wl_display_roundtrip(inside);
	CHECK(peer_serial != 0, "a sandboxed window got no configure");
	xdg_surface_ack_configure(xdg, peer_serial);
	wl_surface_attach(surface, harness_buffer_create(boxed.shm, 100, 100), 0, 0);
	wl_surface_commit(surface);
	CHECK(wl_display_roundtrip(inside) >= 0, "a sandboxed window failed");

	/* Hung up: no more clients, and the one there stays. */
	close(close_end);
	harness_roundtrip(h);
	struct timespec settle = { 0, 100 * 1000000L };

	nanosleep(&settle, NULL);
	harness_roundtrip(h);
	/* Polo has closed its copy, and this test closed its own. */
	struct sockaddr_un gone = { .sun_family = AF_UNIX };
	int late_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);

	strcpy(gone.sun_path, socket_path);
	CHECK(connect(late_fd, (struct sockaddr *)&gone, sizeof gone) < 0 &&
		      errno == ECONNREFUSED,
	      "the socket still listens after close_fd hung up");
	close(late_fd);
	CHECK(wl_display_roundtrip(inside) >= 0, "the client inside was dropped");
	wait_sandbox(h, "org.flatpak org.example.App 1234;");
	wl_display_disconnect(inside);
	wait_sandbox(h, "");

	/* A connection gone before Polo accepts it must not freeze Polo. */
	maker = fresh(h, &creator);
	context = new_context(maker, &creator, &close_end);
	wp_security_context_v1_commit(context);
	wl_display_roundtrip(maker);
	int listen_copy = listening_socket();
	int pipes_nb[2];

	CHECK(pipe(pipes_nb) == 0, "pipe");
	struct wp_security_context_v1 *blocking =
		wp_security_context_manager_v1_create_listener(creator.manager,
							       listen_copy, pipes_nb[0]);

	wl_display_roundtrip(maker);
	wp_security_context_v1_commit(blocking);
	wl_display_roundtrip(maker);
	CHECK(fcntl(listen_copy, F_GETFL) & O_NONBLOCK,
	      "Polo left a client's socket blocking");
	close(listen_copy);
	close(pipes_nb[0]);
	close(pipes_nb[1]);
	close(close_end);
	wl_display_disconnect(maker);

	/* Never committed: nothing listens. */
	maker = fresh(h, &creator);
	context = new_context(maker, &creator, &close_end);
	wl_display_roundtrip(maker);
	late_fd = connect_socket();
	struct wl_display *late = wl_display_connect_to_fd(late_fd);

	wl_display_sync(late);
	wl_display_flush(late);
	struct pollfd poller = { .fd = late_fd, .events = POLLIN };

	CHECK(poll(&poller, 1, 300) == 0, "an uncommitted context accepted a client");
	wl_display_disconnect(late);
	wp_security_context_v1_destroy(context);
	close(close_end);
	wl_display_roundtrip(maker);

	/* A context with no metadata is still a sandbox. */
	context = new_context(maker, &creator, &close_end);
	wp_security_context_v1_commit(context);
	wl_display_roundtrip(maker);
	inside = wl_display_connect_to_fd(connect_socket());
	memset(&boxed, 0, sizeof boxed);
	wl_registry_add_listener(wl_display_get_registry(inside), &registry_listener,
				 &boxed);
	wl_display_roundtrip(inside);
	wl_display_roundtrip(inside);
	CHECK(!boxed.layer_shell && !boxed.security,
	      "no metadata, and the client was not treated as sandboxed");
	wl_display_disconnect(inside);
	close(close_end);
	wl_display_disconnect(maker);

	/* close_fd hanging up while the object lives: destroying it is still fine,
	 * and anything else counts as using it. */
	maker = fresh(h, &creator);
	context = new_context(maker, &creator, &close_end);
	wp_security_context_v1_commit(context);
	wl_display_roundtrip(maker);
	close(close_end);
	nanosleep(&settle, NULL);
	wl_display_roundtrip(maker);
	wp_security_context_v1_destroy(context);
	CHECK(wl_display_roundtrip(maker) >= 0, "destroying a hung up context failed");
	context = new_context(maker, &creator, &close_end);
	wp_security_context_v1_commit(context);
	wl_display_roundtrip(maker);
	close(close_end);
	nanosleep(&settle, NULL);
	wl_display_roundtrip(maker);
	wp_security_context_v1_set_app_id(context, "a");
	expect_error(maker, &wp_security_context_v1_interface,
		     WP_SECURITY_CONTEXT_V1_ERROR_ALREADY_USED);

	/* Errors. */
	maker = fresh(h, &creator);
	context = new_context(maker, &creator, &close_end);
	wp_security_context_v1_set_app_id(context, "a");
	wp_security_context_v1_set_app_id(context, "b");
	expect_error(maker, &wp_security_context_v1_interface,
		     WP_SECURITY_CONTEXT_V1_ERROR_ALREADY_SET);
	close(close_end);

	maker = fresh(h, &creator);
	context = new_context(maker, &creator, &close_end);
	wp_security_context_v1_commit(context);
	wp_security_context_v1_set_app_id(context, "a");
	expect_error(maker, &wp_security_context_v1_interface,
		     WP_SECURITY_CONTEXT_V1_ERROR_ALREADY_USED);
	close(close_end);

	maker = fresh(h, &creator);
	context = new_context(maker, &creator, &close_end);
	wp_security_context_v1_commit(context);
	wp_security_context_v1_commit(context);
	expect_error(maker, &wp_security_context_v1_interface,
		     WP_SECURITY_CONTEXT_V1_ERROR_ALREADY_USED);
	close(close_end);

	maker = fresh(h, &creator);
	int pipes[2];

	CHECK(pipe(pipes) == 0, "pipe");
	wp_security_context_manager_v1_create_listener(creator.manager, pipes[0],
						       pipes[1]);
	expect_error(maker, &wp_security_context_manager_v1_interface,
		     WP_SECURITY_CONTEXT_MANAGER_V1_ERROR_INVALID_LISTEN_FD);

	unlink(socket_path);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
