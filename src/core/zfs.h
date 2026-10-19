/*
 * The ZFS shtetl asks of the system, as library calls (libzfs and
 * libzfs_core), never zfs(8): plan 2, rule 6, facts from their binary
 * source. Two things implement it, chosen at the link: src/fbsd/zfs.c
 * on FreeBSD, and tests/zfake.c, pools in memory for the tests.
 * tests/zcells.c holds the two to the same cells (tests/MATRIX.md,
 * family Z).
 *
 * Every call returns 0, or -1 with the refusal reported to E as "NAME:
 * why", in the words given here; libzfs's own description where none
 * is given. A walk calls FN for each thing it finds, and FN's non-zero
 * return stops it and is returned.
 */

#ifndef	SH_ZFS_H
#define	SH_ZFS_H

#include <stddef.h>

#include "core/err.h"

struct sh_zfs;

/* One property to set: its name and value, as zfs set takes them. */
struct sh_zprop {
	const char	*zp_name;
	const char	*zp_value;
};

int sh_zfs_open(struct sh_zfs **zp, struct sh_err *e);
void sh_zfs_close(struct sh_zfs *z);

/*
 * Every filesystem of every imported pool where the user property PROP
 * is set locally, with its value: one where it is inherited is not.
 */
int sh_zfs_local(struct sh_zfs *z, const char *prop,
    int (*fn)(void *arg, const char *ds, const char *value), void *arg,
    struct sh_err *e);

/*
 * The user property PROP of NAME, a filesystem or a snapshot (ds@snap),
 * set there or inherited, into VAL: 0; 1 when nothing sets it. Refused:
 * "no such filesystem", "no such snapshot".
 */
int sh_zfs_user(struct sh_zfs *z, const char *name, const char *prop,
    char *val, size_t len, struct sh_err *e);

/* The filesystems directly below DS. Refused: "no such filesystem". */
int sh_zfs_children(struct sh_zfs *z, const char *ds,
    int (*fn)(void *arg, const char *child), void *arg, struct sh_err *e);

/*
 * The filesystem DS, PROPS set locally from its birth; nothing is
 * mounted. Refused: "exists", "no parent".
 */
int sh_zfs_create(struct sh_zfs *z, const char *ds,
    const struct sh_zprop *props, size_t nprops, struct sh_err *e);

/*
 * The N snapshots NAMES, ds@snap each, PROPS set on every one from its
 * birth, in one transaction (lzc_snapshot): all of them are taken, or
 * none. The one refused is named: "not a snapshot's name", "named
 * twice", "no such filesystem", "taken".
 */
int sh_zfs_snapshot(struct sh_zfs *z, const char *const *names, size_t n,
    const struct sh_zprop *props, size_t nprops, struct sh_err *e);

/*
 * The snapshots of DS, oldest first: FN with each one's name after the
 * @. Refused: "no such filesystem".
 */
int sh_zfs_snapshots(struct sh_zfs *z, const char *ds,
    int (*fn)(void *arg, const char *snap), void *arg, struct sh_err *e);

/*
 * DS rolled back to its snapshot SNAP, which must be its newest: what
 * was written since is gone, mounted or not. Refused: "no such
 * filesystem"; DS@SNAP "not the newest snapshot".
 */
int sh_zfs_rollback(struct sh_zfs *z, const char *ds, const char *snap,
    struct sh_err *e);

/*
 * NAME destroyed: a snapshot, or a filesystem with no snapshots and no
 * children, unmounted. Refused, in the order ZFS looks: "no such
 * dataset"; a filesystem "mounted", "has snapshots", "has children".
 */
int sh_zfs_destroy(struct sh_zfs *z, const char *name, struct sh_err *e);

/*
 * DS mounted where its mountpoint says; nothing done when it is there
 * already. Refused: "no mountpoint" (none or legacy), "canmount is
 * off", "mounted at PATH, not its mountpoint".
 */
int sh_zfs_mount(struct sh_zfs *z, const char *ds, struct sh_err *e);

/*
 * DS unmounted; nothing done when it is not mounted. Refused: "busy",
 * as when another filesystem is mounted inside it.
 */
int sh_zfs_unmount(struct sh_zfs *z, const char *ds, struct sh_err *e);

/*
 * Where DS is mounted, into PATH: 1; 0 when it is not. Refused: "no
 * such filesystem".
 */
int sh_zfs_mounted(struct sh_zfs *z, const char *ds, char *path,
    size_t len, struct sh_err *e);

#endif	/* SH_ZFS_H */
