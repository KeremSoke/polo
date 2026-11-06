/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * polo_shell_ui_v1 (v1) shim. One bound resource at a time; who may bind
 * is the global filter's decision. Events go out as Vala asks; requests
 * come back as signals on PoloBackendShellUi.
 */

#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <libweston/libweston.h>
#include <pixman.h>

#include "polo-backend-private.h"
#include "polo-shell-ui-v1-server-protocol.h"

#define SHELL_UI_VERSION 2
#define COLOR_TABLE_FLOATS (8 * 5 * 4)
#define MAX_ICON_SIZE 1024

struct dialog_surface {
	struct wl_list link;
	guint id;
	struct weston_surface *surface;
	struct wl_listener surface_destroy;
};

struct _PoloBackendShellUi {
	GObject parent_instance;

	PoloBackendServer *server;
	struct wl_global *global;
	struct wl_resource *resource;
	struct wl_list dialogs; /* struct dialog_surface */

	struct wl_protocol_logger *logger;
	uint32_t input_serial;
	gboolean menu_open;
	struct wl_listener resource_created;
	struct wl_event_source *restore;
	uint32_t saved_serial;
	gboolean lent;
};

enum {
	SIGNAL_BOUND,
	SIGNAL_UNBOUND,
	SIGNAL_SWITCHER_SELECT,
	SIGNAL_MENU_ACTION,
	SIGNAL_DIALOG_RESPONSE,
	SIGNAL_COLOR_TABLE,
	SIGNAL_CUSTOM_COLOR,
	SIGNAL_ICON,
	SIGNAL_ICON_THEME_CHANGED,
	N_SIGNALS,
};

static guint signals[N_SIGNALS];

G_DEFINE_FINAL_TYPE(PoloBackendShellUi, polo_backend_shell_ui, G_TYPE_OBJECT)

static void
handle_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void
handle_switcher_select(struct wl_client *client, struct wl_resource *resource,
		       uint32_t id)
{
	PoloBackendShellUi *self = wl_resource_get_user_data(resource);

	g_signal_emit(self, signals[SIGNAL_SWITCHER_SELECT], 0, id);
}

static void
handle_menu_action(struct wl_client *client, struct wl_resource *resource,
		   uint32_t window_id, uint32_t op, int32_t workspace)
{
	PoloBackendShellUi *self = wl_resource_get_user_data(resource);

	self->menu_open = FALSE;
	g_signal_emit(self, signals[SIGNAL_MENU_ACTION], 0, window_id, op,
		      workspace);
}

static void
dialog_surface_free(struct dialog_surface *dialog)
{
	wl_list_remove(&dialog->surface_destroy.link);
	wl_list_remove(&dialog->link);
	g_free(dialog);
}

static void
dialog_surface_destroyed(struct wl_listener *listener, void *data)
{
	struct dialog_surface *dialog =
		wl_container_of(listener, dialog, surface_destroy);

	dialog_surface_free(dialog);
}

static void
handle_dialog_surface(struct wl_client *client, struct wl_resource *resource,
		      uint32_t dialog_id, struct wl_resource *surface_resource)
{
	PoloBackendShellUi *self = wl_resource_get_user_data(resource);
	struct dialog_surface *dialog = g_new0(struct dialog_surface, 1);

	dialog->id = dialog_id;
	dialog->surface = wl_resource_get_user_data(surface_resource);
	dialog->surface_destroy.notify = dialog_surface_destroyed;
	wl_signal_add(&dialog->surface->destroy_signal,
		      &dialog->surface_destroy);
	wl_list_insert(&self->dialogs, &dialog->link);
}

guint
polo_backend_shell_ui_get_dialog(PoloBackendShellUi *self,
				 PoloBackendToplevel *toplevel)
{
	PoloBackendView *view;
	struct weston_view *weston_view;
	struct weston_surface *surface;
	struct dialog_surface *dialog;

	g_return_val_if_fail(POLO_BACKEND_IS_SHELL_UI(self), 0);
	view = polo_backend_toplevel_get_view(toplevel);
	weston_view = view ? polo_backend_view_get_weston_view(view) : NULL;
	if (!weston_view)
		return 0;
	surface = weston_surface_get_main_surface(weston_view->surface);
	wl_list_for_each(dialog, &self->dialogs,
			 link) if (dialog->surface ==
				   surface) return dialog->id;
	return 0;
}

