/*
 * Copyright © 2012 Intel Corporation
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

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>

#include <libweston/libweston.h>
#include "xwayland.h"
#include "polo-backend-private.h"
#include "hash.h"
#include "shared/helpers.h"

#ifdef WM_DEBUG
#define wm_log(...) weston_log(__VA_ARGS__)
#else
#define wm_log(...) do {} while (0)
#endif

struct pending_read {
	struct wl_list link;
	xcb_atom_t selection;
	xcb_atom_t target;
	int fd;
};

static void
start_read(struct weston_wm *wm, xcb_atom_t selection, xcb_atom_t target, int fd)
{
	fcntl(fd, F_SETFL, O_WRONLY | O_NONBLOCK);
	wm->data_source_fd = fd;
	wm->read_active = 1;
	wm->incr = 0;
	xcb_convert_selection(wm->conn, wm->selection_window, selection, target,
			      wm->atom.wl_selection, XCB_TIME_CURRENT_TIME);
	xcb_flush(wm->conn);
}

/* libweston's clipboard.c reads every new selection at once, so a paste can
 * arrive during that read; there is one property to read through. */
static void
finish_read(struct weston_wm *wm)
{
	struct pending_read *next;

	close(wm->data_source_fd);
	wm->data_source_fd = -1;
	wm->read_active = 0;
	wm->incr = 0;
	if (wl_list_empty(&wm->read_queue))
		return;
	next = wl_container_of(wm->read_queue.next, next, link);
	wl_list_remove(&next->link);
	start_read(wm, next->selection, next->target, next->fd);
	free(next);
}

/* The reader may be gone. SIGPIPE stays blocked only here: a process-wide
 * SIG_IGN would be inherited by everything Polo launches. */
static ssize_t
write_no_sigpipe(int fd, const void *buffer, size_t size)
{
	sigset_t pipe_set, old, pending;
	struct timespec zero = { 0, 0 };
	bool was_pending;
	ssize_t len;
	int saved;

	sigemptyset(&pipe_set);
	sigaddset(&pipe_set, SIGPIPE);
	sigpending(&pending);
	was_pending = sigismember(&pending, SIGPIPE);
	pthread_sigmask(SIG_BLOCK, &pipe_set, &old);
	len = write(fd, buffer, size);
	saved = errno;
	if (len == -1 && errno == EPIPE && !was_pending)
		sigtimedwait(&pipe_set, NULL, &zero);
	pthread_sigmask(SIG_SETMASK, &old, NULL);
	errno = saved;
	return len;
}

static int
writable_callback(int fd, uint32_t mask, void *data)
{
	struct weston_wm *wm = data;
	unsigned char *property;
	int len, remainder;

	property = xcb_get_property_value(wm->property_reply);
	remainder = xcb_get_property_value_length(wm->property_reply) -
		wm->property_start;

	len = write_no_sigpipe(fd, property + wm->property_start, remainder);
	if (len == -1 && (errno == EAGAIN || errno == EINTR))
		return 1;
	if (len == -1) {
		free(wm->property_reply);
		wm->property_reply = NULL;
		if (wm->property_source)
			wl_event_source_remove(wm->property_source);
		wm->property_source = NULL;
		weston_log("write error to target fd: %s\n", strerror(errno));
		finish_read(wm);
		return 1;
	}

	wm_log("wrote %d (chunk size %d) of %d bytes\n",
		wm->property_start + len,
		len, xcb_get_property_value_length(wm->property_reply));

	wm->property_start += len;
	if (len == remainder) {
		free(wm->property_reply);
		wm->property_reply = NULL;
		if (wm->property_source)
			wl_event_source_remove(wm->property_source);
		wm->property_source = NULL;

		if (wm->incr) {
			xcb_delete_property(wm->conn,
					    wm->selection_window,
					    wm->atom.wl_selection);
			/* A slow reader lands here outside the XCB event
			 * loop; unflushed, the owner never sends more. */
			xcb_flush(wm->conn);
		} else {
			wm_log("transfer complete\n");
			finish_read(wm);
		}
	}

	return 1;
}

static void
weston_wm_write_property(struct weston_wm *wm, xcb_get_property_reply_t *reply)
{
	wm->property_start = 0;
	wm->property_reply = reply;
	writable_callback(wm->data_source_fd, WL_EVENT_WRITABLE, wm);

	if (wm->property_reply)
		wm->property_source =
			wl_event_loop_add_fd(wm->server->loop,
					     wm->data_source_fd,
					     WL_EVENT_WRITABLE,
					     writable_callback, wm);
}

