/*
 * The ZFS shtetl's core asks of the system, as library calls (libzfs
 * and libzfs_core), never zfs(8): planning.md 3, facts from their
 * binary source. Three things implement it, chosen at link time:
 * src/fbsd/zfs.c on FreeBSD, src/core/zstub.c in the portable build
 * (every call refused: there is no ZFS to ask), and tests/zfake.c, an
 * in-memory pool for the unit tests.
 *
 * Every call returns 0, or -1 with the reason reported to E, except
 * where it says otherwise.
 */

#ifndef	SH_ZFS_H
#define	SH_ZFS_H

#include <stddef.h>

#include "core/err.h"

struct sh_zfs;

/* Where a property's value comes from, as zfs get's SOURCE says. */
enum sh_zsrc {
	SH_ZSRC_NONE,		/* a user property nobody set */
	SH_ZSRC_DEFAULT,
	SH_ZSRC_LOCAL,
	SH_ZSRC_INHERITED,
	SH_ZSRC_RECEIVED,
	SH_ZSRC_TEMPORARY
};

/* One property to set: its name and value. */
struct sh_zprop {
	const char	*zp_name;
	const char	*zp_value;
};

/* The longest value shtetl reads: ZFS_MAXPROPLEN. */
#define	SH_ZFS_VALMAX	8192

int sh_zfs_open(struct sh_zfs **zp, struct sh_err *e);
void sh_zfs_close(struct sh_zfs *z);

/*
 * Every filesystem of every imported pool where PROP, a user
 * property, is set locally (an inherited value is not), with its
 * value, in the order the pools and their trees are walked. FN's
 * non-zero return stops the walk and is returned.
 */
int sh_zfs_local(struct sh_zfs *z, const char *prop,
    int (*fn)(void *arg, const char *ds, const char *value), void *arg,
    struct sh_err *e);

/*
 * PROP of DS, a filesystem or a snapshot (ds@snap), native or user:
 * its value into VAL and, when SRC is not NULL, where it comes from.
 * 1, and nothing reported, when there is no DS.
 */
int sh_zfs_get(struct sh_zfs *z, const char *ds, const char *prop,
    char *val, size_t len, enum sh_zsrc *src, struct sh_err *e);

/* The direct child filesystems of DS, by full name. */
int sh_zfs_children(struct sh_zfs *z, const char *ds,
    int (*fn)(void *arg, const char *child), void *arg, struct sh_err *e);

/* A filesystem DS, created with PROPS set locally from its birth. */
int sh_zfs_create(struct sh_zfs *z, const char *ds,
    const struct sh_zprop *props, size_t nprops, struct sh_err *e);

/*
 * PROP of DS, a filesystem or a snapshot, set locally to VALUE. A
 * mountpoint moves mounts as zfs set does: DS and the datasets taking
 * their mountpoint from it are unmounted, and then mounted where it
 * now says if they were mounted or their canmount is on, unless
 * canmount is off or the mountpoint none or legacy. So setting one
 * mounts a dataset that was not.
 */
int sh_zfs_set(struct sh_zfs *z, const char *ds, const char *prop,
    const char *value, struct sh_err *e);

/*
 * PROP of DS no longer set locally: inherited, or its default. A
 * mountpoint moves mounts as sh_zfs_set's does.
 */
int sh_zfs_inherit(struct sh_zfs *z, const char *ds, const char *prop,
    struct sh_err *e);

/*
 * Snapshots of N filesystems at once, NAMES ds@snap each, with PROPS
 * set on every one from its birth: one transaction (lzc_snapshot), so
 * all of them are taken, or none. A refusal names the one refused: a
 * name taken, malformed or given twice, or a filesystem missing.
 */
int sh_zfs_snapshot(struct sh_zfs *z, const char *const *names, size_t n,
    const struct sh_zprop *props, size_t nprops, struct sh_err *e);

/*
 * The snapshots of DS, oldest first (by createtxg): FN with each one's
 * name after the @. FN's non-zero return stops the walk and is
 * returned.
 */
int sh_zfs_snapshots(struct sh_zfs *z, const char *ds,
    int (*fn)(void *arg, const char *snap), void *arg, struct sh_err *e);

/*
 * DS rolled back to SNAP, its newest snapshot (lzc_rollback_to): what
 * was written since is gone. Refused when SNAP is not the newest, or
 * DS is not there.
 */
int sh_zfs_rollback(struct sh_zfs *z, const char *ds, const char *snap,
    struct sh_err *e);

/*
 * NAME destroyed: a snapshot (ds@snap), or a filesystem with no
 * children and no snapshots. Refused when NAME is not there, a
 * snapshot as a filesystem; a filesystem with both is refused for its
 * snapshots, as ZFS looks at those first.
 */
int sh_zfs_destroy(struct sh_zfs *z, const char *name, struct sh_err *e);

/*
 * DS mounted where its mountpoint says, and unmounted. Mounted there
 * already, nothing is done; mounted elsewhere, or with no mountpoint,
 * or canmount off, it is refused.
 */
int sh_zfs_mount(struct sh_zfs *z, const char *ds, struct sh_err *e);
int sh_zfs_unmount(struct sh_zfs *z, const char *ds, struct sh_err *e);

/*
 * A pool's property, and setting one. "ashift" is answered with the
 * ashift of the pool's top-level vdevs (the largest), which is what
 * the disks were laid out with; the pool property of that name is
 * only the default for vdevs added later.
 */
int sh_zpool_get(struct sh_zfs *z, const char *pool, const char *prop,
    char *val, size_t len, struct sh_err *e);
int sh_zpool_set(struct sh_zfs *z, const char *pool, const char *prop,
    const char *value, struct sh_err *e);

#endif	/* SH_ZFS_H */
