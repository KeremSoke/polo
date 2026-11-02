/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A text input client and an input method on two connections, through Polo's
 * relay: state reaches the method, text reaches the client, keys go through the
 * grab after Polo's shortcuts, and the method follows keyboard focus.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <linux/input.h>
#include <sys/mman.h>

#include "harness.h"
#include "input-method-unstable-v2-client-protocol.h"
#include "text-input-unstable-v3-client-protocol.h"
#include "virtual-keyboard-unstable-v1-client-protocol.h"

/* Connections die on purpose here with their proxies live. Polo is its own
 * process and is still checked. */
int
__lsan_is_turned_off(void);

int
__lsan_is_turned_off(void)
{
	return 1;
}

struct text_client {
	bool entered;
	int enters, leaves, dones;
	uint32_t last_serial;
	char preedit[64];
	char commit[64];
	uint32_t deleted_before, deleted_after;
};

struct method {
	struct wl_display *display;
	struct wl_seat *seat;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct xdg_wm_base *wm_base;
	struct zwp_input_method_manager_v2 *manager;
	struct zwp_virtual_keyboard_manager_v1 *vk_manager;
	struct zwp_input_method_v2 *method;
	bool active, unavailable;
	int activations, deactivations;
	uint32_t dones;
	char surrounding[64];
	uint32_t purpose;
	bool got_keymap;
	int keys;
	uint32_t last_key;
	int modifiers;
	int popup_enters, popup_leaves;
	int rect_x, rect_y, rect_w, rect_h, rects;
};

static struct text_client text;

static void
text_enter(void *data, struct zwp_text_input_v3 *ti, struct wl_surface *surface)
{
	text.entered = true;
	text.enters++;
}

static void
text_leave(void *data, struct zwp_text_input_v3 *ti, struct wl_surface *surface)
{
	text.entered = false;
	text.leaves++;
}

static void
text_preedit(void *data, struct zwp_text_input_v3 *ti, const char *s, int32_t b,
	     int32_t e)
{
	snprintf(text.preedit, sizeof text.preedit, "%s", s ? s : "");
}

static void
text_commit(void *data, struct zwp_text_input_v3 *ti, const char *s)
{
	snprintf(text.commit, sizeof text.commit, "%s", s ? s : "");
}

static void
text_delete(void *data, struct zwp_text_input_v3 *ti, uint32_t b, uint32_t a)
{
	text.deleted_before = b;
	text.deleted_after = a;
}

static void
text_done(void *data, struct zwp_text_input_v3 *ti, uint32_t serial)
{
	text.dones++;
	text.last_serial = serial;
}

static const struct zwp_text_input_v3_listener text_listener = {
	.enter = text_enter,
	.leave = text_leave,
	.preedit_string = text_preedit,
	.commit_string = text_commit,
	.delete_surrounding_text = text_delete,
	.done = text_done,
};

static void
method_activate(void *data, struct zwp_input_method_v2 *m)
{
	struct method *im = data;

	im->active = true;
	im->activations++;
}

static void
method_deactivate(void *data, struct zwp_input_method_v2 *m)
{
	struct method *im = data;

	im->active = false;
	im->deactivations++;
}

static void
method_surrounding(void *data, struct zwp_input_method_v2 *m, const char *s,
		   uint32_t cursor, uint32_t anchor)
{
	snprintf(((struct method *)data)->surrounding, 64, "%s", s);
}

static void
method_cause(void *data, struct zwp_input_method_v2 *m, uint32_t cause)
{
}

static void
method_content(void *data, struct zwp_input_method_v2 *m, uint32_t hint,
	       uint32_t purpose)
{
	((struct method *)data)->purpose = purpose;
}

static void
method_done(void *data, struct zwp_input_method_v2 *m)
{
	((struct method *)data)->dones++;
}

static void
method_unavailable(void *data, struct zwp_input_method_v2 *m)
{
	((struct method *)data)->unavailable = true;
}

static const struct zwp_input_method_v2_listener method_listener = {
	method_activate, method_deactivate, method_surrounding, method_cause,
	method_content,	 method_done,	    method_unavailable,
};

static void
grab_keymap(void *data, struct zwp_input_method_keyboard_grab_v2 *g,
	    uint32_t format, int fd, uint32_t size)
{
	((struct method *)data)->got_keymap = size > 0;
	close(fd);
}