static void
weston_wm_get_incr_chunk(struct weston_wm *wm)
{
	xcb_get_property_cookie_t cookie;
	xcb_get_property_reply_t *reply;
	FILE *fp;
	char *logstr;
	size_t logsize;

	cookie = xcb_get_property(wm->conn,
				  0, /* delete */
				  wm->selection_window,
				  wm->atom.wl_selection,
				  XCB_GET_PROPERTY_TYPE_ANY,
				  0, /* offset */
				  0x1fffffff /* length */);

	reply = xcb_get_property_reply(wm->conn, cookie, NULL);
	if (reply == NULL)
		return;

	fp = open_memstream(&logstr, &logsize);
	if (fp) {
		dump_property(fp, wm, wm->atom.wl_selection, reply);
		if (fclose(fp) == 0)
			wm_log("%s", logstr);
		free(logstr);
	}

	if (xcb_get_property_value_length(reply) > 0) {
		/* reply's ownership is transferred to wm, which is responsible
		 * for freeing it */
		weston_wm_write_property(wm, reply);
	} else {
		wm_log("transfer complete\n");
		finish_read(wm);
		free(reply);
	}
}

#define UTF8_MIME "text/plain;charset=utf-8"

void
weston_wm_read_selection(struct weston_wm *wm, xcb_atom_t selection,
			 const char *mime_type, int32_t fd)
{
	xcb_atom_t target = wm->atom.utf8_string;

	if (strcmp(mime_type, UTF8_MIME) != 0) {
		xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(
			wm->conn,
			xcb_intern_atom(wm->conn, 0, strlen(mime_type), mime_type),
			NULL);

		target = reply ? reply->atom : XCB_ATOM_NONE;
		free(reply);
	}
	if (target == XCB_ATOM_NONE) {
		close(fd);
	} else if (wm->read_active) {
		struct pending_read *pending = zalloc(sizeof *pending);

		if (pending) {
			pending->selection = selection;
			pending->target = target;
			pending->fd = fd;
			wl_list_insert(wm->read_queue.prev, &pending->link);
		} else {
			close(fd);
		}
	} else {
		start_read(wm, selection, target, fd);
	}
}

struct x11_data_source {
	struct weston_data_source base;
	struct weston_wm *wm;
	xcb_atom_t selection;
};

static struct polo_primary *
seat_primary(struct weston_wm *wm, struct weston_seat *seat)
{
	PoloBackendSeat *polo_seat = seat ?
		polo_backend_server_lookup_seat(
			weston_compositor_get_user_data(wm->server->compositor),
			seat) :
		NULL;

	return polo_seat ? polo_backend_seat_get_primary(polo_seat) : NULL;
}

static void
data_source_accept(struct weston_data_source *source,
		   uint32_t serial, const char *mime_type)
{
}

static void
data_source_send(struct weston_data_source *base,
		 const char *mime_type, int32_t fd)
{
	struct x11_data_source *source = (struct x11_data_source *) base;
	struct weston_wm *wm = source->wm;

	weston_wm_read_selection(wm, source->selection, mime_type, fd);
}

static void
data_source_cancel(struct weston_data_source *source)
{
	char **mime;

	wl_signal_emit(&source->destroy_signal, source);
	wl_array_for_each(mime, &source->mime_types)
		free(*mime);
	wl_array_release(&source->mime_types);
	free(source);
}

static void
weston_wm_get_selection_targets(struct weston_wm *wm, xcb_atom_t selection)
{
	bool primary = selection == XCB_ATOM_PRIMARY;
	xcb_atom_t property = primary ? wm->atom.wl_primary : wm->atom.wl_selection;
	struct x11_data_source *source;
	struct weston_compositor *compositor;
	struct weston_seat *seat = weston_wm_pick_seat(wm);
	xcb_get_property_cookie_t cookie;
	xcb_get_property_reply_t *reply;
	xcb_atom_t *value;
	char **p;
	uint32_t i;
	FILE *fp;
	char *logstr;
	size_t logsize;

	if (!seat)
		return;

	cookie = xcb_get_property(wm->conn,
				  1, /* delete */
				  wm->selection_window,
				  property,
				  XCB_GET_PROPERTY_TYPE_ANY,
				  0, /* offset */
				  4096 /* length */);

	reply = xcb_get_property_reply(wm->conn, cookie, NULL);
	if (reply == NULL)
		return;

	fp = open_memstream(&logstr, &logsize);
	if (fp) {
		dump_property(fp, wm, property, reply);
		if (fclose(fp) == 0)
			wm_log("%s", logstr);
		free(logstr);
	}

	if (reply->type != XCB_ATOM_ATOM) {
		free(reply);
		return;
	}

	source = zalloc(sizeof *source);
	if (source == NULL) {
		free(reply);
		return;
	}

	wl_signal_init(&source->base.destroy_signal);
	source->base.accept = data_source_accept;
	source->base.send = data_source_send;
	source->base.cancel = data_source_cancel;
	source->wm = wm;
	source->selection = selection;

	wl_array_init(&source->base.mime_types);
	value = xcb_get_property_value(reply);
	for (i = 0; i < reply->value_len; i++) {
		const char *name = value[i] == wm->atom.utf8_string
			? UTF8_MIME : get_atom_name(wm->conn, value[i]);
		char **existing;
		int seen = 0;

		if (!strchr(name, '/'))
			continue;
		wl_array_for_each(existing, &source->base.mime_types)
			seen |= strcmp(*existing, name) == 0;
		if (seen)
			continue;
		p = wl_array_add(&source->base.mime_types, sizeof *p);
		if (p)
			*p = strdup(name);
	}

	compositor = wm->server->compositor;
	if (primary)
		polo_primary_set_source(seat_primary(wm, seat), &source->base);
	else
		weston_seat_set_selection(seat, &source->base,
					  wl_display_next_serial(compositor->wl_display));

	free(reply);
}

