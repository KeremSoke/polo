/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * mate_input_device_manager_v1 on fake devices (polo_test_v1
 * add_input_device): the list at bind ending in done, every field, the kind
 * rules, twin mice told apart by their physical path, a touchscreen mapped
 * through WL_OUTPUT or to the first output, a device added later, removal,
 * and stop.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness.h"
#include "mate-input-device-v1-client-protocol.h"

enum {
	CAP_KEYBOARD = 1,
	CAP_POINTER = 2,
	CAP_TOUCH = 4,
	CAP_GESTURE = 32,
	BTN_L = 1,
	BTN_R = 2,
	BTN_M = 4,
	TAG_TOUCHPAD = 1,
	TAG_STICK = 2,
};

static const int modes[][3] = { { 1024, 768, 60000 } };

struct device {
	struct mate_input_device_v1 *proxy;
	char name[256], id[256], output[256];
	uint32_t vendor, product, kind, capabilities, buttons, config;
	uint32_t tap_fingers, accel, scroll, click, send_events;
	int dones, removed;
};

struct client {
	struct wl_display *display;
	struct mate_input_device_manager_v1 *manager;
	struct device devices[32];
	int n_devices, dones, finished;
};

static struct device *
dev(void *data, struct mate_input_device_v1 *proxy)
{
	struct client *c = data;

	for (int i = 0; i < c->n_devices; i++) {
		if (c->devices[i].proxy == proxy)
			return &c->devices[i];
	}
	return NULL;
}

static void
d_name(void *data, struct mate_input_device_v1 *p, const char *name)
{
	snprintf(dev(data, p)->name, 256, "%s", name);
}

static void
d_id(void *data, struct mate_input_device_v1 *p, const char *id)
{
	snprintf(dev(data, p)->id, 256, "%s", id);
}

static void
d_usb(void *data, struct mate_input_device_v1 *p, uint32_t v, uint32_t pr)
{
	dev(data, p)->vendor = v;
	dev(data, p)->product = pr;
}

#define SETTER(field)                                                          \
	static void d_##field(void *data, struct mate_input_device_v1 *p,      \
			      uint32_t value)                                  \
	{                                                                      \
		dev(data, p)->field = value;                                   \
	}
SETTER(kind)
SETTER(capabilities)
SETTER(buttons)
SETTER(config)
SETTER(tap_fingers)
SETTER(accel)
SETTER(scroll)
SETTER(click)
SETTER(send_events)

static void
d_output(void *data, struct mate_input_device_v1 *p, const char *output)
{
	snprintf(dev(data, p)->output, 256, "%s", output);
}

static void
d_done(void *data, struct mate_input_device_v1 *p)
{
	dev(data, p)->dones++;
}

static void
d_removed(void *data, struct mate_input_device_v1 *p)
{
	dev(data, p)->removed = 1;
}

static const struct mate_input_device_v1_listener device_listener = {
	d_name,    d_id,          d_usb,         d_kind,  d_capabilities,
	d_buttons, d_config,      d_tap_fingers, d_accel, d_scroll,
	d_click,   d_send_events, d_output,      d_done,  d_removed,
};

static void
m_device(void *data, struct mate_input_device_manager_v1 *m,
	 struct mate_input_device_v1 *proxy)
{
	struct client *c = data;
	struct device *d = &c->devices[c->n_devices++];

	memset(d, 0, sizeof *d);
	d->proxy = proxy;
	mate_input_device_v1_add_listener(proxy, &device_listener, c);
}

static void
m_done(void *data, struct mate_input_device_manager_v1 *m)
{
	((struct client *)data)->dones++;
}

static void
m_finished(void *data, struct mate_input_device_manager_v1 *m)
{
	((struct client *)data)->finished = 1;
}

static const struct mate_input_device_manager_v1_listener manager_listener = {
	m_device, m_done, m_finished
};

static void
global(void *data, struct wl_registry *registry, uint32_t name,
       const char *interface, uint32_t version)
{
	struct client *c = data;

	if (strcmp(interface, mate_input_device_manager_v1_interface.name) ==
	    0) {
		c->manager = wl_registry_bind(
			registry, name, &mate_input_device_manager_v1_interface,
			1);
		mate_input_device_manager_v1_add_listener(c->manager,
							  &manager_listener, c);
	}
}

