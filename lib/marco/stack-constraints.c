/*
 * Copyright (C) 2001 Havoc Pennington
 * Copyright (C) 2002, 2003 Red Hat, Inc.
 * Copyright (C) 2004 Rob Adams
 * Copyright (C) 2004, 2005 Elijah Newren
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Ported from Marco 1.28.2 src/core/stack.c: the transient constraint graph
 * (add_constraint, create_constraints, graph_constraints, ensure_above,
 * traverse_constraint, apply_constraints) and
 * meta_window_set_stack_position_no_sync. Windows are indexes into a
 * PoloMarcoStackWindow array instead of MetaWindow pointers.
 */

#include "polo-marco.h"

#define WINDOW_HAS_TRANSIENT_TYPE(w)                                           \
	((w)->type == POLO_WINDOW_DIALOG ||                                    \
	 (w)->type == POLO_WINDOW_MODAL_DIALOG ||                              \
	 (w)->type == POLO_WINDOW_TOOLBAR || (w)->type == POLO_WINDOW_MENU ||  \
	 (w)->type == POLO_WINDOW_UTILITY)

#define WINDOW_TRANSIENT_FOR_WHOLE_GROUP(w)                                    \
	(((w)->transient_for < 0 || (w)->transient_parent_is_root_window) &&   \
	 WINDOW_HAS_TRANSIENT_TYPE(w))

#define WINDOW_IN_STACK(w) ((w)->stack_position >= 0)

typedef struct Constraint Constraint;

struct Constraint {
	int above;
	int below;
	Constraint *next;
	GSList *next_nodes;
	unsigned int applied : 1;
	unsigned int has_prev : 1;
};

typedef struct {
	PoloMarcoStackWindow *windows;
	int n_windows;
} Stack;

void
polo_marco_set_stack_position(PoloMarcoStackWindow *windows, int n_windows,
			      int window, int position)
{
	PoloMarcoStackWindow *w = &windows[window];
	int low, high, delta;
	int i;

	g_return_if_fail(w->stack_position >= 0);
	g_return_if_fail(position >= 0);

	if (position == w->stack_position)
		return;

	if (position < w->stack_position) {
		low = position;
		high = w->stack_position - 1;
		delta = 1;
	} else {
		low = w->stack_position + 1;
		high = position;
		delta = -1;
	}

	for (i = 0; i < n_windows; i++) {
		PoloMarcoStackWindow *other = &windows[i];

		if (other->stack_position >= low &&
		    other->stack_position <= high)
			other->stack_position += delta;
	}

	w->stack_position = position;
}

static void
add_constraint(Stack *stack, Constraint **constraints, int above, int below)
{
	Constraint *c;
	int below_position = stack->windows[below].stack_position;

	c = constraints[below_position];
	while (c != NULL) {
		if (c->above == above)
			return;
		c = c->next;
	}

	c = g_new(Constraint, 1);
	c->above = above;
	c->below = below;
	c->next = constraints[below_position];
	c->next_nodes = NULL;
	c->applied = FALSE;
	c->has_prev = FALSE;

	constraints[below_position] = c;
}

static void
create_constraints(Stack *stack, Constraint **constraints,
		   const int *group_order)
{
	int i;

	for (i = 0; i < stack->n_windows; i++) {
		PoloMarcoStackWindow *w = &stack->windows[i];

		if (!WINDOW_IN_STACK(w))
			continue;

		if (WINDOW_TRANSIENT_FOR_WHOLE_GROUP(w)) {
			int j;

			for (j = 0; j < stack->n_windows; j++) {
				int g = group_order[j];
				PoloMarcoStackWindow *group_window =
					&stack->windows[g];
				gboolean same_group = g == i ||
					(w->group != 0 &&
					 group_window->group == w->group);

				if (!same_group)
					continue;

				if (!WINDOW_IN_STACK(group_window))
					continue;

				if (!WINDOW_HAS_TRANSIENT_TYPE(group_window))
					add_constraint(stack, constraints, i,
						       g);
			}
		} else if (w->transient_for >= 0 &&
			   !w->transient_parent_is_root_window) {
			PoloMarcoStackWindow *parent =
				&stack->windows[w->transient_for];

			if (WINDOW_IN_STACK(parent))
				add_constraint(stack, constraints, i,
					       w->transient_for);
		}
	}
}

static void
graph_constraints(Stack *stack, Constraint **constraints, int n_constraints)
{
	int i;

	i = 0;
	while (i < n_constraints) {
		Constraint *c;

		c = constraints[i];
		while (c != NULL) {
			Constraint *n;

			g_assert(stack->windows[c->below].stack_position == i);

			n = constraints[stack->windows[c->above]
						.stack_position];
			while (n != NULL) {
				c->next_nodes =
					g_slist_prepend(c->next_nodes, n);
				n->has_prev = TRUE;

				n = n->next;
			}

			c = c->next;
		}

		++i;
	}
}

static void
free_constraints(Constraint **constraints, int n_constraints)
{
	int i;

	i = 0;
	while (i < n_constraints) {
		Constraint *c;

		c = constraints[i];
		while (c != NULL) {
			Constraint *next = c->next;

			g_slist_free(c->next_nodes);

			g_free(c);

			c = next;
		}

		++i;
	}
}

static void
ensure_above(Stack *stack, int above_index, int below_index)
{
	PoloMarcoStackWindow *above = &stack->windows[above_index];
	PoloMarcoStackWindow *below = &stack->windows[below_index];

	if (WINDOW_HAS_TRANSIENT_TYPE(above) && above->layer < below->layer)
		above->layer = below->layer;

	if (above->stack_position < below->stack_position) {
		polo_marco_set_stack_position(stack->windows, stack->n_windows,
					      above_index,
					      below->stack_position);
		g_assert(below->stack_position + 1 == above->stack_position);
	}
}

static void
traverse_constraint(Stack *stack, Constraint *c)
{
	GSList *tmp;

	if (c->applied)
		return;

	ensure_above(stack, c->above, c->below);
	c->applied = TRUE;

	tmp = c->next_nodes;
	while (tmp != NULL) {
		traverse_constraint(stack, tmp->data);

		tmp = tmp->next;
	}
}

static void
apply_constraints(Stack *stack, Constraint **constraints, int n_constraints)
{
	GSList *heads;
	GSList *tmp;
	int i;

	heads = NULL;
	i = 0;
	while (i < n_constraints) {
		Constraint *c;

		c = constraints[i];
		while (c != NULL) {
			if (!c->has_prev)
				heads = g_slist_prepend(heads, c);

			c = c->next;
		}

		++i;
	}

	tmp = heads;
	while (tmp != NULL) {
		Constraint *c = tmp->data;

		traverse_constraint(stack, c);

		tmp = tmp->next;
	}

	g_slist_free(heads);
}

void
polo_marco_stack_constrain(PoloMarcoStackWindow *windows, int n_windows,
			   const int *group_order, int n_positions)
{
	Stack stack = { windows, n_windows };
	Constraint **constraints;

	constraints = g_new0(Constraint *, n_positions);

	create_constraints(&stack, constraints, group_order);

	graph_constraints(&stack, constraints, n_positions);

	apply_constraints(&stack, constraints, n_positions);

	free_constraints(constraints, n_positions);
	g_free(constraints);
}