static void
weston_wm_get_selection_data(struct weston_wm *wm)
{
	xcb_get_property_cookie_t cookie;
	xcb_get_property_reply_t *reply;
	FILE *fp;
	char *logstr;
	size_t logsize;

	cookie = xcb_get_property(wm->conn,
				  1, /* delete */
				  wm->selection_window,
				  wm->atom.wl_selection,
				  XCB_GET_PROPERTY_TYPE_ANY,
				  0, /* offset */
				  0x1fffffff /* length */);

	reply = xcb_get_property_reply(wm->conn, cookie, NULL);

	fp = open_memstream(&logstr, &logsize);
	if (fp) {
		dump_property(fp, wm, wm->atom.wl_selection, reply);
		if (fclose(fp) == 0)
			wm_log("%s", logstr);
		free(logstr);
	}

	if (reply == NULL) {
		return;
	} else if (reply->type == wm->atom.incr) {
		wm->incr = 1;
		free(reply);
	} else {
		wm->incr = 0;
		/* reply's ownership is transferred to wm, which is responsible
		 * for freeing it */
		weston_wm_write_property(wm, reply);
	}
}

static void
weston_wm_handle_selection_notify(struct weston_wm *wm,
				xcb_generic_event_t *event)
{
	xcb_selection_notify_event_t *selection_notify =
		(xcb_selection_notify_event_t *) event;

	if (selection_notify->property == XCB_ATOM_NONE) {
		/* convert selection failed */
		if (selection_notify->target != wm->atom.targets &&
		    wm->read_active)
			finish_read(wm);
	} else if (selection_notify->target == wm->atom.targets) {
		weston_wm_get_selection_targets(wm, selection_notify->selection);
	} else {
		weston_wm_get_selection_data(wm);
	}
}

static const size_t incr_chunk_size = 64 * 1024;

static void
weston_wm_send_selection_notify(struct weston_wm *wm, xcb_atom_t property)
{
	xcb_selection_notify_event_t selection_notify;

	memset(&selection_notify, 0, sizeof selection_notify);
	selection_notify.response_type = XCB_SELECTION_NOTIFY;
	selection_notify.sequence = 0;
	selection_notify.time = wm->selection_request.time;
	selection_notify.requestor = wm->selection_request.requestor;
	selection_notify.selection = wm->selection_request.selection;
	selection_notify.target = wm->selection_request.target;
	selection_notify.property = property;

	xcb_send_event(wm->conn, 0, /* propagate */
		       wm->selection_request.requestor,
		       XCB_EVENT_MASK_NO_EVENT, (char *) &selection_notify);
}

/* What the selection being requested holds. */
static struct weston_data_source *
requested_source(struct weston_wm *wm)
{
	struct weston_seat *seat = weston_wm_pick_seat(wm);

	if (wm->selection_request.selection == wm->atom.xdnd_selection)
		return wm->bridge_source;
	if (wm->selection_request.selection == XCB_ATOM_PRIMARY)
		return seat ? polo_primary_get_source(seat_primary(wm, seat)) : NULL;
	return seat ? seat->selection_data_source : NULL;
}

/* The MIME type of the seat's selection a target stands for, or NULL. */
static const char *
mime_for_target(struct weston_wm *wm, struct weston_data_source *source,
		xcb_atom_t target)
{
	const char **mime;
	const char *text = NULL;
	const char *name = get_atom_name(wm->conn, target);

	wl_array_for_each(mime, &source->mime_types) {
		if (strcmp(*mime, name) == 0)
			return *mime;
		if (strcmp(*mime, UTF8_MIME) == 0)
			text = *mime;
		else if (!text && strcmp(*mime, "text/plain") == 0)
			text = *mime;
	}
	return target == wm->atom.utf8_string || target == wm->atom.text ?
		text : NULL;
}