static void
grab_key(void *data, struct zwp_input_method_keyboard_grab_v2 *g, uint32_t serial,
	 uint32_t time, uint32_t key, uint32_t state)
{
	struct method *im = data;

	im->keys++;
	im->last_key = key;
}

static void
grab_modifiers(void *data, struct zwp_input_method_keyboard_grab_v2 *g,
	       uint32_t serial, uint32_t d, uint32_t la, uint32_t lo, uint32_t gr)
{
	((struct method *)data)->modifiers++;
}

static void
grab_repeat(void *data, struct zwp_input_method_keyboard_grab_v2 *g, int32_t r,
	    int32_t d)
{
}

static const struct zwp_input_method_keyboard_grab_v2_listener grab_listener = {
	grab_keymap,
	grab_key,
	grab_modifiers,
	grab_repeat,
};

static void
popup_rect(void *data, struct zwp_input_popup_surface_v2 *p, int32_t x, int32_t y,
	   int32_t w, int32_t h)
{
	struct method *im = data;

	im->rect_x = x;
	im->rect_y = y;
	im->rect_w = w;
	im->rect_h = h;
	im->rects++;
}

static const struct zwp_input_popup_surface_v2_listener popup_listener = {
	popup_rect,
};

static void
popup_surface_enter(void *data, struct wl_surface *s, struct wl_output *o)
{
	((struct method *)data)->popup_enters++;
}

static void
popup_surface_leave(void *data, struct wl_surface *s, struct wl_output *o)
{
	((struct method *)data)->popup_leaves++;
}

static const struct wl_surface_listener popup_surface_listener = {
	.enter = popup_surface_enter,
	.leave = popup_surface_leave,
};

static void
global(void *data, struct wl_registry *registry, uint32_t name,
       const char *interface, uint32_t version)
{
	struct method *im = data;

	if (strcmp(interface, wl_compositor_interface.name) == 0)
		im->compositor = wl_registry_bind(registry, name,
						  &wl_compositor_interface, 4);
	else if (strcmp(interface, wl_shm_interface.name) == 0)
		im->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	else if (strcmp(interface, xdg_wm_base_interface.name) == 0)
		im->wm_base = wl_registry_bind(registry, name,
					       &xdg_wm_base_interface, 1);
	else if (strcmp(interface, wl_output_interface.name) == 0)
		wl_registry_bind(registry, name, &wl_output_interface, 1);
	else if (strcmp(interface, wl_seat_interface.name) == 0 && !im->seat)
		im->seat = wl_registry_bind(registry, name, &wl_seat_interface, 1);
	else if (strcmp(interface, zwp_input_method_manager_v2_interface.name) == 0)
		im->manager = wl_registry_bind(
			registry, name, &zwp_input_method_manager_v2_interface, 1);
	else if (strcmp(interface,
			zwp_virtual_keyboard_manager_v1_interface.name) == 0)
		im->vk_manager = wl_registry_bind(
			registry, name, &zwp_virtual_keyboard_manager_v1_interface, 1);
}

static void
global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = { global,
							       global_remove };

static struct zwp_text_input_manager_v3 *text_manager;

static void
main_global(void *data, struct wl_registry *registry, uint32_t name,
	    const char *interface, uint32_t version)
{
	if (strcmp(interface, zwp_text_input_manager_v3_interface.name) == 0)
		text_manager = wl_registry_bind(
			registry, name, &zwp_text_input_manager_v3_interface, 1);
}

static const struct wl_registry_listener main_registry_listener = { main_global,
								    global_remove };

static void
method_connect(struct method *im, struct harness *h)
{
	memset(im, 0, sizeof *im);
	im->display = wl_display_connect(h->socket);
	CHECK(im->display != NULL, "cannot connect");
	wl_registry_add_listener(wl_display_get_registry(im->display),
				 &registry_listener, im);
	wl_display_roundtrip(im->display);
	CHECK(im->manager && im->vk_manager, "no input method globals");
	im->method = zwp_input_method_manager_v2_get_input_method(im->manager,
								  im->seat);
	zwp_input_method_v2_add_listener(im->method, &method_listener, im);
	wl_display_roundtrip(im->display);
}

