/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cairo-gobject.h>

#include <gdk/gdkwayland.h>
#include <wayland-client.h>

#include "polo-shell-ui-v1-client-protocol.h"
#include "protocol-client.h"

#define COLOR_TABLE_FLOATS (8 * 5 * 4)

struct _PoloShellUiClient {
	GObject parent_instance;

	GdkDisplay *display;
	struct wl_display *wl_display;
	struct polo_shell_ui_v1 *shell_ui;
};

enum {
	SIGNAL_SWITCHER_ENTRY,
	SIGNAL_SWITCHER_THUMBNAIL,
	SIGNAL_SWITCHER_ICON_NAME,
	SIGNAL_SWITCHER_ICON_PIXELS,
	SIGNAL_SHOW_SWITCHER,
	SIGNAL_UPDATE_SELECTED,
	SIGNAL_HIDE_SWITCHER,
	SIGNAL_WORKSPACE_SWITCHER_ENTRY,
	SIGNAL_WORKSPACE_SWITCHER_WINDOW,
	SIGNAL_SHOW_WORKSPACE_SWITCHER,
	SIGNAL_UPDATE_WORKSPACE_SWITCHER,
	SIGNAL_WINDOW_MENU_WORKSPACE,
	SIGNAL_SHOW_WINDOW_MENU,
	SIGNAL_HIDE_WINDOW_MENU,
	SIGNAL_SHOW_OSD,
	SIGNAL_SHOW_DIALOG,
	SIGNAL_HIDE_DIALOG,
	SIGNAL_LOOKUP_COLOR,
	SIGNAL_LOOKUP_ICON,
	N_SIGNALS,
};

static guint signals[N_SIGNALS];

G_DEFINE_FINAL_TYPE(PoloShellUiClient, polo_shell_ui_client, G_TYPE_OBJECT)

G_DEFINE_QUARK(polo - shell - ui - client - error - quark,
	       polo_shell_ui_client_error)

static GdkMonitor *
monitor_for(PoloShellUiClient *self, struct wl_output *output)
{
	int i, n;

	if (!output)
		return NULL;
	n = gdk_display_get_n_monitors(self->display);
	for (i = 0; i < n; i++) {
		GdkMonitor *monitor = gdk_display_get_monitor(self->display, i);

		if (gdk_wayland_monitor_get_wl_output(monitor) == output)
			return monitor;
	}
	return NULL;
}

static void
switcher_entry(void *data, struct polo_shell_ui_v1 *shell_ui, uint32_t id,
	       const char *title, const char *app_id, uint32_t flags)
{
	g_signal_emit(data, signals[SIGNAL_SWITCHER_ENTRY], 0, id, title,
		      app_id, flags);
}

static void
switcher_thumbnail(void *data, struct polo_shell_ui_v1 *shell_ui, uint32_t id,
		   int32_t fd, int32_t width, int32_t height)
{
	size_t size = (size_t)width * height * 4;
	cairo_surface_t *surface;
	void *pixels;

	if (width <= 0 || height <= 0) {
		close(fd);
		return;
	}
	pixels = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (pixels == MAP_FAILED)
		return;
	surface =
		cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
	cairo_surface_flush(surface);
	for (int y = 0; y < height; y++)
		memcpy(cairo_image_surface_get_data(surface) +
			       (size_t)y *
				       cairo_image_surface_get_stride(surface),
		       (const char *)pixels + (size_t)y * width * 4,
		       (size_t)width * 4);
	cairo_surface_mark_dirty(surface);
	munmap(pixels, size);
	g_signal_emit(data, signals[SIGNAL_SWITCHER_THUMBNAIL], 0, id, surface);
	cairo_surface_destroy(surface);
}

static void
switcher_icon_name(void *data, struct polo_shell_ui_v1 *shell_ui, uint32_t id,
		   const char *name)
{
	g_signal_emit(data, signals[SIGNAL_SWITCHER_ICON_NAME], 0, id, name);
}

