/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The port of Marco's theme parser: every installed theme loads and gives a
 * frame geometry, colours parse as gdk_rgba_parse () parses them, position
 * expressions evaluate, and a broken theme is an error.
 */

#include <glib/gstdio.h>
#include <pango/pangocairo.h>
#include <string.h>

#include "theme-parser.h"

static void
rgba(void)
{
	PoloRGBA c;

	g_assert_true(polo_marco_rgba_parse(&c, "#ff0000"));
	g_assert_cmpfloat(c.red, ==, 1);
	g_assert_cmpfloat(c.green, ==, 0);
	g_assert_cmpfloat(c.alpha, ==, 1);
	g_assert_true(polo_marco_rgba_parse(&c, "#fff"));
	g_assert_cmpfloat(c.blue, ==, 1);
	g_assert_true(polo_marco_rgba_parse(&c, "white"));
	g_assert_true(polo_marco_rgba_parse(&c, "rgb(255, 0, 51)"));
	g_assert_cmpfloat_with_epsilon(c.blue, 0.2, 1e-9);
	g_assert_true(polo_marco_rgba_parse(&c, "rgba(0,0,0,0.5)"));
	g_assert_cmpfloat(c.alpha, ==, 0.5);
	g_assert_true(polo_marco_rgba_parse(&c, "rgb(50%,0,0)"));
	g_assert_cmpfloat(c.red, ==, 0.5);
	g_assert_false(polo_marco_rgba_parse(&c, "rgb(1,2)"));
	g_assert_false(polo_marco_rgba_parse(&c, "rgba(1,2,3)"));
	g_assert_false(polo_marco_rgba_parse(&c, "notacolour"));
	g_assert_false(polo_marco_rgba_parse(&c, "rgb(1,2,3) x"));
	g_assert_false(polo_marco_rgba_parse(&c, "rgba(0,0,0 0.5)"));
	g_assert_true(polo_marco_rgba_parse(&c, "#0000ff"));
	g_assert_cmpfloat(c.red, ==, 0);
	g_assert_cmpfloat(c.blue, ==, 1);
}

static void
colour_specs(void)
{
	GError *error = NULL;
	PoloColorSpec *spec =
		polo_marco_color_spec_new_from_string("#00ff00", &error);

	g_assert_no_error(error);
	g_assert_nonnull(spec);
	g_assert_cmpint(spec->type, ==, POLO_COLOR_SPEC_BASIC);
	g_assert_cmpfloat(spec->data.basic.color.green, ==, 1);
	polo_marco_color_spec_free(spec);
	g_assert_null(
		polo_marco_color_spec_new_from_string("notacolour", &error));
	g_assert_error(error, POLO_THEME_ERROR, POLO_THEME_ERROR_FAILED);
	g_clear_error(&error);
	spec = polo_marco_color_spec_new_from_string(
		"blend/#000000/#ffffff/0.5", &error);
	g_assert_no_error(error);
	g_assert_cmpint(spec->type, ==, POLO_COLOR_SPEC_BLEND);
	polo_marco_color_spec_free(spec);
}

static int
evaluate(PoloTheme *theme, const char *expression, int *value)
{
	GError *error = NULL;
	PoloDrawSpec *spec =
		polo_marco_draw_spec_new(theme, expression, &error);
	PoloPositionExprEnv env = { 0 };
	int x = 0, y = 0;
	gboolean ok;

	if (!spec) {
		g_clear_error(&error);
		return FALSE;
	}
	env.rect = (PoloRect){ 10, 20, 100, 50 };
	env.object_width = 16;
	env.object_height = 8;
	env.title_width = 60;
	env.theme = theme;
	ok = polo_marco_parse_position_expression(spec, &env, &x, &y, &error);
	g_clear_error(&error);
	polo_marco_draw_spec_free(spec);
	*value = x;
	return ok;
}

static void
expressions(void)
{
	PoloTheme *theme = polo_marco_theme_new();
	GError *error = NULL;
	int value;

	g_assert_true(
		polo_marco_theme_define_int_constant(theme, "Gap", 3, &error));
	g_assert_true(evaluate(theme, "0", &value));
	g_assert_cmpint(value, ==, 10);
	g_assert_true(evaluate(theme, "width - 2", &value));
	g_assert_cmpint(value, ==, 10 + 98);
	g_assert_true(evaluate(theme, "(width - object_width) / 2", &value));
	g_assert_cmpint(value, ==, 10 + 42);
	g_assert_true(evaluate(theme, "2 * 3 + 4", &value));
	g_assert_cmpint(value, ==, 10 + 10);
	g_assert_true(evaluate(theme, "width `max` 200", &value));
	g_assert_cmpint(value, ==, 10 + 200);
	g_assert_true(evaluate(theme, "title_width `min` Gap", &value));
	g_assert_cmpint(value, ==, 10 + 3);
	g_assert_true(evaluate(theme, "7 % 4", &value));
	g_assert_cmpint(value, ==, 10 + 3);
	g_assert_false(evaluate(theme, "width +", &value));
	g_assert_false(evaluate(theme, "nosuchvariable", &value));
	g_assert_false(evaluate(theme, "(1", &value));
	polo_marco_theme_free(theme);
}

