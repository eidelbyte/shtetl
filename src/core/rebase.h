/*
 * zfs_rebase, run and not linked (plan 2): a step of a job whose child
 * runs it, watched as every child is. It exits 0 done, 1 stopped at
 * conflicts, 2 refused before anything was touched, 3 failed part way
 * or drifted (zfs_rebase(8)). While a run is open its result carries
 * zfs_rebase:tag set on itself, and zfs_rebase:phase, the gate it is
 * at, and it holds its inputs, until done or --abort; an --abort of a
 * run in applying2 steps it back to the conflicts gate, and a second
 * takes the rebase away. Two implementations, picked at the link as
 * the ZFS layer's are: src/fbsd/rebase.c runs the program;
 * tests/rbfake.c is a fake over the fake ZFS.
 */

#ifndef	SH_REBASE_H
#define	SH_REBASE_H

#include "core/job.h"

/* The user properties a run's result carries while the run is open. */
#define	SH_REBASE_TAG	"zfs_rebase:tag"
#define	SH_REBASE_PHASE	"zfs_rebase:phase"

/* A step of J named STEP: zfs_rebase with ARGV, NULL-ended, copied. */
void sh_rebase(struct sh_job *j, const char *step, const char *const *argv);

#endif	/* SH_REBASE_H */
