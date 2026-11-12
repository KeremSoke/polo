/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cairo-gobject.h>
#include <gobject/gvaluecollector.h>

#include <gdk/gdkwayland.h>
#include <gtk/gtk.h>
#include <wayland-client.h>

#include "polo-shell-ui-v1-client-protocol.h"
#include "protocol-client.h"

#define COLOR_TABLE_FLOATS (8 * 5 * 4)

struct _PoloShellUiClient {
	GObject parent_instance;

	GdkDisplay *display;
	struct wl_display *wl_display;
	struct polo_shell_ui_v1 *shell_ui;
	gboolean emitting;
	GQueue held;
	guint drain_id;
};

enum {
	SIGNAL_SWITCHER_ENTRY,
	SIGNAL_SWITCHER_THUMBNAIL,
	SIGNAL_SWITCHER_ICON_NAME,
	SIGNAL_SWITCHER_ICON_PIXELS,
	SIGNAL_SHOW_SWITCHER,
	SIGNAL_UPDATE_SELECTED,
	SIGNAL_HIDE_SWITCHER,
	SIGNAL_SWITCHER_HEADING,
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

struct held_event {
	guint signal_id;
	gboolean broken;
	guint n_values;
	GValue values[];
};

static void
deliver(PoloShellUiClient *self, struct held_event *event)
{
	self->emitting = TRUE;
	if (!event->broken)
		g_signal_emitv(event->values, event->signal_id, 0, NULL);
	self->emitting = FALSE;
	for (guint i = 0; i < event->n_values; i++)
		g_value_unset(&event->values[i]);
	g_free(event);
}

static gboolean
drain(gpointer data)
{
	PoloShellUiClient *self = data;

	if (!self->emitting)
		deliver(self, g_queue_pop_head(&self->held));
	if (!g_queue_is_empty(&self->held))
		return G_SOURCE_CONTINUE;
	self->drain_id = 0;
	return G_SOURCE_REMOVE;
}

static void
emit(PoloShellUiClient *self, guint signal_id, ...)
{
	struct held_event *event;
	GSignalQuery query;
	va_list args;

	g_signal_query(signal_id, &query);
	event = g_malloc0(sizeof *event +
			  (query.n_params + 1) * sizeof(GValue));
	event->signal_id = signal_id;
	event->n_values = query.n_params + 1;
	g_value_init_from_instance(&event->values[0], self);
	va_start(args, signal_id);
	for (guint i = 0; i < query.n_params; i++) {
		char *error = NULL;

		G_VALUE_COLLECT_INIT(&event->values[i + 1],
				     query.param_types[i] &
					     ~G_SIGNAL_TYPE_STATIC_SCOPE,
				     args, 0, &error);
		if (error) {
			g_critical("%s: %s", query.signal_name, error);
			g_free(error);
			event->broken = TRUE;
			event->n_values = i + 1;
			break;
		}
	}
	va_end(args);
	if (!self->emitting && g_queue_is_empty(&self->held)) {
		deliver(self, event);
		return;
	}
	g_queue_push_tail(&self->held, event);
	if (!self->drain_id)
		self->drain_id = g_idle_add(drain, self);
}

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
	emit(data, signals[SIGNAL_SWITCHER_ENTRY], id, title, app_id, flags);
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
	emit(data, signals[SIGNAL_SWITCHER_THUMBNAIL], id, surface);
	cairo_surface_destroy(surface);
}

static void
switcher_icon_name(void *data, struct polo_shell_ui_v1 *shell_ui, uint32_t id,
		   const char *name)
{
	emit(data, signals[SIGNAL_SWITCHER_ICON_NAME], id, name);
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
	emit(data, signals[SIGNAL_SWITCHER_ICON_PIXELS], id, surface);
	cairo_surface_destroy(surface);
}

static void
show_switcher(void *data, struct polo_shell_ui_v1 *shell_ui,
	      struct wl_output *output, uint32_t selected, uint32_t columns,
	      uint32_t expand_to_fit, uint32_t icon_size)
{
	emit(data, signals[SIGNAL_SHOW_SWITCHER], monitor_for(data, output),
	     selected, columns, expand_to_fit != 0, icon_size);
}

static void
update_selected(void *data, struct polo_shell_ui_v1 *shell_ui,
		uint32_t selected)
{
	emit(data, signals[SIGNAL_UPDATE_SELECTED], selected);
}

static void
hide_switcher(void *data, struct polo_shell_ui_v1 *shell_ui)
{
	emit(data, signals[SIGNAL_HIDE_SWITCHER]);
}

