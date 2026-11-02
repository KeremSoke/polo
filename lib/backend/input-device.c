/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * libinput devices handed to configure_device by the DRM backend. They stay
 * alive so settings changes can reach them and are released on removal.
 */

#include <libinput.h>

#include <libweston/libweston.h>

#include "polo-backend-private.h"

struct _PoloBackendInputDevice {
	GObject parent_instance;

	struct libinput_device *device;
};

enum {
	SIGNAL_REMOVED,
	N_SIGNALS,
};

static guint signals[N_SIGNALS];

G_DEFINE_FINAL_TYPE(PoloBackendInputDevice, polo_backend_input_device,
		    G_TYPE_OBJECT)

static void
polo_backend_input_device_finalize(GObject *object)
{
	PoloBackendInputDevice *self = POLO_BACKEND_INPUT_DEVICE(object);

	g_clear_pointer(&self->device, libinput_device_unref);
	G_OBJECT_CLASS(polo_backend_input_device_parent_class)
		->finalize(object);
}

static void
polo_backend_input_device_class_init(PoloBackendInputDeviceClass *klass)
{
	G_OBJECT_CLASS(klass)->finalize = polo_backend_input_device_finalize;

	signals[SIGNAL_REMOVED] = g_signal_new(
		"removed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL,
		NULL, NULL, G_TYPE_NONE, 0);
}

static void
polo_backend_input_device_init(PoloBackendInputDevice *self)
{
}

PoloBackendInputDevice *
polo_backend_input_device_new(struct libinput_device *device)
{
	PoloBackendInputDevice *self =
		g_object_new(POLO_BACKEND_TYPE_INPUT_DEVICE, NULL);

	self->device = libinput_device_ref(device);
	return self;
}

struct libinput_device *
polo_backend_input_device_get_libinput(PoloBackendInputDevice *self)
{
	return self->device;
}

void
polo_backend_input_device_remove(PoloBackendInputDevice *self)
{
	if (!self->device)
		return;
	g_signal_emit(self, signals[SIGNAL_REMOVED], 0);
	g_clear_pointer(&self->device, libinput_device_unref);
}

const char *
polo_backend_input_device_get_name(PoloBackendInputDevice *self)
{
	g_return_val_if_fail(POLO_BACKEND_IS_INPUT_DEVICE(self), "");
	return self->device ? libinput_device_get_name(self->device) : "";
}

gboolean
polo_backend_input_device_has_capability(PoloBackendInputDevice *self,
					 PoloBackendDeviceCapability cap)
{
	static const enum libinput_device_capability caps[] = {
		[POLO_BACKEND_DEVICE_KEYBOARD] = LIBINPUT_DEVICE_CAP_KEYBOARD,
		[POLO_BACKEND_DEVICE_POINTER] = LIBINPUT_DEVICE_CAP_POINTER,
		[POLO_BACKEND_DEVICE_TOUCH] = LIBINPUT_DEVICE_CAP_TOUCH,
		[POLO_BACKEND_DEVICE_TABLET_TOOL] =
			LIBINPUT_DEVICE_CAP_TABLET_TOOL,
	};

	g_return_val_if_fail(POLO_BACKEND_IS_INPUT_DEVICE(self), FALSE);
	g_return_val_if_fail(cap < G_N_ELEMENTS(caps), FALSE);
	return self->device &&
		libinput_device_has_capability(self->device, caps[cap]);
}

gboolean
polo_backend_input_device_is_touchpad(PoloBackendInputDevice *self)
{
	g_return_val_if_fail(POLO_BACKEND_IS_INPUT_DEVICE(self), FALSE);
	return self->device &&
		libinput_device_config_tap_get_finger_count(self->device) > 0;
}

guint32
polo_backend_input_device_get_features(PoloBackendInputDevice *self)
{
	struct libinput_device *d;
	guint32 features = 0;

	g_return_val_if_fail(POLO_BACKEND_IS_INPUT_DEVICE(self), 0);

	d = self->device;
	if (!d)
		return 0;
	if (libinput_device_config_left_handed_is_available(d))
		features |= POLO_BACKEND_DEVICE_LEFT_HANDED;
	if (libinput_device_config_accel_is_available(d))
		features |= POLO_BACKEND_DEVICE_ACCEL;
	if (libinput_device_config_accel_get_profiles(d) !=
	    LIBINPUT_CONFIG_ACCEL_PROFILE_NONE)
		features |= POLO_BACKEND_DEVICE_ACCEL_PROFILE;
	if (libinput_device_config_middle_emulation_is_available(d))
		features |= POLO_BACKEND_DEVICE_MIDDLE_EMULATION;
	if (libinput_device_config_dwt_is_available(d))
		features |= POLO_BACKEND_DEVICE_DWT;
	if (libinput_device_config_tap_get_finger_count(d) > 0)
		features |= POLO_BACKEND_DEVICE_TAP;
	if (libinput_device_config_click_get_methods(d) !=
	    LIBINPUT_CONFIG_CLICK_METHOD_NONE)
		features |= POLO_BACKEND_DEVICE_CLICK_METHOD;
	if (libinput_device_config_scroll_get_methods(d) !=
	    LIBINPUT_CONFIG_SCROLL_NO_SCROLL)
		features |= POLO_BACKEND_DEVICE_SCROLL_METHOD;
	if (libinput_device_config_scroll_has_natural_scroll(d))
		features |= POLO_BACKEND_DEVICE_NATURAL_SCROLL;
	if (libinput_device_config_send_events_get_modes(d) &
	    LIBINPUT_CONFIG_SEND_EVENTS_DISABLED)
		features |= POLO_BACKEND_DEVICE_SEND_EVENTS;
	return features;
}

