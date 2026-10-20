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
#include <stdint.h>

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

/*
 * The user property PROP set on NAME to VALUE: a filesystem, below it
 * inherited, or a snapshot (ds@snap). Refused: "no such filesystem",
 * "no such snapshot".
 */
int sh_zfs_set(struct sh_zfs *z, const char *name, const char *prop,
    const char *value, struct sh_err *e);

/*
 * The user property PROP set on NAME, a filesystem or a snapshot, no
 * more: NAME takes its parent's, a snapshot its filesystem's, or none;
 * nothing done where NAME does not set it. Refused: "no such
 * filesystem", "no such snapshot".
 */
int sh_zfs_inherit(struct sh_zfs *z, const char *name, const char *prop,
    struct sh_err *e);

/*
 * The user properties set on NAME itself, a filesystem or a snapshot:
 * FN with each one's name and value; none inherited. Refused: "no such
 * filesystem", "no such snapshot".
 */
int sh_zfs_props(struct sh_zfs *z, const char *name,
    int (*fn)(void *arg, const char *prop, const char *value), void *arg,
    struct sh_err *e);

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

/*
 * The snapshot NAME's guid, which a snapshot received from it shares,
 * and createtxg, which orders a filesystem's snapshots. Refused: "no
 * such snapshot".
 */
int sh_zfs_snapinfo(struct sh_zfs *z, const char *name, uint64_t *guid,
    uint64_t *txg, struct sh_err *e);

/*
 * The snapshot the filesystem DS is a clone of, into ORIGIN: 1; 0 when
 * DS is no clone. Refused: "no such filesystem".
 */
int sh_zfs_origin(struct sh_zfs *z, const char *ds, char *origin,
    size_t len, struct sh_err *e);

/*
 * DS made a clone of the snapshot ORIGIN, PROPS set locally from its
 * birth; nothing is mounted. Refused: "no such snapshot", "exists",
 * "no parent".
 */
int sh_zfs_clone(struct sh_zfs *z, const char *origin, const char *ds,
    const struct sh_zprop *props, size_t nprops, struct sh_err *e);

/*
 * DS, a clone, promoted: its origin's snapshots up to the one DS was
 * cloned from become DS's, and the origin's filesystem a clone of DS's.
 * Refused: "no such filesystem", "not a clone", "SNAP is the origin's
 * too" (a name both have).
 */
int sh_zfs_promote(struct sh_zfs *z, const char *ds, struct sh_err *e);

/*
 * The snapshot SNAP sent and received as TO (ds@snap), resumably (plan
 * 8.1): whole when FROM is NULL, TO's filesystem made by the receive;
 * incremental from FROM, an earlier snapshot of SNAP's filesystem or
 * the one it is a clone of, onto TO's filesystem, whose newest
 * snapshot is FROM's counterpart (the same guid); or, with ORIGIN, a
 * clone stream from FROM, TO's filesystem made a clone of ORIGIN,
 * FROM's counterpart. Nothing but the data travels (no -p), and
 * nothing is mounted. A stream broken off leaves a receive to resume
 * on TO's filesystem (sh_zfs_resumable), and while it stands no other
 * stream is taken there. Refused: "no such snapshot" (SNAP, FROM or
 * ORIGIN); TO's filesystem "exists", or "no such filesystem" for an
 * incremental; "a receive to resume"; its "newest snapshot is not
 * FROM's counterpart", or ORIGIN "not FROM's counterpart"; the
 * system's words.
 */
int sh_zfs_send(struct sh_zfs *z, const char *snap, const char *from,
    const char *to, const char *origin, struct sh_err *e);

/*
 * Whether a broken receive stands on DS to be resumed: 1, or 0.
 * Refused: "no such filesystem".
 */
int sh_zfs_resumable(struct sh_zfs *z, const char *ds, struct sh_err *e);

/*
 * DS's broken receive resumed: the stream sent on from where it broke
 * off. Refused: "no receive to resume"; the snapshot it sends gone,
 * "SNAP: no such snapshot", or the one it was sent from, "SNAP: its
 * base gone"; the system's words.
 */
int sh_zfs_resume(struct sh_zfs *z, const char *ds, struct sh_err *e);

/*
 * DS's broken receive taken away (zfs receive -A): the part received
 * gone, and DS with it where the stream was making DS. Refused: "no
 * receive to resume".
 */
int sh_zfs_abort(struct sh_zfs *z, const char *ds, struct sh_err *e);

#endif	/* SH_ZFS_H */