static void
switcher_icon_pixels(void *data, struct polo_shell_ui_v1 *shell_ui, uint32_t id,
		     int32_t fd, int32_t size)
{
	size_t length = (size_t)size * size * 4;
	cairo_surface_t *surface;
	void *pixels;

	if (size <= 0) {
		close(fd);
		return;
	}
	pixels = mmap(NULL, length, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (pixels == MAP_FAILED)
		return;
	surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
	cairo_surface_flush(surface);
	for (int y = 0; y < size; y++)
		memcpy(cairo_image_surface_get_data(surface) +
			       (size_t)y *
				       cairo_image_surface_get_stride(surface),
		       (const char *)pixels + (size_t)y * size * 4,
		       (size_t)size * 4);
	cairo_surface_mark_dirty(surface);
	munmap(pixels, length);
	g_signal_emit(data, signals[SIGNAL_SWITCHER_ICON_PIXELS], 0, id,
		      surface);
	cairo_surface_destroy(surface);
}

static void
show_switcher(void *data, struct polo_shell_ui_v1 *shell_ui,
	      struct wl_output *output, uint32_t selected, uint32_t columns,
	      uint32_t expand_to_fit, uint32_t icon_size)
{
	g_signal_emit(data, signals[SIGNAL_SHOW_SWITCHER], 0,
		      monitor_for(data, output), selected, columns,
		      expand_to_fit != 0, icon_size);
}

static void
update_selected(void *data, struct polo_shell_ui_v1 *shell_ui,
		uint32_t selected)
{
	g_signal_emit(data, signals[SIGNAL_UPDATE_SELECTED], 0, selected);
}

static void
hide_switcher(void *data, struct polo_shell_ui_v1 *shell_ui)
{
	g_signal_emit(data, signals[SIGNAL_HIDE_SWITCHER], 0);
}

static void
workspace_switcher_entry(void *data, struct polo_shell_ui_v1 *shell_ui,
			 uint32_t id, const char *title, uint32_t blank)
{
	g_signal_emit(data, signals[SIGNAL_WORKSPACE_SWITCHER_ENTRY], 0, id,
		      title, blank != 0);
}

static void
workspace_switcher_window(void *data, struct polo_shell_ui_v1 *shell_ui,
			  uint32_t id, int32_t x, int32_t y, int32_t width,
			  int32_t height, uint32_t focused, const char *app_id)
{
	g_signal_emit(data, signals[SIGNAL_WORKSPACE_SWITCHER_WINDOW], 0, id, x,
		      y, width, height, focused != 0, app_id);
}

static void
show_workspace_switcher(void *data, struct polo_shell_ui_v1 *shell_ui,
			struct wl_output *output, uint32_t selected,
			uint32_t columns, uint32_t active, int32_t screen_width,
			int32_t screen_height, int32_t output_width,
			int32_t output_height, uint32_t icon_size)
{
	g_signal_emit(data, signals[SIGNAL_SHOW_WORKSPACE_SWITCHER], 0,
		      monitor_for(data, output), selected, columns, active,
		      screen_width, screen_height, output_width, output_height,
		      icon_size);
}

static void
update_workspace_switcher(void *data, struct polo_shell_ui_v1 *shell_ui,
			  uint32_t selected, uint32_t active)
{
	g_signal_emit(data, signals[SIGNAL_UPDATE_WORKSPACE_SWITCHER], 0,
		      selected, active);
}

static void
window_menu_workspace(void *data, struct polo_shell_ui_v1 *shell_ui,
		      uint32_t index, const char *name)
{
	g_signal_emit(data, signals[SIGNAL_WINDOW_MENU_WORKSPACE], 0, index,
		      name);
}

static void
show_window_menu(void *data, struct polo_shell_ui_v1 *shell_ui,
		 uint32_t window_id, struct wl_output *output, int32_t x,
		 int32_t y, uint32_t ops, uint32_t insensitive,
		 uint32_t active_workspace, uint32_t n_workspaces,
		 uint32_t time)
{
	g_signal_emit(data, signals[SIGNAL_SHOW_WINDOW_MENU], 0, window_id,
		      monitor_for(data, output), x, y, ops, insensitive,
		      active_workspace, n_workspaces, time);
}

static void
hide_window_menu(void *data, struct polo_shell_ui_v1 *shell_ui,
		 uint32_t window_id)
{
	g_signal_emit(data, signals[SIGNAL_HIDE_WINDOW_MENU], 0, window_id);
}

static void
show_osd(void *data, struct polo_shell_ui_v1 *shell_ui, uint32_t kind,
	 struct wl_output *output, const char *text, int32_t value)
{
	g_signal_emit(data, signals[SIGNAL_SHOW_OSD], 0, kind,
		      monitor_for(data, output), text, value);
}

static void
show_dialog(void *data, struct polo_shell_ui_v1 *shell_ui, uint32_t dialog_id,
	    uint32_t kind, struct wl_output *output, const char *payload)
{
	g_signal_emit(data, signals[SIGNAL_SHOW_DIALOG], 0, dialog_id, kind,
		      monitor_for(data, output), payload);
}

static void
hide_dialog(void *data, struct polo_shell_ui_v1 *shell_ui, uint32_t dialog_id)
{
	g_signal_emit(data, signals[SIGNAL_HIDE_DIALOG], 0, dialog_id);
}

static void
lookup_color(void *data, struct polo_shell_ui_v1 *shell_ui, const char *name)
{
	g_signal_emit(data, signals[SIGNAL_LOOKUP_COLOR], 0, name);
}

static void
lookup_icon(void *data, struct polo_shell_ui_v1 *shell_ui, uint32_t serial,
	    const char *name, const char *desktop_id, int32_t mini_size,
	    int32_t size, int32_t scale, uint32_t fallback)
{
	g_signal_emit(data, signals[SIGNAL_LOOKUP_ICON], 0, serial, name,
		      desktop_id, mini_size, size, scale, fallback != 0);
}

static const struct polo_shell_ui_v1_listener shell_ui_listener = {
	.switcher_entry = switcher_entry,
	.switcher_icon_name = switcher_icon_name,
	.switcher_icon_pixels = switcher_icon_pixels,
	.switcher_thumbnail = switcher_thumbnail,
	.show_switcher = show_switcher,
	.update_selected = update_selected,
	.hide_switcher = hide_switcher,
	.workspace_switcher_entry = workspace_switcher_entry,
	.workspace_switcher_window = workspace_switcher_window,
	.show_workspace_switcher = show_workspace_switcher,
	.update_workspace_switcher = update_workspace_switcher,
	.window_menu_workspace = window_menu_workspace,
	.show_window_menu = show_window_menu,
	.hide_window_menu = hide_window_menu,
	.show_osd = show_osd,
	.show_dialog = show_dialog,
	.hide_dialog = hide_dialog,
	.lookup_color = lookup_color,
	.lookup_icon = lookup_icon,
};

struct registry_scan {
	struct wl_registry *registry;
	struct polo_shell_ui_v1 *shell_ui;
};

static void
registry_global(void *data, struct wl_registry *registry, uint32_t name,
		const char *interface, uint32_t version)
{
	struct registry_scan *scan = data;

	if (strcmp(interface, polo_shell_ui_v1_interface.name) == 0 &&
	    !scan->shell_ui)
		scan->shell_ui = wl_registry_bind(
			registry, name, &polo_shell_ui_v1_interface, 1);
}

static void
registry_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = {
	registry_global,
	registry_global_remove,
};

PoloShellUiClient *
polo_shell_ui_client_new(GdkDisplay *display, GError **error)
{
	PoloShellUiClient *self;
	struct wl_display *wl_display;
	struct wl_event_queue *queue;
	struct wl_display *wrapper;
	struct registry_scan scan = { 0 };

	if (!GDK_IS_WAYLAND_DISPLAY(display)) {
		g_set_error(error, POLO_SHELL_UI_CLIENT_ERROR,
			    POLO_SHELL_UI_CLIENT_ERROR_NOT_WAYLAND,
			    "polo-shell-ui needs a Wayland display");
		return NULL;
	}
	wl_display = gdk_wayland_display_get_wl_display(display);

	queue = wl_display_create_queue(wl_display);
	wrapper = wl_proxy_create_wrapper(wl_display);
	wl_proxy_set_queue((struct wl_proxy *)wrapper, queue);
	scan.registry = wl_display_get_registry(wrapper);
	wl_proxy_wrapper_destroy(wrapper);
	wl_registry_add_listener(scan.registry, &registry_listener, &scan);
	wl_display_roundtrip_queue(wl_display, queue);
	wl_registry_destroy(scan.registry);

	if (!scan.shell_ui) {
		wl_event_queue_destroy(queue);
		g_set_error(error, POLO_SHELL_UI_CLIENT_ERROR,
			    POLO_SHELL_UI_CLIENT_ERROR_NO_GLOBAL,
			    "the compositor offers no polo_shell_ui_v1");
		return NULL;
	}
	wl_proxy_set_queue((struct wl_proxy *)scan.shell_ui, NULL);
	wl_event_queue_destroy(queue);

	self = g_object_new(POLO_SHELL_UI_TYPE_CLIENT, NULL);
	self->display = g_object_ref(display);
	self->wl_display = wl_display;
	self->shell_ui = scan.shell_ui;
	polo_shell_ui_v1_add_listener(self->shell_ui, &shell_ui_listener, self);
	return self;
}

void
polo_shell_ui_client_switcher_select(PoloShellUiClient *self, guint id)
{
	g_return_if_fail(POLO_SHELL_UI_IS_CLIENT(self));
	polo_shell_ui_v1_switcher_select(self->shell_ui, id);
	wl_display_flush(self->wl_display);
}

void
polo_shell_ui_client_menu_action(PoloShellUiClient *self, guint window_id,
				 guint op, int workspace)
{
	g_return_if_fail(POLO_SHELL_UI_IS_CLIENT(self));
	polo_shell_ui_v1_menu_action(self->shell_ui, window_id, op, workspace);
	wl_display_flush(self->wl_display);
}

void
polo_shell_ui_client_dialog_surface(PoloShellUiClient *self, guint dialog_id,
				    GdkWindow *window)
{
	g_return_if_fail(POLO_SHELL_UI_IS_CLIENT(self));
	g_return_if_fail(GDK_IS_WAYLAND_WINDOW(window));
	polo_shell_ui_v1_dialog_surface(
		self->shell_ui, dialog_id,
		gdk_wayland_window_get_wl_surface(window));
	wl_display_flush(self->wl_display);
}

void
polo_shell_ui_client_dialog_response(PoloShellUiClient *self, guint dialog_id,
				     int response, const char *text)
{
	g_return_if_fail(POLO_SHELL_UI_IS_CLIENT(self));
	polo_shell_ui_v1_dialog_response(self->shell_ui, dialog_id, response,
					 text);
	wl_display_flush(self->wl_display);
}

void
polo_shell_ui_client_custom_color(PoloShellUiClient *self, const char *name,
				  const float *rgba)
{
	struct wl_array array;

	g_return_if_fail(POLO_SHELL_UI_IS_CLIENT(self));
	wl_array_init(&array);
	if (rgba)
		memcpy(wl_array_add(&array, 4 * sizeof(float)), rgba,
		       4 * sizeof(float));
	polo_shell_ui_v1_custom_color(self->shell_ui, name, &array);
	wl_array_release(&array);
	wl_display_flush(self->wl_display);
}

static gboolean
copy_surface(guint8 *to, cairo_surface_t *surface)
{
	int width = cairo_image_surface_get_width(surface);
	int height = cairo_image_surface_get_height(surface);
	int stride = cairo_image_surface_get_stride(surface);
	const guint8 *from;

	cairo_surface_flush(surface);
	from = cairo_image_surface_get_data(surface);
	if (!from ||
	    cairo_image_surface_get_format(surface) != CAIRO_FORMAT_ARGB32)
		return FALSE;
	for (int y = 0; y < height; y++)
		memcpy(to + (gsize)y * width * 4, from + (gsize)y * stride,
		       (gsize)width * 4);
	return TRUE;
}

void
polo_shell_ui_client_icon(PoloShellUiClient *self, guint serial,
			  cairo_surface_t *mini_icon, cairo_surface_t *icon)
{
	int mini_width = cairo_image_surface_get_width(mini_icon);
	int mini_height = cairo_image_surface_get_height(mini_icon);
	int width = cairo_image_surface_get_width(icon);
	int height = cairo_image_surface_get_height(icon);
	gsize mini_size = (gsize)mini_width * mini_height * 4;
	gsize size = mini_size + (gsize)width * height * 4;
	guint8 *pixels;
	int fd;

	g_return_if_fail(POLO_SHELL_UI_IS_CLIENT(self));
	fd = memfd_create("polo-icon", MFD_CLOEXEC);
	if (fd < 0 || ftruncate(fd, (off_t)size) != 0) {
		if (fd >= 0)
			close(fd);
		polo_shell_ui_client_no_icon(self, serial);
		return;
	}
	pixels = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (pixels == MAP_FAILED || !copy_surface(pixels, mini_icon) ||
	    !copy_surface(pixels + mini_size, icon)) {
		if (pixels != MAP_FAILED)
			munmap(pixels, size);
		close(fd);
		polo_shell_ui_client_no_icon(self, serial);
		return;
	}
	munmap(pixels, size);
	polo_shell_ui_v1_icon(self->shell_ui, serial, fd, mini_width,
			      mini_height, width, height);
	close(fd);
	wl_display_flush(self->wl_display);
}

void
polo_shell_ui_client_no_icon(PoloShellUiClient *self, guint serial)
{
	g_return_if_fail(POLO_SHELL_UI_IS_CLIENT(self));
	polo_shell_ui_v1_no_icon(self->shell_ui, serial);
	wl_display_flush(self->wl_display);
}

void
polo_shell_ui_client_icon_theme_changed(PoloShellUiClient *self)
{
	g_return_if_fail(POLO_SHELL_UI_IS_CLIENT(self));
	polo_shell_ui_v1_icon_theme_changed(self->shell_ui);
	wl_display_flush(self->wl_display);
}

void
polo_shell_ui_client_color_table(PoloShellUiClient *self, const float *colors,
				 int n_colors)
{
	struct wl_array array;

	g_return_if_fail(POLO_SHELL_UI_IS_CLIENT(self));
	g_return_if_fail(n_colors == COLOR_TABLE_FLOATS);
	wl_array_init(&array);
	memcpy(wl_array_add(&array, n_colors * sizeof(float)), colors,
	       n_colors * sizeof(float));
	polo_shell_ui_v1_color_table(self->shell_ui, &array);
	wl_array_release(&array);
	wl_display_flush(self->wl_display);
}

static void
polo_shell_ui_client_finalize(GObject *object)
{
	PoloShellUiClient *self = POLO_SHELL_UI_CLIENT(object);

	polo_shell_ui_v1_destroy(self->shell_ui);
	wl_display_flush(self->wl_display);
	g_object_unref(self->display);
	G_OBJECT_CLASS(polo_shell_ui_client_parent_class)->finalize(object);
}

static void
polo_shell_ui_client_class_init(PoloShellUiClientClass *klass)
{
	GType type = G_TYPE_FROM_CLASS(klass);

	G_OBJECT_CLASS(klass)->finalize = polo_shell_ui_client_finalize;

	signals[SIGNAL_SWITCHER_ENTRY] =
		g_signal_new("switcher-entry", type, G_SIGNAL_RUN_LAST, 0, NULL,
			     NULL, NULL, G_TYPE_NONE, 4, G_TYPE_UINT,
			     G_TYPE_STRING, G_TYPE_STRING, G_TYPE_UINT);
	signals[SIGNAL_SWITCHER_THUMBNAIL] = g_signal_new(
		"switcher-thumbnail", type, G_SIGNAL_RUN_LAST, 0, NULL, NULL,
		NULL, G_TYPE_NONE, 2, G_TYPE_UINT, CAIRO_GOBJECT_TYPE_SURFACE);
	signals[SIGNAL_SWITCHER_ICON_NAME] = g_signal_new(
		"switcher-icon-name", type, G_SIGNAL_RUN_LAST, 0, NULL, NULL,
		NULL, G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_STRING);
	signals[SIGNAL_SWITCHER_ICON_PIXELS] = g_signal_new(
		"switcher-icon-pixels", type, G_SIGNAL_RUN_LAST, 0, NULL, NULL,
		NULL, G_TYPE_NONE, 2, G_TYPE_UINT, CAIRO_GOBJECT_TYPE_SURFACE);
	signals[SIGNAL_SHOW_SWITCHER] = g_signal_new(
		"show-switcher", type, G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
		G_TYPE_NONE, 5, GDK_TYPE_MONITOR, G_TYPE_UINT, G_TYPE_UINT,
		G_TYPE_BOOLEAN, G_TYPE_UINT);
	signals[SIGNAL_UPDATE_SELECTED] =
		g_signal_new("update-selected", type, G_SIGNAL_RUN_LAST, 0,
			     NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_UINT);
	signals[SIGNAL_HIDE_SWITCHER] =
		g_signal_new("hide-switcher", type, G_SIGNAL_RUN_LAST, 0, NULL,
			     NULL, NULL, G_TYPE_NONE, 0);
	signals[SIGNAL_WORKSPACE_SWITCHER_ENTRY] = g_signal_new(
		"workspace-switcher-entry", type, G_SIGNAL_RUN_LAST, 0, NULL,
		NULL, NULL, G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_STRING,
		G_TYPE_BOOLEAN);
	signals[SIGNAL_WORKSPACE_SWITCHER_WINDOW] = g_signal_new(
		"workspace-switcher-window", type, G_SIGNAL_RUN_LAST, 0, NULL,
		NULL, NULL, G_TYPE_NONE, 7, G_TYPE_UINT, G_TYPE_INT, G_TYPE_INT,
		G_TYPE_INT, G_TYPE_INT, G_TYPE_BOOLEAN, G_TYPE_STRING);
	signals[SIGNAL_SHOW_WORKSPACE_SWITCHER] = g_signal_new(
		"show-workspace-switcher", type, G_SIGNAL_RUN_LAST, 0, NULL,
		NULL, NULL, G_TYPE_NONE, 9, GDK_TYPE_MONITOR, G_TYPE_UINT,
		G_TYPE_UINT, G_TYPE_UINT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT,
		G_TYPE_INT, G_TYPE_UINT);
	signals[SIGNAL_UPDATE_WORKSPACE_SWITCHER] = g_signal_new(
		"update-workspace-switcher", type, G_SIGNAL_RUN_LAST, 0, NULL,
		NULL, NULL, G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);
	signals[SIGNAL_WINDOW_MENU_WORKSPACE] = g_signal_new(
		"window-menu-workspace", type, G_SIGNAL_RUN_LAST, 0, NULL, NULL,
		NULL, G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_STRING);
	signals[SIGNAL_SHOW_WINDOW_MENU] = g_signal_new(
		"show-window-menu", type, G_SIGNAL_RUN_LAST, 0, NULL, NULL,
		NULL, G_TYPE_NONE, 9, G_TYPE_UINT, GDK_TYPE_MONITOR, G_TYPE_INT,
		G_TYPE_INT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT,
		G_TYPE_UINT);
	signals[SIGNAL_HIDE_WINDOW_MENU] =
		g_signal_new("hide-window-menu", type, G_SIGNAL_RUN_LAST, 0,
			     NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_UINT);
	signals[SIGNAL_SHOW_OSD] =
		g_signal_new("show-osd", type, G_SIGNAL_RUN_LAST, 0, NULL, NULL,
			     NULL, G_TYPE_NONE, 4, G_TYPE_UINT,
			     GDK_TYPE_MONITOR, G_TYPE_STRING, G_TYPE_INT);
	signals[SIGNAL_SHOW_DIALOG] =
		g_signal_new("show-dialog", type, G_SIGNAL_RUN_LAST, 0, NULL,
			     NULL, NULL, G_TYPE_NONE, 4, G_TYPE_UINT,
			     G_TYPE_UINT, GDK_TYPE_MONITOR, G_TYPE_STRING);
	signals[SIGNAL_HIDE_DIALOG] =
		g_signal_new("hide-dialog", type, G_SIGNAL_RUN_LAST, 0, NULL,
			     NULL, NULL, G_TYPE_NONE, 1, G_TYPE_UINT);
	signals[SIGNAL_LOOKUP_COLOR] =
		g_signal_new("lookup-color", type, G_SIGNAL_RUN_LAST, 0, NULL,
			     NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);
	signals[SIGNAL_LOOKUP_ICON] = g_signal_new(
		"lookup-icon", type, G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
		G_TYPE_NONE, 7, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING,
		G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_BOOLEAN);
}

static void
polo_shell_ui_client_init(PoloShellUiClient *self)
{
}