static void
switcher_heading(void *data, struct polo_shell_ui_v1 *shell_ui,
		 const char *text)
{
	emit(data, signals[SIGNAL_SWITCHER_HEADING], text);
}

static void
workspace_switcher_entry(void *data, struct polo_shell_ui_v1 *shell_ui,
			 uint32_t id, const char *title, uint32_t blank)
{
	emit(data, signals[SIGNAL_WORKSPACE_SWITCHER_ENTRY], id, title,
	     blank != 0);
}

static void
workspace_switcher_window(void *data, struct polo_shell_ui_v1 *shell_ui,
			  uint32_t id, int32_t x, int32_t y, int32_t width,
			  int32_t height, uint32_t focused, const char *app_id)
{
	emit(data, signals[SIGNAL_WORKSPACE_SWITCHER_WINDOW], id, x, y, width,
	     height, focused != 0, app_id);
}

static void
show_workspace_switcher(void *data, struct polo_shell_ui_v1 *shell_ui,
			struct wl_output *output, uint32_t selected,
			uint32_t columns, uint32_t active, int32_t screen_width,
			int32_t screen_height, int32_t output_width,
			int32_t output_height, uint32_t icon_size)
{
	emit(data, signals[SIGNAL_SHOW_WORKSPACE_SWITCHER],
	     monitor_for(data, output), selected, columns, active, screen_width,
	     screen_height, output_width, output_height, icon_size);
}

static void
update_workspace_switcher(void *data, struct polo_shell_ui_v1 *shell_ui,
			  uint32_t selected, uint32_t active)
{
	emit(data, signals[SIGNAL_UPDATE_WORKSPACE_SWITCHER], selected, active);
}

static void
window_menu_workspace(void *data, struct polo_shell_ui_v1 *shell_ui,
		      uint32_t index, const char *name)
{
	emit(data, signals[SIGNAL_WINDOW_MENU_WORKSPACE], index, name);
}

static void
show_window_menu(void *data, struct polo_shell_ui_v1 *shell_ui,
		 uint32_t window_id, struct wl_output *output, int32_t x,
		 int32_t y, uint32_t ops, uint32_t insensitive,
		 uint32_t active_workspace, uint32_t n_workspaces,
		 uint32_t time)
{
	emit(data, signals[SIGNAL_SHOW_WINDOW_MENU], window_id,
	     monitor_for(data, output), x, y, ops, insensitive,
	     active_workspace, n_workspaces, time);
}

static void
hide_window_menu(void *data, struct polo_shell_ui_v1 *shell_ui,
		 uint32_t window_id)
{
	emit(data, signals[SIGNAL_HIDE_WINDOW_MENU], window_id);
}

static void
show_osd(void *data, struct polo_shell_ui_v1 *shell_ui, uint32_t kind,
	 struct wl_output *output, const char *icon, const char *text,
	 int32_t value)
{
	emit(data, signals[SIGNAL_SHOW_OSD], kind, monitor_for(data, output),
	     icon, text, value);
}

static void
show_dialog(void *data, struct polo_shell_ui_v1 *shell_ui, uint32_t dialog_id,
	    uint32_t kind, struct wl_output *output, const char *payload)
{
	emit(data, signals[SIGNAL_SHOW_DIALOG], dialog_id, kind,
	     monitor_for(data, output), payload);
}

static void
hide_dialog(void *data, struct polo_shell_ui_v1 *shell_ui, uint32_t dialog_id)
{
	emit(data, signals[SIGNAL_HIDE_DIALOG], dialog_id);
}

static void
lookup_color(void *data, struct polo_shell_ui_v1 *shell_ui, const char *name)
{
	emit(data, signals[SIGNAL_LOOKUP_COLOR], name);
}

static void
lookup_icon(void *data, struct polo_shell_ui_v1 *shell_ui, uint32_t serial,
	    const char *name, const char *desktop_id, int32_t mini_size,
	    int32_t size, int32_t scale, uint32_t fallback)
{
	emit(data, signals[SIGNAL_LOOKUP_ICON], serial, name, desktop_id,
	     mini_size, size, scale, fallback != 0);
}

static const struct polo_shell_ui_v1_listener shell_ui_listener = {
	.switcher_entry = switcher_entry,
	.switcher_icon_name = switcher_icon_name,
	.switcher_icon_pixels = switcher_icon_pixels,
	.switcher_thumbnail = switcher_thumbnail,
	.show_switcher = show_switcher,
	.update_selected = update_selected,
	.hide_switcher = hide_switcher,
	.switcher_heading = switcher_heading,
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
		scan->shell_ui = wl_registry_bind(registry, name,
						  &polo_shell_ui_v1_interface,
						  MIN(version, 2));
}

