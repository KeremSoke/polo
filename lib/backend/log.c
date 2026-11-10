/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Polo's log, built on libweston's log framework so that Polo scopes and
 * libweston scopes ("drm-backend", "gl-renderer", ...) share one context,
 * one sink and one --debug-scopes switch.
 *
 * Every message goes through the "log" scope (always subscribed), except
 * debug messages, which go through their own scope and are printed only when
 * that scope is subscribed via --debug-scopes.
 */

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <libweston/libweston.h>
#include <libweston/weston-log.h>

#include "polo-backend-private.h"

struct _PoloLogScope {
	char *name;
	struct weston_log_scope *scope;
};

static const struct {
	const char *name;
	const char *description;
} polo_scopes[] = {
	{ "polo", "Polo core\n" },
	{ "polo-policy", "Window-management policy decisions\n" },
	{ "polo-input", "Bindings, grabs and input-device configuration\n" },
	{ "polo-xwm", "XWayland window manager\n" },
	{ "polo-session", "Session management\n" },
	{ "polo-effects", "Compositing effects\n" },
	{ "polo-protocol", "Polo-implemented Wayland protocols\n" },
};

static struct {
	struct weston_log_context *ctx;
	struct weston_log_subscriber *subscriber;
	struct weston_log_scope *log;
	FILE *file;
	GHashTable *scopes;
	int cached_tm_mday;
} polo_log;

static int
weston_vlog(const char *fmt, va_list ap)
{
	char timestr[128];
	int len;

	len = weston_log_scope_printf(
		polo_log.log, "%s ",
		weston_log_timestamp(timestr, sizeof(timestr),
				     &polo_log.cached_tm_mday));
	return len + weston_log_scope_vprintf(polo_log.log, fmt, ap);
}

static int
weston_vlog_continue(const char *fmt, va_list ap)
{
	return weston_log_scope_vprintf(polo_log.log, fmt, ap);
}

static void
scope_free(gpointer data)
{
	PoloLogScope *scope = data;

	weston_log_scope_destroy(scope->scope);
	g_free(scope->name);
	g_free(scope);
}

static PoloLogScope *
scope_create(const char *name, const char *description)
{
	PoloLogScope *scope = g_new0(PoloLogScope, 1);

	scope->name = g_strdup(name);
	scope->scope = weston_log_ctx_add_log_scope(
		polo_log.ctx, name, description, NULL, NULL, NULL);
	g_hash_table_insert(polo_log.scopes, scope->name, scope);

	return scope;
}

gboolean
polo_backend_log_init(const char *log_file, const char *debug_scopes,
		      GError **error)
{
	FILE *file = stderr;
	gchar **names;
	size_t i;

	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (polo_log.ctx)
		polo_backend_log_shutdown();

	if (log_file) {
		file = fopen(log_file, "ae");
		if (!file) {
			int err = errno;

			g_set_error(error, G_FILE_ERROR,
				    g_file_error_from_errno(err),
				    "Cannot open log file %s: %s", log_file,
				    g_strerror(err));
			return FALSE;
		}
		setvbuf(file, NULL, _IOLBF, 256);
	}

	polo_log.file = file;
	polo_log.cached_tm_mday = -1;
	polo_log.ctx = weston_log_ctx_create();
	polo_log.scopes = g_hash_table_new_full(g_str_hash, g_str_equal, NULL,
						scope_free);
	polo_log.log = weston_log_ctx_add_log_scope(polo_log.ctx, "log",
						    "Polo and libweston log\n",
						    NULL, NULL, NULL);
	weston_log_set_handler(weston_vlog, weston_vlog_continue);

	for (i = 0; i < G_N_ELEMENTS(polo_scopes); i++)
		scope_create(polo_scopes[i].name, polo_scopes[i].description);

	polo_log.subscriber = weston_log_subscriber_create_log(file);
	weston_log_subscribe(polo_log.ctx, polo_log.subscriber, "log");

	/* Unknown names stay pending until the scope is created, which is
	 * how libweston's own scopes become subscribable from the CLI. */
	names = g_strsplit(debug_scopes ? debug_scopes : "", ",", -1);
	for (i = 0; names[i]; i++) {
		g_strstrip(names[i]);
		if (names[i][0] != '\0' && strcmp(names[i], "log") != 0)
			weston_log_subscribe(polo_log.ctx, polo_log.subscriber,
					     names[i]);
	}
	g_strfreev(names);

	return TRUE;
}

int
polo_backend_log_fd(void)
{
	return polo_log.file ? fileno(polo_log.file) : STDERR_FILENO;
}

void
polo_backend_log_shutdown(void)
{
	if (!polo_log.ctx)
		return;

	g_clear_pointer(&polo_log.scopes, g_hash_table_destroy);
	weston_log_scope_destroy(polo_log.log);
	polo_log.log = NULL;
	weston_log_subscriber_destroy(polo_log.subscriber);
	polo_log.subscriber = NULL;
	weston_log_ctx_destroy(polo_log.ctx);
	polo_log.ctx = NULL;

	if (polo_log.file && polo_log.file != stderr)
		fclose(polo_log.file);
	polo_log.file = NULL;
}

static void
ensure_init(void)
{
	if (!polo_log.ctx)
		polo_backend_log_init(NULL, NULL, NULL);
}

struct weston_log_context *
polo_backend_log_get_context(void)
{
	ensure_init();
	return polo_log.ctx;
}

PoloLogScope *
polo_backend_log_scope(const char *name)
{
	PoloLogScope *scope;

	g_return_val_if_fail(name != NULL, NULL);
	ensure_init();

	scope = g_hash_table_lookup(polo_log.scopes, name);
	if (!scope)
		scope = scope_create(name, "Polo\n");

	return scope;
}

const char *
polo_log_scope_get_name(PoloLogScope *scope)
{
	return scope->name;
}

gboolean
polo_log_scope_is_enabled(PoloLogScope *scope)
{
	return weston_log_scope_is_enabled(scope->scope);
}

static void
scope_vprint(struct weston_log_scope *target, const char *name,
	     const char *level, const char *format, va_list ap)
{
	char timestr[128];
	char *msg = g_strdup_vprintf(format, ap);
	size_t len = strlen(msg);

	weston_log_scope_printf(target, "%s %s%s: %s%s",
				weston_log_timestamp(timestr, sizeof(timestr),
						     &polo_log.cached_tm_mday),
				level, name, msg,
				len > 0 && msg[len - 1] == '\n' ? "" : "\n");
	g_free(msg);
}

void
polo_log_scope_debug(PoloLogScope *scope, const char *format, ...)
{
	va_list ap;

	if (!weston_log_scope_is_enabled(scope->scope))
		return;

	va_start(ap, format);
	scope_vprint(scope->scope, scope->name, "", format, ap);
	va_end(ap);
}

void
polo_log_scope_info(PoloLogScope *scope, const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	scope_vprint(polo_log.log, scope->name, "", format, ap);
	va_end(ap);
}

void
polo_log_scope_warning(PoloLogScope *scope, const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	scope_vprint(polo_log.log, scope->name, "WARNING ", format, ap);
	va_end(ap);
}
