/*
 * A foundation's packages and commands (plan 6 step 5), as steps of
 * its roll, carried from main, in the order docs/configuration.md
 * gives: the recipe's commands-external on the host, each piece by
 * sh -e with the head's path in SHTETL_ROOT, the foundation's name in
 * SHTETL_NAME and foundation in SHTETL_KIND; then in a chroot of the
 * head, devfs mounted on its dev and the host's resolv.conf lent for
 * pkg where the head has none, the packages (pkg -c HEAD install) and
 * the commands, each piece by /bin/sh -e; devfs unmounted and the
 * resolv.conf taken back. Every label and piece in the recipe's order.
 *
 * A failure part way leaves devfs mounted; sh_foundation_idle, which
 * every failure and every start runs, unmounts it.
 */

#ifndef	SH_COMMANDS_H
#define	SH_COMMANDS_H

#include "core/cascade.h"
#include "core/job.h"

struct sh_commands {
	const char		*cm_head;	/* where the head is mounted */
	const char		*cm_name;	/* the foundation's */
	const struct sh_recipe	*cm_recipe;
	const char		*cm_resolv;	/* the host's resolv.conf */
	int			cm_lent;	/* resolv.conf lent */
};

/* CM's steps added to J; CM outlives them. */
void sh_commands_steps(struct sh_job *j, struct sh_commands *cm);

#endif	/* SH_COMMANDS_H */