static void
registry_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = {
	registry_global,
	registry_global_remove,
};

static gpointer
watch_connection(gpointer data)
{
	struct wl_display *wl_display = data;

	for (;;) {
		g_usleep(G_USEC_PER_SEC);
		if (wl_display_get_error(wl_display) != 0) {
			g_usleep(2 * G_USEC_PER_SEC);
			g_printerr("polo-shell-ui: lost the compositor\n");
			_exit(1);
		}
	}
	return NULL;
}

/* GTK 3.24's Wayland backend takes a reference on every native window it
 * creates and drops it only in destroy_notify, which only the X11 and Win32
 * backends call: each popup's GdkWindow outlives its destruction. Whether
 * this GTK does is found out once, on a throwaway window. */
static gboolean gdk_keeps_windows;

/* GTK 3.24 connects every Wayland window to its screen's monitors-changed
 * and never disconnects it, so a window freed with it connected is run into
 * by the next screen plugged in, unplugged or rescaled. */
static void
forget_screen(GdkWindow *window)
{
	g_signal_handlers_disconnect_matched(gdk_window_get_screen(window),
					     G_SIGNAL_MATCH_DATA, 0, 0, NULL,
					     NULL, window);
}

struct kept_window {
	GdkWindow *window;
	int checks;
	int steady;
};

/* Ours and one more, unchanged for 3 s after the window went: whoever held
 * it for a while is done, and the one left is GDK's. */
static gboolean
drop_kept_window(gpointer data)
{
	struct kept_window *kept = data;
	guint refs = G_OBJECT(kept->window)->ref_count;

	kept->checks++;
	if (!gdk_window_is_destroyed(kept->window) || refs > 2) {
		kept->steady = 0;
		if (kept->checks < 60)
			return G_SOURCE_CONTINUE;
	} else if (refs == 2 && ++kept->steady < 6) {
		return G_SOURCE_CONTINUE;
	} else if (refs == 2) {
		g_object_unref(kept->window);
	}
	g_object_unref(kept->window);
	g_free(kept);
	return G_SOURCE_REMOVE;
}

void
polo_shell_ui_client_release_window(GdkWindow *window)
{
	struct kept_window *kept;

	if (!gdk_keeps_windows || !window || !gdk_window_has_native(window) ||
	    g_object_get_data(G_OBJECT(window), "polo-released"))
		return;
	g_object_set_data(G_OBJECT(window), "polo-released", window);
	forget_screen(window);
	kept = g_new0(struct kept_window, 1);
	kept->window = g_object_ref(window);
	g_timeout_add(500, drop_kept_window, kept);
}

static void
find_out_if_gdk_keeps_windows(void)
{
	GdkWindowAttr attributes = {
		.width = 1,
		.height = 1,
		.wclass = GDK_INPUT_OUTPUT,
		.window_type = GDK_WINDOW_TOPLEVEL,
	};
	GdkWindow *window = gdk_window_new(NULL, &attributes, 0);

	forget_screen(window);
	g_object_ref(window);
	gdk_window_destroy(window);
	gdk_keeps_windows = G_OBJECT(window)->ref_count > 1;
	if (gdk_keeps_windows)
		g_object_unref(window);
	g_object_unref(window);
}

/* Unrealize runs before GTK destroys a widget's window. */
static gboolean
forget_screen_handlers(GSignalInvocationHint *hint, guint n_values,
		       const GValue *values, gpointer data)
{
	GtkWidget *widget = g_value_get_object(&values[0]);
	GdkWindow *window = widget ? gtk_widget_get_window(widget) : NULL;

	if (window && gtk_widget_get_has_window(widget)) {
		forget_screen(window);
		polo_shell_ui_client_release_window(window);
	}
	return TRUE;
}

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
	g_thread_unref(
		g_thread_new("connection-watch", watch_connection, wl_display));
	find_out_if_gdk_keeps_windows();
	g_signal_add_emission_hook(
		g_signal_lookup("unrealize", GTK_TYPE_WIDGET), 0,
		forget_screen_handlers, NULL, NULL);
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
	signals[SIGNAL_SWITCHER_HEADING] =
		g_signal_new("switcher-heading", type, G_SIGNAL_RUN_LAST, 0,
			     NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);
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
	signals[SIGNAL_SHOW_OSD] = g_signal_new(
		"show-osd", type, G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
		G_TYPE_NONE, 5, G_TYPE_UINT, GDK_TYPE_MONITOR, G_TYPE_STRING,
		G_TYPE_STRING, G_TYPE_INT);
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