static void
weston_wm_send_targets(struct weston_wm *wm)
{
	struct weston_data_source *source = requested_source(wm);
	xcb_atom_t targets[128];
	uint32_t n = 0;
	const char **mime;

	targets[n++] = wm->atom.timestamp;
	targets[n++] = wm->atom.targets;
	if (source && mime_for_target(wm, source, wm->atom.utf8_string)) {
		targets[n++] = wm->atom.utf8_string;
		targets[n++] = wm->atom.text;
	}
	if (source) {
		wl_array_for_each(mime, &source->mime_types) {
			xcb_intern_atom_reply_t *reply;

			if (n == ARRAY_LENGTH(targets))
				break;
			reply = xcb_intern_atom_reply(
				wm->conn,
				xcb_intern_atom(wm->conn, 0, strlen(*mime), *mime),
				NULL);
			if (reply)
				targets[n++] = reply->atom;
			free(reply);
		}
	}

	xcb_change_property(wm->conn,
			    XCB_PROP_MODE_REPLACE,
			    wm->selection_request.requestor,
			    wm->selection_request.property,
			    XCB_ATOM_ATOM,
			    32, /* format */
			    n, targets);

	weston_wm_send_selection_notify(wm, wm->selection_request.property);
}

static void
weston_wm_send_timestamp(struct weston_wm *wm)
{
	xcb_change_property(wm->conn,
			    XCB_PROP_MODE_REPLACE,
			    wm->selection_request.requestor,
			    wm->selection_request.property,
			    XCB_ATOM_INTEGER,
			    32, /* format */
			    1,
			    wm->selection_request.selection == XCB_ATOM_PRIMARY ?
				    &wm->primary_timestamp :
				    &wm->selection_timestamp);

	weston_wm_send_selection_notify(wm, wm->selection_request.property);
}

static int
weston_wm_flush_source_data(struct weston_wm *wm)
{
	int length;

	xcb_change_property(wm->conn,
			    XCB_PROP_MODE_REPLACE,
			    wm->selection_request.requestor,
			    wm->selection_request.property,
			    wm->selection_target,
			    8, /* format */
			    wm->source_data.size,
			    wm->source_data.data);
	wm->selection_property_set = 1;
	length = wm->source_data.size;
	wm->source_data.size = 0;

	return length;
}

static void
release_source_data(struct weston_wm *wm)
{
	wl_array_release(&wm->source_data);
	wl_array_init(&wm->source_data);
}

/* INCR is driven by deletes on the requestor; only root's children are
 * selected already, and a requestor can be any window. */
static void
watch_requestor(struct weston_wm *wm)
{
	uint32_t mask = XCB_EVENT_MASK_PROPERTY_CHANGE;

	if (!hash_table_lookup(wm->window_hash, wm->selection_request.requestor))
		xcb_change_window_attributes(wm->conn,
					     wm->selection_request.requestor,
					     XCB_CW_EVENT_MASK, &mask);
}

