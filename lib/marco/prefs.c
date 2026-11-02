/*
 * Copyright (C) 2001 Havoc Pennington, Copyright (C) 2002 Red Hat Inc.
 * Copyright (C) 2006 Elijah Newren
 * Copyright (C) 2008 Thomas Thurman
 * Copyright (C) 1998, 2001 Tim Janik
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Ported from Marco 1.28.2 src/core/prefs.c (button_layout_handler,
 * button_function_from_string, button_opposite_function, titlebar_handler)
 * and src/ui/ui.c (meta_ui_parse_accelerator, meta_ui_parse_modifier), with
 * GTK 3.24 gtkaccelgroup.c gtk_accelerator_parse_with_keycode for the
 * accelerator grammar.
 */

#include <stdlib.h>
#include <string.h>

#include <pango/pango.h>

#include "polo-marco.h"

static PoloButtonFunction
button_function_from_string(const char *str)
{
	if (strcmp(str, "menu") == 0)
		return POLO_BUTTON_FUNCTION_MENU;
	else if (strcmp(str, "appmenu") == 0)
		return POLO_BUTTON_FUNCTION_APPMENU;
	else if (strcmp(str, "minimize") == 0)
		return POLO_BUTTON_FUNCTION_MINIMIZE;
	else if (strcmp(str, "maximize") == 0)
		return POLO_BUTTON_FUNCTION_MAXIMIZE;
	else if (strcmp(str, "close") == 0)
		return POLO_BUTTON_FUNCTION_CLOSE;
	else if (strcmp(str, "shade") == 0)
		return POLO_BUTTON_FUNCTION_SHADE;
	else if (strcmp(str, "above") == 0)
		return POLO_BUTTON_FUNCTION_ABOVE;
	else if (strcmp(str, "stick") == 0)
		return POLO_BUTTON_FUNCTION_STICK;
	else
		return POLO_BUTTON_FUNCTION_LAST;
}

static PoloButtonFunction
button_opposite_function(PoloButtonFunction ofwhat)
{
	switch (ofwhat) {
	case POLO_BUTTON_FUNCTION_SHADE:
		return POLO_BUTTON_FUNCTION_UNSHADE;
	case POLO_BUTTON_FUNCTION_UNSHADE:
		return POLO_BUTTON_FUNCTION_SHADE;
	case POLO_BUTTON_FUNCTION_ABOVE:
		return POLO_BUTTON_FUNCTION_UNABOVE;
	case POLO_BUTTON_FUNCTION_UNABOVE:
		return POLO_BUTTON_FUNCTION_ABOVE;
	case POLO_BUTTON_FUNCTION_STICK:
		return POLO_BUTTON_FUNCTION_UNSTICK;
	case POLO_BUTTON_FUNCTION_UNSTICK:
		return POLO_BUTTON_FUNCTION_STICK;
	default:
		return POLO_BUTTON_FUNCTION_LAST;
	}
}

static void
parse_side(const char *side, PoloButtonFunction *buttons_out,
	   gboolean *has_spacer)
{
	gboolean used[POLO_BUTTON_FUNCTION_LAST];
	char **buttons;
	int i, b;

	for (i = 0; i < POLO_BUTTON_FUNCTION_LAST; i++)
		used[i] = FALSE;

	buttons = g_strsplit(side, ",", -1);
	i = 0;
	b = 0;
	while (buttons[b] != NULL) {
		PoloButtonFunction f = button_function_from_string(buttons[b]);

		if (i > 0 && strcmp("spacer", buttons[b]) == 0) {
			has_spacer[i - 1] = TRUE;
		} else if (f != POLO_BUTTON_FUNCTION_LAST && !used[f]) {
			buttons_out[i] = f;
			used[f] = TRUE;
			++i;
			f = button_opposite_function(f);
			if (f != POLO_BUTTON_FUNCTION_LAST)
				buttons_out[i++] = f;
		}
		++b;
	}
	buttons_out[i] = POLO_BUTTON_FUNCTION_LAST;
	has_spacer[i] = FALSE;
	g_strfreev(buttons);
}

