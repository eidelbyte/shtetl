/*
 * A fake ZFS for the unit tests: tests/zfake.c implements
 * src/core/zfs.h over pools and filesystems held in memory, with
 * ZFS's inheritance (user properties and the native ones inherit,
 * canmount does not, a mountpoint inherits as a path), and these
 * calls to set it up.
 */

#ifndef	SH_ZFAKE_H
#define	SH_ZFAKE_H

/* Everything forgotten. */
void zfake_reset(void);

/* A pool, and its root filesystem of the same name. */
void zfake_pool(const char *pool);

/* A filesystem; its parent must be there. */
void zfake_fs(const char *ds);

/* PROP of DS set locally. */
void zfake_set(const char *ds, const char *prop, const char *value);

/*
 * The next call of the interface named WHAT ("local", "get", ...)
 * fails; zfake_fail_nth, the Nth from now, counting from 1.
 */
void zfake_fail(const char *what);
void zfake_fail_nth(const char *what, int nth);

/* How many filesystems there are, and whether DS is one. */
int zfake_count(void);
int zfake_exists(const char *ds);

/*
 * Whether the snapshot NAME (ds@snap) is there, the newest taken, and
 * the snapshot the last rollback went back to (NULL: none yet).
 */
int zfake_snapshot(const char *name);

/* A hold on the snapshot SNAP: userrefs counts it, destroy refuses. */
void zfake_hold(const char *snap);

/* BYTES written to DS now, which written@ counts after its snapshots. */
void zfake_write(const char *ds, unsigned long bytes);
const char *zfake_last_snapshot(void);
const char *zfake_last_rollback(void);

/*
 * FN told of each snapshot taken, rollback and snapshot destroyed, OP
 * "snapshot", "rollback" or "destroy" and NAME ds@snap, once done: a
 * test carries them out on files of its own. zfake_reset forgets it.
 */
void zfake_hook(void (*fn)(const char *op, const char *name, void *arg),
    void *arg);

#endif	/* SH_ZFAKE_H */
