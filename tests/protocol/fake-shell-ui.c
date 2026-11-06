/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A stand-in for polo-shell-ui that Polo starts in tests. It appends a
 * line per event to $FAKE_SHELL_UI_LOG, answers the window menu with Move
 * to workspace 1 (or the "op workspace" in $FAKE_SHELL_UI_MENU_FILE), dialogs
 * with response -5 "yes" (or $FAKE_SHELL_UI_DIALOG_RESPONSE) and hide_dialog
 * with a colour table. With FAKE_SHELL_UI_EXIT it exits right after binding;
 * with FAKE_SHELL_UI_SELECT=N it picks entry N of every switcher it shows.
 * lookup_icon gets green icons for a name or desktop id in the
 * space-separated $FAKE_SHELL_UI_ICON, red ones as the fallback.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>
#include "polo-shell-ui-v1-client-protocol.h"

static struct polo_shell_ui_v1 *shell_ui;

static void
record(const char *fmt, ...)
{
	const char *path = getenv("FAKE_SHELL_UI_LOG");
	char line[512];
	va_list args;
	FILE *log;

	if (!path)
		return;
	va_start(args, fmt);
	vsnprintf(line, sizeof line, fmt, args);
	va_end(args);
	log = fopen(path, "a");
	if (!log)
		return;
	fprintf(log, "%s\n", line);
	fclose(log);
}

static const char *
output_name(struct wl_output *output)
{
	return output ? "output" : "none";
}

static void
switcher_entry(void *data, struct polo_shell_ui_v1 *ui, uint32_t id,
	       const char *title, const char *app_id, uint32_t flags)
{
	record("switcher_entry %u %s %s %u", id, title,
	       app_id ? app_id : "(null)", flags);
}

static void
switcher_icon_name(void *data, struct polo_shell_ui_v1 *ui, uint32_t id,
		   const char *name)
{
	record("switcher_icon_name %u %s", id, name);
}

static void
switcher_icon_pixels(void *data, struct polo_shell_ui_v1 *ui, uint32_t id,
		     int32_t fd, int32_t size)
{
	uint32_t first = 0;

	if (read(fd, &first, sizeof first) != sizeof first)
		first = 0;
	record("switcher_icon_pixels %u %d %08x", id, size, first);
	close(fd);
}

static void
switcher_thumbnail(void *data, struct polo_shell_ui_v1 *ui, uint32_t id,
		   int32_t fd, int32_t width, int32_t height)
{
	record("switcher_thumbnail %u %dx%d", id, width, height);
	close(fd);
}

static void
show_switcher(void *data, struct polo_shell_ui_v1 *ui, struct wl_output *output,
	      uint32_t selected, uint32_t columns, uint32_t expand_to_fit,
	      uint32_t icon_size)
{
	record("show_switcher %s %u %u %u %u", output_name(output), selected,
	       columns, expand_to_fit, icon_size);
	if (getenv("FAKE_SHELL_UI_SELECT"))
		polo_shell_ui_v1_switcher_select(
			ui, (uint32_t)atoi(getenv("FAKE_SHELL_UI_SELECT")));
}

static void
update_selected(void *data, struct polo_shell_ui_v1 *ui, uint32_t selected)
{
	record("update_selected %u", selected);
}

static void
hide_switcher(void *data, struct polo_shell_ui_v1 *ui)
{
	record("hide_switcher");
}

static void
workspace_switcher_entry(void *data, struct polo_shell_ui_v1 *ui, uint32_t id,
			 const char *title, uint32_t blank)
{
	record("workspace_switcher_entry %u %s %u", id, title, blank);
}

static void
workspace_switcher_window(void *data, struct polo_shell_ui_v1 *ui, uint32_t id,
			  int32_t x, int32_t y, int32_t width, int32_t height,
			  uint32_t focused, const char *app_id)
{
	record("workspace_switcher_window %u %dx%d %u", id, width, height,
	       focused);
}

static void
show_workspace_switcher(void *data, struct polo_shell_ui_v1 *ui,
			struct wl_output *output, uint32_t selected,
			uint32_t columns, uint32_t active, int32_t screen_width,
			int32_t screen_height, int32_t output_width,
			int32_t output_height, uint32_t icon_size)
{
	record("show_workspace_switcher %s %u %u %u %dx%d", output_name(output),
	       selected, columns, active, screen_width, screen_height);
}

static void
update_workspace_switcher(void *data, struct polo_shell_ui_v1 *ui,
			  uint32_t selected, uint32_t active)
{
	record("update_workspace_switcher %u %u", selected, active);
}

static void
window_menu_workspace(void *data, struct polo_shell_ui_v1 *ui, uint32_t index,
		      const char *name)
{
	record("window_menu_workspace %u %s", index, name);
}

static void
show_window_menu(void *data, struct polo_shell_ui_v1 *ui, uint32_t window_id,
		 struct wl_output *output, int32_t x, int32_t y, uint32_t ops,
		 uint32_t insensitive, uint32_t active_workspace,
		 uint32_t n_workspaces, uint32_t time)
{
	record("show_window_menu %u %s %d %d %#x %#x %u %u", window_id,
	       output_name(output), x, y, ops, insensitive, active_workspace,
	       n_workspaces);
	uint32_t op = POLO_SHELL_UI_V1_MENU_OP_WORKSPACES;
	int workspace = 1;
	const char *answer = getenv("FAKE_SHELL_UI_MENU_FILE");
	FILE *file = answer ? fopen(answer, "r") : NULL;

	if (file) {
		if (fscanf(file, "%u %d", &op, &workspace) != 2)
			op = 0;
		fclose(file);
	}
	polo_shell_ui_v1_menu_action(ui, window_id, op, workspace);
}

