/*
 * The tests' zfs_rebase (src/core/rebase.h) over the fake ZFS
 * (tests/zfake.h): a call forked as the program is a child, doing to
 * the fake what the program does to ZFS. The clone form's result a
 * clone of onto, read-only, mountpoint none; the dataset form's
 * pre-apply snapshot, the dataset unmounted for the run and mounted
 * again at done, refused where it will not unmount; the record,
 * zfs_rebase:tag and zfs_rebase:phase set on the result itself while a
 * run is open, the phase applying1, conflicts where it stopped there,
 * applying2 where it failed part way; --abort undoing either, but from
 * applying2 only stepping back to conflicts. Its sides must be
 * mounted, and a dataset form given -u and -b, or it refuses, as the
 * program does. Each start is logged
 * to $FAKE_LOG as tests/bin's programs log theirs, "zfs_rebase ARGS",
 * and a run that does not reach done ends on the line "exited RC", so
 * that the job says of it what it says of the program. The content of
 * a replay is not the fake's: zfs_rebase's own tests and probe P6 on
 * the box hold it.
 */

#ifndef	SH_RBFAKE_H
#define	SH_RBFAKE_H

/*
 * The start answering to IDENT, a clone form's result or a dataset
 * form's onto@result, ends RC: 1 stopped at conflicts, 2 refused, 3
 * failed part way; -1 never, sleeping until it is killed.
 */
void rbfake_end(const char *ident, int rc);

/* Every rbfake_end forgotten. */
void rbfake_reset(void);

#endif	/* SH_RBFAKE_H */