static void
sync_all(struct harness *h, struct method *im)
{
	for (int i = 0; i < 2; i++) {
		harness_roundtrip(h);
		wl_display_roundtrip(im->display);
	}
}

static void
wait_popup(struct harness *h, struct method *im, int enters, int leaves)
{
	for (int i = 0; i < 100; i++) {
		if (im->popup_enters == enters && im->popup_leaves == leaves)
			return;
		usleep(20000);
		sync_all(h, im);
	}
}

static void
tap(struct harness *h, uint32_t key)
{
	harness_key(h, key, true);
	harness_key(h, key, false);
}

static uint32_t peer_serial;

static void
peer_configure(void *data, struct xdg_surface *s, uint32_t serial)
{
	peer_serial = serial;
}

static const struct xdg_surface_listener peer_listener = { peer_configure };

static struct zwp_virtual_keyboard_v1 *
virtual_keyboard(struct method *im)
{
	struct zwp_virtual_keyboard_v1 *vk =
		zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(
			im->vk_manager, im->seat);
	int fd = memfd_create("keymap", 0);

	CHECK(fd >= 0 && write(fd, "x", 1) == 1, "keymap file");
	zwp_virtual_keyboard_v1_keymap(vk, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd, 1);
	close(fd);
	return vk;
}

int
main(int argc, char **argv)
{
	struct harness *h = harness_start(argc, argv);
	struct method im, second;

	wl_registry_add_listener(wl_display_get_registry(h->display),
				 &main_registry_listener, NULL);
	harness_roundtrip(h);
	CHECK(text_manager, "no zwp_text_input_manager_v3");
	struct harness_window *w = harness_window_create(h, 300, 200, "text");

	struct zwp_text_input_v3 *ti =
		zwp_text_input_manager_v3_get_text_input(text_manager, h->seat);

	zwp_text_input_v3_add_listener(ti, &text_listener, NULL);
	harness_roundtrip(h);
	CHECK(text.entered, "a text input on the focused client was not entered");

	method_connect(&im, h);
	CHECK(!im.unavailable, "the first input method was refused");
	method_connect(&second, h);
	CHECK(second.unavailable, "a second input method on the seat was accepted");
	CHECK(!second.active, "the second input method was activated");
	wl_display_disconnect(second.display);

	/* Enable: the method hears the client's state. */
	uint32_t commits = 0;

	CHECK(!im.active, "active before anything was enabled");
	zwp_text_input_v3_enable(ti);
	zwp_text_input_v3_set_surrounding_text(ti, "before", 6, 6);
	zwp_text_input_v3_set_text_change_cause(
		ti, ZWP_TEXT_INPUT_V3_CHANGE_CAUSE_OTHER);
	zwp_text_input_v3_set_content_type(ti, 0,
					   ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_EMAIL);
	zwp_text_input_v3_set_cursor_rectangle(ti, 20, 30, 2, 16);
	zwp_text_input_v3_commit(ti);
	commits++;
	sync_all(h, &im);
	CHECK(im.active && im.activations == 1, "the method was not activated");
	CHECK(strcmp(im.surrounding, "before") == 0, "surrounding: %s",
	      im.surrounding);
	CHECK(im.purpose == ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_EMAIL, "purpose %u",
	      im.purpose);
	CHECK(im.dones == 1, "%u done events", im.dones);

	/* Text from the method, atomically on its commit. */
	zwp_input_method_v2_set_preedit_string(im.method, "pre", 0, 3);
	zwp_input_method_v2_commit_string(im.method, "done");
	zwp_input_method_v2_delete_surrounding_text(im.method, 2, 1);
	wl_display_roundtrip(im.display);
	harness_roundtrip(h);
	CHECK(text.dones == 0, "text arrived before the method's commit");
	zwp_input_method_v2_commit(im.method, im.dones);
	sync_all(h, &im);
	CHECK(strcmp(text.preedit, "pre") == 0, "preedit: %s", text.preedit);
	CHECK(strcmp(text.commit, "done") == 0, "commit: %s", text.commit);
	CHECK(text.deleted_before == 2 && text.deleted_after == 1, "delete %u %u",
	      text.deleted_before, text.deleted_after);
	CHECK(text.dones == 1 && text.last_serial == commits, "done %d serial %u",
	      text.dones, text.last_serial);

	/* A later commit carries a new state and counts in the serial. */
	zwp_text_input_v3_set_surrounding_text(ti, "after", 5, 5);
	zwp_text_input_v3_set_text_change_cause(
		ti, ZWP_TEXT_INPUT_V3_CHANGE_CAUSE_INPUT_METHOD);
	zwp_text_input_v3_commit(ti);
	commits++;
	sync_all(h, &im);
	CHECK(strcmp(im.surrounding, "after") == 0, "surrounding: %s",
	      im.surrounding);
	CHECK(im.activations == 1, "activated again by a plain commit");
	zwp_input_method_v2_commit_string(im.method, "x");
	zwp_input_method_v2_commit(im.method, im.dones);
	sync_all(h, &im);
	CHECK(text.last_serial == commits, "serial %u of %u", text.last_serial,
	      commits);

	/* A key held when the grab starts: its release stays with the client. */
	harness_key(h, KEY_B, true);
	harness_roundtrip(h);
	CHECK(h->last_key == KEY_B && h->last_key_pressed, "client missed the press");

	struct zwp_input_method_keyboard_grab_v2 *grab =
		zwp_input_method_v2_grab_keyboard(im.method);

	zwp_input_method_keyboard_grab_v2_add_listener(grab, &grab_listener, &im);
	sync_all(h, &im);
	CHECK(im.got_keymap, "the grab got no keymap");
	CHECK(im.modifiers >= 1, "the grab got no modifiers");
	harness_key(h, KEY_B, false);
	sync_all(h, &im);
	CHECK(im.keys == 0, "the method got the release of a key held before");
	CHECK(h->last_key == KEY_B && !h->last_key_pressed,
	      "the client never got the release");

	/* Keys go to the method and not the client. */
	h->last_key = 0;
	tap(h, KEY_A);
	sync_all(h, &im);
	CHECK(im.keys == 2 && im.last_key == KEY_A, "%d keys to the method", im.keys);
	CHECK(h->last_key == 0, "the client got a grabbed key");

	/* Polo's shortcuts come first. */
	im.keys = 0;
	CHECK(!w->xdg_maximized, "maximized already");
	harness_key(h, KEY_LEFTALT, true);
	tap(h, KEY_F10);
	harness_key(h, KEY_LEFTALT, false);
	sync_all(h, &im);
	CHECK(w->xdg_maximized, "Alt+F10 did not reach Polo during the grab");
	CHECK(im.last_key != KEY_F10, "the method got a shortcut");
	harness_key(h, KEY_LEFTALT, true);
	tap(h, KEY_F10);
	harness_key(h, KEY_LEFTALT, false);
	sync_all(h, &im);

	/* A virtual keyboard sends back what the method did not want. */
	struct zwp_virtual_keyboard_v1 *vk = virtual_keyboard(&im);

	im.keys = 0;
	h->last_key = 0;
	zwp_virtual_keyboard_v1_key(vk, 0, KEY_C, WL_KEYBOARD_KEY_STATE_PRESSED);
	sync_all(h, &im);
	CHECK(h->last_key == KEY_C, "the virtual key did not reach the client");
	CHECK(im.keys == 0, "the virtual key came back to the method");
	zwp_virtual_keyboard_v1_key(vk, 0, KEY_C, WL_KEYBOARD_KEY_STATE_RELEASED);
	sync_all(h, &im);

	/* Popup: shown only while active, next to the cursor rectangle. */
	struct wl_surface *popup_surface = wl_compositor_create_surface(im.compositor);

	wl_surface_add_listener(popup_surface, &popup_surface_listener, &im);
	struct zwp_input_popup_surface_v2 *popup =
		zwp_input_method_v2_get_input_popup_surface(im.method, popup_surface);

	zwp_input_popup_surface_v2_add_listener(popup, &popup_listener, &im);
	wl_surface_attach(popup_surface, harness_buffer_create(im.shm, 100, 40), 0, 0);
	wl_surface_damage(popup_surface, 0, 0, 100, 40);
	wl_surface_commit(popup_surface);
	wait_popup(h, &im, 1, 0);
	CHECK(im.popup_enters == 1, "the popup is not on an output");
	CHECK(im.rects >= 1 && im.rect_x == 0 && im.rect_y == -16 &&
		      im.rect_w == 2 && im.rect_h == 16,
	      "rectangle %d,%d %dx%d", im.rect_x, im.rect_y, im.rect_w, im.rect_h);

	/* Near the bottom of the output it opens above the cursor. */
	const struct harness_window_info *info = harness_window_info(w);
	int below = HARNESS_OUTPUT_HEIGHT - info->y - 10;

	zwp_text_input_v3_set_surrounding_text(ti, "after", 5, 5);
	zwp_text_input_v3_set_cursor_rectangle(ti, 20, below, 2, 16);
	zwp_text_input_v3_commit(ti);
	commits++;
	sync_all(h, &im);
	CHECK(im.rect_y == 40, "near the bottom the rectangle is at %d", im.rect_y);

	/* Focus leaves: leave, deactivate, popup gone. */
	struct wl_surface *other = wl_compositor_create_surface(im.compositor);
	struct xdg_surface *xdg = xdg_wm_base_get_xdg_surface(im.wm_base, other);

	xdg_surface_add_listener(xdg, &peer_listener, NULL);
	struct xdg_toplevel *other_top = xdg_surface_get_toplevel(xdg);

	wl_surface_commit(other);
	wl_display_roundtrip(im.display);
	xdg_surface_ack_configure(xdg, peer_serial);
	wl_surface_attach(other, harness_buffer_create(im.shm, 200, 150), 0, 0);
	wl_surface_commit(other);
	sync_all(h, &im);
	CHECK(!text.entered && text.leaves == 1, "no leave when focus moved");
	CHECK(!im.active && im.deactivations == 1,
	      "the method stayed active when focus left");
	wait_popup(h, &im, 1, 1);
	CHECK(im.popup_leaves == 1, "the popup stayed on screen");
	im.keys = 0;
	tap(h, KEY_D);
	sync_all(h, &im);
	CHECK(im.keys == 0, "an inactive method got keys");

	/* Focus comes back: the client must enable again to activate it. */
	xdg_toplevel_destroy(other_top);
	xdg_surface_destroy(xdg);
	wl_surface_destroy(other);
	sync_all(h, &im);
	sync_all(h, &im);
	CHECK(text.entered && text.enters == 2, "no second enter");
	CHECK(im.active, "focus coming back did not reactivate the method");
	wait_popup(h, &im, 2, 1);
	CHECK(im.popup_enters == 2, "the popup did not come back");

	/* Disabling deactivates. */
	zwp_text_input_v3_disable(ti);
	zwp_text_input_v3_commit(ti);
	commits++;
	sync_all(h, &im);
	CHECK(!im.active && im.deactivations == 2, "disable did not deactivate");

	zwp_text_input_v3_enable(ti);
	zwp_text_input_v3_commit(ti);
	commits++;
	sync_all(h, &im);
	CHECK(im.active, "enable after disable did nothing");
	zwp_input_method_v2_set_preedit_string(im.method, "live", 0, 4);
	zwp_input_method_v2_commit(im.method, im.dones);
	sync_all(h, &im);
	CHECK(strcmp(text.preedit, "live") == 0, "preedit: %s", text.preedit);

	/* The method dying leaves no stale preedit and frees the seat. */
	wl_display_disconnect(im.display);
	for (int i = 0; i < 20 && text.preedit[0]; i++)
		harness_roundtrip(h);
	CHECK(text.preedit[0] == '\0', "preedit outlived the method");
	struct method next;

	method_connect(&next, h);
	CHECK(!next.unavailable, "the seat stayed taken after the method died");
	sync_all(h, &next);
	CHECK(next.active, "a new method was not activated for the enabled input");

	/* A virtual keyboard must have a keymap first. */
	struct zwp_virtual_keyboard_v1 *bad =
		zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(
			next.vk_manager, next.seat);

	zwp_virtual_keyboard_v1_key(bad, 0, KEY_E, WL_KEYBOARD_KEY_STATE_PRESSED);
	for (int i = 0; i < 10 && !wl_display_get_error(next.display); i++)
		wl_display_roundtrip(next.display);
	CHECK(wl_display_get_error(next.display) == EPROTO,
	      "no error for a key before a keymap");
	wl_display_disconnect(next.display);

	zwp_text_input_v3_destroy(ti);
	harness_roundtrip(h);
	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
