/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * polo --stats: where the time goes, summed up in the log every interval.
 * Per output: repaint start to presentation, presentation to the next repaint
 * start (frame callbacks go out at presentation), missed vblanks, damaged
 * share of the output, and how often the top view was on a hardware plane,
 * and which programs committed the most frames.
 * For input (key and button presses, pointer motion): event to Polo handling
 * it, and event to the presentation of the first frame repainted after it. For
 * effects: simulation time per frame and deformed vertices. Off, every hook
 * returns at once.
 */

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <libweston/libweston.h>
#include "libweston-internal.h"

#include "polo-backend-private.h"

#define SAMPLES 2048

struct series {
	gint64 values[SAMPLES];
	guint n;
	guint next;
	gint64 max;
};

struct stats_output {
	struct wl_list link;
	struct weston_output *output;
	struct wl_listener latch;
	struct wl_listener frame;
	struct wl_listener destroy;
	int (*start_repaint_loop)(struct weston_output *output);
	gint64 saved_present;
	gint64 pending_start;
	gint64 pending_input;
	gint64 last_present;
	guint frames;
	guint missed;
	guint damage_frames;
	double damage_sum;
	guint top_candidates;
	guint top_on_plane;
	GHashTable *off_plane;
	struct series repaint_present;
	struct series present_repaint;
	struct series input_frame;
};

static struct {
	gboolean on;
	PoloBackendServer *server;
	struct wl_list outputs;
	struct wl_listener output_created;
	gint64 input_waiting;
	struct series input_handled;
	struct series effect_time;
	guint64 vertices_sum;
	guint vertices_max;
	guint effect_frames;
	struct wl_listener surface_created;
	GHashTable *commits;
} stats;

struct watched_surface {
	struct weston_surface *surface;
	struct wl_listener commit;
	struct wl_listener destroy;
};

static gint64
timespec_usec(const struct timespec *ts)
{
	return (gint64)ts->tv_sec * G_USEC_PER_SEC + ts->tv_nsec / 1000;
}

/* Frames are timed on the presentation clock, which the backend chooses. */
static gint64
presentation_now(void)
{
	struct timespec now;

	weston_compositor_read_presentation_clock(stats.server->compositor,
						  &now);
	return timespec_usec(&now);
}

static gint64
clock_usec(clockid_t clock)
{
	struct timespec now;

	clock_gettime(clock, &now);
	return timespec_usec(&now);
}

static void
series_add(struct series *s, gint64 value)
{
	s->values[s->next] = value;
	s->next = (s->next + 1) % SAMPLES;
	if (s->n < SAMPLES)
		s->n++;
	if (value > s->max)
		s->max = value;
}

static int
compare(const void *a, const void *b)
{
	gint64 x = *(const gint64 *)a, y = *(const gint64 *)b;

	return x < y ? -1 : x > y;
}

static void
series_print(GString *out, const char *name, struct series *s)
{
	gint64 sorted[SAMPLES];

	if (s->n == 0)
		return;
	memcpy(sorted, s->values, s->n * sizeof sorted[0]);
	qsort(sorted, s->n, sizeof sorted[0], compare);
	g_string_append_printf(out, "; %s p50 %.1f p95 %.1f max %.1f ms (%u)",
			       name, sorted[s->n / 2] / 1000.0,
			       sorted[(s->n * 95) / 100] / 1000.0,
			       s->max / 1000.0, s->n);
	memset(s, 0, sizeof *s);
}

static gint64
refresh_period_usec(struct weston_output *output)
{
	if (!output->current_mode || output->current_mode->refresh <= 0)
		return 0;
	return (gint64)1000000000 / output->current_mode->refresh;
}

static const char *const LAYER_NAMES[POLO_BACKEND_N_LAYERS] = {
	"background", "bottom", "below",      "normal",
	"above",      "dock",   "fullscreen", "override-redirect",
	"overlay",    "lock",
};

static int
layer_of(struct weston_view *view)
{
	while (view->geometry.parent)
		view = view->geometry.parent;
	for (int i = 0; i < POLO_BACKEND_N_LAYERS; i++)
		if (view->layer_link.layer == &stats.server->layers[i].layer)
			return i;
	return -1;
}

static char *
program_name(struct weston_surface *surface)
{
	const char *name = "compositor";

	if (surface->resource) {
		PoloBackendClient *client = polo_backend_server_get_client(
			stats.server,
			wl_resource_get_client(surface->resource));
		const char *exe = polo_backend_client_get_executable(client);

		if (polo_backend_client_get_xserver(client))
			name = "Xwayland";
		else if (exe)
			name = exe;
	}
	return g_path_get_basename(name);
}

