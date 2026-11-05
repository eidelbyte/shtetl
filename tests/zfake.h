/*
 * The fake ZFS of the tests: tests/zfake.c implements src/core/zfs.h
 * over pools and filesystems held in memory, refusing in the order the
 * real layer does and in its words (tests/zcells.c holds the two to
 * the same cells), with these calls to set it up and look inside. The
 * memory is shared with the children a test forks, as the kernel's
 * state is.
 */

#ifndef	SH_ZFAKE_H
#define	SH_ZFAKE_H

#include <stdint.h>

/* Everything forgotten. */
void zfake_reset(void);

/* A pool, and its root filesystem of the same name. */
void zfake_pool(const char *pool);

/*
 * The next call of the interface named WHAT ("rollback", "user", ...)
 * fails; zfake_fail_nth, the Nth from now, counting from 1.
 */
void zfake_fail(const char *what);
void zfake_fail_nth(const char *what, int nth);

/*
 * The next stream sh_zfs_send or sh_zfs_resume receives breaks off
 * part way: refused, and left on its target to resume, as a stream cut
 * in a pipe leaves it; zfake_cut_nth, the Nth from now.
 */
void zfake_cut(void);
void zfake_cut_nth(int nth);

/*
 * The pool POOL exported, as far as the calls can tell: nothing in it
 * is there; zfake_import, back as it was.
 */
void zfake_export(const char *pool);
void zfake_import(void);

/* PROP as set on the filesystem DS itself, native or user; or NULL. */
const char *zfake_local(const char *ds, const char *prop);

/*
 * The fake's data: WORD written into the mounted filesystem DS, as a
 * file would be, and whether DS holds it. A snapshot keeps the words
 * DS held; a rollback puts them back. 0, or -1 when DS is not mounted.
 */
int zfake_put(const char *ds, const char *word);
int zfake_has(const char *ds, const char *word);

/*
 * From now on, for every test after it, the files in a mounted
 * filesystem's directory kept as ZFS keeps them for what reads them: a
 * snapshot of it copies them, not those of a filesystem mounted inside
 * it, into MOUNT/.zfs/snapshot/SNAP; a mountpoint set that moves it
 * moves its directory; unmounted, they are put away in the directory
 * STORE, gone from where it was mounted, and mounted, brought back; a
 * filesystem destroyed takes its away for good. Off until asked: a
 * foundation's tests walk trees in which ZFS hides .zfs. The box holds
 * the real ones (I12, I14, Z20).
 */
void zfake_files(const char *store);

/* The snapshot SNAP's creation made SECONDS earlier. */
void zfake_age(const char *snap, int64_t seconds);

#endif	/* SH_ZFAKE_H */
