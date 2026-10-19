/*
 * The ZFS of the FreeBSD build: src/core/zfs.h over libzfs and
 * libzfs_core. Built against the OpenZFS headers of a FreeBSD source
 * tree (the Makefile's ZFS_CFLAGS) and linked against libzfs,
 * libzfs_core and libnvpair. Nothing here runs zfs(8).
 *
 * A refusal libzfs recorded is reported with the handle's own
 * description, as zfs(8) would print it; libzfs prints nothing itself.
 * Where zfs.h words a refusal, the words come from what libzfs or the
 * kernel answered, never from a look beforehand, and tests/zcells.c
 * holds them to the fake's. nvlists are built with libnvpair's fnvlist
 * calls, which end the process on an allocation failure, as shtetl's
 * own allocators do (plan 10).
 */

#include <sys/types.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <libnvpair.h>
#include <libzfs.h>
#include <libzfs_core.h>
#include <sys/fs/zfs.h>
#include <sys/nvpair.h>

#include "core/err.h"
#include "core/mem.h"
#include "core/zfs.h"

struct sh_zfs {
	libzfs_handle_t	*zz_hdl;
};

static void
zz_err(struct sh_zfs *z, struct sh_err *e, const char *name)
{
	sh_errf(e, "%s: %s", name, libzfs_error_description(z->zz_hdl));
}

int
sh_zfs_open(struct sh_zfs **zp, struct sh_err *e)
{
	libzfs_handle_t *hdl;

	if ((hdl = libzfs_init()) == NULL) {
		sh_errf(e, "libzfs: %s", libzfs_error_init(errno));
		return (-1);
	}
	libzfs_print_on_error(hdl, B_FALSE);
	*zp = sh_calloc(1, sizeof (**zp));
	(*zp)->zz_hdl = hdl;
	return (0);
}

void
sh_zfs_close(struct sh_zfs *z)
{
	libzfs_fini(z->zz_hdl);
	free(z);
}

/*
 * NAME opened, a snapshot when it has an @, a filesystem otherwise.
 * Refused "no such snapshot" or "no such filesystem".
 */
static zfs_handle_t *
zz_open(struct sh_zfs *z, const char *name, struct sh_err *e)
{
	zfs_handle_t *zhp;
	int snap = strchr(name, '@') != NULL;

	zhp = zfs_open(z->zz_hdl, name, snap ? ZFS_TYPE_SNAPSHOT :
	    ZFS_TYPE_FILESYSTEM);
	if (zhp == NULL && libzfs_errno(z->zz_hdl) == EZFS_NOENT)
		sh_errf(e, "%s: no such %s", name, snap ? "snapshot" :
		    "filesystem");
	else if (zhp == NULL)
		zz_err(z, e, name);
	return (zhp);
}

/*
 * The user property PROP of ZHP, as zfs get reads it (cmd/zfs/
 * zfs_main.c, get_callback): its value, and whether it is set on ZHP
 * itself, its source being ZHP's own name.
 */
static const char *
zz_user(zfs_handle_t *zhp, const char *prop, int *local)
{
	nvlist_t *pv;
	const char *val, *src;

	if (nvlist_lookup_nvlist(zfs_get_user_props(zhp), prop, &pv) != 0 ||
	    nvlist_lookup_string(pv, ZPROP_VALUE, &val) != 0 ||
	    nvlist_lookup_string(pv, ZPROP_SOURCE, &src) != 0)
		return (NULL);
	*local = strcmp(src, zfs_get_name(zhp)) == 0;
	return (val);
}

/* One walk of sh_zfs_local, or of sh_zfs_children. */
struct zz_walk {
	struct sh_zfs	*zw_z;
	struct sh_err	*zw_err;
	const char	*zw_prop;	/* NULL: children */
	int		(*zw_local)(void *, const char *, const char *);
	int		(*zw_child)(void *, const char *);
	void		*zw_arg;
	int		zw_stop;	/* FN's non-zero return */
	int		zw_failed;	/* reported already */
};

