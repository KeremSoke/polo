/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * libFuzzer: random requests from a client to the protocols Polo implements,
 * through a socketpair, into a headless compositor that stays up between
 * inputs.
 *
 * POLO_FUZZ_PROTOCOLS: the interfaces the client binds, comma-separated.
 *
 * What every client has, in object ids: 1 wl_display, 2 wl_registry,
 * 3 wl_compositor, 4 wl_seat, 5 wl_output, 6 xdg_wm_base, 7 wl_shm, then the
 * interfaces of POLO_FUZZ_PROTOCOLS from 8, then a wl_surface. Four memfds ride
 * along for requests that take a file descriptor.
 *
 * The input is records: object id (low 6 bits), opcode, payload length,
 * payload. Each record becomes one wire message, so the framing is always valid
 * and the bytes inside it are what changes.
 */

#include <errno.h>
#include <stdarg.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>

#include <wayland-server-core.h>

#include "polo-backend-private.h"

#define MAX_INPUT 16384
#define MAX_WIRE (MAX_INPUT * 2 + 4096)
#define MAX_GLOBALS 256
#define MAX_TARGETS 8
#define SPARE_FDS 4

static void
quiet(const char *format, va_list args)
{
}

static char runtime_dir[160];

static void
remove_runtime_dir(void)
{
	char path[200];

	snprintf(path, sizeof path, "%s/polo-fuzz", runtime_dir);
	unlink(path);
	snprintf(path, sizeof path, "%s/polo-fuzz.lock", runtime_dir);
	unlink(path);
	rmdir(runtime_dir);
}

static const char *const FIXED[] = { "wl_compositor", "wl_seat", "wl_output",
				     "xdg_wm_base", "wl_shm" };

static PoloBackendServer *server;
static struct {
	uint32_t name;
	uint32_t version;
	char interface[96];
} globals[MAX_GLOBALS];
static int n_globals;
static char *targets[MAX_TARGETS];
static int n_targets;
static int spare_fds[SPARE_FDS];

struct wire {
	uint8_t data[MAX_WIRE];
	size_t len;
};

static void
put32(struct wire *w, uint32_t value)
{
	memcpy(w->data + w->len, &value, 4);
	w->len += 4;
}

static size_t
begin(struct wire *w, uint32_t object, uint32_t opcode)
{
	size_t at = w->len;

	put32(w, object);
	put32(w, opcode);
	return at;
}

static void
finish(struct wire *w, size_t at)
{
	uint32_t word = (uint32_t)(w->len - at) << 16;
	uint32_t opcode;

	memcpy(&opcode, w->data + at + 4, 4);
	word |= opcode & 0xffff;
	memcpy(w->data + at + 4, &word, 4);
}

static void
put_string(struct wire *w, const char *text)
{
	uint32_t length = (uint32_t)strlen(text) + 1;
	size_t padded = (length + 3) & ~3u;

	put32(w, length);
	memset(w->data + w->len, 0, padded);
	memcpy(w->data + w->len, text, length);
	w->len += padded;
}

static const char *
global_named(const char *interface, uint32_t *name, uint32_t *version)
{
	for (int i = 0; i < n_globals; i++) {
		if (strcmp(globals[i].interface, interface) == 0) {
			*name = globals[i].name;
			*version = globals[i].version;
			return interface;
		}
	}
	return NULL;
}

static void
bind_global(struct wire *w, const char *interface, uint32_t id)
{
	uint32_t name = 0, version = 0;
	size_t at;

	if (!global_named(interface, &name, &version)) {
		fprintf(stderr, "fuzz: the compositor has no %s\n", interface);
		exit(2);
	}
	at = begin(w, 2, 0);
	put32(w, name);
	put_string(w, interface);
	put32(w, version);
	put32(w, id);
	finish(w, at);
}

static void
pump(void)
{
	for (int round = 0; round < 8; round++) {
		int dispatched = 0;

		while (g_main_context_iteration(NULL, FALSE) && dispatched < 64)
			dispatched++;
		wl_display_flush_clients(server->display);
	}
}

