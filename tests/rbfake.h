/*
 * A rebaser for the tests (src/core/rebase.h), acting on the fake ZFS
 * (tests/zfake.c) as zfs_rebase acts on pools: the clone form's result
 * made unmounted, mountpoint none, readonly; the dataset form's
 * pre-apply snapshot taken; a conflict the test sets stopping the start
 * at the gate with a resolution beside the manifest, in the tool's own
 * grammar, and the record on the result; --continue done once the
 * document is complete and --no-merge is not given; --abort taking the
 * rebase away. Every call is logged, one line each.
 */

#ifndef	SH_RBFAKE_H
#define	SH_RBFAKE_H

#include "core/rebase.h"

extern const struct sh_rebaser rbfake;

/* Everything forgotten: the settings and the log. */
void rbfake_reset(void);

/*
 * The STEPth start (from 1) of RESULT stops at the gate, PATHS (as the
 * tree spells them, NULL-ended) its conflicts, each "-".
 */
void rbfake_conflicts(const char *result, int step,
    const char *const *paths);

/* The STEPth start of RESULT exits RC (2, refused; 3, failed). */
void rbfake_exit(const char *result, int step, int rc);

/* What the picker does with the resolution at PATH, for -i. */
void rbfake_picker(int (*fn)(const char *path, void *arg), void *arg);

/* The calls, a line each: "start ...", "continue ...", "abort ...". */
const char *rbfake_log(void);

/* The resolution of the open rebase IDENT, or NULL. */
const char *rbfake_resolution(const char *ident);

#endif	/* SH_RBFAKE_H */
