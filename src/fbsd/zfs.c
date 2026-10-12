/*
 * The ZFS of the FreeBSD build: src/core/zfs.h over libzfs. Built
 * against the OpenZFS headers of a FreeBSD source tree (the
 * Makefile's ZFS_CFLAGS, as zfs_rebase's are) and linked against
 * libzfs and libnvpair. Nothing here runs zfs(8).
 *
 * A failure that libzfs recorded is reported with the handle's own
 * description, which is what zfs(8) would print; libzfs prints
 * nothing itself (libzfs_print_on_error is off).
 *
 * A user property's source, as zfs get reads it (cmd/zfs/zfs_main.c,
 * get_callback): the dataset's own name where it is set locally,
 * "$recvd" where it was received, and otherwise the ancestor it is
 * inherited from.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* DS opened as a filesystem: 1 and nothing reported if there is none. */
static int
zz_open(struct sh_zfs *z, const char *ds, zfs_handle_t **zhpp,
    struct sh_err *e)
{
	*zhpp = zfs_open(z->zz_hdl, ds, ZFS_TYPE_FILESYSTEM);
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
	int r;

	if ((r = zz_open(z, ds, &zhp, e)) != 0)
		return (r);
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
	if (zfs_prop_get(zhp, p, val, len, &zs, where, sizeof (where),
	    B_FALSE) != 0) {
		sh_errf(e, "%s: %s does not apply", ds, prop);
		zfs_close(zhp);
		return (-1);
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
			sh_errf(e, "%s: no such filesystem", ds);
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
			sh_errf(e, "%s: no such filesystem", ds);
		return (-1);
	}
	r = zfs_prop_set(zhp, prop, value);
	if (r != 0)
		zz_err(z, e, ds);
	zfs_close(zhp);
	return (r != 0 ? -1 : 0);
}

/*
 * lzc_snapshot (lib/libzfs_core/libzfs_core.c): the snapshots as the
 * keys of one nvlist, the properties in another, all of it one ioctl,
 * so the snapshot exists with its properties or not at all.
 */
int
sh_zfs_snapshot(struct sh_zfs *z, const char *name,
    const struct sh_zprop *props, size_t nprops, struct sh_err *e)
{
	nvlist_t *snaps, *pv, *errlist;
	int r;

	(void) z;
	errlist = NULL;
	if ((pv = zz_props(props, nprops)) == NULL ||
	    nvlist_alloc(&snaps, NV_UNIQUE_NAME, 0) != 0) {
		nvlist_free(pv);
		sh_err_nomem(e);
		return (-1);
	}
	if (nvlist_add_boolean(snaps, name) != 0) {
		nvlist_free(snaps);
		nvlist_free(pv);
		sh_err_nomem(e);
		return (-1);
	}
	r = lzc_snapshot(snaps, nprops > 0 ? pv : NULL, &errlist);
	nvlist_free(snaps);
	nvlist_free(pv);
	nvlist_free(errlist);
	if (r != 0) {
		sh_errf(e, "%s: %s", name, strerror(r));
		return (-1);
	}
	return (0);
}

int
sh_zfs_mount(struct sh_zfs *z, const char *ds, struct sh_err *e)
{
	zfs_handle_t *zhp;
	int r;

	if ((r = zz_open(z, ds, &zhp, e)) != 0) {
		if (r == 1)
			sh_errf(e, "%s: no such filesystem", ds);
		return (-1);
	}
	r = zfs_mount(zhp, NULL, 0);
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
