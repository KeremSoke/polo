/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Headless test harness: starts `polo --backend=headless
 * --test-mode` in a private XDG_RUNTIME_DIR and talks to it as an ordinary
 * wayland-client, plus polo_test_v1 for inspection and input injection.
 */

#ifndef POLO_TEST_HARNESS_H
#define POLO_TEST_HARNESS_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#include <wayland-client.h>

#include "polo-test-v1-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "xdg-decoration-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#define HARNESS_OUTPUT_WIDTH 1024
#define HARNESS_OUTPUT_HEIGHT 768

struct harness_window_info {
	bool known;
	int x, y, width, height;
	int layer, stack_index, workspace;
	uint32_t state;
	bool focused;
};

struct harness_lamp_target {
	bool known;
	int x, y, width, height;
	uint32_t edge, source;
};

struct harness_layer_info {
	bool known;
	int x, y, width, height;
	int layer;
	bool mapped;
};

struct harness_window {
	struct harness *harness;
	struct wl_surface *surface;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *xdg_toplevel;
	struct wl_buffer *buffer;
	int width, height;                 /* current buffer size */
	int pending_width, pending_height; /* from xdg_toplevel.configure */
	bool configured;
	bool auto_configure;
	int configure_count;
	/* Keep whole multiples of this, as a terminal keeps whole cells. */
	int size_inc;
	bool close_requested;
	/* The xdg_toplevel tiled states of the last configure, bit 0 = left,
	 * 1 = right, 2 = top, 3 = bottom. */
	uint32_t tiled;
	bool xdg_maximized;
	struct harness_window_info info;
	struct harness_lamp_target lamp;
	struct zxdg_toplevel_decoration_v1 *decoration;
};

struct harness_output_state {
	char name[32];
	bool enabled, primary;
	int x, y, width, height, scale;
	uint32_t refresh;
	int area_x, area_y, area_width, area_height;
};

struct harness {
	pid_t pid;
	char runtime_dir[256];
	char socket[64];

	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct xdg_wm_base *wm_base;
	struct zxdg_decoration_manager_v1 *decoration_manager;
	struct wl_seat *seat;
	struct wl_pointer *pointer;
	struct wl_keyboard *keyboard;
	struct polo_test_v1 *test;
	int pointer_x, pointer_y;
	struct zwlr_layer_shell_v1 *layer_shell;
	struct weston_capture_v1 *capture;

	/* wl_outputs, in registry order. */
	struct wl_output *outputs[8];
	uint32_t output_globals[8];
	int n_outputs;
	/* wl_registry.global_remove events seen. */
	int globals_removed;

	/* Mapped windows; any of them can carry a frame callback. */
	struct harness_window *windows[32];
	int n_windows;

	struct wl_surface *keyboard_focus;
	struct wl_surface *pointer_focus;
	uint32_t pointer_enter_serial;
	uint32_t last_button_serial;
	int button_presses;
	int button_releases;
	int motions;
	uint32_t last_key;
	uint32_t last_keyboard_serial;
	bool last_key_pressed;
	struct wl_surface *last_key_surface;
	int keyboard_enters;
	uint32_t enter_keys[32];
	int n_enter_keys;
	int keymaps;
	uint32_t mods_locked;
	uint32_t mods_depressed, mods_latched;
	uint32_t key_mods;
	uint32_t group;
	int32_t repeat_rate, repeat_delay;
	int repeat_infos;
	struct harness_layer_info layer_info;
	char setting[1024];
	/* polo_test_v1 v4: polo-shell-ui as Polo sees it. */
	int shell_ui_pid; /* 0 while unbound */
	int shell_ui_binds;
	int shell_ui_unbinds;
	char shell_ui_requests[16][256];
	int n_shell_ui_requests;
	/* polo_test_v1 v11: outputs as Polo has set them. */
	struct harness_output_state output_states[16];
	int n_output_states;
	bool outputs_done;
	unsigned monitors;
	char config_dir[300];
	/* Leave xdg_wm_base pings unanswered. */
	bool ignore_pings;
	/* Interfaces this (untrusted) client was offered. */
	bool saw_shell_ui_global;
};

