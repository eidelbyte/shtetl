/*
 * The foundation actors (plan 6, 9): one for each filesystem under
 * <lake>/foundations, each a foundation recipe's dataset whose
 * snapshots are its versions. M1's part of one is its start.
 */

#ifndef	SH_FOUNDATION_H
#define	SH_FOUNDATION_H

#include <stddef.h>
#include <stdio.h>

#include "core/err.h"
#include "core/zfs.h"

enum sh_fstate {
	SH_F_IDLE,
	SH_F_STALLED
};

struct sh_foundation {
	char		*f_name;	/* the recipe's, its dataset's last */
	char		*f_ds;
	enum sh_fstate	f_state;
	char		*f_newest;	/* its newest version's snapshot */
	char		*f_stall;	/* why it is stalled */
};

/*
 * The foundations under ROOT, <lake>/foundations, sorted by name into
 * *FP and *NP, each started as an idle one is: every dataset of its
 * tree, children first, unmounted and rolled back to its newest
 * snapshot, or destroyed where it has none, which is what a first
 * build that never finished leaves; a foundation with no version
 * left is no foundation. Started, it is idle, or stalled when
 * shtetl:stall says so or ZFS refused a step of the start, which is
 * said to E. -1 only when ROOT's children cannot be read.
 */
int sh_foundations_start(struct sh_zfs *z, const char *root,
    struct sh_foundation **fp, size_t *np, struct sh_err *e);
void sh_foundations_free(struct sh_foundation *f, size_t n);

/* F's line of shtetl status. */
void sh_foundation_status(const struct sh_foundation *f, FILE *out);

#endif	/* SH_FOUNDATION_H */
