/*
 * Copyright © 2011 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "config.h"

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>

#include "xwayland.h"
#include <libweston/xwayland-api.h>
#include "shared/helpers.h"
#include "shared/string-helpers.h"
#include "polo-xwm-private.h"

static void
weston_xserver_client_destroyed(struct wl_listener *listener, void *data)
{
	struct weston_xserver *wxs =
		container_of(listener, struct weston_xserver, client_destroy_listener);

	wxs->client = NULL;
}

static int
weston_xserver_handle_event(int listen_fd, uint32_t mask, void *data)
{
	struct weston_xserver *wxs = data;
	char display[8];

	snprintf(display, sizeof display, ":%d", wxs->display);

	wxs->client = wxs->spawn_func(wxs->user_data, display, wxs->abstract_fd, wxs->unix_fd);
	if (wxs->client == NULL) {
		weston_log("Failed to spawn the Xwayland server\n");
		return 1;
	}
	wxs->client_destroy_listener.notify = weston_xserver_client_destroyed;
	wl_client_add_destroy_late_listener(wxs->client,
					    &wxs->client_destroy_listener);

	wl_event_source_remove(wxs->abstract_source);
	wl_event_source_remove(wxs->unix_source);
	/* Polo: shutdown must not remove them again if the client is gone
	 * before xserver_exited re-adds them. */
	wxs->abstract_source = NULL;
	wxs->unix_source = NULL;

	return 1;
}

static void
weston_xserver_shutdown(struct weston_xserver *wxs)
{
	char path[256];

	snprintf(path, sizeof path, "/tmp/.X%d-lock", wxs->display);
	unlink(path);
	snprintf(path, sizeof path, "/tmp/.X11-unix/X%d", wxs->display);
	unlink(path);
	if (wxs->client) {
		wl_client_destroy(wxs->client);
		wxs->client = NULL;
	} else {
		if (wxs->abstract_source)
			wl_event_source_remove(wxs->abstract_source);
		if (wxs->unix_source)
			wl_event_source_remove(wxs->unix_source);
	}
	close(wxs->abstract_fd);
	close(wxs->unix_fd);
	if (wxs->wm) {
		weston_wm_destroy(wxs->wm);
		wxs->wm = NULL;
	}
	wxs->loop = NULL;
}

static int
bind_to_abstract_socket(int display)
{
	struct sockaddr_un addr;
	socklen_t size, name_size;
	int fd;

	fd = socket(PF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd < 0)
		return -1;

	addr.sun_family = AF_LOCAL;
	name_size = snprintf(addr.sun_path, sizeof addr.sun_path,
			     "%c/tmp/.X11-unix/X%d", 0, display);
	size = offsetof(struct sockaddr_un, sun_path) + name_size;
	if (bind(fd, (struct sockaddr *) &addr, size) < 0) {
		weston_log("failed to bind to @%s: %s\n", addr.sun_path + 1,
			   strerror(errno));
		close(fd);
		return -1;
	}

	if (listen(fd, 1) < 0) {
		close(fd);
		return -1;
	}

	return fd;
}

static int
bind_to_unix_socket(int display)
{
	struct sockaddr_un addr;
	socklen_t size, name_size;
	int fd;

	fd = socket(PF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd < 0)
		return -1;

	addr.sun_family = AF_LOCAL;
	name_size = snprintf(addr.sun_path, sizeof addr.sun_path,
			     "/tmp/.X11-unix/X%d", display) + 1;
	size = offsetof(struct sockaddr_un, sun_path) + name_size;
	unlink(addr.sun_path);
	if (bind(fd, (struct sockaddr *) &addr, size) < 0) {
		weston_log("failed to bind to %s: %s\n", addr.sun_path,
			   strerror(errno));
		close(fd);
		return -1;
	}

	if (listen(fd, 1) < 0) {
		unlink(addr.sun_path);
		close(fd);
		return -1;
	}

	return fd;
}

static int
create_lockfile(int display, char *lockfile, size_t lsize)
{
	/* 10 decimal characters, trailing LF and NUL byte; see comment
	 * at end of function. */
	char pid[11];
	int fd, size;
	pid_t other;

	snprintf(lockfile, lsize, "/tmp/.X%d-lock", display);
	fd = open(lockfile, O_WRONLY | O_CLOEXEC | O_CREAT | O_EXCL, 0444);
	if (fd < 0 && errno == EEXIST) {
		fd = open(lockfile, O_CLOEXEC | O_RDONLY);
		if (fd < 0 || read(fd, pid, 11) != 11) {
			weston_log("can't read lock file %s: %s\n",
				lockfile, strerror(errno));
			if (fd >= 0)
				close (fd);

			errno = EEXIST;
			return -1;
		}

		/* Trim the trailing LF, or at least ensure it's NULL. */
		pid[10] = '\0';

		if (!safe_strtoint(pid, &other)) {
			weston_log("can't parse lock file %s\n",
				lockfile);
			close(fd);
			errno = EEXIST;
			return -1;
		}

		if (kill(other, 0) < 0 && errno == ESRCH) {
			/* stale lock file; unlink and try again */
			weston_log("unlinking stale lock file %s\n", lockfile);
			close(fd);
			if (unlink(lockfile))
				/* If we fail to unlink, return EEXIST
				   so we try the next display number.*/
				errno = EEXIST;
			else
				errno = EAGAIN;
			return -1;
		}

		close(fd);
		errno = EEXIST;
		return -1;
	} else if (fd < 0) {
		weston_log("failed to create lock file %s: %s\n",
			lockfile, strerror(errno));
		return -1;
	}

	/* Subtle detail: we use the pid of the wayland compositor, not the
	 * xserver in the lock file.
	 * Also subtle is that we don't emit a trailing NUL to the file, so
	 * our size here is 11 rather than 12. */
	size = dprintf(fd, "%10d\n", getpid());
	if (size != 11) {
		unlink(lockfile);
		close(fd);
		return -1;
	}

	close(fd);

	return 0;
}

