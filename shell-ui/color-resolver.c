/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The GTK colours Marco's theme engine reads, resolved as Marco 1.28.2 does
 * it in theme.c (meta_set_color_from_style ()) on the style context
 * frames.c builds (create_style_context ()). gtk_style_shade () and its HLS
 * helpers are GTK 2's, as Marco copies them. Copyright (C) 2001 Havoc
 * Pennington; GPL-2.0-or-later.
 */

#include <gtk/gtk.h>

#include "color-resolver.h"

#define LIGHTNESS_MULT 1.3
#define DARKNESS_MULT 0.7

enum {
	COLOR_FG,
	COLOR_BG,
	COLOR_LIGHT,
	COLOR_DARK,
	COLOR_MID,
	COLOR_TEXT,
	COLOR_BASE,
	COLOR_TEXT_AA,
	N_COMPONENTS,
};

static const GtkStateFlags STATES[] = {
	GTK_STATE_FLAG_NORMAL,      GTK_STATE_FLAG_ACTIVE,
	GTK_STATE_FLAG_PRELIGHT,    GTK_STATE_FLAG_SELECTED,
	GTK_STATE_FLAG_INSENSITIVE,
};

struct _PoloShellUiColorResolver {
	GObject parent_instance;
	PoloShellUiClient *client;
	GtkWidget *window;
	GtkStyleContext *style;
	GHashTable *asked;
	gulong theme_changed;
};

G_DEFINE_FINAL_TYPE(PoloShellUiColorResolver, polo_shell_ui_color_resolver,
		    G_TYPE_OBJECT)

static void
gtk_style_shade(GdkRGBA *a, GdkRGBA *b, gdouble k);
static void
rgb_to_hls(gdouble *r, gdouble *g, gdouble *b);
static void
hls_to_rgb(gdouble *h, gdouble *l, gdouble *s);

/* frames.c: create_style_context (frames, NULL) */
static GtkStyleContext *
create_style_context(GtkWidget *window)
{
	GtkStyleContext *style = gtk_style_context_new();
	char *theme_name = NULL;

	g_object_get(gtk_settings_get_default(), "gtk-theme-name", &theme_name,
		     NULL);
	gtk_style_context_set_path(style, gtk_widget_get_path(window));
	if (theme_name && *theme_name) {
		GtkCssProvider *provider =
			gtk_css_provider_get_named(theme_name, NULL);

		gtk_style_context_add_provider(
			style, GTK_STYLE_PROVIDER(provider),
			GTK_STYLE_PROVIDER_PRIORITY_SETTINGS);
	}
	g_free(theme_name);
	return style;
}

/* theme.c: get_background_color_real () and get_background_color () */
static void
get_background_color_real(GtkStyleContext *context, GtkStateFlags state,
			  GdkRGBA *color)
{
	GdkRGBA *c;

	gtk_style_context_get(context, state, "background-color", &c, NULL);
	*color = *c;
	gdk_rgba_free(c);
}

static void
get_background_color(GtkStyleContext *context, GtkStateFlags state,
		     GdkRGBA *color)
{
	GdkRGBA empty = { 0.0, 0.0, 0.0, 0.0 };
	GdkRGBA rgba;

	get_background_color_real(context, state, &rgba);
	if (gdk_rgba_equal(&rgba, &empty)) {
		GtkWidget *toplevel = gtk_window_new(GTK_WINDOW_TOPLEVEL);

		get_background_color_real(
			gtk_widget_get_style_context(toplevel), state, &rgba);
		gtk_widget_destroy(toplevel);
	}
	*color = rgba;
}

static void
get_light_color(GtkStyleContext *style, GtkStateFlags state, GdkRGBA *color)
{
	get_background_color(style, state, color);
	gtk_style_shade(color, color, LIGHTNESS_MULT);
}

static void
get_dark_color(GtkStyleContext *style, GtkStateFlags state, GdkRGBA *color)
{
	get_background_color(style, state, color);
	gtk_style_shade(color, color, DARKNESS_MULT);
}

