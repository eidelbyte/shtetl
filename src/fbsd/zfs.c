/*
 * The ZFS of the FreeBSD build: src/core/zfs.h over libzfs. Built
 * against the OpenZFS headers of a FreeBSD source tree (the
 * Makefile's ZFS_CFLAGS, as zfs_rebase's are) and linked against
 * libzfs and libnvpair. Nothing here runs zfs(8).
 *
 * A failure that libzfs recorded is reported with the handle's own
 * description, which is what zfs(8) would print; libzfs prints
 * nothing itself (libzfs_print_on_error is off). The lzc_* calls
 * return only an errno: where one says why in the terms of
 * src/core/zfs.h, the refusal is said in tests/zfake.c's words, and
 * tests/box/zcheck.c holds the two to it.
 *
 * A user property's source, as zfs get reads it (cmd/zfs/zfs_main.c,
 * get_callback): the dataset's own name where it is set locally,
 * "$recvd" where it was received, and otherwise the ancestor it is
 * inherited from.
 */

#include <sys/types.h>
#include <sys/wait.h>

#include <errno.h>
#include <stdint.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libnvpair.h>
#include <libzfs.h>
#include <libzfs_core.h>
#include <sys/fs/zfs.h>
#include <sys/nvpair.h>

#include "core/err.h"
#include "core/zfs.h"

struct sh_zfs {
	libzfs_handle_t	*zz_hdl;
};

static void
zz_err(struct sh_zfs *z, struct sh_err *e, const char *what)
{
	sh_errf(e, "%s: %s", what, libzfs_error_description(z->zz_hdl));
}

/* NAME is not there: a snapshot when it has an @. */
static void
zz_none(struct sh_err *e, const char *name)
{
	sh_errf(e, "%s: no such %s", name, strchr(name, '@') != NULL ?
	    "snapshot" : "filesystem");
}

int
sh_zfs_open(struct sh_zfs **zp, struct sh_err *e)
{
	struct sh_zfs *z;

	*zp = NULL;
	if ((z = calloc(1, sizeof (*z))) == NULL) {
		sh_err_nomem(e);
		return (-1);
	}
	if ((z->zz_hdl = libzfs_init()) == NULL) {
		sh_errf(e, "libzfs: %s", libzfs_error_init(errno));
		free(z);
		return (-1);
	}
	libzfs_print_on_error(z->zz_hdl, B_FALSE);
	*zp = z;
	return (0);
}

void
sh_zfs_close(struct sh_zfs *z)
{
	if (z == NULL)
		return;
	libzfs_fini(z->zz_hdl);
	free(z);
}

/* One walk for sh_zfs_local. */
struct zz_walk {
	struct sh_zfs	*zw_z;
	struct sh_err	*zw_err;
	const char	*zw_prop;
	int		(*zw_fn)(void *, const char *, const char *);
	void		*zw_arg;
	int		zw_stop;	/* FN's non-zero return */
	int		zw_failed;	/* reported already */
};

/*
 * PROP's value on ZHP, a user property, and its source: 0 none set,
 * 1 local, 2 received, 3 inherited.
 */
static int
zz_user(zfs_handle_t *zhp, const char *prop, const char **valp)
{
	nvlist_t *pv;
	const char *src;

	*valp = "-";
	if (nvlist_lookup_nvlist(zfs_get_user_props(zhp), prop, &pv) != 0 ||
	    nvlist_lookup_string(pv, ZPROP_VALUE, valp) != 0 ||
	    nvlist_lookup_string(pv, ZPROP_SOURCE, &src) != 0)
		return (0);
	if (strcmp(src, zfs_get_name(zhp)) == 0)
		return (1);
	if (strcmp(src, ZPROP_SOURCE_VAL_RECVD) == 0)
		return (2);
	return (3);
}