/* Exit with a message unless cond holds. */
#define CHECK(cond, ...)                                                       \
	do {                                                                   \
		if (!(cond)) {                                                 \
			harness_fail(__FILE__, __LINE__, #cond, __VA_ARGS__);  \
		}                                                              \
	} while (0)

void
harness_fail(const char *file, int line, const char *cond, const char *fmt, ...)
	__attribute__((noreturn, format(printf, 4, 5)));

/* argv[1] is the polo executable. */
struct harness *
harness_start(int argc, char **argv);

/* Disconnect, SIGTERM Polo, and return its exit status (-1 if killed). */
int
harness_stop(struct harness *h);

void
harness_roundtrip(struct harness *h);

/*
 * Wait until Polo has repainted. libweston picks the view under the pointer
 * from the scene as of the last repaint (as Weston's own tests account
 * for), so tests wait for one after anything that changes the scene.
 */
void
harness_wait_repaint(struct harness *h);

/* Commit surface and wait for its frame callback (a Polo repaint). */
void
harness_wait_frame(struct harness *h, struct wl_surface *surface);

/* Ask Polo for the layer surface's view geometry (fills h->layer_info). */
const struct harness_layer_info *
harness_layer_info(struct harness *h, struct wl_surface *surface);

/* Map a toplevel of the given size and wait until Polo shows it. */
struct harness_window *
harness_window_create(struct harness *h, int width, int height,
		      const char *title);

struct harness_window *
harness_window_open(struct harness *h, int width, int height,
		    const char *title);

/* Creates one after requesting server-side decorations before its first commit.
 */
struct harness_window *
harness_window_create_decorated(struct harness *h, int width, int height,
				const char *title);

/* Creates one with a decoration object but no preferred mode. */
struct harness_window *
harness_window_create_with_decoration(struct harness *h, int width, int height,
				      const char *title);

/* A filled ARGB8888 SHM buffer of the given size. */
struct wl_buffer *
harness_buffer_create(struct wl_shm *shm, int width, int height);

struct wl_buffer *
harness_buffer_create_colour(struct wl_shm *shm, int width, int height,
			     uint32_t argb);

void
harness_window_destroy(struct harness_window *w);

void
harness_window_close(struct harness_window *w);

/* Attach a null buffer: the toplevel unmaps and keeps its role. */
void
harness_window_unmap(struct harness_window *w);

/* Initial commit, configure and a buffer again after harness_window_unmap. */
void
harness_window_remap(struct harness_window *w, int width, int height);

/* Ask Polo for its model of the window (fills w->info). */
const struct harness_window_info *
harness_window_info(struct harness_window *w);

const struct harness_lamp_target *
harness_lamp_target(struct harness_window *w);

/* Draw the last configured size after deliberately delaying a resize. */
void
harness_window_draw_pending(struct harness_window *w);

/*
 * GSettings in Polo's memory backend; values in GVariant text form. Setting
 * fails the test when Polo cannot write the key.
 */
const char *
harness_set_setting(struct harness *h, const char *schema, const char *key,
		    const char *value);

const char *
harness_get_setting(struct harness *h, const char *schema, const char *key);

/* The first output's framebuffer as width x height XRGB8888 pixels (free
 * them), or NULL. */
uint32_t *
harness_capture(struct harness *h, int *width, int *height);

uint32_t *
harness_capture_output(struct harness *h, int index, int *width, int *height);

void
harness_move_pointer(struct harness *h, int x, int y);

void
harness_move_pointer_absolute(struct harness *h, int x, int y);

void
harness_pointer_position(struct harness *h, int *x, int *y);

void
harness_scroll(struct harness *h, int value);

void
harness_button(struct harness *h, uint32_t button, bool pressed);

void
harness_click(struct harness *h, int x, int y);

void
harness_key(struct harness *h, uint32_t key, bool pressed);

/* CPU time (user + system) Polo has used, in clock ticks. */
long
harness_cpu_ticks(struct harness *h);

/* A 128-byte EDID 1.4 with a vendor's three letters, product, serial, a
 * monitor name and the physical size, its preferred timing width x height. */
void
harness_make_edid(uint8_t edid[128], const char *vendor, uint16_t product,
		  uint32_t serial, const char *model, int width_mm,
		  int height_mm, int width, int height);

/* Plugs in a fake monitor (polo_test_v1 add_output); modes are width,
 * height and refresh in mHz, the first preferred. A NULL vendor gives no
 * EDID. */
void
harness_add_output(struct harness *h, const char *name, const char *vendor,
		   uint16_t product, uint32_t serial, const char *model,
		   bool internal, int width_mm, int height_mm,
		   const int modes[][3], int n_modes);

void
harness_connect_output(struct harness *h, const char *name, bool connected);

void
harness_set_session_active(struct harness *h, bool active);

/* The monitor and its connector go, as a dock or an MST hub unplugged. */
void
harness_remove_output(struct harness *h, const char *name);

/* Every output as Polo has set it; the one named, or NULL. */
const struct harness_output_state *
harness_output_state(struct harness *h, const char *name);

int
harness_output_count(struct harness *h);

#endif /* POLO_TEST_HARNESS_H */