static int
zz_walk_fs(zfs_handle_t *zhp, void *arg)
{
	struct zz_walk *w = arg;
	const char *val;
	int local;

	if ((val = zz_user(zhp, w->zw_prop, &local)) != NULL && local)
		w->zw_stop = w->zw_local(w->zw_arg, zfs_get_name(zhp), val);
	if (w->zw_stop == 0 &&
	    zfs_iter_filesystems_v2(zhp, 0, zz_walk_fs, w) != 0 &&
	    w->zw_stop == 0 && !w->zw_failed) {
		zz_err(w->zw_z, w->zw_err, zfs_get_name(zhp));
		w->zw_failed = 1;
	}
	zfs_close(zhp);
	return (w->zw_stop != 0 || w->zw_failed ? -1 : 0);
}

static int
zz_walk_pool(zpool_handle_t *zph, void *arg)
{
	struct zz_walk *w = arg;
	zfs_handle_t *zhp;

	zhp = zz_open(w->zw_z, zpool_get_name(zph), w->zw_err);
	zpool_close(zph);
	if (zhp == NULL) {
		w->zw_failed = 1;
		return (-1);
	}
	return (zz_walk_fs(zhp, w));
}

int
sh_zfs_local(struct sh_zfs *z, const char *prop,
    int (*fn)(void *, const char *, const char *), void *arg,
    struct sh_err *e)
{
	struct zz_walk w = { z, e, prop, fn, NULL, arg, 0, 0 };

	if (zpool_iter(z->zz_hdl, zz_walk_pool, &w) != 0 && w.zw_stop == 0 &&
	    !w.zw_failed) {
		sh_errf(e, "pools: %s", libzfs_error_description(z->zz_hdl));
		return (-1);
	}
	return (w.zw_stop != 0 ? w.zw_stop : w.zw_failed ? -1 : 0);
}

int
sh_zfs_user(struct sh_zfs *z, const char *name, const char *prop,
    char *val, size_t len, struct sh_err *e)
{
	zfs_handle_t *zhp;
	const char *v;
	int local;

	if ((zhp = zz_open(z, name, e)) == NULL)
		return (-1);
	if ((v = zz_user(zhp, prop, &local)) != NULL)
		(void) strlcpy(val, v, len);
	zfs_close(zhp);
	return (v != NULL ? 0 : 1);
}

int
sh_zfs_set(struct sh_zfs *z, const char *ds, const char *prop,
    const char *value, struct sh_err *e)
{
	zfs_handle_t *zhp;
	int r;

	if ((zhp = zz_open(z, ds, e)) == NULL)
		return (-1);
	if ((r = zfs_prop_set(zhp, prop, value)) != 0)
		zz_err(z, e, ds);
	zfs_close(zhp);
	return (r != 0 ? -1 : 0);
}

int
sh_zfs_inherit(struct sh_zfs *z, const char *ds, const char *prop,
    struct sh_err *e)
{
	zfs_handle_t *zhp;
	int r;

	if ((zhp = zz_open(z, ds, e)) == NULL)
		return (-1);
	if ((r = zfs_prop_inherit(zhp, prop, B_FALSE)) != 0)
		zz_err(z, e, ds);
	zfs_close(zhp);
	return (r != 0 ? -1 : 0);
}

static int
zz_child(zfs_handle_t *zhp, void *arg)
{
	struct zz_walk *w = arg;

	w->zw_stop = w->zw_child(w->zw_arg, zfs_get_name(zhp));
	zfs_close(zhp);
	return (w->zw_stop != 0 ? -1 : 0);
}

