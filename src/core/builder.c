/*
 * The builder's turns.
 */

#include <stdlib.h>

#include "core/builder.h"
#include "core/mem.h"

struct sh_turn {
	void		(*t_go)(void *);
	void		*t_arg;
	struct sh_turn	*t_next;
};

void
sh_builder_init(struct sh_builder *b)
{
	b->b_busy = 0;
	b->b_wait = NULL;
	b->b_last = &b->b_wait;
}

void
sh_builder_ask(struct sh_builder *b, void (*go)(void *), void *arg)
{
	struct sh_turn *t;

	if (!b->b_busy) {
		b->b_busy = 1;
		go(arg);
		return;
	}
	t = sh_calloc(1, sizeof (*t));
	t->t_go = go;
	t->t_arg = arg;
	*b->b_last = t;
	b->b_last = &t->t_next;
}

void
sh_builder_give(struct sh_builder *b)
{
	struct sh_turn *t;

	if ((t = b->b_wait) == NULL) {
		b->b_busy = 0;
		return;
	}
	if ((b->b_wait = t->t_next) == NULL)
		b->b_last = &b->b_wait;
	t->t_go(t->t_arg);
	free(t);
}