static void
hide_window_menu(void *data, struct polo_shell_ui_v1 *ui, uint32_t window_id)
{
	record("hide_window_menu %u", window_id);
}

static void
show_osd(void *data, struct polo_shell_ui_v1 *ui, uint32_t kind,
	 struct wl_output *output, const char *icon, const char *text,
	 int32_t value)
{
	record("show_osd %u %s %s %s %d", kind, output_name(output),
	       icon ? icon : "(null)", text ? text : "(null)", value);
}

static void
show_dialog(void *data, struct polo_shell_ui_v1 *ui, uint32_t dialog_id,
	    uint32_t kind, struct wl_output *output, const char *payload)
{
	record("show_dialog %u %u %s %s", dialog_id, kind, output_name(output),
	       payload ? payload : "(null)");
	const char *response = getenv("FAKE_SHELL_UI_DIALOG_RESPONSE");

	polo_shell_ui_v1_dialog_response(ui, dialog_id,
					 response ? atoi(response) : -5, "yes");
}

static void
hide_dialog(void *data, struct polo_shell_ui_v1 *ui, uint32_t dialog_id)
{
	struct wl_array colors;
	float *values;

	record("hide_dialog %u", dialog_id);
	wl_array_init(&colors);
	values = wl_array_add(&colors, 160 * sizeof(float));
	for (int i = 0; i < 160; i++)
		values[i] = 0.5f;
	values[0] = 0.25f;
	values[159] = 0.75f;
	polo_shell_ui_v1_color_table(ui, &colors);
	wl_array_release(&colors);
}

static void
lookup_color(void *data, struct polo_shell_ui_v1 *ui, const char *name)
{
	struct wl_array empty;

	wl_array_init(&empty);
	polo_shell_ui_v1_custom_color(ui, name, &empty);
	wl_array_release(&empty);
}

static int
known_icon(const char *name)
{
	const char *known = getenv("FAKE_SHELL_UI_ICON");
	size_t length = name ? strlen(name) : 0;

	for (const char *p = known; p && length && *p;) {
		const char *end = strchr(p, ' ');
		size_t word = end ? (size_t)(end - p) : strlen(p);

		if (word == length && strncmp(p, name, length) == 0)
			return 1;
		p += word + (end ? 1 : 0);
	}
	return 0;
}

static void
lookup_icon(void *data, struct polo_shell_ui_v1 *ui, uint32_t serial,
	    const char *name, const char *desktop_id, int32_t mini_size,
	    int32_t size, int32_t scale, uint32_t fallback)
{
	int found = known_icon(name) || known_icon(desktop_id);
	int mini = mini_size * scale, full = size * scale;
	size_t bytes = ((size_t)mini * mini + (size_t)full * full) * 4;
	uint32_t *pixels;
	int fd;

	record("lookup_icon %s %s %d %d %d %u", name ? name : "(null)",
	       desktop_id ? desktop_id : "(null)", mini_size, size, scale,
	       fallback);
	if (!found && !fallback) {
		polo_shell_ui_v1_no_icon(ui, serial);
		return;
	}
	fd = memfd_create("fake-icon", MFD_CLOEXEC);
	if (fd < 0 || ftruncate(fd, (off_t)bytes) != 0 ||
	    (pixels = mmap(NULL, bytes, PROT_WRITE, MAP_SHARED, fd, 0)) ==
		    MAP_FAILED) {
		if (fd >= 0)
			close(fd);
		polo_shell_ui_v1_no_icon(ui, serial);
		return;
	}
	for (size_t i = 0; i < bytes / 4; i++)
		pixels[i] = found ? 0xff00ff00 : 0xffff0000;
	munmap(pixels, bytes);
	polo_shell_ui_v1_icon(ui, serial, fd, mini, mini, full, full);
	close(fd);
}

static const struct polo_shell_ui_v1_listener shell_ui_listener = {
	.switcher_entry = switcher_entry,
	.switcher_thumbnail = switcher_thumbnail,
	.switcher_icon_name = switcher_icon_name,
	.switcher_icon_pixels = switcher_icon_pixels,
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

static void
registry_global(void *data, struct wl_registry *registry, uint32_t name,
		const char *interface, uint32_t version)
{
	if (strcmp(interface, polo_shell_ui_v1_interface.name) == 0) {
		shell_ui = wl_registry_bind(registry, name,
					    &polo_shell_ui_v1_interface, 1);
		polo_shell_ui_v1_add_listener(shell_ui, &shell_ui_listener,
					      NULL);
	} else if (strcmp(interface, wl_output_interface.name) == 0) {
		wl_registry_bind(registry, name, &wl_output_interface, 1);
	}
}

static void
registry_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = {
	registry_global,
	registry_global_remove,
};

int
main(void)
{
	struct wl_display *display = wl_display_connect(NULL);
	struct wl_registry *registry;

	record("start %d", (int)getpid());
	if (!display) {
		record("no display");
		return 1;
	}
	registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, NULL);
	wl_display_roundtrip(display);
	if (!shell_ui) {
		record("missing");
		return 1;
	}

	wl_display_roundtrip(display);
	record("bound");
	if (getenv("FAKE_SHELL_UI_EXIT"))
		return 3;

	while (wl_display_dispatch(display) != -1)
		;
	record("disconnected");
	return 0;
}