typedef struct {
	char *name;
	char *dir;
} ThemeCase;

static void
loads(gconstpointer data)
{
	const ThemeCase *c = data;
	GError *error = NULL;
	PoloTheme *theme = polo_marco_theme_load(c->name, &error);
	PoloButtonLayout layout;
	PoloFrameGeometry geometry;
	PoloThemeBorders borders;

	g_assert_no_error(error);
	g_assert_nonnull(theme);
	polo_marco_parse_button_layout("menu:minimize,maximize,close", FALSE,
				       &layout);
	for (int type = 0; type < POLO_FRAME_TYPE_LAST; type++) {
		if (!polo_marco_theme_get_frame_style(theme, type,
						      POLO_FRAME_HAS_FOCUS))
			continue;
		polo_marco_theme_get_frame_borders(
			theme, type, 14, POLO_FRAME_HAS_FOCUS, &borders);
		polo_marco_theme_calc_geometry(
			theme, type, 14,
			POLO_FRAME_HAS_FOCUS | POLO_FRAME_ALLOWS_DELETE |
				POLO_FRAME_ALLOWS_MINIMIZE |
				POLO_FRAME_ALLOWS_MAXIMIZE |
				POLO_FRAME_ALLOWS_MENU,
			640, 480, &layout, &geometry);
		g_assert_cmpint(geometry.width, ==,
				640 + geometry.borders.total.left +
					geometry.borders.total.right);
		g_assert_cmpint(borders.visible.top, >=, 0);
	}
	polo_marco_theme_free(theme);
}

static PoloThemeColors *
grey_colours(void)
{
	PoloThemeColors *colors = polo_marco_theme_colors_new();
	float values[POLO_THEME_COLOR_COMPONENTS * POLO_THEME_COLOR_STATES * 4];

	for (int i = 0;
	     i < POLO_THEME_COLOR_COMPONENTS * POLO_THEME_COLOR_STATES; i++) {
		float v = (float)(i % 7) / 6;

		values[i * 4] = v;
		values[i * 4 + 1] = v;
		values[i * 4 + 2] = 1 - v;
		values[i * 4 + 3] = 1;
	}
	g_assert_true(polo_marco_theme_colors_set_table(colors, values,
							G_N_ELEMENTS(values)));
	return colors;
}

/* Draws one frame and returns the number of painted titlebar pixels. */
static int
draw(PoloTheme *theme, PoloThemeColors *colors, const char *name,
     PoloFrameType type, PoloFrameFlags flags)
{
	PoloButtonLayout layout;
	PoloFrameGeometry geometry;
	PoloButtonState states[POLO_BUTTON_TYPE_LAST] = { 0 };
	cairo_surface_t *surface;
	cairo_t *cr;
	PangoLayout *title;
	PangoRectangle extents;
	const char *dir = g_getenv("POLO_THEME_PNG_DIR");
	int painted = 0;

	polo_marco_parse_button_layout("menu:minimize,maximize,close", FALSE,
				       &layout);
	surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
	cr = cairo_create(surface);
	title = pango_cairo_create_layout(cr);
	pango_layout_set_text(title, "A window title", -1);
	pango_layout_get_pixel_extents(title, NULL, &extents);
	cairo_destroy(cr);
	cairo_surface_destroy(surface);

	polo_marco_theme_calc_geometry(theme, type, extents.height, flags, 320,
				       200, &layout, &geometry);
	surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
					     geometry.width, geometry.height);
	cr = cairo_create(surface);
	polo_marco_theme_draw_frame(theme, colors, cr, type, flags, 320, 200,
				    title, extents.height, &layout, states,
				    NULL, NULL);
	g_assert_cmpint(cairo_status(cr), ==, CAIRO_STATUS_SUCCESS);
	cairo_surface_flush(surface);
	{
		const guint32 *data =
			(const guint32 *)cairo_image_surface_get_data(surface);
		int stride = cairo_image_surface_get_stride(surface) / 4;
		int top = geometry.borders.total.top;

		for (int y = geometry.borders.invisible.top; y < top; y++)
			for (int x = 0; x < geometry.width; x++)
				if (data[y * stride + x] >> 24)
					painted++;
	}
	if (dir) {
		char *file = g_strdup_printf(
			"%s/%s-%d-%s.png", dir, name, type,
			flags & POLO_FRAME_HAS_FOCUS ? "focused" : "unfocused");

		cairo_surface_write_to_png(surface, file);
		g_free(file);
	}
	g_object_unref(title);
	cairo_destroy(cr);
	cairo_surface_destroy(surface);
	return painted;
}