static int
weston_wm_read_data_source(int fd, uint32_t mask, void *data)
{
	struct weston_wm *wm = data;
	int len, current, available;
	void *p;

	current = wm->source_data.size;
	if (wm->source_data.size < incr_chunk_size)
		p = wl_array_add(&wm->source_data, incr_chunk_size);
	else
		p = (char *) wm->source_data.data + wm->source_data.size;
	available = wm->source_data.alloc - current;

	len = read(fd, p, available);
	if (len == -1) {
		weston_log("read error from data source: %s\n",
			   strerror(errno));
		weston_wm_send_selection_notify(wm, XCB_ATOM_NONE);
		if (wm->property_source)
			wl_event_source_remove(wm->property_source);
		wm->property_source = NULL;
		close(fd);
		release_source_data(wm);
		return 1;
	}

	wm_log("read %d (available %d, mask 0x%x) bytes: \"%.*s\"\n",
		len, available, mask, len, (char *) p);

	wm->source_data.size = current + len;
	if (wm->source_data.size >= incr_chunk_size) {
		if (!wm->incr) {
			wm_log("got %zu bytes, starting incr\n",
				wm->source_data.size);
			wm->incr = 1;
			watch_requestor(wm);
			xcb_change_property(wm->conn,
					    XCB_PROP_MODE_REPLACE,
					    wm->selection_request.requestor,
					    wm->selection_request.property,
					    wm->atom.incr,
					    32, /* format */
					    1, &incr_chunk_size);
			wm->selection_property_set = 1;
			wm->flush_property_on_delete = 1;
			if (wm->property_source)
				wl_event_source_remove(wm->property_source);
			wm->property_source = NULL;
			weston_wm_send_selection_notify(wm, wm->selection_request.property);
		} else if (wm->selection_property_set) {
			wm_log("got %zu bytes, waiting for "
				"property delete\n", wm->source_data.size);

			wm->flush_property_on_delete = 1;
			if (wm->property_source)
				wl_event_source_remove(wm->property_source);
			wm->property_source = NULL;
		} else {
			wm_log("got %zu bytes, "
				"property deleted, setting new property\n",
				wm->source_data.size);
			weston_wm_flush_source_data(wm);
		}
	} else if (len == 0 && !wm->incr) {
		wm_log("non-incr transfer complete\n");
		/* Non-incr transfer all done. */
		weston_wm_flush_source_data(wm);
		weston_wm_send_selection_notify(wm, wm->selection_request.property);
		xcb_flush(wm->conn);
		if (wm->property_source)
			wl_event_source_remove(wm->property_source);
		wm->property_source = NULL;
		close(fd);
		release_source_data(wm);
		wm->selection_request.requestor = XCB_NONE;
	} else if (len == 0 && wm->incr) {
		wm_log("incr transfer complete\n");

		wm->flush_property_on_delete = 1;
		if (wm->selection_property_set) {
			wm_log("got %zu bytes, waiting for "
				"property delete\n", wm->source_data.size);
		} else {
			wm_log("got %zu bytes, "
				"property deleted, setting new property\n",
				wm->source_data.size);
			weston_wm_flush_source_data(wm);
		}
		xcb_flush(wm->conn);
		if (wm->property_source)
			wl_event_source_remove(wm->property_source);
		wm->property_source = NULL;
		close(wm->data_source_fd);
		wm->data_source_fd = -1;
	} else {
		wm_log("nothing happened, buffered the bytes\n");
	}

	/* This runs outside the XCB event loop, which flushes after itself. */
	xcb_flush(wm->conn);
	return 1;
}

static void
weston_wm_send_data(struct weston_wm *wm, xcb_atom_t target, const char *mime_type)
{
	struct weston_data_source *source = requested_source(wm);
	int p[2];

	if (pipe2(p, O_CLOEXEC | O_NONBLOCK) == -1) {
		weston_log("pipe2 failed: %s\n", strerror(errno));
		weston_wm_send_selection_notify(wm, XCB_ATOM_NONE);
		return;
	}

	release_source_data(wm);
	wm->selection_target = target;
	wm->data_source_fd = p[0];
	wm->property_source = wl_event_loop_add_fd(wm->server->loop,
						   wm->data_source_fd,
						   WL_EVENT_READABLE,
						   weston_wm_read_data_source,
						   wm);

	source->send(source, mime_type, p[1]);
}

static void
weston_wm_send_incr_chunk(struct weston_wm *wm)
{
	int length;

	wm_log("property deleted\n");

	wm->selection_property_set = 0;
	if (wm->flush_property_on_delete) {
		wm_log("setting new property, %zu bytes\n",
			wm->source_data.size);
		wm->flush_property_on_delete = 0;
		length = weston_wm_flush_source_data(wm);

		if (wm->data_source_fd >= 0) {
			wm->property_source =
				wl_event_loop_add_fd(wm->server->loop,
						     wm->data_source_fd,
						     WL_EVENT_READABLE,
						     weston_wm_read_data_source,
						     wm);
		} else if (length > 0) {
			/* Transfer is all done, but queue a flush for
			 * the delete of the last chunk so we can set
			 * the 0 sized property to signal the end of
			 * the transfer. */
			wm->flush_property_on_delete = 1;
		} else {
			release_source_data(wm);
			wm->selection_request.requestor = XCB_NONE;
		}
	}
}

static int
weston_wm_handle_selection_property_notify(struct weston_wm *wm,
					   xcb_generic_event_t *event)
{
	xcb_property_notify_event_t *property_notify =
		(xcb_property_notify_event_t *) event;

	if (property_notify->window == wm->selection_window) {
		if (property_notify->state == XCB_PROPERTY_NEW_VALUE &&
		    property_notify->atom == wm->atom.wl_selection &&
		    wm->incr)
			weston_wm_get_incr_chunk(wm);
		return 1;
	} else if (property_notify->window == wm->selection_request.requestor) {
		/* The requestor can be a managed window: its other properties
		 * are the window manager's. */
		if (property_notify->atom != wm->selection_request.property)
			return 0;
		if (property_notify->state == XCB_PROPERTY_DELETE && wm->incr)
			weston_wm_send_incr_chunk(wm);
		return 1;
	}

	return 0;
}

/* A requestor that died mid-INCR never deletes the property again. Its id can
 * be reused at once, and a window asking again has given up on the old transfer
 * in any case. */