int
sh_zfs_children(struct sh_zfs *z, const char *ds,
    int (*fn)(void *, const char *), void *arg, struct sh_err *e)
{
	struct zz_walk w = { z, e, NULL, NULL, fn, arg, 0, 0 };
	zfs_handle_t *zhp;
	int r;

	if ((zhp = zz_open(z, ds, e)) == NULL)
		return (-1);
	r = zfs_iter_filesystems_v2(zhp, ZFS_ITER_SIMPLE, zz_child, &w);
	if (r != 0 && w.zw_stop == 0)
		zz_err(z, e, ds);
	zfs_close(zhp);
	return (w.zw_stop != 0 ? w.zw_stop : r != 0 ? -1 : 0);
}

static nvlist_t *
zz_props(const struct sh_zprop *props, size_t nprops)
{
	nvlist_t *nv = fnvlist_alloc();
	size_t i;

	for (i = 0; i < nprops; i++)
		fnvlist_add_string(nv, props[i].zp_name, props[i].zp_value);
	return (nv);
}

/*
 * zfs_create (lib/libzfs/libzfs_dataset.c) looks for the parent first
 * (check_parents: EZFS_NOENT), then for the name (EZFS_EXISTS).
 */
int
sh_zfs_create(struct sh_zfs *z, const char *ds,
    const struct sh_zprop *props, size_t nprops, struct sh_err *e)
{
	nvlist_t *nv = zz_props(props, nprops);
	int r;

	r = zfs_create(z->zz_hdl, ds, ZFS_TYPE_FILESYSTEM, nv);
	nvlist_free(nv);
	if (r == 0)
		return (0);
	if (libzfs_errno(z->zz_hdl) == EZFS_NOENT)
		sh_errf(e, "%s: no parent", ds);
	else if (libzfs_errno(z->zz_hdl) == EZFS_EXISTS)
		sh_errf(e, "%s: exists", ds);
	else
		zz_err(z, e, ds);
	return (-1);
}

/* Why NAME, ds@snap, cannot be a snapshot's name, or NULL. */
static const char *
zz_snapname(const char *name)
{
	const char *at;

	if ((at = strchr(name, '@')) == NULL || at == name || at[1] == '\0' ||
	    strchr(at + 1, '@') != NULL || strchr(at + 1, '/') != NULL)
		return ("not a snapshot's name");
	return (NULL);
}

/*
 * lzc_snapshot (lib/libzfs_core/libzfs_core.c): the snapshots the keys
 * of one nvlist, the properties another, one ioctl, so they exist with
 * their properties, or none of them do. Two of one name would be one
 * key and taken once, so a name twice is refused here, with a
 * malformed one, before anything is asked; what ZFS refuses, errlist
 * names with its errno (dsl_dataset_snapshot_check).
 */
int
sh_zfs_snapshot(struct sh_zfs *z, const char *const *names, size_t n,
    const struct sh_zprop *props, size_t nprops, struct sh_err *e)
{
	nvlist_t *snaps, *pv, *errlist = NULL;
	nvpair_t *bad;
	const char *why;
	size_t i, j;
	int32_t r32;
	int r;

	(void) z;
	for (i = 0; i < n; i++) {
		why = zz_snapname(names[i]);
		for (j = 0; why == NULL && j < i; j++) {
			if (strcmp(names[j], names[i]) == 0)
				why = "named twice";
		}
		if (why != NULL) {
			sh_errf(e, "%s: %s", names[i], why);
			return (-1);
		}
	}
	snaps = fnvlist_alloc();
	for (i = 0; i < n; i++)
		fnvlist_add_boolean(snaps, names[i]);
	pv = zz_props(props, nprops);
	r = lzc_snapshot(snaps, nprops > 0 ? pv : NULL, &errlist);
	if (r != 0 && errlist != NULL &&
	    (bad = nvlist_next_nvpair(errlist, NULL)) != NULL &&
	    nvpair_value_int32(bad, &r32) == 0)
		sh_errf(e, "%s: %s", nvpair_name(bad), r32 == EEXIST ? "taken" :
		    r32 == ENOENT ? "no such filesystem" : strerror(r32));
	else if (r != 0)
		sh_errf(e, "%s: %s", names[0], strerror(r));
	nvlist_free(snaps);
	nvlist_free(pv);
	nvlist_free(errlist);
	return (r != 0 ? -1 : 0);
}