void
polo_marco_parse_button_layout(const char *string, gboolean rtl,
			       PoloButtonLayout *layout)
{
	PoloButtonLayout new_layout;
	char **sides = NULL;
	int i, j;

	for (i = 0; i <= POLO_MAX_BUTTONS_PER_CORNER; i++) {
		new_layout.left_buttons[i] = POLO_BUTTON_FUNCTION_LAST;
		new_layout.right_buttons[i] = POLO_BUTTON_FUNCTION_LAST;
		new_layout.left_buttons_has_spacer[i] = FALSE;
		new_layout.right_buttons_has_spacer[i] = FALSE;
	}

	if (string)
		sides = g_strsplit(string, ":", 2);
	if (sides != NULL && sides[0] != NULL)
		parse_side(sides[0], new_layout.left_buttons,
			   new_layout.left_buttons_has_spacer);
	if (sides != NULL && sides[0] != NULL && sides[1] != NULL)
		parse_side(sides[1], new_layout.right_buttons,
			   new_layout.right_buttons_has_spacer);
	g_strfreev(sides);

	if (rtl) {
		PoloButtonLayout rtl_layout;

		for (i = 0; i <= POLO_MAX_BUTTONS_PER_CORNER; i++) {
			rtl_layout.left_buttons[i] = POLO_BUTTON_FUNCTION_LAST;
			rtl_layout.right_buttons[i] = POLO_BUTTON_FUNCTION_LAST;
			rtl_layout.left_buttons_has_spacer[i] = FALSE;
			rtl_layout.right_buttons_has_spacer[i] = FALSE;
		}

		for (i = 0;
		     new_layout.left_buttons[i] != POLO_BUTTON_FUNCTION_LAST;
		     i++)
			;
		for (j = 0; j < i; j++) {
			rtl_layout.right_buttons[j] =
				new_layout.left_buttons[i - j - 1];
			if (j == 0)
				rtl_layout.right_buttons_has_spacer[i - 1] =
					new_layout
						.left_buttons_has_spacer[i - j -
									 1];
			else
				rtl_layout.right_buttons_has_spacer[j - 1] =
					new_layout
						.left_buttons_has_spacer[i - j -
									 1];
		}
		rtl_layout.right_buttons[j] = POLO_BUTTON_FUNCTION_LAST;
		rtl_layout.right_buttons_has_spacer[j] = FALSE;

		for (i = 0;
		     new_layout.right_buttons[i] != POLO_BUTTON_FUNCTION_LAST;
		     i++)
			;
		for (j = 0; j < i; j++) {
			rtl_layout.left_buttons[j] =
				new_layout.right_buttons[i - j - 1];
			if (j == 0)
				rtl_layout.left_buttons_has_spacer[i - 1] =
					new_layout.right_buttons_has_spacer[i -
									    j -
									    1];
			else
				rtl_layout.left_buttons_has_spacer[j - 1] =
					new_layout.right_buttons_has_spacer[i -
									    j -
									    1];
		}
		rtl_layout.left_buttons[j] = POLO_BUTTON_FUNCTION_LAST;
		rtl_layout.left_buttons_has_spacer[j] = FALSE;

		new_layout = rtl_layout;
	}

	*layout = new_layout;
}

gboolean
polo_marco_button_layout_equal(const PoloButtonLayout *a,
			       const PoloButtonLayout *b)
{
	int i;

	for (i = 0; i <= POLO_MAX_BUTTONS_PER_CORNER; i++) {
		if (a->left_buttons[i] != b->left_buttons[i] ||
		    a->right_buttons[i] != b->right_buttons[i] ||
		    a->left_buttons_has_spacer[i] !=
			    b->left_buttons_has_spacer[i] ||
		    a->right_buttons_has_spacer[i] !=
			    b->right_buttons_has_spacer[i])
			return FALSE;
	}
	return TRUE;
}

