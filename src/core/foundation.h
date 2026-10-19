/*
 * The foundation actors (plan 6, 9): one for each filesystem under
 * <lake>/foundations, each a foundation recipe's dataset whose
 * snapshots are its versions, and one for a recipe whose first roll
 * is under way.
 */

#ifndef	SH_FOUNDATION_H
#define	SH_FOUNDATION_H

#include <stddef.h>
#include <stdio.h>

#include "core/err.h"
#include "core/zfs.h"

enum sh_fstate {
	SH_F_IDLE,
	SH_F_WAITING,		/* for the builder's turn */
	SH_F_ROLLING,
	SH_F_STALLED
};

struct sh_roll;

struct sh_foundation {
	char		*f_name;	/* the recipe's, its dataset's last */
	char		*f_ds;
	enum sh_fstate	f_state;
	char		*f_newest;	/* its newest version, or NULL */
	char		*f_stall;	/* why it is stalled */
	struct sh_roll	*f_roll;	/* the roll waiting or under way */
};

/*
 * The foundations under ROOT, <lake>/foundations, sorted by name into
 * *FP and *NP, each started as an idle one is (sh_foundation_idle); a
 * foundation with no version left is no foundation. Started, it is
 * idle, or stalled when shtetl:stall says so or ZFS refused a step of
 * the start, which is said to E. -1 only when ROOT's children cannot
 * be read.
 */
int sh_foundations_start(struct sh_zfs *z, const char *root,
    struct sh_foundation ***fp, size_t *np, struct sh_err *e);
void sh_foundations_free(struct sh_foundation **f, size_t n);

/*
 * The foundation NAME under ROOT, in *FP's place by name, made when
 * there is none, with no version: a first roll's.
 */
struct sh_foundation *sh_foundation_find(struct sh_foundation ***fp,
    size_t *np, const char *root, const char *name);

/*
 * DS and everything below it put where an idle foundation is,
 * children first: unmounted and rolled back to its newest snapshot,
 * which changes nothing where nothing was written, or destroyed where
 * it has none, which is what a first roll that never finished leaves.
 */
int sh_foundation_idle(struct sh_zfs *z, const char *ds, struct sh_err *e);

/* F's line of shtetl status. */
void sh_foundation_status(const struct sh_foundation *f, FILE *out);

#endif	/* SH_FOUNDATION_H */