static void
drain(int fd)
{
	uint8_t scratch[4096];
	struct msghdr header = { 0 };
	struct iovec chunk = { scratch, sizeof scratch };
	char control[CMSG_SPACE(sizeof(int) * 28)];

	header.msg_iov = &chunk;
	header.msg_iovlen = 1;
	header.msg_control = control;
	header.msg_controllen = sizeof control;
	while (recvmsg(fd, &header, MSG_DONTWAIT | MSG_CMSG_CLOEXEC) > 0) {
		struct cmsghdr *c;

		for (c = CMSG_FIRSTHDR(&header); c;
		     c = CMSG_NXTHDR(&header, c)) {
			if (c->cmsg_level == SOL_SOCKET &&
			    c->cmsg_type == SCM_RIGHTS) {
				size_t count = (c->cmsg_len - CMSG_LEN(0)) /
					sizeof(int);
				int *received = (int *)CMSG_DATA(c);

				for (size_t i = 0; i < count; i++)
					close(received[i]);
			}
		}
		header.msg_controllen = sizeof control;
	}
}

static void
discover(void)
{
	struct wire w = { .len = 0 };
	int pair[2];
	uint8_t buffer[16384];
	ssize_t got;
	size_t off = 0;
	size_t at;

	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair) != 0)
		exit(2);
	wl_client_create(server->display, pair[0]);
	at = begin(&w, 1, 1);
	put32(&w, 2);
	finish(&w, at);
	if (write(pair[1], w.data, w.len) != (ssize_t)w.len)
		exit(2);
	pump();
	got = recv(pair[1], buffer, sizeof buffer, MSG_DONTWAIT);
	while (got > 0 && off + 8 <= (size_t)got) {
		uint32_t object, word, size, opcode;

		memcpy(&object, buffer + off, 4);
		memcpy(&word, buffer + off + 4, 4);
		size = word >> 16;
		opcode = word & 0xffff;
		if (size < 8 || off + size > (size_t)got)
			break;
		if (object == 2 && opcode == 0 && n_globals < MAX_GLOBALS) {
			uint32_t length;
			const uint8_t *p = buffer + off + 8;

			memcpy(&globals[n_globals].name, p, 4);
			memcpy(&length, p + 4, 4);
			if (length > 0 &&
			    length < sizeof globals[0].interface) {
				memcpy(globals[n_globals].interface, p + 8,
				       length);
				globals[n_globals].interface[length - 1] = '\0';
				memcpy(&globals[n_globals].version,
				       p + 8 + ((length + 3) & ~3u), 4);
				n_globals++;
			}
		}
		off += size;
	}
	close(pair[1]);
	pump();
}

int
LLVMFuzzerInitialize(int *argc, char ***argv)
{
	const char *base = g_get_tmp_dir();
	char *runtime;
	const char *list = getenv("POLO_FUZZ_PROTOCOLS");
	PoloBackendConfig config = { 0 };
	GError *error = NULL;
	char *copy, *token, *save = NULL;

	wl_log_set_handler_server(quiet);
	if (strlen(base) > 60)
		base = "/tmp";
	runtime = g_build_filename(base, "polo-fuzz-XXXXXX", NULL);
	if (!mkdtemp(runtime))
		exit(2);
	setenv("XDG_RUNTIME_DIR", runtime, 1);
	snprintf(runtime_dir, sizeof runtime_dir, "%s", runtime);
	g_free(runtime);
	atexit(remove_runtime_dir);
	setenv("GSETTINGS_BACKEND", "memory", 1);
	if (!polo_backend_log_init("/dev/null", NULL, &error)) {
		fprintf(stderr, "fuzz: %s\n", error->message);
		exit(2);
	}

	copy = g_strdup(list ? list : "");
	for (token = strtok_r(copy, ",", &save);
	     token && n_targets < MAX_TARGETS;
	     token = strtok_r(NULL, ",", &save))
		targets[n_targets++] = g_strdup(token);
	g_free(copy);

	for (int i = 0; i < SPARE_FDS; i++) {
		spare_fds[i] = memfd_create("polo-fuzz", MFD_CLOEXEC);
		if (spare_fds[i] < 0 || ftruncate(spare_fds[i], 4096) != 0)
			exit(2);
	}

	server = polo_backend_server_new();
	config.kind = POLO_BACKEND_KIND_HEADLESS;
	config.renderer = POLO_BACKEND_RENDERER_PIXMAN;
	config.width = 640;
	config.height = 480;
	config.scale = 1;
	config.output_count = 1;
	config.socket_name = "polo-fuzz";
	if (!polo_backend_server_start(server, &config, &error)) {
		fprintf(stderr, "fuzz: cannot start the compositor: %s\n",
			error->message);
		exit(2);
	}
	polo_backend_server_attach(server, NULL);
	discover();
	return 0;
}