static gboolean
is_token(const char *s, const char *token)
{
	size_t n = strlen(token);

	return g_ascii_strncasecmp(s, token, n) == 0;
}

static gboolean
is_modx(const char *s)
{
	return s[0] == '<' && (s[1] == 'm' || s[1] == 'M') &&
		(s[2] == 'o' || s[2] == 'O') && (s[3] == 'd' || s[3] == 'D') &&
		s[4] >= '1' && s[4] <= '5' && s[5] == '>';
}

static gboolean
is_keycode(const char *s)
{
	return s[0] == '0' && s[1] == 'x' && g_ascii_isxdigit(s[2]) &&
		g_ascii_isxdigit(s[3]);
}

enum {
	GTK_SHIFT = 1 << 0,
	GTK_CONTROL = 1 << 2,
	GTK_MOD1 = 1 << 3,
	GTK_MOD2 = 1 << 4,
	GTK_MOD3 = 1 << 5,
	GTK_MOD4 = 1 << 6,
	GTK_MOD5 = 1 << 7,
	GTK_SUPER = 1 << 26,
	GTK_HYPER = 1 << 27,
	GTK_META = 1 << 28,
	GTK_RELEASE = 1 << 30,
};

static void
gtk_accelerator_parse(const char *accelerator, char **key_name, guint *mods_out)
{
	static const guint mod_vals[] = { GTK_MOD1, GTK_MOD2, GTK_MOD3,
					  GTK_MOD4, GTK_MOD5 };
	gboolean error = FALSE;
	char *name = NULL;
	guint mods = 0;
	int len = strlen(accelerator);

	while (len) {
		if (*accelerator == '<') {
			if (len >= 9 && is_token(accelerator, "<release>")) {
				accelerator += 9;
				len -= 9;
				mods |= GTK_RELEASE;
			} else if (len >= 9 &&
				   is_token(accelerator, "<primary>")) {
				accelerator += 9;
				len -= 9;
				mods |= GTK_CONTROL;
			} else if (len >= 9 &&
				   is_token(accelerator, "<control>")) {
				accelerator += 9;
				len -= 9;
				mods |= GTK_CONTROL;
			} else if (len >= 7 &&
				   is_token(accelerator, "<shift>")) {
				accelerator += 7;
				len -= 7;
				mods |= GTK_SHIFT;
			} else if (len >= 6 &&
				   is_token(accelerator, "<shft>")) {
				accelerator += 6;
				len -= 6;
				mods |= GTK_SHIFT;
			} else if (len >= 6 &&
				   is_token(accelerator, "<ctrl>")) {
				accelerator += 6;
				len -= 6;
				mods |= GTK_CONTROL;
			} else if (len >= 6 && is_modx(accelerator)) {
				len -= 6;
				accelerator += 4;
				mods |= mod_vals[*accelerator - '1'];
				accelerator += 2;
			} else if (len >= 5 && is_token(accelerator, "<ctl>")) {
				accelerator += 5;
				len -= 5;
				mods |= GTK_CONTROL;
			} else if (len >= 5 && is_token(accelerator, "<alt>")) {
				accelerator += 5;
				len -= 5;
				mods |= GTK_MOD1;
			} else if (len >= 6 &&
				   is_token(accelerator, "<meta>")) {
				accelerator += 6;
				len -= 6;
				mods |= GTK_META;
			} else if (len >= 7 &&
				   is_token(accelerator, "<hyper>")) {
				accelerator += 7;
				len -= 7;
				mods |= GTK_HYPER;
			} else if (len >= 7 &&
				   is_token(accelerator, "<super>")) {
				accelerator += 7;
				len -= 7;
				mods |= GTK_SUPER;
			} else {
				char last_ch = *accelerator;

				while (last_ch && last_ch != '>') {
					accelerator += 1;
					len -= 1;
					last_ch = *accelerator;
				}
			}
		} else {
			if (len >= 4 && is_keycode(accelerator)) {
				error = TRUE;
				break;
			}
			name = g_strdup(accelerator);
			accelerator += len;
			len -= len;
		}
	}

	if (error) {
		g_clear_pointer(&name, g_free);
		mods = 0;
	}
	*key_name = name;
	*mods_out = mods;
}