static bool
abandon_if_requestor_gone(struct weston_wm *wm, xcb_window_t asking)
{
	xcb_get_window_attributes_reply_t *reply =
		xcb_get_window_attributes_reply(
			wm->conn,
			xcb_get_window_attributes(
				wm->conn, wm->selection_request.requestor),
			NULL);

	if (reply && asking != wm->selection_request.requestor) {
		free(reply);
		return false;
	}
	free(reply);
	if (wm->property_source)
		wl_event_source_remove(wm->property_source);
	wm->property_source = NULL;
	if (wm->data_source_fd >= 0)
		close(wm->data_source_fd);
	wm->data_source_fd = -1;
	release_source_data(wm);
	wm->incr = 0;
	wm->flush_property_on_delete = 0;
	wm->selection_property_set = 0;
	wm->selection_request.requestor = XCB_NONE;
	return true;
}

static void
refuse_request(struct weston_wm *wm, xcb_selection_request_event_t *request)
{
	xcb_selection_request_event_t running = wm->selection_request;

	wm->selection_request = *request;
	weston_wm_send_selection_notify(wm, XCB_ATOM_NONE);
	wm->selection_request = running;
}

static void
weston_wm_handle_selection_request(struct weston_wm *wm,
				 xcb_generic_event_t *event)
{
	xcb_selection_request_event_t *selection_request =
		(xcb_selection_request_event_t *) event;

	/* Our own conversion reaches us when we took the selection after
	 * asking for it; refuse it rather than serve ourselves. */
	if (selection_request->requestor == wm->selection_window) {
		refuse_request(wm, selection_request);
		return;
	}

	/* One transfer at a time: state below belongs to the running one. */
	if (wm->selection_request.requestor != XCB_NONE &&
	    (wm->property_source || wm->flush_property_on_delete) &&
	    !abandon_if_requestor_gone(wm, selection_request->requestor)) {
		refuse_request(wm, selection_request);
		return;
	}

	wm->selection_request = *selection_request;
	wm->incr = 0;
	wm->flush_property_on_delete = 0;

	if (selection_request->selection == wm->atom.clipboard_manager) {
		/* The weston clipboard should already have grabbed
		 * the first target, so just send selection notify
		 * now.  This isn't synchronized with the clipboard
		 * finishing getting the data, so there's a race here. */
		weston_wm_send_selection_notify(wm, wm->selection_request.property);
		wm->selection_request.requestor = XCB_NONE;
		return;
	}

	if (selection_request->target == wm->atom.targets) {
		weston_wm_send_targets(wm);
		wm->selection_request.requestor = XCB_NONE;
	} else if (selection_request->target == wm->atom.timestamp) {
		weston_wm_send_timestamp(wm);
		wm->selection_request.requestor = XCB_NONE;
	} else {
		struct weston_data_source *source = requested_source(wm);
		const char *mime = source ?
			mime_for_target(wm, source, selection_request->target) :
			NULL;

		if (mime) {
			weston_wm_send_data(
				wm,
				selection_request->target == wm->atom.text ?
					wm->atom.utf8_string :
					selection_request->target,
				mime);
		} else {
			weston_wm_send_selection_notify(wm, XCB_ATOM_NONE);
			wm->selection_request.requestor = XCB_NONE;
		}
	}
}

static void
weston_wm_primary_owner_changed(struct weston_wm *wm,
				xcb_xfixes_selection_notify_event_t *notify)
{
	struct weston_seat *seat = weston_wm_pick_seat(wm);

	if (notify->owner == XCB_WINDOW_NONE) {
		if (seat && wm->primary_owner != wm->selection_window)
			polo_primary_set_source(seat_primary(wm, seat), NULL);
		wm->primary_owner = XCB_WINDOW_NONE;
		return;
	}
	wm->primary_owner = notify->owner;
	if (notify->owner == wm->selection_window) {
		wm->primary_timestamp = notify->timestamp;
		return;
	}
	xcb_convert_selection(wm->conn, wm->selection_window, XCB_ATOM_PRIMARY,
			      wm->atom.targets, wm->atom.wl_primary,
			      notify->timestamp);
	xcb_flush(wm->conn);
}

