/*
 * A foundation's packages and commands (plan 6 step 5), as steps of
 * its roll, carried from main, and a house's as it is made (m5-create),
 * in the order docs/configuration.md gives: the recipe's
 * commands-external on the host, each piece by sh -e with the head's
 * path in SHTETL_ROOT, the name in SHTETL_NAME and the kind in
 * SHTETL_KIND; then in a chroot of the
 * head, devfs mounted on its dev and the host's resolv.conf lent for
 * pkg where the head has none, the packages (pkg -c HEAD install) and
 * the commands, each piece by /bin/sh -e; devfs unmounted and the
 * resolv.conf taken back. Every label and piece in the recipe's order,
 * but those of the layer CM_RAN, a house's foundation's, which ran when
 * the foundation was built (plan 5), and in a house's roll those that
 * CM_HAD, the house.conf of the version it rolls from, holds alike,
 * the same text under the same key and label, which ran when that
 * version was made (plan 7.2 step 4); no chroot where nothing is left.
 *
 * A failure part way leaves devfs mounted; sh_foundation_idle, which
 * every failure and every start runs, unmounts it, and a house's
 * version releases it before it is unmounted (H20).
 */

#ifndef	SH_COMMANDS_H
#define	SH_COMMANDS_H

#include "core/cascade.h"
#include "core/job.h"

struct sh_commands {
	const char		*cm_head;	/* where the head is mounted */
	const char		*cm_name;	/* the foundation or house */
	const char		*cm_kind;	/* foundation, container */
	const char		*cm_ran;	/* the layer run, or NULL */
	const struct sh_node	*cm_had;	/* a house.conf, or NULL */
	const struct sh_recipe	*cm_recipe;	/* none for an upgrade */
	const struct sh_node	*cm_pkgs;	/* the packages, or NULL */
	const char		*cm_resolv;	/* the host's resolv.conf */
	int			cm_upgrade;	/* a pkg roll's: pkg upgrade */
	int			cm_lent;	/* resolv.conf lent */
};

/*
 * CM's steps added to J; CM outlives them. An upgrade's are the
 * chroot's with pkg upgrade of CM_PKGS in it, the packages the
 * version's foundation.conf lists (ruled 2026-10-20), and nothing
 * else; none where it lists none.
 */
void sh_commands_steps(struct sh_job *j, struct sh_commands *cm);

#endif	/* SH_COMMANDS_H */