static void
draws(gconstpointer data)
{
	const ThemeCase *c = data;
	GError *error = NULL;
	PoloTheme *theme = polo_marco_theme_load(c->name, &error);

	g_assert_no_error(error);
	PoloThemeColors *colors = grey_colours();
	PoloFrameFlags allowed = POLO_FRAME_ALLOWS_DELETE |
		POLO_FRAME_ALLOWS_MINIMIZE | POLO_FRAME_ALLOWS_MAXIMIZE |
		POLO_FRAME_ALLOWS_MENU | POLO_FRAME_ALLOWS_MOVE;

	g_assert_nonnull(theme);
	for (int type = 0; type < POLO_FRAME_TYPE_LAST; type++) {
		if (!polo_marco_theme_get_frame_style(theme, type,
						      POLO_FRAME_HAS_FOCUS))
			continue;
		draw(theme, colors, c->name, type,
		     allowed | POLO_FRAME_HAS_FOCUS);
		draw(theme, colors, c->name, type, allowed);
	}
	g_assert_cmpint(draw(theme, colors, c->name, POLO_FRAME_TYPE_NORMAL,
			     allowed | POLO_FRAME_HAS_FOCUS),
			>, 0);
	polo_marco_theme_colors_free(colors);
	polo_marco_theme_free(theme);
}

static void
custom_colours(void)
{
	PoloThemeColors *colors = polo_marco_theme_colors_new();
	PoloRGBA red = { 1, 0, 0, 1 }, got;
	PoloColorSpec *spec = polo_marco_color_spec_new_from_string(
		"gtk:custom(wm_title,#00ff00)", NULL);

	g_assert_nonnull(spec);
	polo_marco_color_spec_render(spec, colors, &got);
	g_assert_cmpfloat(got.green, ==, 1);
	polo_marco_theme_colors_set_custom(colors, "wm_title", &red);
	polo_marco_color_spec_render(spec, colors, &got);
	g_assert_cmpfloat(got.red, ==, 1);
	g_assert_cmpfloat(got.green, ==, 0);
	polo_marco_theme_colors_set_custom(colors, "wm_title", NULL);
	polo_marco_color_spec_render(spec, colors, &got);
	g_assert_cmpfloat(got.green, ==, 1);
	polo_marco_color_spec_free(spec);
	polo_marco_theme_colors_free(colors);
}

static void
broken(void)
{
	char *home = g_dir_make_tmp("polo-theme-XXXXXX", NULL);
	char *dir =
		g_build_filename(home, ".themes", "Broken", "metacity-1", NULL);
	char *file = g_build_filename(dir, "metacity-theme-1.xml", NULL);
	const char *old_home = g_getenv("HOME");
	char *saved = g_strdup(old_home);
	GError *error = NULL;

	g_mkdir_with_parents(dir, 0700);
	g_file_set_contents(file, "<metacity_theme><info><name>x</name>", -1,
			    NULL);
	g_setenv("HOME", home, TRUE);
	g_assert_null(polo_marco_theme_load("Broken", &error));
	g_assert_nonnull(error);
	g_clear_error(&error);
	g_assert_null(polo_marco_theme_load("NoSuchTheme", &error));
	g_assert_error(error, POLO_THEME_ERROR, POLO_THEME_ERROR_FAILED);
	g_clear_error(&error);
	if (saved)
		g_setenv("HOME", saved, TRUE);
	g_unlink(file);
	g_rmdir(dir);
	g_free(file);
	g_free(dir);
	dir = g_build_filename(home, ".themes", "Broken", NULL);
	g_rmdir(dir);
	g_free(dir);
	dir = g_build_filename(home, ".themes", NULL);
	g_rmdir(dir);
	g_free(dir);
	g_rmdir(home);
	g_free(home);
	g_free(saved);
}

static void
free_case(gpointer data)
{
	ThemeCase *c = data;

	g_free(c->name);
	g_free(c->dir);
	g_free(c);
}

static void
add_installed(void)
{
	const char *const *dirs = g_get_system_data_dirs();
	GHashTable *seen = g_hash_table_new(g_str_hash, g_str_equal);

	for (int i = 0; dirs[i]; i++) {
		char *themes = g_build_filename(dirs[i], "themes", NULL);
		GDir *dir = g_dir_open(themes, 0, NULL);
		const char *name;

		while (dir && (name = g_dir_read_name(dir))) {
			char *metacity = g_build_filename(themes, name,
							  "metacity-1", NULL);

			if (!g_hash_table_contains(seen, name) &&
			    g_file_test(metacity, G_FILE_TEST_IS_DIR)) {
				ThemeCase *c = g_new0(ThemeCase, 1);
				char *path;

				c->name = g_strdup(name);
				c->dir = metacity;
				g_hash_table_add(seen, c->name);
				path = g_strdup_printf("/theme/loads/%s", name);
				g_test_add_data_func(path, c, loads);
				g_free(path);
				path = g_strdup_printf("/theme/draws/%s", name);
				g_test_add_data_func_full(path, c, draws,
							  free_case);
				g_free(path);
			} else {
				g_free(metacity);
			}
		}
		if (dir)
			g_dir_close(dir);
		g_free(themes);
	}
	g_hash_table_unref(seen);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/theme/rgba", rgba);
	g_test_add_func("/theme/colour-specs", colour_specs);
	g_test_add_func("/theme/custom-colours", custom_colours);
	g_test_add_func("/theme/expressions", expressions);
	g_test_add_func("/theme/broken", broken);
	int status;

	add_installed();
	status = g_test_run();
	pango_cairo_font_map_set_default(NULL);
	return status;
}