static int
zz_walk_fs(zfs_handle_t *zhp, void *arg)
{
	struct zz_walk *w = arg;
	const char *val;
	int r;

	r = 0;
	if (zz_user(zhp, w->zw_prop, &val) == 1 &&
	    (r = w->zw_fn(w->zw_arg, zfs_get_name(zhp), val)) != 0)
		w->zw_stop = r;
	if (r == 0 && zfs_iter_filesystems_v2(zhp, 0, zz_walk_fs, w) != 0 &&
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

	zhp = zfs_open(w->zw_z->zz_hdl, zpool_get_name(zph),
	    ZFS_TYPE_FILESYSTEM);
	if (zhp == NULL) {
		zz_err(w->zw_z, w->zw_err, zpool_get_name(zph));
		w->zw_failed = 1;
		zpool_close(zph);
		return (-1);
	}
	zpool_close(zph);
	return (zz_walk_fs(zhp, w));
}

int
sh_zfs_local(struct sh_zfs *z, const char *prop,
    int (*fn)(void *, const char *, const char *), void *arg,
    struct sh_err *e)
{
	struct zz_walk w;

	(void) memset(&w, 0, sizeof (w));
	w.zw_z = z;
	w.zw_err = e;
	w.zw_prop = prop;
	w.zw_fn = fn;
	w.zw_arg = arg;
	if (zpool_iter(z->zz_hdl, zz_walk_pool, &w) != 0 && w.zw_stop == 0 &&
	    !w.zw_failed) {
		zz_err(z, e, "the imported pools");
		return (-1);
	}
	if (w.zw_stop != 0)
		return (w.zw_stop);
	return (w.zw_failed ? -1 : 0);
}

/*
 * DS opened, a snapshot when its name has an @, a filesystem
 * otherwise: 1 and nothing reported if there is none.
 */
static int
zz_open(struct sh_zfs *z, const char *ds, zfs_handle_t **zhpp,
    struct sh_err *e)
{
	*zhpp = zfs_open(z->zz_hdl, ds, strchr(ds, '@') != NULL ?
	    ZFS_TYPE_SNAPSHOT : ZFS_TYPE_FILESYSTEM);
	if (*zhpp != NULL)
		return (0);
	if (libzfs_errno(z->zz_hdl) == EZFS_NOENT)
		return (1);
	zz_err(z, e, ds);
	return (-1);
}

/* nvlist_t *, built from PROPS; NULL when out of memory. */
static nvlist_t *
zz_props(const struct sh_zprop *props, size_t nprops)
{
	nvlist_t *nv;
	size_t i;

	if (nvlist_alloc(&nv, NV_UNIQUE_NAME, 0) != 0)
		return (NULL);
	for (i = 0; i < nprops; i++) {
		if (nvlist_add_string(nv, props[i].zp_name,
		    props[i].zp_value) != 0) {
			nvlist_free(nv);
			return (NULL);
		}
	}
	return (nv);
}

int
sh_zfs_get(struct sh_zfs *z, const char *ds, const char *prop, char *val,
    size_t len, enum sh_zsrc *src, struct sh_err *e)
{
	static const enum sh_zsrc user[] = { SH_ZSRC_NONE, SH_ZSRC_LOCAL,
	    SH_ZSRC_RECEIVED, SH_ZSRC_INHERITED };
	zfs_handle_t *zhp;
	zprop_source_t zs;
	zfs_prop_t p;
	const char *uv;
	char where[ZFS_MAX_DATASET_NAME_LEN];
	uint64_t written;
	int r;

	if ((r = zz_open(z, ds, &zhp, e)) != 0)
		return (r);
	/* written@SNAP: a number, the binary source of it */
	if (strncmp(prop, "written@", 8) == 0) {
		r = zfs_prop_get_written_int(zhp, prop, &written);
		zfs_close(zhp);
		if (r != 0) {
			sh_errf(e, "%s@%s: no such snapshot", ds, prop + 8);
			return (-1);
		}
		(void) snprintf(val, len, "%ju", (uintmax_t)written);
		if (src != NULL)
			*src = SH_ZSRC_NONE;
		return (0);
	}
	p = zfs_name_to_prop(prop);
	if (p == ZPROP_USERPROP) {
		if (!zfs_prop_user(prop)) {
			sh_errf(e, "%s: no property %s", ds, prop);
			zfs_close(zhp);
			return (-1);
		}
		r = zz_user(zhp, prop, &uv);
		(void) snprintf(val, len, "%s", uv);
		if (src != NULL)
			*src = user[r];
		zfs_close(zhp);
		return (0);
	}
	/* none to have, as zfs get's get_callback shows it: "-" */
	if (zfs_prop_get(zhp, p, val, len, &zs, where, sizeof (where),
	    B_FALSE) != 0) {
		(void) snprintf(val, len, "-");
		zs = ZPROP_SRC_NONE;
	}
	if (src != NULL) {
		switch (zs) {
		case ZPROP_SRC_DEFAULT:
			*src = SH_ZSRC_DEFAULT;
			break;
		case ZPROP_SRC_LOCAL:
			*src = SH_ZSRC_LOCAL;
			break;
		case ZPROP_SRC_INHERITED:
			*src = SH_ZSRC_INHERITED;
			break;
		case ZPROP_SRC_RECEIVED:
			*src = SH_ZSRC_RECEIVED;
			break;
		case ZPROP_SRC_TEMPORARY:
			*src = SH_ZSRC_TEMPORARY;
			break;
		default:
			*src = SH_ZSRC_NONE;
			break;
		}
	}
	zfs_close(zhp);
	return (0);
}

/* One walk for sh_zfs_children. */
struct zz_kids {
	int	(*zk_fn)(void *, const char *);
	void	*zk_arg;
	int	zk_stop;
};

static int
zz_kid(zfs_handle_t *zhp, void *arg)
{
	struct zz_kids *k = arg;

	if (k->zk_stop == 0)
		k->zk_stop = k->zk_fn(k->zk_arg, zfs_get_name(zhp));
	zfs_close(zhp);
	return (k->zk_stop != 0 ? -1 : 0);
}

int
sh_zfs_children(struct sh_zfs *z, const char *ds,
    int (*fn)(void *, const char *), void *arg, struct sh_err *e)
{
	struct zz_kids k;
	zfs_handle_t *zhp;
	int r;

	if ((r = zz_open(z, ds, &zhp, e)) != 0) {
		if (r == 1)
			zz_none(e, ds);
		return (-1);
	}
	k.zk_fn = fn;
	k.zk_arg = arg;
	k.zk_stop = 0;
	r = zfs_iter_filesystems_v2(zhp, ZFS_ITER_SIMPLE, zz_kid, &k);
	if (r != 0 && k.zk_stop == 0)
		zz_err(z, e, ds);
	zfs_close(zhp);
	if (k.zk_stop != 0)
		return (k.zk_stop);
	return (r != 0 ? -1 : 0);
}

int
sh_zfs_create(struct sh_zfs *z, const char *ds,
    const struct sh_zprop *props, size_t nprops, struct sh_err *e)
{
	nvlist_t *nv;
	int r;

	if ((nv = zz_props(props, nprops)) == NULL) {
		sh_err_nomem(e);
		return (-1);
	}
	r = zfs_create(z->zz_hdl, ds, ZFS_TYPE_FILESYSTEM, nv);
	nvlist_free(nv);
	if (r != 0) {
		zz_err(z, e, ds);
		return (-1);
	}
	return (0);
}

int
sh_zfs_set(struct sh_zfs *z, const char *ds, const char *prop,
    const char *value, struct sh_err *e)
{
	zfs_handle_t *zhp;
	int r;

	if ((r = zz_open(z, ds, &zhp, e)) != 0) {
		if (r == 1)
			zz_none(e, ds);
		return (-1);
	}
	r = zfs_prop_set(zhp, prop, value);
	if (r != 0)
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

	if ((r = zz_open(z, ds, &zhp, e)) != 0) {
		if (r == 1)
			zz_none(e, ds);
		return (-1);
	}
	r = zfs_prop_inherit(zhp, prop, B_FALSE);
	if (r != 0)
		zz_err(z, e, ds);
	zfs_close(zhp);
	return (r != 0 ? -1 : 0);
}

/* An nvlist whose keys are NAMES, as lzc_* takes snapshots. */
static nvlist_t *
zz_names(const char *const *names, size_t n)
{
	nvlist_t *nv;
	size_t i;

	if (nvlist_alloc(&nv, NV_UNIQUE_NAME, 0) != 0)
		return (NULL);
	for (i = 0; i < n; i++) {
		if (nvlist_add_boolean(nv, names[i]) != 0) {
			nvlist_free(nv);
			return (NULL);
		}
	}
	return (nv);
}

/* NAME, ds@snap, as a snapshot's: why not, or NULL. */
static const char *
zz_snapname(const char *name)
{
	const char *at;

	if ((at = strchr(name, '@')) == NULL || at[1] == '\0' ||
	    strchr(at + 1, '@') != NULL || strchr(at + 1, '/') != NULL)
		return ("not a snapshot's name");
	return (NULL);
}

/* Why ZFS refused to take a snapshot: its errno R in errlist. */
static const char *
zz_snapwhy(int r)
{
	switch (r) {
	case EEXIST:
		return ("taken");
	case ENOENT:
		return ("no such filesystem");
	default:
		return (strerror(r));
	}
}

/*
 * lzc_snapshot (lib/libzfs_core/libzfs_core.c): the snapshots as the
 * keys of one nvlist, the properties in another, all of it one ioctl,
 * so the snapshots exist with their properties, or none of them do.
 * A name twice would be one key, so it is refused here, with a
 * malformed one; what ZFS refuses, errlist names, with its errno
 * (dsl_dataset_snapshot_check).
 */
int
sh_zfs_snapshot(struct sh_zfs *z, const char *const *names, size_t n,
    const struct sh_zprop *props, size_t nprops, struct sh_err *e)
{
	nvlist_t *snaps, *pv, *errlist;
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
	errlist = NULL;
	snaps = NULL;
	if ((pv = zz_props(props, nprops)) == NULL ||
	    (snaps = zz_names(names, n)) == NULL) {
		nvlist_free(pv);
		sh_err_nomem(e);
		return (-1);
	}
	r = lzc_snapshot(snaps, nprops > 0 ? pv : NULL, &errlist);
	nvlist_free(snaps);
	nvlist_free(pv);
	if (r != 0) {
		if (errlist != NULL &&
		    (bad = nvlist_next_nvpair(errlist, NULL)) != NULL &&
		    nvpair_value_int32(bad, &r32) == 0)
			sh_errf(e, "%s: %s", nvpair_name(bad),
			    zz_snapwhy(r32));
		else
			sh_errf(e, "%s%s: %s", names[0],
			    n > 1 ? " and the rest" : "", strerror(r));
	}
	nvlist_free(errlist);
	return (r != 0 ? -1 : 0);
}

/* One walk for sh_zfs_snapshots. */
struct zz_snaps {
	int	(*zs_fn)(void *, const char *);
	void	*zs_arg;
	int	zs_stop;
};

static int
zz_snap(zfs_handle_t *zhp, void *arg)
{
	struct zz_snaps *w = arg;
	const char *at;

	at = strchr(zfs_get_name(zhp), '@');
	if (w->zs_stop == 0 && at != NULL)
		w->zs_stop = w->zs_fn(w->zs_arg, at + 1);
	zfs_close(zhp);
	return (w->zs_stop != 0 ? -1 : 0);
}

int
sh_zfs_snapshots(struct sh_zfs *z, const char *ds,
    int (*fn)(void *, const char *), void *arg, struct sh_err *e)
{
	struct zz_snaps w;
	zfs_handle_t *zhp;
	int r;

	if ((r = zz_open(z, ds, &zhp, e)) != 0) {
		if (r == 1)
			zz_none(e, ds);
		return (-1);
	}
	w.zs_fn = fn;
	w.zs_arg = arg;
	w.zs_stop = 0;
	r = zfs_iter_snapshots_sorted_v2(zhp, 0, zz_snap, &w, 0, 0);
	if (r != 0 && w.zs_stop == 0)
		zz_err(z, e, ds);
	zfs_close(zhp);
	if (w.zs_stop != 0)
		return (w.zs_stop);
	return (r != 0 ? -1 : 0);
}

/* A walk of snapshots: the name of each into ARG, the newest last. */
static int
zz_newest(void *arg, const char *snap)
{
	(void) snprintf(arg, ZFS_MAX_DATASET_NAME_LEN, "%s", snap);
	return (0);
}

/*
 * dsl_dataset_rollback_check: ENOENT, no DS; ESRCH, no SNAP of DS's;
 * EEXIST, snapshots after SNAP, or a bookmark after the newest. So
 * on either of the last two, the newest is looked at.
 */
int
sh_zfs_rollback(struct sh_zfs *z, const char *ds, const char *snap,
    struct sh_err *e)
{
	char name[ZFS_MAX_DATASET_NAME_LEN], last[ZFS_MAX_DATASET_NAME_LEN];
	int r;

	(void) snprintf(name, sizeof (name), "%s@%s", ds, snap);
	if ((r = lzc_rollback_to(ds, name)) == 0)
		return (0);
	last[0] = '\0';
	if (r == ENOENT)
		zz_none(e, ds);
	else if ((r == ESRCH || r == EEXIST) &&
	    sh_zfs_snapshots(z, ds, zz_newest, last, e) == 0 &&
	    strcmp(last, snap) != 0)
		sh_errf(e, "%s: not the newest snapshot", name);
	else
		sh_errf(e, "%s: %s", name, strerror(r));
	return (-1);
}

/* A walk of snapshots stopped at the first: there is one. */
static int
zz_any(void *arg, const char *snap)
{
	(void) arg;
	(void) snap;
	return (1);
}

/* Whether the filesystem NAME is mounted. */
static int
zz_mounted(struct sh_zfs *z, const char *name)
{
	zfs_handle_t *zhp;
	int r;

	if ((zhp = zfs_open(z->zz_hdl, name, ZFS_TYPE_FILESYSTEM)) == NULL)
		return (0);
	r = zfs_is_mounted(zhp, NULL) ? 1 : 0;
	zfs_close(zhp);
	return (r);
}

/*
 * A filesystem: dsl_destroy_head_check_impl refuses EBUSY for a
 * dataset held long, as a mount holds it, then EBUSY for snapshots,
 * then EEXIST for children; so on EBUSY the mount and the snapshots
 * are looked for. A snapshot:
 * lzc_destroy_snaps counts one that is not there as destroyed, so it
 * is looked for first, and refused as a filesystem would be.
 */
int
sh_zfs_destroy(struct sh_zfs *z, const char *name, struct sh_err *e)
{
	nvlist_t *snaps, *errlist;
	int r;

	if (strchr(name, '@') == NULL) {
		if ((r = lzc_destroy(name)) == 0)
			return (0);
		if (r == ENOENT)
			sh_errf(e, "%s: no such dataset", name);
		else if (r == EEXIST)
			sh_errf(e, "%s: has children", name);
		else if (r == EBUSY && zz_mounted(z, name))
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
	errlist = NULL;
	if ((snaps = zz_names(&name, 1)) == NULL) {
		sh_err_nomem(e);
		return (-1);
	}
	r = lzc_destroy_snaps(snaps, B_FALSE, &errlist);
	nvlist_free(snaps);
	nvlist_free(errlist);
	/* dsl_destroy_snapshot_check_impl: EBUSY holds, EEXIST clones */
	if (r != 0) {
		sh_errf(e, "%s: %s", name, r == EEXIST ? "has clones" :
		    r == EBUSY ? "has holds" : strerror(r));
		return (-1);
	}
	return (0);
}

/* Whether NAME, of the TYPES, is there. */
static int
zz_there(struct sh_zfs *z, const char *name, zfs_type_t types)
{
	return (zfs_dataset_exists(z->zz_hdl, name, types) ? 1 : 0);
}

/* DS's parent there: 0; refused as zfs.h words it otherwise. */
static int
zz_parent(struct sh_zfs *z, const char *ds, struct sh_err *e)
{
	char parent[ZFS_MAX_DATASET_NAME_LEN];
	const char *slash;

	if ((slash = strrchr(ds, '/')) != NULL) {
		(void) snprintf(parent, sizeof (parent), "%.*s",
		    (int)(slash - ds), ds);
		if (zz_there(z, parent, ZFS_TYPE_FILESYSTEM))
			return (0);
	}
	sh_errf(e, "%s: no parent", ds);
	return (-1);
}

int
sh_zfs_promote(struct sh_zfs *z, const char *ds, struct sh_err *e)
{
	char origin[ZFS_MAX_DATASET_NAME_LEN];
	char conflict[ZFS_MAX_DATASET_NAME_LEN];
	int r;

	if (!zz_there(z, ds, ZFS_TYPE_FILESYSTEM)) {
		sh_errf(e, "%s: no such filesystem", ds);
		return (-1);
	}
	if (sh_zfs_get(z, ds, "origin", origin, sizeof (origin), NULL,
	    e) != 0)
		return (-1);
	if (strcmp(origin, "-") == 0) {
		sh_errf(e, "%s: not a clone", ds);
		return (-1);
	}
	conflict[0] = '\0';
	if ((r = lzc_promote(ds, conflict, sizeof (conflict))) == 0)
		return (0);
	if (r == EEXIST)
		sh_errf(e, "%s: %s is the origin's too", ds, conflict);
	else
		sh_errf(e, "%s: %s", ds, strerror(r));
	return (-1);
}

/* zfs_rename: its changelist moves the mounts, as zfs rename's does. */
int
sh_zfs_rename(struct sh_zfs *z, const char *from, const char *to,
    struct sh_err *e)
{
	zfs_handle_t *zhp;
	renameflags_t flags;
	int r;

	if (!zz_there(z, from, ZFS_TYPE_FILESYSTEM)) {
		sh_errf(e, "%s: no such filesystem", from);
		return (-1);
	}
	if (zz_there(z, to, ZFS_TYPE_DATASET)) {
		sh_errf(e, "%s: exists", to);
		return (-1);
	}
	if (zz_parent(z, to, e) != 0)
		return (-1);
	if ((r = zz_open(z, from, &zhp, e)) != 0) {
		if (r == 1)
			zz_none(e, from);
		return (-1);
	}
	(void) memset(&flags, 0, sizeof (flags));
	r = zfs_rename(zhp, to, flags);
	if (r != 0)
		zz_err(z, e, from);
	zfs_close(zhp);
	return (r != 0 ? -1 : 0);
}

/*
 * zfs_clone, not lzc_clone: it takes PROPS as words and gives the
 * kernel native ones in their own types (zfs_valid_proplist).
 */
int
sh_zfs_clone(struct sh_zfs *z, const char *origin, const char *ds,
    const struct sh_zprop *props, size_t nprops, struct sh_err *e)
{
	zfs_handle_t *zhp;
	nvlist_t *pv;
	int r;

	if (!zz_there(z, origin, ZFS_TYPE_SNAPSHOT)) {
		sh_errf(e, "%s: no such snapshot", origin);
		return (-1);
	}
	if (zz_there(z, ds, ZFS_TYPE_DATASET)) {
		sh_errf(e, "%s: exists", ds);
		return (-1);
	}
	if (zz_parent(z, ds, e) != 0)
		return (-1);
	if ((pv = zz_props(props, nprops)) == NULL) {
		sh_err_nomem(e);
		return (-1);
	}
	if ((r = zz_open(z, origin, &zhp, e)) != 0) {
		nvlist_free(pv);
		return (-1);
	}
	r = zfs_clone(zhp, ds, nprops > 0 ? pv : NULL);
	if (r != 0)
		zz_err(z, e, ds);
	zfs_close(zhp);
	nvlist_free(pv);
	return (r != 0 ? -1 : 0);
}

/* A walk of snapshots: each one's name into ARG, the newest last. */
static int
zz_last(void *arg, const char *snap)
{
	(void) snprintf(arg, ZFS_MAX_DATASET_NAME_LEN, "%s", snap);
	return (0);
}

/* PROP, a number, of the snapshot NAME into *VAL: 0, or -1 refused. */
static int
zz_snapint(struct sh_zfs *z, const char *name, zfs_prop_t prop,
    uint64_t *val, struct sh_err *e)
{
	zfs_handle_t *zhp;
	int r;

	if ((r = zz_open(z, name, &zhp, e)) != 0) {
		if (r == 1)
			sh_errf(e, "%s: no such snapshot", name);
		return (-1);
	}
	*val = zfs_prop_get_int(zhp, prop);
	zfs_close(zhp);
	return (0);
}

/* Whether the snapshots A and B are counterparts: 1, 0, or -1. */
static int
zz_same(struct sh_zfs *z, const char *a, const char *b, struct sh_err *e)
{
	uint64_t ga, gb;

	if (zz_snapint(z, a, ZFS_PROP_GUID, &ga, e) != 0 ||
	    zz_snapint(z, b, ZFS_PROP_GUID, &gb, e) != 0)
		return (-1);
	return (ga == gb ? 1 : 0);
}

/*
 * Whether FROM is an earlier snapshot of SNAP's filesystem, by
 * createtxg, or the snapshot that filesystem is a clone of: 1, 0, or
 * -1. lzc_send says so too (EXDEV), but only once the target has
 * been looked at, and the fake looks at this before the target.
 */
static int
zz_earlier(struct sh_zfs *z, const char *snap, const char *from,
    struct sh_err *e)
{
	char fs[ZFS_MAX_DATASET_NAME_LEN], origin[ZFS_MAX_DATASET_NAME_LEN];
	uint64_t ts, tf;
	size_t n;

	n = strcspn(snap, "@");
	if (strncmp(snap, from, n) == 0 && from[n] == '@') {
		if (zz_snapint(z, snap, ZFS_PROP_CREATETXG, &ts, e) != 0 ||
		    zz_snapint(z, from, ZFS_PROP_CREATETXG, &tf, e) != 0)
			return (-1);
		return (tf < ts ? 1 : 0);
	}
	(void) snprintf(fs, sizeof (fs), "%.*s", (int)n, snap);
	if (sh_zfs_get(z, fs, "origin", origin, sizeof (origin), NULL,
	    e) != 0)
		return (-1);
	return (strcmp(origin, from) == 0 ? 1 : 0);
}

/*
 * Whether FS's newest snapshot is FROM's counterpart: 1, 0, or -1
 * refused. The receive itself asks less: dmu_recv.c looks back through
 * FS's snapshots for FROM's guid and refuses only a change since it,
 * so one taken after it with nothing changed is passed over (box run
 * 4, Z16). zfs.h asks for the newest, as beam and save send.
 */
static int
zz_newest_is(struct sh_zfs *z, const char *fs, const char *from,
    struct sh_err *e)
{
	char last[ZFS_MAX_DATASET_NAME_LEN];
	char name[ZFS_MAX_DATASET_NAME_LEN * 2];

	last[0] = '\0';
	if (sh_zfs_snapshots(z, fs, zz_last, last, e) != 0)
		return (-1);
	if (last[0] == '\0')
		return (0);
	(void) snprintf(name, sizeof (name), "%s@%s", fs, last);
	return (zz_same(z, name, from, e));
}

/*
 * lzc_send in a child, into a pipe, lzc_receive from it here: the
 * stream as zfs send -Lec makes it, nothing of -p. What each refuses
 * is looked at first, in the order the fake looks (box runs 4 and 5,
 * Z16), so that it is said in zfs.h's words: SNAP and FROM there, TO
 * a snapshot's name, FROM earlier; then the target, as the send
 * makes it or adds to it. After, the sender's errno (EXDEV: FROM no
 * ancestor) and the receiver's (dmu_recv.c: ENODEV, the base not
 * FROM's counterpart; EEXIST, the target there; ETXTBSY, changed
 * since its newest snapshot), should anything change between.
 */
int
sh_zfs_send(struct sh_zfs *z, const char *snap, const char *from,
    const char *to, const char *origin, struct sh_err *e)
{
	char tofs[ZFS_MAX_DATASET_NAME_LEN];
	const char *at;
	int fds[2], st, rr, sr, r;
	pid_t pid;

	if (!zz_there(z, snap, ZFS_TYPE_SNAPSHOT) || (from != NULL &&
	    !zz_there(z, from, ZFS_TYPE_SNAPSHOT))) {
		sh_errf(e, "%s: no such snapshot",
		    !zz_there(z, snap, ZFS_TYPE_SNAPSHOT) ? snap : from);
		return (-1);
	}
	if ((at = strchr(to, '@')) == NULL) {
		sh_errf(e, "%s: not a snapshot's name", to);
		return (-1);
	}
	(void) snprintf(tofs, sizeof (tofs), "%.*s", (int)(at - to), to);
	if (from != NULL && (r = zz_earlier(z, snap, from, e)) != 1) {
		if (r == 0)
			sh_errf(e, "%s: not an earlier snapshot of %s", from,
			    snap);
		return (-1);
	}
	if (from == NULL || origin != NULL) {
		if (origin != NULL && !zz_there(z, origin,
		    ZFS_TYPE_SNAPSHOT)) {
			sh_errf(e, "%s: no such snapshot", origin);
			return (-1);
		}
		if (zz_there(z, tofs, ZFS_TYPE_DATASET)) {
			sh_errf(e, "%s: exists", tofs);
			return (-1);
		}
		if (zz_parent(z, tofs, e) != 0)
			return (-1);
		if (origin != NULL && (r = from != NULL ?
		    zz_same(z, origin, from, e) : 0) != 1) {
			if (r == 0)
				sh_errf(e, "%s: not %s's counterpart", origin,
				    from != NULL ? from : snap);
			return (-1);
		}
	} else if (!zz_there(z, tofs, ZFS_TYPE_FILESYSTEM)) {
		sh_errf(e, "%s: no such filesystem", tofs);
		return (-1);
	} else if ((r = zz_newest_is(z, tofs, from, e)) != 1) {
		if (r == 0)
			sh_errf(e, "%s: newest snapshot is not %s's "
			    "counterpart", tofs, from);
		return (-1);
	}
	if (pipe(fds) != 0) {
		sh_errf(e, "pipe: %s", strerror(errno));
		return (-1);
	}
	if ((pid = fork()) == -1) {
		sh_errf(e, "fork: %s", strerror(errno));
		(void) close(fds[0]);
		(void) close(fds[1]);
		return (-1);
	}
	if (pid == 0) {
		/* a receiver that stops reads EPIPE, not a signal */
		(void) close(fds[0]);
		(void) signal(SIGPIPE, SIG_IGN);
		sr = lzc_send(snap, from, fds[1], LZC_SEND_FLAG_LARGE_BLOCK |
		    LZC_SEND_FLAG_EMBED_DATA | LZC_SEND_FLAG_COMPRESS);
		_exit(sr & 0xff);
	}
	(void) close(fds[1]);
	rr = lzc_receive(to, NULL, origin, B_FALSE, B_FALSE, fds[0]);
	(void) close(fds[0]);
	while (waitpid(pid, &st, 0) == -1 && errno == EINTR)
		;
	sr = WIFEXITED(st) ? WEXITSTATUS(st) : EINTR;
	if (sr != 0 && sr != EPIPE) {
		if (sr == EXDEV)
			sh_errf(e, "%s: not an earlier snapshot of %s", from,
			    snap);
		else
			sh_errf(e, "%s: %s", snap, strerror(sr));
		return (-1);
	}
	if (rr == 0)
		return (0);
	if (rr == ENODEV && origin != NULL)
		sh_errf(e, "%s: not %s's counterpart", origin, from);
	else if (rr == ENODEV)
		sh_errf(e, "%s: newest snapshot is not %s's counterpart", tofs,
		    from);
	else if (rr == EEXIST)
		sh_errf(e, "%s: exists", tofs);
	else if (rr == ETXTBSY)
		sh_errf(e, "%s: changed since its newest snapshot", tofs);
	else
		sh_errf(e, "%s: %s", to, strerror(rr));
	return (-1);
}

/* A user property set on the dataset itself: its source its name. */
int
sh_zfs_userprops(struct sh_zfs *z, const char *name,
    int (*fn)(void *, const char *, const char *), void *arg,
    struct sh_err *e)
{
	zfs_handle_t *zhp;
	nvlist_t *pv;
	nvpair_t *pr;
	const char *val, *src;
	int r;

	if ((r = zz_open(z, name, &zhp, e)) != 0) {
		if (r == 1)
			zz_none(e, name);
		return (-1);
	}
	r = 0;
	for (pr = nvlist_next_nvpair(zfs_get_user_props(zhp), NULL);
	    r == 0 && pr != NULL;
	    pr = nvlist_next_nvpair(zfs_get_user_props(zhp), pr)) {
		if (nvpair_value_nvlist(pr, &pv) != 0 ||
		    nvlist_lookup_string(pv, ZPROP_VALUE, &val) != 0 ||
		    nvlist_lookup_string(pv, ZPROP_SOURCE, &src) != 0 ||
		    strcmp(src, zfs_get_name(zhp)) != 0)
			continue;
		r = fn(arg, nvpair_name(pr), val);
	}
	zfs_close(zhp);
	return (r);
}

/*
 * zfs_mount (lib/libzfs/libzfs_mount.c) mounts nothing and says
 * nothing where the mountpoint is none or legacy or canmount is off
 * (zfs_is_mountable), and fails EBUSY on a dataset mounted already,
 * as setting a mountpoint mounts one (libzfs_changelist.c). So those
 * are looked at first: mounted where its mountpoint says is done;
 * mounted elsewhere, as zfs set -u leaves one, is refused.
 */
int
sh_zfs_mount(struct sh_zfs *z, const char *ds, struct sh_err *e)
{
	zfs_handle_t *zhp;
	char mp[ZFS_MAXPROPLEN];
	char *at;
	int r;

	if ((r = zz_open(z, ds, &zhp, e)) != 0) {
		if (r == 1)
			zz_none(e, ds);
		return (-1);
	}
	at = NULL;
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

int
sh_zfs_unmount(struct sh_zfs *z, const char *ds, struct sh_err *e)
{
	zfs_handle_t *zhp;
	int r;

	if ((r = zz_open(z, ds, &zhp, e)) != 0) {
		if (r == 1)
			zz_none(e, ds);
		return (-1);
	}
	r = zfs_unmount(zhp, NULL, 0);
	if (r != 0)
		zz_err(z, e, ds);
	zfs_close(zhp);
	return (r != 0 ? -1 : 0);
}

/*
 * The largest ashift of the pool's top-level vdevs, from its config
 * (ZPOOL_CONFIG_VDEV_TREE, its children, ZPOOL_CONFIG_ASHIFT).
 */
static int
zz_ashift(zpool_handle_t *zph, char *val, size_t len)
{
	nvlist_t *config, *tree, **kids;
	uint_t nkids, i;
	uint64_t ashift, best;

	if ((config = zpool_get_config(zph, NULL)) == NULL ||
	    nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE, &tree) != 0 ||
	    nvlist_lookup_nvlist_array(tree, ZPOOL_CONFIG_CHILDREN, &kids,
	    &nkids) != 0)
		return (-1);
	best = 0;
	for (i = 0; i < nkids; i++) {
		if (nvlist_lookup_uint64(kids[i], ZPOOL_CONFIG_ASHIFT,
		    &ashift) == 0 && ashift > best)
			best = ashift;
	}
	(void) snprintf(val, len, "%llu", (unsigned long long)best);
	return (0);
}

int
sh_zpool_get(struct sh_zfs *z, const char *pool, const char *prop,
    char *val, size_t len, struct sh_err *e)
{
	zpool_handle_t *zph;
	zpool_prop_t p;
	int r;

	if (strcmp(prop, "ashift") == 0) {
		if ((zph = zpool_open(z->zz_hdl, pool)) == NULL) {
			zz_err(z, e, pool);
			return (-1);
		}
		r = zz_ashift(zph, val, len);
		zpool_close(zph);
		if (r != 0)
			sh_errf(e, "pool %s: no vdev tree in its config",
			    pool);
		return (r);
	}
	if ((p = zpool_name_to_prop(prop)) == ZPOOL_PROP_INVAL) {
		sh_errf(e, "pool %s: no property %s", pool, prop);
		return (-1);
	}
	if ((zph = zpool_open(z->zz_hdl, pool)) == NULL) {
		zz_err(z, e, pool);
		return (-1);
	}
	r = zpool_get_prop(zph, p, val, len, NULL, B_FALSE);
	if (r != 0)
		zz_err(z, e, pool);
	zpool_close(zph);
	return (r != 0 ? -1 : 0);
}

int
sh_zpool_set(struct sh_zfs *z, const char *pool, const char *prop,
    const char *value, struct sh_err *e)
{
	zpool_handle_t *zph;
	int r;

	if ((zph = zpool_open(z->zz_hdl, pool)) == NULL) {
		zz_err(z, e, pool);
		return (-1);
	}
	r = zpool_set_prop(zph, prop, value);
	if (r != 0)
		zz_err(z, e, pool);
	zpool_close(zph);
	return (r != 0 ? -1 : 0);
}