/* theme.c: meta_set_color_from_style () */
static void
set_color_from_style(GdkRGBA *color, GtkStyleContext *context,
		     GtkStateFlags state, int component)
{
	GdkRGBA other;

	gtk_style_context_add_class(context, GTK_STYLE_CLASS_BACKGROUND);
	switch (component) {
	case COLOR_BG:
	case COLOR_BASE:
		get_background_color(context, state, color);
		break;
	case COLOR_FG:
	case COLOR_TEXT:
		gtk_style_context_get_color(context, state, color);
		break;
	case COLOR_TEXT_AA:
		gtk_style_context_get_color(context, state, color);
		set_color_from_style(&other, context, state, COLOR_BASE);
		color->red = (color->red + other.red) / 2;
		color->green = (color->green + other.green) / 2;
		color->blue = (color->blue + other.blue) / 2;
		break;
	case COLOR_MID:
		get_light_color(context, state, color);
		get_dark_color(context, state, &other);
		color->red = (color->red + other.red) / 2;
		color->green = (color->green + other.green) / 2;
		color->blue = (color->blue + other.blue) / 2;
		break;
	case COLOR_LIGHT:
		get_light_color(context, state, color);
		break;
	case COLOR_DARK:
		get_dark_color(context, state, color);
		break;
	}
}

static void
send_table(PoloShellUiColorResolver *self)
{
	float values[N_COMPONENTS * G_N_ELEMENTS(STATES) * 4];

	for (int c = 0; c < N_COMPONENTS; c++) {
		for (guint s = 0; s < G_N_ELEMENTS(STATES); s++) {
			float *v = &values[(c * G_N_ELEMENTS(STATES) + s) * 4];
			GdkRGBA color;

			set_color_from_style(&color, self->style, STATES[s], c);
			v[0] = color.red;
			v[1] = color.green;
			v[2] = color.blue;
			v[3] = color.alpha;
		}
	}
	polo_shell_ui_client_color_table(self->client, values,
					 G_N_ELEMENTS(values));
}

static void
send_custom(PoloShellUiColorResolver *self, const char *name)
{
	GdkRGBA color;

	if (gtk_style_context_lookup_color(self->style, name, &color)) {
		float rgba[4] = { color.red, color.green, color.blue,
				  color.alpha };

		polo_shell_ui_client_custom_color(self->client, name, rgba);
	} else {
		polo_shell_ui_client_custom_color(self->client, name, NULL);
	}
}

static void
on_lookup_color(PoloShellUiClient *client, const char *name, gpointer data)
{
	PoloShellUiColorResolver *self = data;

	g_hash_table_add(self->asked, g_strdup(name));
	send_custom(self, name);
}

static void
on_theme_changed(GtkSettings *settings, GParamSpec *pspec, gpointer data)
{
	PoloShellUiColorResolver *self = data;
	GHashTableIter iter;
	gpointer name;

	g_object_unref(self->style);
	self->style = create_style_context(self->window);
	send_table(self);
	g_hash_table_iter_init(&iter, self->asked);
	while (g_hash_table_iter_next(&iter, &name, NULL))
		send_custom(self, name);
}

PoloShellUiColorResolver *
polo_shell_ui_color_resolver_new(PoloShellUiClient *client)
{
	PoloShellUiColorResolver *self =
		g_object_new(POLO_SHELL_UI_TYPE_COLOR_RESOLVER, NULL);

	self->client = g_object_ref(client);
	self->window = g_object_ref_sink(gtk_window_new(GTK_WINDOW_TOPLEVEL));
	self->style = create_style_context(self->window);
	self->asked =
		g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	g_signal_connect_object(client, "lookup-color",
				G_CALLBACK(on_lookup_color), self, 0);
	self->theme_changed = g_signal_connect(
		gtk_settings_get_default(), "notify::gtk-theme-name",
		G_CALLBACK(on_theme_changed), self);
	send_table(self);
	return self;
}

static void
polo_shell_ui_color_resolver_dispose(GObject *object)
{
	PoloShellUiColorResolver *self = POLO_SHELL_UI_COLOR_RESOLVER(object);

	if (self->theme_changed) {
		g_signal_handler_disconnect(gtk_settings_get_default(),
					    self->theme_changed);
		self->theme_changed = 0;
	}
	g_clear_object(&self->style);
	if (self->window) {
		gtk_widget_destroy(self->window);
		g_clear_object(&self->window);
	}
	g_clear_pointer(&self->asked, g_hash_table_unref);
	g_clear_object(&self->client);
	G_OBJECT_CLASS(polo_shell_ui_color_resolver_parent_class)
		->dispose(object);
}