static void
weston_xserver_destroy(struct weston_xserver *wxs)
{
	polo_xserver_destroyed(wxs);
	wl_list_remove(&wxs->compositor_destroy_listener.link);

	if (wxs->loop)
		weston_xserver_shutdown(wxs);

	weston_log_scope_destroy(wxs->wm_debug);

	free(wxs);
}

static void
weston_xserver_destroy_handler(struct wl_listener *l, void *data)
{
	struct weston_xserver *wxs =
		container_of(l, struct weston_xserver, compositor_destroy_listener);

	weston_xserver_destroy(wxs);
}

static int
weston_xwayland_listen(struct weston_xwayland *xwayland, void *user_data,
		       weston_xwayland_spawn_xserver_func_t spawn_func)
{
	struct weston_xserver *wxs = (struct weston_xserver *)xwayland;
	char lockfile[256], display_name[8];

	wxs->user_data = user_data;
	wxs->spawn_func = spawn_func;

retry:
	if (create_lockfile(wxs->display, lockfile, sizeof lockfile) < 0) {
		if (errno == EAGAIN) {
			goto retry;
		} else if (errno == EEXIST) {
			wxs->display++;
			goto retry;
		} else {
			weston_xserver_destroy(wxs);
			return -1;
		}
	}

	wxs->abstract_fd = bind_to_abstract_socket(wxs->display);
	if (wxs->abstract_fd < 0 && errno == EADDRINUSE) {
		wxs->display++;
		unlink(lockfile);
		goto retry;
	}

	wxs->unix_fd = bind_to_unix_socket(wxs->display);
	if (wxs->unix_fd < 0) {
		unlink(lockfile);
		close(wxs->abstract_fd);
		weston_xserver_destroy(wxs);
		return -1;
	}

	snprintf(display_name, sizeof display_name, ":%d", wxs->display);
	weston_log("xserver listening on display %s\n", display_name);
	setenv("DISPLAY", display_name, 1);

	wxs->loop = wl_display_get_event_loop(wxs->wl_display);
	wxs->abstract_source =
		wl_event_loop_add_fd(wxs->loop, wxs->abstract_fd,
				     WL_EVENT_READABLE,
				     weston_xserver_handle_event, wxs);
	wxs->unix_source =
		wl_event_loop_add_fd(wxs->loop, wxs->unix_fd,
				     WL_EVENT_READABLE,
				     weston_xserver_handle_event, wxs);

	return 0;
}

static void
weston_xwayland_xserver_loaded(struct weston_xwayland *xwayland, int wm_fd)
{
	struct weston_xserver *wxs = (struct weston_xserver *)xwayland;
	wxs->wm = weston_wm_create(wxs, wm_fd);
}

static void
weston_xwayland_xserver_exited(struct weston_xwayland *xwayland)
{
	struct weston_xserver *wxs = (struct weston_xserver *)xwayland;

	if (wxs->client)
		wl_client_destroy(wxs->client);
	wxs->client = NULL;

	wxs->abstract_source =
		wl_event_loop_add_fd(wxs->loop, wxs->abstract_fd,
				     WL_EVENT_READABLE,
				     weston_xserver_handle_event, wxs);
	wxs->unix_source =
		wl_event_loop_add_fd(wxs->loop, wxs->unix_fd,
				     WL_EVENT_READABLE,
				     weston_xserver_handle_event, wxs);

	if (wxs->wm) {
		weston_log("xserver exited, will restart on demand\n");
		weston_wm_destroy(wxs->wm);
		wxs->wm = NULL;
	}
}

struct weston_xserver *
polo_xserver_create(struct weston_compositor *compositor)
{
	struct weston_xserver *wxs;

	wxs = zalloc(sizeof *wxs);
	if (wxs == NULL)
		return NULL;
	wxs->wl_display = compositor->wl_display;
	wxs->compositor = compositor;

	if (!weston_compositor_add_destroy_listener_once(compositor,
							 &wxs->compositor_destroy_listener,
							 weston_xserver_destroy_handler)) {
		free(wxs);
		return NULL;
	}

	wxs->wm_debug =
		weston_compositor_add_log_scope(wxs->compositor, "xwm-wm-x11",
						"XWM's window management X11 events\n",
						NULL, NULL, NULL);

	return wxs;
}

int
polo_xserver_listen(struct weston_xserver *wxs, void *user_data,
		    weston_xwayland_spawn_xserver_func_t spawn_func)
{
	return weston_xwayland_listen((struct weston_xwayland *)wxs, user_data,
				      spawn_func);
}

void
polo_xserver_loaded(struct weston_xserver *wxs, int wm_fd)
{
	weston_xwayland_xserver_loaded((struct weston_xwayland *)wxs, wm_fd);
}

struct weston_wm *
polo_xserver_get_wm(struct weston_xserver *wxs)
{
	return wxs->wm;
}

void
polo_xserver_exited(struct weston_xserver *wxs)
{
	weston_xwayland_xserver_exited((struct weston_xwayland *)wxs);
}

void
polo_xserver_spawn(struct weston_xserver *wxs)
{
	weston_xserver_handle_event(-1, 0, wxs);
}

void
polo_xserver_give_up(struct weston_xserver *wxs)
{
	weston_xserver_shutdown(wxs);
}

int
polo_xserver_get_display(struct weston_xserver *wxs)
{
	return wxs->display;
}