/* One walk of sh_zfs_snapshots. */
struct zz_snaps {
	int	(*zs_fn)(void *, const char *);
	void	*zs_arg;
	int	zs_stop;
};

/*
 * zfs_iter_snapshots_sorted_v2 (lib/libzfs/libzfs_iter.c) calls this
 * for every snapshot whatever it returns, so once FN has stopped the
 * walk, the rest are only closed (box run 1, Z6).
 */
static int
zz_snap(zfs_handle_t *zhp, void *arg)
{
	struct zz_snaps *w = arg;

	if (w->zs_stop == 0)
		w->zs_stop = w->zs_fn(w->zs_arg,
		    strchr(zfs_get_name(zhp), '@') + 1);
	zfs_close(zhp);
	return (w->zs_stop != 0 ? -1 : 0);
}

int
sh_zfs_snapshots(struct sh_zfs *z, const char *ds,
    int (*fn)(void *, const char *), void *arg, struct sh_err *e)
{
	struct zz_snaps w = { fn, arg, 0 };
	zfs_handle_t *zhp;
	int r;

	if ((zhp = zz_open(z, ds, e)) == NULL)
		return (-1);
	r = zfs_iter_snapshots_sorted_v2(zhp, 0, zz_snap, &w, 0, 0);
	if (r != 0 && w.zs_stop == 0)
		zz_err(z, e, ds);
	zfs_close(zhp);
	return (w.zs_stop != 0 ? w.zs_stop : r != 0 ? -1 : 0);
}

/*
 * dsl_dataset_rollback_check: ENOENT, no DS; ESRCH, SNAP not DS's;
 * EEXIST, a snapshot or a bookmark after SNAP. lzc_rollback_to asks
 * for SNAP to be the newest, so ESRCH and EEXIST both say it is not.
 */
int
sh_zfs_rollback(struct sh_zfs *z, const char *ds, const char *snap,
    struct sh_err *e)
{
	char name[ZFS_MAX_DATASET_NAME_LEN];
	int r;

	(void) z;
	(void) snprintf(name, sizeof (name), "%s@%s", ds, snap);
	if ((r = lzc_rollback_to(ds, name)) == 0)
		return (0);
	if (r == ENOENT)
		sh_errf(e, "%s: no such filesystem", ds);
	else if (r == ESRCH || r == EEXIST)
		sh_errf(e, "%s: not the newest snapshot", name);
	else
		sh_errf(e, "%s: %s", name, strerror(r));
	return (-1);
}

/* A walk of snapshots that stops at the first: there is one. */
static int
zz_any(void *arg, const char *snap)
{
	(void) arg;
	(void) snap;
	return (1);
}

/*
 * A filesystem: dsl_destroy_head_check_impl answers EBUSY for a
 * dataset held long, as a mount holds it, and EBUSY again for its
 * snapshots, then EEXIST for children; so an EBUSY is told apart by
 * looking at the mount and the snapshots after it. A snapshot:
 * lzc_destroy_snaps counts one that is not there as destroyed, so it is
 * looked for, and its EBUSY is holds and EEXIST clones
 * (dsl_destroy_snapshot_check_impl).
 */
