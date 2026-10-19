/*
 * The builder (plan 6, 9): builds share zbuild and the CPU, so one
 * foundation's roll at a time takes a turn, in the order they asked,
 * and nothing polls for one.
 */

#ifndef	SH_BUILDER_H
#define	SH_BUILDER_H

struct sh_turn;

struct sh_builder {
	int		b_busy;
	struct sh_turn	*b_wait;	/* the first asked first */
	struct sh_turn	**b_last;
};

void sh_builder_init(struct sh_builder *b);

/*
 * A turn asked for: GO(ARG) called when it comes, at once when the
 * builder is free, else when the turns asked before it are given back.
 */
void sh_builder_ask(struct sh_builder *b, void (*go)(void *), void *arg);

/* The turn given back: the next asked goes. */
void sh_builder_give(struct sh_builder *b);

#endif	/* SH_BUILDER_H */