static char *
blocker_name(struct weston_view *view)
{
	int layer = layer_of(view);
	char *program, *name;

	if (view->layer_link.layer == &view->surface->compositor->cursor_layer)
		return g_strdup("under the cursor");
	program = program_name(view->surface);
	name = g_strdup_printf("under %s (%s)", program,
			       layer >= 0 ? LAYER_NAMES[layer] : "sub-surface");
	g_free(program);
	return name;
}

static char *
failure_names(uint32_t reasons)
{
	GString *names = g_string_new(NULL);

	if (reasons == FAILURE_REASONS_NONE) {
		g_string_free(names, TRUE);
		return g_strdup("not tried");
	}
	for (uint32_t bit = 1; bit; bit <<= 1) {
		if (!(reasons & bit))
			continue;
		if (names->len)
			g_string_append(names, " + ");
		g_string_append(names,
				weston_plane_failure_reasons_to_str(bit));
	}
	return g_string_free(names, FALSE);
}

static void
note_off_plane(struct stats_output *o, char *why)
{
	g_hash_table_insert(
		o->off_plane, why,
		GUINT_TO_POINTER(GPOINTER_TO_UINT(g_hash_table_lookup(
					 o->off_plane, why)) +
				 1));
}

/* A window covering the whole output (a fullscreen game or video) could be
 * scanned out directly. Count how often it was, and when not, why: a view the
 * renderer composites above it, or what the DRM backend said. */
static void
check_top_view(struct stats_output *o)
{
	struct weston_output *output = o->output;
	struct weston_paint_node *pnode;
	struct weston_view *above = NULL;
	pixman_box32_t *area = pixman_region32_extents(&output->region);

	wl_list_for_each(pnode, &output->paint_node_z_order_list, z_order_link)
	{
		pixman_box32_t *box;
		gboolean composited;

		if (!pnode->view || !weston_view_is_mapped(pnode->view))
			continue;
		box = pixman_region32_extents(
			&pnode->view->transform.boundingbox);
		composited =
			!pnode->plane || pnode->plane == &output->primary_plane;
		if (box->x1 <= area->x1 && box->y1 <= area->y1 &&
		    box->x2 >= area->x2 && box->y2 >= area->y2) {
			if (layer_of(pnode->view) < POLO_BACKEND_LAYER_BELOW)
				return;
			o->top_candidates++;
			if (!composited)
				o->top_on_plane++;
			else if (above)
				note_off_plane(o, blocker_name(above));
			else
				note_off_plane(
					o,
					failure_names(
						pnode->try_view_on_plane_failure_reasons));
			return;
		}
		if (!above && composited && box->x2 > area->x1 &&
		    box->x1 < area->x2 && box->y2 > area->y1 &&
		    box->y1 < area->y2)
			above = pnode->view;
	}
}

static void
output_latched(struct wl_listener *listener, void *data)
{
	struct stats_output *o = wl_container_of(listener, o, latch);
	gint64 now = presentation_now();
	gboolean fresh =
		!(o->output->frame_flags & WP_PRESENTATION_FEEDBACK_INVALID);
	gint64 present = fresh ? timespec_usec(&o->output->frame_time)
			       : o->saved_present;
	gint64 period = refresh_period_usec(o->output);

	o->saved_present = 0;
	if (o->pending_start && present >= o->pending_start) {
		gint64 took = present - o->pending_start;

		series_add(&o->repaint_present, took);
		if (period && took > period)
			o->missed++;
		if (o->pending_input && present >= o->pending_input)
			series_add(&o->input_frame, present - o->pending_input);
		o->frames++;
	}
	if (fresh && present > o->last_present && now >= present &&
	    now - present < G_USEC_PER_SEC)
		series_add(&o->present_repaint, now - present);
	o->last_present = present;
	check_top_view(o);
	o->pending_start = now;
	o->pending_input = stats.input_waiting;
	stats.input_waiting = 0;
}

static int
start_repaint_loop(struct weston_output *output)
{
	struct stats_output *o;

	wl_list_for_each(o, &stats.outputs, link)
	{
		if (o->output != output)
			continue;
		if (!(output->frame_flags & WP_PRESENTATION_FEEDBACK_INVALID))
			o->saved_present = timespec_usec(&output->frame_time);
		return o->start_repaint_loop(output);
	}
	return 0;
}

