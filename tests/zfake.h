/*
 * The fake ZFS of the tests: tests/zfake.c implements src/core/zfs.h
 * over pools and filesystems held in memory, refusing in the order the
 * real layer does and in its words (tests/zcells.c holds the two to
 * the same cells), with these calls to set it up and look inside.
 */

#ifndef	SH_ZFAKE_H
#define	SH_ZFAKE_H

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
 * in a pipe leaves it.
 */
void zfake_cut(void);

/* PROP as set on the filesystem DS itself, native or user; or NULL. */
const char *zfake_local(const char *ds, const char *prop);

/*
 * The fake's data: WORD written into the mounted filesystem DS, as a
 * file would be, and whether DS holds it. A snapshot keeps the words
 * DS held; a rollback puts them back. 0, or -1 when DS is not mounted.
 */
int zfake_put(const char *ds, const char *word);
int zfake_has(const char *ds, const char *word);

#endif	/* SH_ZFAKE_H */