static void
global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = { global,
							       global_remove };

static struct client *
connect_client(struct harness *h)
{
	struct client *c = calloc(1, sizeof *c);
	struct wl_registry *registry;

	c->display = wl_display_connect(h->socket);
	CHECK(c->display, "cannot connect");
	registry = wl_display_get_registry(c->display);
	wl_registry_add_listener(registry, &registry_listener, c);
	wl_display_roundtrip(c->display);
	CHECK(c->manager, "no mate_input_device_manager_v1");
	wl_display_roundtrip(c->display);
	wl_registry_destroy(registry);
	return c;
}

static void
disconnect_client(struct client *c)
{
	for (int i = 0; i < c->n_devices; i++)
		mate_input_device_v1_destroy(c->devices[i].proxy);
	mate_input_device_manager_v1_destroy(c->manager);
	wl_display_disconnect(c->display);
	free(c);
}

static void
settle(struct harness *h, struct client *c)
{
	for (int i = 0; i < 3; i++) {
		harness_roundtrip(h);
		wl_display_roundtrip(c->display);
	}
}

static struct device *
named(struct client *c, const char *name, int nth)
{
	for (int i = 0; i < c->n_devices; i++) {
		if (strcmp(c->devices[i].name, name) == 0 && nth-- == 0)
			return &c->devices[i];
	}
	CHECK(false, "no device %s", name);
	return NULL;
}

static void
add(struct harness *h, const char *name, uint32_t vendor, uint32_t product,
    uint32_t tags, uint32_t caps, uint32_t buttons, uint32_t config,
    uint32_t tap, const char *phys, const char *output)
{
	polo_test_v1_add_input_device(h->test, name, vendor, product, tags,
				      caps, buttons, config, tap, 3, 1, 2, 1,
				      phys, output);
	harness_roundtrip(h);
}