static int
weston_wm_handle_xfixes_selection_notify(struct weston_wm *wm,
				       xcb_generic_event_t *event)
{
	xcb_xfixes_selection_notify_event_t *xfixes_selection_notify =
		(xcb_xfixes_selection_notify_event_t *) event;
	struct weston_compositor *compositor;
	struct weston_seat *seat = weston_wm_pick_seat(wm);
	uint32_t serial;

	if (xfixes_selection_notify->selection == XCB_ATOM_PRIMARY) {
		weston_wm_primary_owner_changed(wm, xfixes_selection_notify);
		return 1;
	}
	if (xfixes_selection_notify->selection != wm->atom.clipboard)
		return 0;

	wm_log("xfixes selection notify event: owner %d\n",
	       xfixes_selection_notify->owner);

	if (xfixes_selection_notify->owner == XCB_WINDOW_NONE) {
		if (!seat)
			return 1;

		if (wm->selection_owner != wm->selection_window) {
			/* A real X client selection went away, not our
			 * proxy selection.  Clear the wayland selection. */
			compositor = wm->server->compositor;
			serial = wl_display_next_serial(compositor->wl_display);
			weston_seat_set_selection(seat, NULL, serial);
		}

		wm->selection_owner = XCB_WINDOW_NONE;

		return 1;
	}

	wm->selection_owner = xfixes_selection_notify->owner;

	/* We have to use XCB_TIME_CURRENT_TIME when we claim the
	 * selection, so grab the actual timestamp here so we can
	 * answer TIMESTAMP conversion requests correctly. */
	if (xfixes_selection_notify->owner == wm->selection_window) {
		wm->selection_timestamp = xfixes_selection_notify->timestamp;
		wm_log("our window, skipping\n");
		return 1;
	}

	wm->incr = 0;
	xcb_convert_selection(wm->conn, wm->selection_window,
			      wm->atom.clipboard,
			      wm->atom.targets,
			      wm->atom.wl_selection,
			      xfixes_selection_notify->timestamp);

	xcb_flush(wm->conn);

	return 1;
}

int
weston_wm_handle_selection_event(struct weston_wm *wm,
				 xcb_generic_event_t *event)
{
	switch (event->response_type & ~0x80) {
	case XCB_SELECTION_NOTIFY:
		weston_wm_handle_selection_notify(wm, event);
		return 1;
	case XCB_PROPERTY_NOTIFY:
		return weston_wm_handle_selection_property_notify(wm, event);
	case XCB_SELECTION_REQUEST:
		weston_wm_handle_selection_request(wm, event);
		return 1;
	}

	switch (event->response_type - wm->xfixes->first_event) {
	case XCB_XFIXES_SELECTION_NOTIFY:
		return weston_wm_handle_xfixes_selection_notify(wm, event);
	}

	return 0;
}

static void
weston_wm_set_selection(struct wl_listener *listener, void *data)
{
	struct weston_seat *seat = data;
	struct weston_wm *wm =
		container_of(listener, struct weston_wm, selection_listener);
	struct weston_data_source *source = seat->selection_data_source;

	if (source == NULL) {
		if (wm->selection_owner == wm->selection_window)
			xcb_set_selection_owner(wm->conn,
						XCB_ATOM_NONE,
						wm->atom.clipboard,
						wm->selection_timestamp);
		return;
	}

	if (source->send == data_source_send)
		return;

	xcb_set_selection_owner(wm->conn,
				wm->selection_window,
				wm->atom.clipboard,
				XCB_TIME_CURRENT_TIME);

	xcb_flush(wm->conn);
}

static void
weston_wm_set_primary(struct wl_listener *listener, void *data)
{
	struct weston_seat *seat = data;
	struct weston_wm *wm =
		container_of(listener, struct weston_wm, primary_listener);
	struct polo_primary *primary = seat_primary(wm, seat);
	struct weston_data_source *source;

	if (!primary || seat != weston_wm_pick_seat(wm))
		return;
	source = polo_primary_get_source(primary);
	if (source == NULL) {
		if (wm->primary_owner == wm->selection_window)
			xcb_set_selection_owner(wm->conn, XCB_ATOM_NONE,
						XCB_ATOM_PRIMARY,
						wm->primary_timestamp);
	} else if (source->send != data_source_send) {
		xcb_set_selection_owner(wm->conn, wm->selection_window,
					XCB_ATOM_PRIMARY,
					XCB_TIME_CURRENT_TIME);
	}
	xcb_flush(wm->conn);
}

static void
maybe_reassign_selection_seat(struct weston_wm *wm)
{
	struct weston_seat *seat;

	/* If we already have a seat, keep it */
	if (!wl_list_empty(&wm->selection_listener.link))
		return;

	seat = weston_wm_pick_seat(wm);
	if (!seat)
		return;

	wl_list_remove(&wm->selection_listener.link);
	wl_list_remove(&wm->seat_destroy_listener.link);

	wl_signal_add(&seat->selection_signal, &wm->selection_listener);
	wl_signal_add(&seat->destroy_signal, &wm->seat_destroy_listener);

	weston_wm_set_selection(&wm->selection_listener, seat);
	weston_wm_set_primary(&wm->primary_listener, seat);
}