static void
handle_dialog_response(struct wl_client *client, struct wl_resource *resource,
		       uint32_t dialog_id, int32_t response, const char *text)
{
	PoloBackendShellUi *self = wl_resource_get_user_data(resource);

	g_signal_emit(self, signals[SIGNAL_DIALOG_RESPONSE], 0, dialog_id,
		      response, text);
}

static void
handle_color_table(struct wl_client *client, struct wl_resource *resource,
		   struct wl_array *colors)
{
	PoloBackendShellUi *self = wl_resource_get_user_data(resource);
	g_autoptr(GBytes) bytes = NULL;

	if (colors->size != COLOR_TABLE_FLOATS * sizeof(float)) {
		wl_resource_post_error(
			resource, POLO_SHELL_UI_V1_ERROR_INVALID_COLOR_TABLE,
			"color_table has %zu bytes, not %zu", colors->size,
			COLOR_TABLE_FLOATS * sizeof(float));
		return;
	}
	bytes = g_bytes_new(colors->data, colors->size);
	g_signal_emit(self, signals[SIGNAL_COLOR_TABLE], 0, bytes);
}

static void
handle_custom_color(struct wl_client *client, struct wl_resource *resource,
		    const char *name, struct wl_array *rgba)
{
	PoloBackendShellUi *self = wl_resource_get_user_data(resource);
	g_autoptr(GBytes) bytes = NULL;

	if (rgba->size != 0 && rgba->size != 4 * sizeof(float)) {
		wl_resource_post_error(
			resource, POLO_SHELL_UI_V1_ERROR_INVALID_COLOR_TABLE,
			"custom_color has %zu bytes, not 0 or %zu", rgba->size,
			4 * sizeof(float));
		return;
	}
	if (rgba->size)
		bytes = g_bytes_new(rgba->data, rgba->size);
	g_signal_emit(self, signals[SIGNAL_CUSTOM_COLOR], 0, name, bytes);
}

static GBytes *
icon_bytes(const guint8 *data, int width, int height)
{
	return g_bytes_new(data, (gsize)width * height * 4);
}