static void
send_all(int fd, const uint8_t *data, size_t length, const int *fds, int n_fds)
{
	struct iovec chunk = { (void *)data, length };
	struct msghdr header = { 0 };
	char control[CMSG_SPACE(sizeof(int) * SPARE_FDS)] = { 0 };
	size_t done = 0;

	header.msg_iov = &chunk;
	header.msg_iovlen = 1;
	if (n_fds > 0) {
		struct cmsghdr *c;

		header.msg_control = control;
		header.msg_controllen = CMSG_SPACE(sizeof(int) * n_fds);
		c = CMSG_FIRSTHDR(&header);
		c->cmsg_level = SOL_SOCKET;
		c->cmsg_type = SCM_RIGHTS;
		c->cmsg_len = CMSG_LEN(sizeof(int) * n_fds);
		memcpy(CMSG_DATA(c), fds, sizeof(int) * n_fds);
	}
	while (done < length) {
		ssize_t n = sendmsg(fd, &header, MSG_DONTWAIT | MSG_NOSIGNAL);

		if (n <= 0) {
			if (errno == EINTR)
				continue;
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				pump();
				drain(fd);
				continue;
			}
			return;
		}
		done += (size_t)n;
		chunk.iov_base = (uint8_t *)chunk.iov_base + n;
		chunk.iov_len -= (size_t)n;
		header.msg_control = NULL;
		header.msg_controllen = 0;
	}
}

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	static struct wire w;
	int pair[2];
	size_t off = 0, at;
	uint32_t next = 8;

	if (size > MAX_INPUT)
		return 0;
	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair) != 0)
		return 0;
	wl_client_create(server->display, pair[0]);

	w.len = 0;
	at = begin(&w, 1, 1);
	put32(&w, 2);
	finish(&w, at);
	for (size_t i = 0; i < G_N_ELEMENTS(FIXED); i++)
		bind_global(&w, FIXED[i], 3 + (uint32_t)i);
	next = 3 + G_N_ELEMENTS(FIXED);
	for (int i = 0; i < n_targets; i++)
		bind_global(&w, targets[i], next++);
	at = begin(&w, 3, 0);
	put32(&w, next++);
	finish(&w, at);

	while (off + 3 <= size) {
		uint32_t object = data[off] & 0x3f;
		uint32_t opcode = data[off + 1];
		size_t length = data[off + 2];
		size_t padded;

		off += 3;
		if (length > size - off)
			length = size - off;
		padded = (length + 3) & ~3u;
		if (object == 0)
			object = 1;
		at = begin(&w, object, opcode);
		memset(w.data + w.len, 0, padded);
		memcpy(w.data + w.len, data + off, length);
		w.len += padded;
		finish(&w, at);
		off += length;
	}

	send_all(pair[1], w.data, w.len, spare_fds, SPARE_FDS);
	pump();
	drain(pair[1]);
	close(pair[1]);
	pump();
	return 0;
}