static void
output_framed(struct wl_listener *listener, void *data)
{
	struct stats_output *o = wl_container_of(listener, o, frame);
	pixman_region32_t *damage = data;
	pixman_box32_t *area = pixman_region32_extents(&o->output->region);
	double total = (double)(area->x2 - area->x1) * (area->y2 - area->y1);
	pixman_region32_t clipped;
	pixman_box32_t *rects;
	int n;
	double damaged = 0;

	if (!damage || total <= 0)
		return;
	pixman_region32_init(&clipped);
	pixman_region32_intersect(&clipped, damage, &o->output->region);
	rects = pixman_region32_rectangles(&clipped, &n);
	for (int i = 0; i < n; i++)
		damaged += (double)(rects[i].x2 - rects[i].x1) *
			(rects[i].y2 - rects[i].y1);
	pixman_region32_fini(&clipped);
	o->damage_sum += damaged / total;
	o->damage_frames++;
}

static void
output_destroyed(struct wl_listener *listener, void *data)
{
	struct stats_output *o = wl_container_of(listener, o, destroy);

	wl_list_remove(&o->latch.link);
	wl_list_remove(&o->frame.link);
	wl_list_remove(&o->destroy.link);
	o->output->start_repaint_loop = o->start_repaint_loop;
	wl_list_remove(&o->link);
	g_hash_table_destroy(o->off_plane);
	g_free(o);
}

static void
watch_output(struct weston_output *output)
{
	struct stats_output *o = g_new0(struct stats_output, 1);

	o->output = output;
	o->off_plane =
		g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	o->latch.notify = output_latched;
	wl_signal_add(&output->post_latch_signal, &o->latch);
	o->frame.notify = output_framed;
	wl_signal_add(&output->frame_signal, &o->frame);
	o->destroy.notify = output_destroyed;
	wl_signal_add(&output->destroy_signal, &o->destroy);
	o->start_repaint_loop = output->start_repaint_loop;
	if (o->start_repaint_loop)
		output->start_repaint_loop = start_repaint_loop;
	wl_list_insert(&stats.outputs, &o->link);
}

static void
surface_committed(struct wl_listener *listener, void *data)
{
	struct watched_surface *w = wl_container_of(listener, w, commit);
	char *base = program_name(w->surface);

	g_hash_table_insert(
		stats.commits, base,
		GUINT_TO_POINTER(GPOINTER_TO_UINT(g_hash_table_lookup(
					 stats.commits, base)) +
				 1));
}

static void
surface_destroyed(struct wl_listener *listener, void *data)
{
	struct watched_surface *w = wl_container_of(listener, w, destroy);

	wl_list_remove(&w->commit.link);
	wl_list_remove(&w->destroy.link);
	g_free(w);
}

static void
surface_created(struct wl_listener *listener, void *data)
{
	struct watched_surface *w = g_new0(struct watched_surface, 1);

	w->surface = data;
	w->commit.notify = surface_committed;
	wl_signal_add(&w->surface->commit_signal, &w->commit);
	w->destroy.notify = surface_destroyed;
	wl_signal_add(&w->surface->destroy_signal, &w->destroy);
}

static gint
by_count(gconstpointer a, gconstpointer b)
{
	guint x = GPOINTER_TO_UINT(
		g_hash_table_lookup(stats.commits, *(char **)a));
	guint y = GPOINTER_TO_UINT(
		g_hash_table_lookup(stats.commits, *(char **)b));

	return x < y ? 1 : x > y ? -1 : 0;
}

static void
print_commits(GString *line)
{
	guint n;
	gpointer *names;

	if (g_hash_table_size(stats.commits) == 0)
		return;
	names = g_hash_table_get_keys_as_array(stats.commits, &n);
	qsort(names, n, sizeof names[0], by_count);
	g_string_append(line, "; most commits:");
	for (guint i = 0; i < n && i < 3; i++)
		g_string_append_printf(line, " %s %u", (char *)names[i],
				       GPOINTER_TO_UINT(g_hash_table_lookup(
					       stats.commits, names[i])));
	g_free(names);
	g_hash_table_remove_all(stats.commits);
}

static void
output_created(struct wl_listener *listener, void *data)
{
	watch_output(data);
}

static void
append_off_plane(GString *line, struct stats_output *o)
{
	GHashTableIter iter;
	gpointer why, count;
	const char *separator = " (off it: ";

	g_hash_table_iter_init(&iter, o->off_plane);
	while (g_hash_table_iter_next(&iter, &why, &count)) {
		g_string_append_printf(line, "%s%s %u", separator, (char *)why,
				       GPOINTER_TO_UINT(count));
		separator = ", ";
	}
	if (g_hash_table_size(o->off_plane))
		g_string_append_c(line, ')');
	g_hash_table_remove_all(o->off_plane);
}

