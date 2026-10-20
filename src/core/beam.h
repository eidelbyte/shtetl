/*
 * The beam (plan 8.1, 9): one actor for the lake and the run of this
 * host, every dataset's copy converging to its owner's. Foundation
 * versions go from <lake>/foundations to <run>/foundations, house
 * versions from <run>/containers and <run>/systems to <lake>/containers
 * and <lake>/systems, in that order: a foundation version before any
 * house version cloned from it.
 *
 * A convergence is one job of looks and streams. A look reads both
 * spaces. A receive to resume below a copy's root is resumed first, and
 * that look does nothing else. Otherwise, for each dataset below an
 * owner's root:
 *
 *   - the shtetl:* properties of its filesystem are made its owner's on
 *     the copy, set or let go, and so are those of each version's
 *     snapshots once all of that version is there (every dataset of
 *     its top, the root's child it is in, that has the snapshot); a
 *     top's snapshot last, so a version whose top's copy holds them
 *     has arrived, "# arrived SNAP" in the log;
 *   - each snapshot the copy lacks after the newest of the dataset's
 *     history whose guid the copy's space holds, wherever it is there,
 *     is sent, oldest first, a parent before its children and a clone's
 *     origin before it: an incremental onto a copy there, a clone
 *     stream onto that space's snapshot of the guid for a copy not
 *     there, a full stream where none of the history is held.
 *
 * Each stream is a call in a child of its own (sh_job_fork), its words
 * in the log, "# send SNAP" or "# resume DS" as it begins; a look that
 * sent anything is followed by another, and one that sends nothing
 * ends the convergence. A stream that fails ends it, "# failed: why"
 * in the log, and nothing is destroyed: a broken stream is a receive
 * to resume, the next convergence's first.
 *
 * It converges when it begins, when told of a snapshot (once more at
 * the end of a convergence under way), and on a timer: the period
 * after a convergence ending well; after one failing, the retry,
 * doubled with each failure after it up to the period.
 */

#ifndef	SH_BEAM_H
#define	SH_BEAM_H

#include <stdio.h>

#include "core/zfs.h"

/* What the beam is given: the daemon's, in a test the test's. */
struct sh_beamenv {
	struct sh_zfs	*be_z;
	int		be_kq;
	const char	*be_lake;	/* the spaces' roots */
	const char	*be_run;
	FILE		*be_log;	/* beam.log */
	long		be_period;	/* ms from a convergence to the next */
	long		be_retry;	/* ms after a first failure */
};

struct sh_beam;

/* The beam begun on ENV, which outlives it: it converges at once. */
struct sh_beam *sh_beam_start(const struct sh_beamenv *env);

/* Told of a snapshot: B converges. */
void sh_beam_converge(struct sh_beam *b);

/* B's timer, the event of the kqueue whose udata is B: B converges. */
void sh_beam_timer(struct sh_beam *b);

/*
 * B's status line: "beam converging STEP", "beam converged", or "beam
 * failing WHY", the last convergence's failure.
 */
void sh_beam_status(const struct sh_beam *b, FILE *out);

/*
 * B stopped where it stands, as when the daemon stops, and freed: its
 * stream's child killed, a receive left to resume, which the next
 * beam's first convergence does.
 */
void sh_beam_stop(struct sh_beam *b);

#endif	/* SH_BEAM_H */