static void
polo_shell_ui_color_resolver_class_init(PoloShellUiColorResolverClass *klass)
{
	G_OBJECT_CLASS(klass)->dispose = polo_shell_ui_color_resolver_dispose;
}

static void
polo_shell_ui_color_resolver_init(PoloShellUiColorResolver *self)
{
}

static void
gtk_style_shade(GdkRGBA *a, GdkRGBA *b, gdouble k)
{
	gdouble red;
	gdouble green;
	gdouble blue;

	red = a->red;
	green = a->green;
	blue = a->blue;

	rgb_to_hls(&red, &green, &blue);

	green *= k;
	if (green > 1.0)
		green = 1.0;
	else if (green < 0.0)
		green = 0.0;

	blue *= k;
	if (blue > 1.0)
		blue = 1.0;
	else if (blue < 0.0)
		blue = 0.0;

	hls_to_rgb(&red, &green, &blue);

	b->red = red;
	b->green = green;
	b->blue = blue;
}

static void
rgb_to_hls(gdouble *r, gdouble *g, gdouble *b)
{
	gdouble min;
	gdouble max;
	gdouble red;
	gdouble green;
	gdouble blue;
	gdouble h, l, s;
	gdouble delta;

	red = *r;
	green = *g;
	blue = *b;

	if (red > green) {
		if (red > blue)
			max = red;
		else
			max = blue;

		if (green < blue)
			min = green;
		else
			min = blue;
	} else {
		if (green > blue)
			max = green;
		else
			max = blue;

		if (red < blue)
			min = red;
		else
			min = blue;
	}

	l = (max + min) / 2;
	s = 0;
	h = 0;

	if (max != min) {
		if (l <= 0.5)
			s = (max - min) / (max + min);
		else
			s = (max - min) / (2 - max - min);

		delta = max - min;
		if (red == max)
			h = (green - blue) / delta;
		else if (green == max)
			h = 2 + (blue - red) / delta;
		else if (blue == max)
			h = 4 + (red - green) / delta;

		h *= 60;
		if (h < 0.0)
			h += 360;
	}

	*r = h;
	*g = l;
	*b = s;
}

static void
hls_to_rgb(gdouble *h, gdouble *l, gdouble *s)
{
	gdouble hue;
	gdouble lightness;
	gdouble saturation;
	gdouble m1, m2;
	gdouble r, g, b;

	lightness = *l;
	saturation = *s;

	if (lightness <= 0.5)
		m2 = lightness * (1 + saturation);
	else
		m2 = lightness + saturation - lightness * saturation;
	m1 = 2 * lightness - m2;

	if (saturation == 0) {
		*h = lightness;
		*l = lightness;
		*s = lightness;
	} else {
		hue = *h + 120;
		while (hue > 360)
			hue -= 360;
		while (hue < 0)
			hue += 360;

		if (hue < 60)
			r = m1 + (m2 - m1) * hue / 60;
		else if (hue < 180)
			r = m2;
		else if (hue < 240)
			r = m1 + (m2 - m1) * (240 - hue) / 60;
		else
			r = m1;

		hue = *h;
		while (hue > 360)
			hue -= 360;
		while (hue < 0)
			hue += 360;

		if (hue < 60)
			g = m1 + (m2 - m1) * hue / 60;
		else if (hue < 180)
			g = m2;
		else if (hue < 240)
			g = m1 + (m2 - m1) * (240 - hue) / 60;
		else
			g = m1;

		hue = *h - 120;
		while (hue > 360)
			hue -= 360;
		while (hue < 0)
			hue += 360;

		if (hue < 60)
			b = m1 + (m2 - m1) * hue / 60;
		else if (hue < 180)
			b = m2;
		else if (hue < 240)
			b = m1 + (m2 - m1) * (240 - hue) / 60;
		else
			b = m1;

		*h = r;
		*l = g;
		*s = b;
	}
}