static gboolean
ok(PoloBackendInputDevice *self, const char *what,
   enum libinput_config_status status)
{
	if (status == LIBINPUT_CONFIG_STATUS_SUCCESS)
		return TRUE;
	weston_log("%s on \"%s\": %s\n", what,
		   libinput_device_get_name(self->device),
		   libinput_config_status_to_str(status));
	return FALSE;
}

#define CHECK_DEVICE(self)                                                     \
	g_return_val_if_fail(POLO_BACKEND_IS_INPUT_DEVICE(self), FALSE);       \
	if (!(self)->device)                                                   \
		return FALSE;

gboolean
polo_backend_input_device_set_left_handed(PoloBackendInputDevice *self,
					  gboolean left_handed)
{
	CHECK_DEVICE(self);
	return ok(self, "left-handed",
		  libinput_device_config_left_handed_set(self->device,
							 left_handed));
}

gboolean
polo_backend_input_device_set_accel_speed(PoloBackendInputDevice *self,
					  double speed)
{
	CHECK_DEVICE(self);
	return ok(self, "accel speed",
		  libinput_device_config_accel_set_speed(self->device, speed));
}

gboolean
polo_backend_input_device_set_accel_profile(PoloBackendInputDevice *self,
					    PoloBackendAccelProfile profile)
{
	enum libinput_config_accel_profile p;

	CHECK_DEVICE(self);
	switch (profile) {
	case POLO_BACKEND_ACCEL_PROFILE_ADAPTIVE:
		p = LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;
		break;
	case POLO_BACKEND_ACCEL_PROFILE_FLAT:
		p = LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT;
		break;
	case POLO_BACKEND_ACCEL_PROFILE_DEFAULT:
	default:
		p = libinput_device_config_accel_get_default_profile(
			self->device);
		break;
	}
	return ok(self, "accel profile",
		  libinput_device_config_accel_set_profile(self->device, p));
}

gboolean
polo_backend_input_device_set_middle_emulation(PoloBackendInputDevice *self,
					       gboolean enabled)
{
	CHECK_DEVICE(self);
	return ok(self, "middle emulation",
		  libinput_device_config_middle_emulation_set_enabled(
			  self->device,
			  enabled ? LIBINPUT_CONFIG_MIDDLE_EMULATION_ENABLED
				  : LIBINPUT_CONFIG_MIDDLE_EMULATION_DISABLED));
}

gboolean
polo_backend_input_device_set_dwt(PoloBackendInputDevice *self,
				  gboolean enabled)
{
	CHECK_DEVICE(self);
	return ok(self, "disable while typing",
		  libinput_device_config_dwt_set_enabled(
			  self->device,
			  enabled ? LIBINPUT_CONFIG_DWT_ENABLED
				  : LIBINPUT_CONFIG_DWT_DISABLED));
}

gboolean
polo_backend_input_device_set_tap(PoloBackendInputDevice *self,
				  gboolean enabled)
{
	CHECK_DEVICE(self);
	return ok(self, "tapping",
		  libinput_device_config_tap_set_enabled(
			  self->device,
			  enabled ? LIBINPUT_CONFIG_TAP_ENABLED
				  : LIBINPUT_CONFIG_TAP_DISABLED));
}

gboolean
polo_backend_input_device_set_click_method(PoloBackendInputDevice *self,
					   PoloBackendClickMethod method)
{
	static const enum libinput_config_click_method methods[] = {
		[POLO_BACKEND_CLICK_METHOD_NONE] =
			LIBINPUT_CONFIG_CLICK_METHOD_NONE,
		[POLO_BACKEND_CLICK_METHOD_BUTTON_AREAS] =
			LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS,
		[POLO_BACKEND_CLICK_METHOD_CLICKFINGER] =
			LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER,
	};

	CHECK_DEVICE(self);
	g_return_val_if_fail(method < G_N_ELEMENTS(methods), FALSE);
	return ok(self, "click method",
		  libinput_device_config_click_set_method(self->device,
							  methods[method]));
}

gboolean
polo_backend_input_device_set_scroll_method(PoloBackendInputDevice *self,
					    PoloBackendScrollMethod method)
{
	static const enum libinput_config_scroll_method methods[] = {
		[POLO_BACKEND_SCROLL_METHOD_NONE] =
			LIBINPUT_CONFIG_SCROLL_NO_SCROLL,
		[POLO_BACKEND_SCROLL_METHOD_TWO_FINGER] =
			LIBINPUT_CONFIG_SCROLL_2FG,
		[POLO_BACKEND_SCROLL_METHOD_EDGE] = LIBINPUT_CONFIG_SCROLL_EDGE,
		[POLO_BACKEND_SCROLL_METHOD_ON_BUTTON_DOWN] =
			LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN,
	};

	CHECK_DEVICE(self);
	g_return_val_if_fail(method < G_N_ELEMENTS(methods), FALSE);
	return ok(self, "scroll method",
		  libinput_device_config_scroll_set_method(self->device,
							   methods[method]));
}

gboolean
polo_backend_input_device_set_natural_scroll(PoloBackendInputDevice *self,
					     gboolean enabled)
{
	CHECK_DEVICE(self);
	return ok(self, "natural scroll",
		  libinput_device_config_scroll_set_natural_scroll_enabled(
			  self->device, enabled));
}

gboolean
polo_backend_input_device_set_send_events(PoloBackendInputDevice *self,
					  gboolean enabled)
{
	CHECK_DEVICE(self);
	return ok(self, "send events",
		  libinput_device_config_send_events_set_mode(
			  self->device,
			  enabled ? LIBINPUT_CONFIG_SEND_EVENTS_ENABLED
				  : LIBINPUT_CONFIG_SEND_EVENTS_DISABLED));
}