int
sh_zfs_destroy(struct sh_zfs *z, const char *name, struct sh_err *e)
{
	nvlist_t *snaps, *errlist = NULL;
	char at[ZFS_MAXPROPLEN];
	int r;

	if (strchr(name, '@') == NULL) {
		if ((r = lzc_destroy(name)) == 0)
			return (0);
		if (r == ENOENT)
			sh_errf(e, "%s: no such dataset", name);
		else if (r == EEXIST)
			sh_errf(e, "%s: has children", name);
		else if (r == EBUSY &&
		    sh_zfs_mounted(z, name, at, sizeof (at), e) == 1)
			sh_errf(e, "%s: mounted", name);
		else if (r == EBUSY &&
		    sh_zfs_snapshots(z, name, zz_any, NULL, e) == 1)
			sh_errf(e, "%s: has snapshots", name);
		else
			sh_errf(e, "%s: %s", name, strerror(r));
		return (-1);
	}
	if (!zfs_dataset_exists(z->zz_hdl, name, ZFS_TYPE_SNAPSHOT)) {
		sh_errf(e, "%s: no such dataset", name);
		return (-1);
	}
	snaps = fnvlist_alloc();
	fnvlist_add_boolean(snaps, name);
	r = lzc_destroy_snaps(snaps, B_FALSE, &errlist);
	nvlist_free(snaps);
	nvlist_free(errlist);
	if (r != 0)
		sh_errf(e, "%s: %s", name, r == EEXIST ? "has clones" :
		    r == EBUSY ? "has holds" : strerror(r));
	return (r != 0 ? -1 : 0);
}

/*
 * zfs_mount (lib/libzfs/libzfs_mount.c) mounts nothing and says nothing
 * where the mountpoint is none or legacy or canmount is off
 * (zfs_is_mountable), and fails EBUSY on a dataset mounted already. So
 * those are what is looked at first: mounted where its mountpoint says
 * is done; mounted elsewhere is refused.
 */
int
sh_zfs_mount(struct sh_zfs *z, const char *ds, struct sh_err *e)
{
	zfs_handle_t *zhp;
	char mp[ZFS_MAXPROPLEN];
	char *at = NULL;
	int r = 0;

	if ((zhp = zz_open(z, ds, e)) == NULL)
		return (-1);
	if (zfs_prop_get(zhp, ZFS_PROP_MOUNTPOINT, mp, sizeof (mp), NULL,
	    NULL, 0, B_FALSE) != 0 || mp[0] != '/') {
		sh_errf(e, "%s: no mountpoint", ds);
		r = -1;
	} else if (zfs_prop_get_int(zhp, ZFS_PROP_CANMOUNT) ==
	    ZFS_CANMOUNT_OFF) {
		sh_errf(e, "%s: canmount is off", ds);
		r = -1;
	} else if (zfs_is_mounted(zhp, &at)) {
		if (strcmp(at, mp) != 0) {
			sh_errf(e, "%s: mounted at %s, not its mountpoint",
			    ds, at);
			r = -1;
		}
	} else if (zfs_mount(zhp, NULL, 0) != 0) {
		zz_err(z, e, ds);
		r = -1;
	}
	free(at);
	zfs_close(zhp);
	return (r);
}

/*
 * zfs_unmount does nothing for a filesystem that is not mounted, and
 * says EZFS_BUSY for umount's EBUSY (unmount_one), as when another
 * filesystem is mounted inside it.
 */
int
sh_zfs_unmount(struct sh_zfs *z, const char *ds, struct sh_err *e)
{
	zfs_handle_t *zhp;
	int r;

	if ((zhp = zz_open(z, ds, e)) == NULL)
		return (-1);
	if ((r = zfs_unmount(zhp, NULL, 0)) != 0 &&
	    libzfs_errno(z->zz_hdl) == EZFS_BUSY)
		sh_errf(e, "%s: busy", ds);
	else if (r != 0)
		zz_err(z, e, ds);
	zfs_close(zhp);
	return (r != 0 ? -1 : 0);
}

int
sh_zfs_mounted(struct sh_zfs *z, const char *ds, char *path, size_t len,
    struct sh_err *e)
{
	zfs_handle_t *zhp;
	char *at = NULL;
	int r;

	if ((zhp = zz_open(z, ds, e)) == NULL)
		return (-1);
	if ((r = zfs_is_mounted(zhp, &at) ? 1 : 0) == 1)
		(void) strlcpy(path, at, len);
	free(at);
	zfs_close(zhp);
	return (r);
}