static PoloVirtualModifier
virtual_modifiers(guint mods)
{
	PoloVirtualModifier mask = 0;

	if (mods & GTK_SHIFT)
		mask |= POLO_VIRTUAL_SHIFT_MASK;
	if (mods & GTK_CONTROL)
		mask |= POLO_VIRTUAL_CONTROL_MASK;
	if (mods & GTK_MOD1)
		mask |= POLO_VIRTUAL_ALT_MASK;
	if (mods & GTK_MOD2)
		mask |= POLO_VIRTUAL_MOD2_MASK;
	if (mods & GTK_MOD3)
		mask |= POLO_VIRTUAL_MOD3_MASK;
	if (mods & GTK_MOD4)
		mask |= POLO_VIRTUAL_MOD4_MASK;
	if (mods & GTK_MOD5)
		mask |= POLO_VIRTUAL_MOD5_MASK;
	if (mods & GTK_SUPER)
		mask |= POLO_VIRTUAL_SUPER_MASK;
	if (mods & GTK_HYPER)
		mask |= POLO_VIRTUAL_HYPER_MASK;
	if (mods & GTK_META)
		mask |= POLO_VIRTUAL_META_MASK;
	return mask;
}

static void
accelerator_parse(const char *accel, char **key_name, guint *keycode,
		  guint *mods)
{
	if (accel[0] == '0' && accel[1] == 'x') {
		*key_name = NULL;
		*keycode = (guint)strtoul(accel, NULL, 16);
		*mods = 0;
	} else {
		*keycode = 0;
		gtk_accelerator_parse(accel, key_name, mods);
	}
}

gboolean
polo_marco_parse_accelerator(const char *accel, char **key_name, guint *keycode,
			     PoloVirtualModifier *mask)
{
	char *name = NULL;
	guint code = 0, mods = 0;

	*key_name = NULL;
	*keycode = 0;
	*mask = 0;

	if (!accel || !accel[0] || strcmp(accel, "disabled") == 0)
		return TRUE;

	accelerator_parse(accel, &name, &code, &mods);
	if (mods == 0 && name == NULL && code == 0)
		return FALSE;
	if (name == NULL && code == 0)
		return FALSE;
	if (mods & GTK_RELEASE) {
		g_free(name);
		return FALSE;
	}

	*key_name = name;
	*keycode = code;
	*mask = virtual_modifiers(mods);
	return TRUE;
}

gboolean
polo_marco_parse_modifier(const char *accel, PoloVirtualModifier *mask)
{
	char *name = NULL;
	guint code = 0, mods = 0;

	*mask = 0;

	if (accel == NULL || !accel[0] || strcmp(accel, "disabled") == 0)
		return TRUE;

	accelerator_parse(accel, &name, &code, &mods);
	if (mods == 0 && name == NULL && code == 0)
		return FALSE;
	if (name != NULL || code != 0) {
		g_free(name);
		return FALSE;
	}
	if (mods & GTK_RELEASE)
		return FALSE;

	*mask = virtual_modifiers(mods);
	return TRUE;
}

char *
polo_marco_parse_font(const char *string)
{
	PangoFontDescription *desc;
	char *normalized;

	if (string == NULL)
		return NULL;
	desc = pango_font_description_from_string(string);
	if (desc == NULL)
		return NULL;
	normalized = pango_font_description_to_string(desc);
	pango_font_description_free(desc);
	return normalized;
}
