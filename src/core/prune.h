/*
 * The prune (plan 8.2): what neither space needs any more, destroyed,
 * after each convergence of the beam, with both spaces read anew.
 *
 *   - the run, a house's versions: the newest RUN_KEEP complete ones,
 *     the active counted; an older one once its copy on the lake has
 *     arrived at its newest snapshot. One not complete is its roll's.
 *   - the lake, the copies of a house's versions: the newest LAKE_KEEP
 *     arrived; an older one once the run holds the version no more,
 *     or the beam would send it back.
 *   - the run, a foundation's copy: every version from the oldest one
 *     a filesystem of the run is a clone of, as a house rolls through
 *     each (7.2); with none, the copy whole (ruled 2026-11-03).
 *   - the lake, a foundation's versions: its newest, any a filesystem
 *     of either space is a clone of, by guid, and any not older than
 *     the newest the run's copy holds, the base of its next stream.
 *
 * A house version is the root's child of <space>/containers or
 * <space>/systems with shtetl:house, a clone of a foundation version;
 * it is complete, or its copy arrived, when a snapshot of it holds
 * shtetl:kind create or update. A house's versions are ordered by
 * their origins' createtxg, a foundation's by their own. A house
 * version goes by promoting every filesystem of its space that is a
 * clone of one of its snapshots, the newer version's own data (7.1),
 * then destroying it, children first; a foundation's version is its
 * snapshot on every dataset of the foundation.
 *
 * Each version pruned is a line of LOG, "# prune NAME". A refusal is
 * said to E and leaves that version as it stands; the rest go on, and
 * the next prune takes it up from what ZFS shows. 0, or -1 when
 * something was refused.
 */

#ifndef	SH_PRUNE_H
#define	SH_PRUNE_H

#include <stdint.h>
#include <stdio.h>

#include "core/err.h"
#include "core/zfs.h"

int sh_prune(struct sh_zfs *z, const char *lake, const char *run,
    int64_t run_keep, int64_t lake_keep, FILE *log, struct sh_err *e);

#endif	/* SH_PRUNE_H */
