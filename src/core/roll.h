/*
 * A foundation's roll (plan 6): one job, the builder's turn held from
 * its first step to its last. The steps:
 *
 *   site, clones, recipe: shtetl.conf read, a recipe repository
 *     missing from zconfig cloned, the recipe resolved, its ver held
 *     to src_branch;
 *   head: on a first roll the foundation's dataset made, with the
 *     site's lake properties; the recipe's datasets made where
 *     missing, a parent the recipe does not give made as a
 *     placeholder, canmount off; the dataset and the recipe's
 *     mounted;
 *   src, patches (src.h);
 *   version: REVISION read from the checkout and held to ver;
 *   obj: the foundation's obj cache made where missing and mounted,
 *     then the build and the install (install.h), the packages and
 *     the commands (commands.h);
 *   inject: foundation.conf, the shaken recipe under what it was
 *     fetched from;
 *   snapshot: the head unmounted, every dataset of it snapshotted at
 *     once as F@<ver>-<branch>-<date>-<create|update>, with
 *     shtetl:kind and shtetl:commit, the src commit.
 *
 * A failure puts every dataset back as its newest version left it,
 * a first roll's destroyed (sh_foundation_idle), stalls the foundation,
 * the step and the reason in shtetl:stall, and resets zbuild; a first
 * roll leaves nothing to stall. shtetl roll F clears a stall and rolls
 * again from the first step.
 */

#ifndef	SH_ROLL_H
#define	SH_ROLL_H

#include <stdio.h>

#include "core/builder.h"
#include "core/err.h"
#include "core/foundation.h"
#include "core/zfs.h"

/* What every roll is given: the daemon's, in a test the test's. */
struct sh_rollenv {
	struct sh_zfs		*re_z;
	struct sh_builder	*re_builder;
	int			re_kq;
	const char		*re_obj;	/* <lake>/foundation-builds */
	const char		*re_resolv;	/* the host's resolv.conf */
	FILE			*re_log;	/* each line a child writes */
	struct sh_err		*re_err;	/* a roll's failure, said */
	unsigned long		re_flags;	/* foundation.conf's: schg */
};

/*
 * A roll of F asked for, as shtetl roll F asks: refused, "F: rolling",
 * while F rolls or waits to; a stall cleared; then F waits for the
 * builder's turn, and rolls. CONFIG and BUILD are where zconfig and
 * zbuild are mounted. ENV outlives the roll.
 *
 * With PKG, as shtetl pkg F asks, a pkg roll (plan 6): its newest
 * version's packages upgraded and nothing else, the steps head (F and
 * the datasets that version's foundation.conf gives, mounted), pkg
 * upgrade of the packages it gives in a chroot (commands.h) and
 * snapshot,
 * F@<ver>-<date>-pkg with the version's shtetl:commit; its failure
 * rolls back and stalls, and leaves zbuild alone; CONFIG and BUILD
 * may be NULL. Refused, "F: no version", for F with none.
 */
int sh_roll_ask(struct sh_foundation *f, const struct sh_rollenv *env,
    const char *config, const char *build, int pkg, struct sh_err *e);

/*
 * F's roll, if it has one, stopped where it stands, as when the daemon
 * stops: its child killed, nothing undone, which the next start does.
 */
void sh_roll_stop(struct sh_foundation *f);

/* The step R is at. */
const char *sh_roll_step(const struct sh_roll *r);

#endif	/* SH_ROLL_H */