static void
handle_icon(struct wl_client *client, struct wl_resource *resource,
	    uint32_t serial, int32_t fd, int32_t mini_width,
	    int32_t mini_height, int32_t width, int32_t height)
{
	PoloBackendShellUi *self = wl_resource_get_user_data(resource);
	g_autoptr(GBytes) mini = NULL;
	g_autoptr(GBytes) icon = NULL;
	gsize mini_size, size;
	off_t length;
	void *data;

	if (mini_width <= 0 || mini_height <= 0 || width <= 0 || height <= 0 ||
	    mini_width > MAX_ICON_SIZE || mini_height > MAX_ICON_SIZE ||
	    width > MAX_ICON_SIZE || height > MAX_ICON_SIZE) {
		close(fd);
		wl_resource_post_error(resource,
				       POLO_SHELL_UI_V1_ERROR_INVALID_ICON,
				       "icon sizes %dx%d and %dx%d", mini_width,
				       mini_height, width, height);
		return;
	}
	mini_size = (gsize)mini_width * mini_height * 4;
	size = (gsize)width * height * 4;
	length = lseek(fd, 0, SEEK_END);
	if (length < 0 || (gsize)length < mini_size + size) {
		close(fd);
		wl_resource_post_error(
			resource, POLO_SHELL_UI_V1_ERROR_INVALID_ICON,
			"icon pixels need %zu bytes", mini_size + size);
		return;
	}
	data = mmap(NULL, mini_size + size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (data == MAP_FAILED) {
		g_signal_emit(self, signals[SIGNAL_ICON], 0, serial, NULL, 0, 0,
			      NULL, 0, 0);
		return;
	}
	mini = icon_bytes(data, mini_width, mini_height);
	icon = icon_bytes((const guint8 *)data + mini_size, width, height);
	munmap(data, mini_size + size);
	g_signal_emit(self, signals[SIGNAL_ICON], 0, serial, mini, mini_width,
		      mini_height, icon, width, height);
}

static void
handle_no_icon(struct wl_client *client, struct wl_resource *resource,
	       uint32_t serial)
{
	g_signal_emit(wl_resource_get_user_data(resource), signals[SIGNAL_ICON],
		      0, serial, NULL, 0, 0, NULL, 0, 0);
}

static void
handle_icon_theme_changed(struct wl_client *client,
			  struct wl_resource *resource)
{
	g_signal_emit(wl_resource_get_user_data(resource),
		      signals[SIGNAL_ICON_THEME_CHANGED], 0);
}

static const struct polo_shell_ui_v1_interface shell_ui_impl = {
	.destroy = handle_destroy,
	.switcher_select = handle_switcher_select,
	.menu_action = handle_menu_action,
	.dialog_surface = handle_dialog_surface,
	.dialog_response = handle_dialog_response,
	.color_table = handle_color_table,
	.custom_color = handle_custom_color,
	.icon = handle_icon,
	.no_icon = handle_no_icon,
	.icon_theme_changed = handle_icon_theme_changed,
};

static struct weston_pointer *
first_pointer(PoloBackendShellUi *self)
{
	struct weston_seat *seat;

	wl_list_for_each(seat, &self->server->compositor->seat_list, link) if (
		weston_seat_get_pointer(
			seat)) return weston_seat_get_pointer(seat);
	return NULL;
}

static void
restore_serial(void *data)
{
	PoloBackendShellUi *self = data;
	struct weston_pointer *pointer = first_pointer(self);

	self->restore = NULL;
	if (self->lent && pointer && pointer->grab_serial == self->input_serial)
		pointer->grab_serial = self->saved_serial;
	self->lent = FALSE;
}

static void
resource_created(struct wl_listener *listener, void *data)
{
	PoloBackendShellUi *self =
		wl_container_of(listener, self, resource_created);
	struct weston_pointer *pointer = first_pointer(self);

	if (!self->menu_open || !pointer ||
	    strcmp(wl_resource_get_class(data), "xdg_popup") != 0)
		return;
	if (!self->lent) {
		self->saved_serial = pointer->grab_serial;
		self->lent = TRUE;
	}
	pointer->grab_serial = self->input_serial;
	if (!self->restore)
		self->restore = wl_event_loop_add_idle(
			wl_display_get_event_loop(self->server->display),
			restore_serial, self);
}

static void
log_input(void *data, enum wl_protocol_logger_type type,
	  const struct wl_protocol_logger_message *message)
{
	PoloBackendShellUi *self = data;
	const char *class, *name = message->message->name;
	uint32_t serial;

	if (type != WL_PROTOCOL_LOGGER_EVENT || !self->resource ||
	    wl_resource_get_client(message->resource) !=
		    wl_resource_get_client(self->resource))
		return;
	class = wl_resource_get_class(message->resource);
	if (strcmp(class, "wl_keyboard") == 0 && strcmp(name, "key") == 0)
		serial = message->arguments[0].u;
	else if (strcmp(class, "wl_pointer") == 0 &&
		 strcmp(name, "button") == 0 && message->arguments[3].u == 1)
		serial = message->arguments[0].u;
	else if (strcmp(class, "wl_touch") == 0 && strcmp(name, "down") == 0)
		serial = message->arguments[0].u;
	else
		return;
	if (serial > self->input_serial)
		self->input_serial = serial;
}

static void
resource_destroyed(struct wl_resource *resource)
{
	PoloBackendShellUi *self = wl_resource_get_user_data(resource);

	if (self->resource != resource)
		return;
	wl_list_remove(&self->resource_created.link);
	wl_list_init(&self->resource_created.link);
	self->resource = NULL;
	self->menu_open = FALSE;
	self->input_serial = 0;
	g_signal_emit(self, signals[SIGNAL_UNBOUND], 0);
}

static void
bind_shell_ui(struct wl_client *client, void *data, uint32_t version,
	      uint32_t id)
{
	PoloBackendShellUi *self = data;
	struct wl_resource *resource;

	resource = wl_resource_create(client, &polo_shell_ui_v1_interface,
				      (int)version, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &shell_ui_impl, self,
				       resource_destroyed);
	if (self->resource) {
		wl_resource_post_error(resource,
				       POLO_SHELL_UI_V1_ERROR_ALREADY_BOUND,
				       "polo_shell_ui_v1 is already bound");
		return;
	}
	self->resource = resource;
	self->input_serial = 0;
	wl_client_add_resource_created_listener(client,
						&self->resource_created);
	g_signal_emit(self, signals[SIGNAL_BOUND], 0,
		      polo_backend_server_get_client(self->server, client));
}

gboolean
polo_backend_shell_ui_global_init(PoloBackendServer *server, GError **error)
{
	PoloBackendShellUi *self =
		g_object_new(POLO_BACKEND_TYPE_SHELL_UI, NULL);

	self->server = server;
	wl_list_init(&self->dialogs);
	self->resource_created.notify = resource_created;
	wl_list_init(&self->resource_created.link);
	self->logger = wl_display_add_protocol_logger(server->display,
						      log_input, self);
	self->global =
		wl_global_create(server->display, &polo_shell_ui_v1_interface,
				 SHELL_UI_VERSION, self, bind_shell_ui);
	if (!self->global) {
		g_object_unref(self);
		g_set_error(error, POLO_BACKEND_ERROR,
			    POLO_BACKEND_ERROR_FAILED,
			    "cannot create the polo_shell_ui_v1 global");
		return FALSE;
	}
	server->shell_ui = self;
	return TRUE;
}

void
polo_backend_shell_ui_global_fini(PoloBackendServer *server)
{
	PoloBackendShellUi *self = server->shell_ui;

	if (!self)
		return;
	if (self->resource) {
		wl_resource_set_user_data(self->resource, NULL);
		wl_resource_set_destructor(self->resource, NULL);
		self->resource = NULL;
	}
	wl_list_remove(&self->resource_created.link);
	struct dialog_surface *dialog, *next_dialog;

	wl_list_for_each_safe(dialog, next_dialog, &self->dialogs, link)
		dialog_surface_free(dialog);
	if (self->restore)
		wl_event_source_remove(self->restore);
	wl_protocol_logger_destroy(self->logger);
	wl_global_destroy(self->global);
	self->global = NULL;
	server->shell_ui = NULL;
	g_object_unref(self);
}

PoloBackendShellUi *
polo_backend_server_get_shell_ui(PoloBackendServer *server)
{
	g_return_val_if_fail(POLO_BACKEND_IS_SERVER(server), NULL);
	return server->shell_ui;
}

/* ---- Events ------------------------------------------------------------- */

static struct wl_resource *
output_resource(PoloBackendShellUi *self, PoloBackendOutput *output)
{
	struct wl_client *client = wl_resource_get_client(self->resource);
	struct weston_output *weston_output;
	struct weston_head *head;

	if (!output)
		return NULL;
	weston_output = polo_backend_output_get_weston_output(output);
	if (!weston_output)
		return NULL;
	wl_list_for_each(head, &weston_output->head_list, output_link)
	{
		struct wl_resource *resource = wl_resource_find_for_client(
			&head->resource_list, client);

		if (resource)
			return resource;
	}
	return NULL;
}

gboolean
polo_backend_shell_ui_is_bound(PoloBackendShellUi *self)
{
	g_return_val_if_fail(POLO_BACKEND_IS_SHELL_UI(self), FALSE);
	return self->resource != NULL;
}

PoloBackendClient *
polo_backend_shell_ui_get_client(PoloBackendShellUi *self)
{
	g_return_val_if_fail(POLO_BACKEND_IS_SHELL_UI(self), NULL);
	if (!self->resource)
		return NULL;
	return polo_backend_server_get_client(
		self->server, wl_resource_get_client(self->resource));
}

void
polo_backend_shell_ui_switcher_entry(PoloBackendShellUi *self, guint id,
				     const char *title, const char *app_id,
				     PoloBackendSwitcherEntryFlags flags)
{
	g_return_if_fail(POLO_BACKEND_IS_SHELL_UI(self));
	if (self->resource)
		polo_shell_ui_v1_send_switcher_entry(
			self->resource, id, title ? title : "", app_id, flags);
}

void
polo_backend_shell_ui_switcher_icon_name(PoloBackendShellUi *self, guint id,
					 const char *name)
{
	g_return_if_fail(POLO_BACKEND_IS_SHELL_UI(self));
	if (self->resource)
		polo_shell_ui_v1_send_switcher_icon_name(self->resource, id,
							 name);
}

void
polo_backend_shell_ui_switcher_icon(PoloBackendShellUi *self, guint id,
				    PoloBackendToplevel *toplevel)
{
	const char *name;
	GBytes *pixels;
	int size;

	g_return_if_fail(POLO_BACKEND_IS_SHELL_UI(self));
	if (!self->resource)
		return;
	name = polo_backend_toplevel_get_icon_name(toplevel);
	if (name)
		polo_shell_ui_v1_send_switcher_icon_name(self->resource, id,
							 name);
	pixels = polo_backend_toplevel_get_icon_pixels(toplevel, &size);
	if (pixels) {
		gsize length;
		const void *data = g_bytes_get_data(pixels, &length);
		int fd = memfd_create("polo-icon", MFD_CLOEXEC);

		if (fd < 0)
			return;
		if (ftruncate(fd, (off_t)length) == 0) {
			void *copy = mmap(NULL, length, PROT_WRITE, MAP_SHARED,
					  fd, 0);

			if (copy != MAP_FAILED) {
				memcpy(copy, data, length);
				munmap(copy, length);
				polo_shell_ui_v1_send_switcher_icon_pixels(
					self->resource, id, fd, size);
			}
		}
		close(fd);
	}
}

static int
thumbnail_fd(struct weston_surface *surface, int width, int height)
{
	g_autofree uint32_t *content = NULL;
	pixman_image_t *source, *target;
	pixman_transform_t transform;
	size_t size = (size_t)width * height * 4;
	int content_width, content_height;
	void *pixels;
	int fd;

	weston_surface_get_content_size(surface, &content_width,
					&content_height);
	content = g_malloc((size_t)content_width * content_height * 4);
	if (weston_surface_copy_content(
		    surface, content,
		    (size_t)content_width * content_height * 4, 0, 0,
		    content_width, content_height) < 0)
		return -1;

	fd = memfd_create("polo-thumbnail", MFD_CLOEXEC);
	if (fd < 0)
		return -1;
	if (ftruncate(fd, (off_t)size) < 0) {
		close(fd);
		return -1;
	}
	pixels = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (pixels == MAP_FAILED) {
		close(fd);
		return -1;
	}

	source = pixman_image_create_bits(PIXMAN_a8b8g8r8, content_width,
					  content_height, content,
					  content_width * 4);
	target = pixman_image_create_bits(PIXMAN_a8r8g8b8, width, height,
					  pixels, width * 4);
	pixman_transform_init_scale(
		&transform,
		pixman_double_to_fixed((double)content_width / width),
		pixman_double_to_fixed((double)content_height / height));
	pixman_image_set_transform(source, &transform);
	pixman_image_set_filter(source, PIXMAN_FILTER_BILINEAR, NULL, 0);
	pixman_image_composite32(PIXMAN_OP_SRC, source, NULL, target, 0, 0, 0,
				 0, 0, 0, width, height);
	pixman_image_unref(source);
	pixman_image_unref(target);
	munmap(pixels, size);
	return fd;
}

gboolean
polo_backend_shell_ui_switcher_thumbnail(PoloBackendShellUi *self, guint id,
					 PoloBackendView *view,
					 double landscape_size,
					 double portrait_size)
{
	struct weston_view *weston_view;
	struct weston_surface *surface;
	int width, height, fd;
	double ratio;

	g_return_val_if_fail(POLO_BACKEND_IS_SHELL_UI(self), FALSE);
	g_return_val_if_fail(POLO_BACKEND_IS_VIEW(view), FALSE);
	weston_view = polo_backend_view_get_weston_view(view);
	if (!self->resource || !weston_view)
		return FALSE;
	surface = weston_surface_get_main_surface(weston_view->surface);
	weston_surface_get_content_size(surface, &width, &height);
	if (width <= 0 || height <= 0)
		return FALSE;

	if (width > height) {
		ratio = (double)width / landscape_size;
		width = (int)landscape_size;
		height = (int)((double)height / ratio);
	} else {
		ratio = (double)height / portrait_size;
		height = (int)portrait_size;
		width = (int)((double)width / ratio);
	}
	if (width <= 0 || height <= 0)
		return FALSE;

	fd = thumbnail_fd(surface, width, height);
	if (fd < 0)
		return FALSE;
	polo_shell_ui_v1_send_switcher_thumbnail(self->resource, id, fd, width,
						 height);
	close(fd);
	return TRUE;
}

void
polo_backend_shell_ui_show_switcher(PoloBackendShellUi *self,
				    PoloBackendOutput *output, guint selected,
				    guint columns, gboolean expand_to_fit,
				    guint icon_size)
{
	g_return_if_fail(POLO_BACKEND_IS_SHELL_UI(self));
	if (self->resource)
		polo_shell_ui_v1_send_show_switcher(
			self->resource, output_resource(self, output), selected,
			columns, expand_to_fit, icon_size);
}

void
polo_backend_shell_ui_update_selected(PoloBackendShellUi *self, guint selected)
{
	g_return_if_fail(POLO_BACKEND_IS_SHELL_UI(self));
	if (self->resource)
		polo_shell_ui_v1_send_update_selected(self->resource, selected);
}

void
polo_backend_shell_ui_switcher_heading(PoloBackendShellUi *self,
				       const char *text)
{
	g_return_if_fail(POLO_BACKEND_IS_SHELL_UI(self));
	if (self->resource &&
	    wl_resource_get_version(self->resource) >=
		    POLO_SHELL_UI_V1_SWITCHER_HEADING_SINCE_VERSION)
		polo_shell_ui_v1_send_switcher_heading(self->resource, text);
}

void
polo_backend_shell_ui_hide_switcher(PoloBackendShellUi *self)
{
	g_return_if_fail(POLO_BACKEND_IS_SHELL_UI(self));
	if (self->resource)
		polo_shell_ui_v1_send_hide_switcher(self->resource);
}

void
polo_backend_shell_ui_workspace_switcher_entry(PoloBackendShellUi *self,
					       guint id, const char *title,
					       gboolean blank)
{
	g_return_if_fail(POLO_BACKEND_IS_SHELL_UI(self));
	if (self->resource)
		polo_shell_ui_v1_send_workspace_switcher_entry(
			self->resource, id, title ? title : "", blank);
}

void
polo_backend_shell_ui_workspace_switcher_window(PoloBackendShellUi *self,
						guint id, int x, int y,
						int width, int height,
						gboolean focused,
						const char *app_id)
{
	g_return_if_fail(POLO_BACKEND_IS_SHELL_UI(self));
	if (self->resource)
		polo_shell_ui_v1_send_workspace_switcher_window(
			self->resource, id, x, y, width, height, focused,
			app_id);
}

void
polo_backend_shell_ui_show_workspace_switcher(
	PoloBackendShellUi *self, PoloBackendOutput *output, guint selected,
	guint columns, guint active, int screen_width, int screen_height,
	int output_width, int output_height, guint icon_size)
{
	g_return_if_fail(POLO_BACKEND_IS_SHELL_UI(self));
	if (self->resource)
		polo_shell_ui_v1_send_show_workspace_switcher(
			self->resource, output_resource(self, output), selected,
			columns, active, screen_width, screen_height,
			output_width, output_height, icon_size);
}

void
polo_backend_shell_ui_update_workspace_switcher(PoloBackendShellUi *self,
						guint selected, guint active)
{
	g_return_if_fail(POLO_BACKEND_IS_SHELL_UI(self));
	if (self->resource)
		polo_shell_ui_v1_send_update_workspace_switcher(
			self->resource, selected, active);
}

void
polo_backend_shell_ui_window_menu_workspace(PoloBackendShellUi *self,
					    guint index, const char *name)
{
	g_return_if_fail(POLO_BACKEND_IS_SHELL_UI(self));
	if (self->resource)
		polo_shell_ui_v1_send_window_menu_workspace(
			self->resource, index, name ? name : "");
}

void
polo_backend_shell_ui_show_window_menu(PoloBackendShellUi *self,
				       guint window_id,
				       PoloBackendOutput *output, int x, int y,
				       guint ops, guint insensitive,
				       guint active_workspace,
				       guint n_workspaces)
{
	g_return_if_fail(POLO_BACKEND_IS_SHELL_UI(self));
	if (!self->resource)
		return;
	self->menu_open = TRUE;
	polo_shell_ui_v1_send_show_window_menu(
		self->resource, window_id, output_resource(self, output), x, y,
		ops, insensitive, active_workspace, n_workspaces,
		polo_backend_server_get_time_msec(self->server));
}

void
polo_backend_shell_ui_hide_window_menu(PoloBackendShellUi *self,
				       guint window_id)
{
	g_return_if_fail(POLO_BACKEND_IS_SHELL_UI(self));
	if (self->resource)
		polo_shell_ui_v1_send_hide_window_menu(self->resource,
						       window_id);
}

void
polo_backend_shell_ui_show_osd(PoloBackendShellUi *self,
			       PoloBackendOsdKind kind,
			       PoloBackendOutput *output, const char *icon,
			       const char *text, int value)
{
	g_return_if_fail(POLO_BACKEND_IS_SHELL_UI(self));
	if (self->resource)
		polo_shell_ui_v1_send_show_osd(self->resource, kind,
					       output_resource(self, output),
					       icon, text, value);
}

void
polo_backend_shell_ui_show_dialog(PoloBackendShellUi *self, guint dialog_id,
				  PoloBackendDialogKind kind,
				  PoloBackendOutput *output,
				  const char *payload)
{
	g_return_if_fail(POLO_BACKEND_IS_SHELL_UI(self));
	if (self->resource)
		polo_shell_ui_v1_send_show_dialog(
			self->resource, dialog_id, kind,
			output_resource(self, output), payload);
}

void
polo_backend_shell_ui_lookup_color(PoloBackendShellUi *self, const char *name)
{
	g_return_if_fail(POLO_BACKEND_IS_SHELL_UI(self));
	if (self->resource)
		polo_shell_ui_v1_send_lookup_color(self->resource, name);
}

gboolean
polo_backend_shell_ui_lookup_icon(PoloBackendShellUi *self, guint serial,
				  const char *name, const char *desktop_id,
				  int mini_size, int size, int scale,
				  gboolean fallback)
{
	g_return_val_if_fail(POLO_BACKEND_IS_SHELL_UI(self), FALSE);
	if (!self->resource)
		return FALSE;
	polo_shell_ui_v1_send_lookup_icon(self->resource, serial, name,
					  desktop_id, mini_size, size, scale,
					  fallback);
	return TRUE;
}

void
polo_backend_shell_ui_hide_dialog(PoloBackendShellUi *self, guint dialog_id)
{
	g_return_if_fail(POLO_BACKEND_IS_SHELL_UI(self));
	if (self->resource)
		polo_shell_ui_v1_send_hide_dialog(self->resource, dialog_id);
}

/* ---- GObject ------------------------------------------------------------ */

static void
polo_backend_shell_ui_class_init(PoloBackendShellUiClass *klass)
{
	signals[SIGNAL_BOUND] = g_signal_new(
		"bound", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL,
		NULL, NULL, G_TYPE_NONE, 1, POLO_BACKEND_TYPE_CLIENT);
	signals[SIGNAL_UNBOUND] = g_signal_new(
		"unbound", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL,
		NULL, NULL, G_TYPE_NONE, 0);
	signals[SIGNAL_SWITCHER_SELECT] = g_signal_new(
		"switcher-select", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
		0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_UINT);
	signals[SIGNAL_MENU_ACTION] = g_signal_new(
		"menu-action", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0,
		NULL, NULL, NULL, G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_UINT,
		G_TYPE_INT);
	signals[SIGNAL_DIALOG_RESPONSE] = g_signal_new(
		"dialog-response", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
		0, NULL, NULL, NULL, G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_INT,
		G_TYPE_STRING);
	signals[SIGNAL_COLOR_TABLE] = g_signal_new(
		"color-table", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0,
		NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_BYTES);
	signals[SIGNAL_CUSTOM_COLOR] = g_signal_new(
		"custom-color", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0,
		NULL, NULL, NULL, G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_BYTES);
	signals[SIGNAL_ICON] = g_signal_new(
		"icon", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL,
		NULL, NULL, G_TYPE_NONE, 7, G_TYPE_UINT, G_TYPE_BYTES,
		G_TYPE_INT, G_TYPE_INT, G_TYPE_BYTES, G_TYPE_INT, G_TYPE_INT);
	signals[SIGNAL_ICON_THEME_CHANGED] = g_signal_new(
		"icon-theme-changed", G_TYPE_FROM_CLASS(klass),
		G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
polo_backend_shell_ui_init(PoloBackendShellUi *self)
{
}