static gboolean
report(gpointer data)
{
	PoloLogScope *log = polo_backend_log_scope("polo");
	struct stats_output *o;
	GString *line = g_string_new(NULL);

	wl_list_for_each(o, &stats.outputs, link)
	{
		if (o->frames == 0 && o->repaint_present.n == 0)
			continue;
		g_string_printf(line, "stats %s: %u frames, %u missed",
				o->output->name, o->frames, o->missed);
		series_print(line, "repaint to presentation",
			     &o->repaint_present);
		series_print(line, "presentation to repaint",
			     &o->present_repaint);
		series_print(line, "input to frame", &o->input_frame);
		if (o->damage_frames)
			g_string_append_printf(line, "; damage %.0f%%",
					       100.0 * o->damage_sum /
						       o->damage_frames);
		g_string_append_printf(line,
				       "; top view on a plane %u of %u frames",
				       o->top_on_plane, o->top_candidates);
		append_off_plane(line, o);
		polo_log_scope_info(log, "%s", line->str);
		o->frames = o->missed = o->damage_frames = 0;
		o->top_candidates = o->top_on_plane = 0;
		o->damage_sum = 0;
	}

	g_string_assign(line, "stats input and effects");
	if (stats.input_handled.n == 0 && stats.effect_frames == 0 &&
	    g_hash_table_size(stats.commits) == 0) {
		g_string_free(line, TRUE);
		return G_SOURCE_CONTINUE;
	}
	series_print(line, "input to handling", &stats.input_handled);
	if (stats.effect_frames) {
		series_print(line, "effect simulation", &stats.effect_time);
		g_string_append_printf(
			line, "; deformed vertices %.0f mean, %u max",
			(double)stats.vertices_sum / stats.effect_frames,
			stats.vertices_max);
	}
	print_commits(line);
	polo_log_scope_info(log, "%s", line->str);
	stats.vertices_sum = 0;
	stats.vertices_max = 0;
	stats.effect_frames = 0;
	g_string_free(line, TRUE);
	return G_SOURCE_CONTINUE;
}

void
polo_backend_stats_start(PoloBackendServer *server, guint interval_msec)
{
	struct weston_output *output;

	g_return_if_fail(POLO_BACKEND_IS_SERVER(server));
	if (stats.on || !server->compositor)
		return;
	stats.on = TRUE;
	stats.server = server;
	wl_list_init(&stats.outputs);
	wl_list_for_each(output, &server->compositor->output_list, link)
		watch_output(output);
	stats.output_created.notify = output_created;
	wl_signal_add(&server->compositor->output_created_signal,
		      &stats.output_created);
	stats.commits =
		g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	stats.surface_created.notify = surface_created;
	wl_signal_add(&server->compositor->create_surface_signal,
		      &stats.surface_created);
	g_timeout_add(MAX(interval_msec, 100u), report, NULL);
	polo_log_scope_info(polo_backend_log_scope("polo"),
			    "stats: a summary every %u ms",
			    MAX(interval_msec, 100u));
}

/* libinput stamps events on CLOCK_MONOTONIC, weston_compositor_get_time ()
 * on CLOCK_REALTIME: the age is whichever is plausible. The event then
 * counts as having happened that long before now on the presentation
 * clock. */
void
polo_backend_stats_input(const struct timespec *event_time)
{
	static const clockid_t clocks[] = { CLOCK_MONOTONIC, CLOCK_REALTIME };
	gint64 at, age = 0;

	if (!stats.on)
		return;
	at = timespec_usec(event_time);
	for (gsize i = 0; i < G_N_ELEMENTS(clocks) && at > 0; i++) {
		gint64 delta = clock_usec(clocks[i]) - at;

		if (delta >= 0 && delta < 10 * G_USEC_PER_SEC) {
			age = delta;
			series_add(&stats.input_handled, age);
			break;
		}
	}
	if (!stats.input_waiting)
		stats.input_waiting = presentation_now() - age;
}

void
polo_backend_stats_effects(gint64 duration_usec, guint vertices)
{
	if (!stats.on)
		return;
	series_add(&stats.effect_time, duration_usec);
	stats.vertices_sum += vertices;
	stats.vertices_max = MAX(stats.vertices_max, vertices);
	stats.effect_frames++;
}