static void
weston_wm_seat_created(struct wl_listener *listener, void *data)
{
	struct weston_wm *wm =
		container_of(listener, struct weston_wm, seat_create_listener);

	maybe_reassign_selection_seat(wm);
}

static void
weston_wm_seat_destroyed(struct wl_listener *listener, void *data)
{
	struct weston_wm *wm =
		container_of(listener, struct weston_wm, seat_destroy_listener);

	wl_list_remove(&wm->selection_listener.link);
	wl_list_init(&wm->selection_listener.link);

	wl_list_remove(&wm->seat_destroy_listener.link);
	wl_list_init(&wm->seat_destroy_listener.link);

	/* Try to pick another available seat to fall back to */
	maybe_reassign_selection_seat(wm);
}

/* An X source must not outlive the connection it reads through: Xwayland can
 * crash with a paste still to come. */
void
weston_wm_selection_fini(struct weston_wm *wm)
{
	struct weston_seat *seat = weston_wm_pick_seat(wm);
	struct pending_read *pending, *next;

	wl_list_remove(&wm->selection_listener.link);
	wl_list_init(&wm->selection_listener.link);
	wl_list_remove(&wm->primary_listener.link);
	wl_list_init(&wm->primary_listener.link);
	if (seat_primary(wm, seat) &&
	    polo_primary_get_source(seat_primary(wm, seat)) &&
	    polo_primary_get_source(seat_primary(wm, seat))->send == data_source_send)
		polo_primary_set_source(seat_primary(wm, seat), NULL);
	if (seat && seat->selection_data_source &&
	    seat->selection_data_source->send == data_source_send)
		weston_seat_set_selection(
			seat, NULL,
			wl_display_next_serial(wm->server->compositor->wl_display));
	wl_list_for_each_safe(pending, next, &wm->read_queue, link) {
		close(pending->fd);
		free(pending);
	}
	if (wm->property_source)
		wl_event_source_remove(wm->property_source);
	free(wm->property_reply);
	if (wm->data_source_fd >= 0)
		close(wm->data_source_fd);
	release_source_data(wm);
}

void
weston_wm_selection_init(struct weston_wm *wm)
{
	uint32_t values[1], mask;

	wl_list_init(&wm->selection_listener.link);
	wl_list_init(&wm->seat_create_listener.link);
	wl_list_init(&wm->seat_destroy_listener.link);

	wm->selection_request.requestor = XCB_NONE;
	wl_list_init(&wm->read_queue);
	wl_array_init(&wm->source_data);
	wm->data_source_fd = -1;

	values[0] = XCB_EVENT_MASK_PROPERTY_CHANGE;
	wm->selection_window = xcb_generate_id(wm->conn);
	xcb_create_window(wm->conn,
			  XCB_COPY_FROM_PARENT,
			  wm->selection_window,
			  wm->screen->root,
			  0, 0,
			  10, 10,
			  0,
			  XCB_WINDOW_CLASS_INPUT_OUTPUT,
			  wm->screen->root_visual,
			  XCB_CW_EVENT_MASK, values);

	xcb_set_selection_owner(wm->conn,
				wm->selection_window,
				wm->atom.clipboard_manager,
				XCB_TIME_CURRENT_TIME);

	mask =
		XCB_XFIXES_SELECTION_EVENT_MASK_SET_SELECTION_OWNER |
		XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_WINDOW_DESTROY |
		XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_CLIENT_CLOSE;
	xcb_xfixes_select_selection_input(wm->conn, wm->selection_window,
					  wm->atom.clipboard, mask);
	xcb_xfixes_select_selection_input(wm->conn, wm->selection_window,
					  XCB_ATOM_PRIMARY, mask);

	/* Try to set up a selection listener for any existing seat - we
	 * have a clipboard manager that can copy a subset of available
	 * selections so they don't disappear when the client owning
	 * them quits, but to make this work we need to have a seat
	 * to hang the selection off.
	 *
	 * If we have no seat or lose our seat we need to make sure we
	 * eventually assign a new one, so we listen for seat creation
	 * and destruction.
	 */
	wm->selection_listener.notify = weston_wm_set_selection;
	wm->seat_destroy_listener.notify = weston_wm_seat_destroyed;
	wm->seat_create_listener.notify = weston_wm_seat_created;
	wm->primary_listener.notify = weston_wm_set_primary;
	wl_signal_add(&((PoloBackendServer *)weston_compositor_get_user_data(
				wm->server->compositor))
			       ->primary_selection_signal,
		      &wm->primary_listener);
	wl_signal_add(&wm->server->compositor->seat_created_signal,
		      &wm->seat_create_listener);
	maybe_reassign_selection_seat(wm);
}