int
main(int argc, char **argv)
{
	setenv("POLO_TEST_MANAGED_OUTPUTS", "1", 1);
	struct harness *h = harness_start(argc, argv);

	harness_add_output(h, "DP-1", "QQA", 0x0101, 11, "Touch", false, 340,
			   270, modes, 1);
	add(h, "Clickpad", 0x06cb, 0xcdab, TAG_TOUCHPAD,
	    CAP_POINTER | CAP_GESTURE, BTN_L | BTN_R, 1 | 8 | 32 | 64 | 256, 3,
	    "pci-0000:00:15.0", "");
	add(h, "Tap pad", 0x04f3, 0x3001, 0, CAP_POINTER, 0, 1, 2, "i2c-1", "");
	add(h, "Mouse", 0x046d, 0xc077, 0, CAP_POINTER, BTN_L | BTN_R | BTN_M,
	    64 | 128 | 256, 0, "usb-1", "");
	add(h, "Mouse", 0x046d, 0xc077, 0, CAP_POINTER, BTN_L | BTN_R | BTN_M,
	    64 | 128 | 256, 0, "usb-2", "");
	add(h, "TrackPoint", 0x0002, 0x000a, TAG_STICK, CAP_POINTER,
	    BTN_L | BTN_R | BTN_M, 2048, 0, "serio-1", "");
	add(h, "Keyboard: AT", 0x0001, 0x0001, 0, CAP_KEYBOARD, 0, 0, 0,
	    "serio-0", "");
	add(h, "Pinned screen", 0x0eef, 0x0001, 0, CAP_TOUCH, 0, 512, 0,
	    "usb-3", "DP-1");
	add(h, "Loose screen", 0x0eef, 0x0002, 0, CAP_TOUCH, 0, 512, 0, "usb-4",
	    "");

	struct client *c = connect_client(h);

	settle(h, c);
	CHECK(c->dones == 1, "%d manager dones after bind", c->dones);
	CHECK(c->n_devices == 8, "%d devices", c->n_devices);
	for (int i = 0; i < c->n_devices; i++)
		CHECK(c->devices[i].dones == 1, "%s: %d dones",
		      c->devices[i].name, c->devices[i].dones);

	struct device *clickpad = named(c, "Clickpad", 0);

	CHECK(clickpad->kind == MATE_INPUT_DEVICE_V1_KIND_TOUCHPAD,
	      "clickpad kind %u", clickpad->kind);
	CHECK(clickpad->buttons & MATE_INPUT_DEVICE_V1_BUTTON_LEFT,
	      "the clickpad has no left button");
	CHECK(clickpad->tap_fingers == 3 && clickpad->vendor == 0x06cb &&
		      clickpad->product == 0xcdab,
	      "clickpad fields");
	CHECK(clickpad->config == (1 | 8 | 32 | 64 | 256), "clickpad config %u",
	      clickpad->config);
	CHECK(clickpad->accel == 3 && clickpad->scroll == 1 &&
		      clickpad->click == 2 && clickpad->send_events == 1,
	      "clickpad methods");
	CHECK(strcmp(clickpad->id, "06cb:cdab:Clickpad") == 0, "clickpad id %s",
	      clickpad->id);
	CHECK(!clickpad->output[0], "the clickpad maps to %s",
	      clickpad->output);

	struct device *tap = named(c, "Tap pad", 0);

	CHECK(tap->kind == MATE_INPUT_DEVICE_V1_KIND_TOUCHPAD,
	      "a pointer with tap fingers is not a touchpad");
	CHECK(tap->buttons == 0, "the tap-only pad has buttons %u",
	      tap->buttons);

	struct device *mouse = named(c, "Mouse", 0);
	struct device *twin = named(c, "Mouse", 1);

	CHECK(mouse->kind == MATE_INPUT_DEVICE_V1_KIND_MOUSE, "mouse kind %u",
	      mouse->kind);
	CHECK(strncmp(mouse->id, "046d:c077:Mouse@", 16) == 0 &&
		      strncmp(twin->id, "046d:c077:Mouse@", 16) == 0 &&
		      strcmp(mouse->id, twin->id) != 0,
	      "twin mice: %s and %s", mouse->id, twin->id);
	CHECK(named(c, "TrackPoint", 0)->kind ==
		      MATE_INPUT_DEVICE_V1_KIND_POINTING_STICK,
	      "the TrackPoint is not a pointing stick");
	CHECK(named(c, "Keyboard: AT", 0)->kind ==
		      MATE_INPUT_DEVICE_V1_KIND_KEYBOARD,
	      "the keyboard's kind");
	CHECK(strcmp(named(c, "Keyboard: AT", 0)->id,
		     "0001:0001:Keyboard__AT") == 0,
	      "keyboard id %s", named(c, "Keyboard: AT", 0)->id);

	struct device *pinned = named(c, "Pinned screen", 0);
	struct device *loose = named(c, "Loose screen", 0);

	CHECK(pinned->kind == MATE_INPUT_DEVICE_V1_KIND_TOUCHSCREEN,
	      "touchscreen kind %u", pinned->kind);
	CHECK(strcmp(pinned->output, "PNP_QQA_-Touch") == 0,
	      "the pinned screen maps to '%s'", pinned->output);
	CHECK(loose->output[0] && strcmp(loose->output, pinned->output) != 0,
	      "the loose screen maps to '%s'", loose->output);

	add(h, "Pen", 0x056a, 0x0357, 0, 8, 0, 0, 0, "usb-5", "");
	settle(h, c);
	CHECK(c->n_devices == 9 && c->dones == 2,
	      "a new device: %d devices, %d dones", c->n_devices, c->dones);
	CHECK(named(c, "Pen", 0)->kind == MATE_INPUT_DEVICE_V1_KIND_TABLET,
	      "the pen is not a tablet");

	polo_test_v1_remove_input_device(h->test, "Mouse");
	settle(h, c);
	CHECK(mouse->removed + twin->removed == 1,
	      "removal: first %d, second %d", mouse->removed, twin->removed);

	mate_input_device_manager_v1_stop(c->manager);
	settle(h, c);
	CHECK(c->finished, "no finished after stop");
	add(h, "Late", 0x1234, 0x5678, 0, CAP_KEYBOARD, 0, 0, 0, "usb-6", "");
	settle(h, c);
	CHECK(c->n_devices == 9, "a device arrived after stop");
	disconnect_client(c);

	CHECK(harness_stop(h) == 0, "polo did not stop cleanly");
	return 0;
}
