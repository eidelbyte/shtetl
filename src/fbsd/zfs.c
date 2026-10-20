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
#include <sys/wait.h>

#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

int
sh_zfs_snapinfo(struct sh_zfs *z, const char *name, uint64_t *guid,
    uint64_t *txg, struct sh_err *e)
{
	zfs_handle_t *zhp;

	/* a filesystem's name would open as one */
	if (strchr(name, '@') == NULL) {
		sh_errf(e, "%s: no such snapshot", name);
		return (-1);
	}
	if ((zhp = zz_open(z, name, e)) == NULL)
		return (-1);
	*guid = zfs_prop_get_int(zhp, ZFS_PROP_GUID);
	*txg = zfs_prop_get_int(zhp, ZFS_PROP_CREATETXG);
	zfs_close(zhp);
	return (0);
}

/*
 * The property PROP of ZHP as zfs get prints it, into BUF: 1; 0 for
 * one it has none of, which zfs get prints "-".
 */
static int
zz_str(zfs_handle_t *zhp, zfs_prop_t prop, char *buf, size_t len)
{
	if (zfs_prop_get(zhp, prop, buf, len, NULL, NULL, 0, B_TRUE) != 0 ||
	    buf[0] == '\0' || strcmp(buf, "-") == 0)
		return (0);
	return (1);
}

int
sh_zfs_origin(struct sh_zfs *z, const char *ds, char *origin, size_t len,
    struct sh_err *e)
{
	zfs_handle_t *zhp;
	int r;

	if ((zhp = zz_open(z, ds, e)) == NULL)
		return (-1);
	r = zz_str(zhp, ZFS_PROP_ORIGIN, origin, len);
	zfs_close(zhp);
	return (r);
}

/*
 * zfs_clone, not lzc_clone: it takes PROPS as words and gives the
 * kernel native ones in their own types; like zfs_create it looks for
 * the parent (EZFS_NOENT), then the name (EZFS_EXISTS).
 */
int
sh_zfs_clone(struct sh_zfs *z, const char *origin, const char *ds,
    const struct sh_zprop *props, size_t nprops, struct sh_err *e)
{
	zfs_handle_t *zhp;
	nvlist_t *nv;
	int r;

	if (strchr(origin, '@') == NULL) {
		sh_errf(e, "%s: no such snapshot", origin);
		return (-1);
	}
	if ((zhp = zz_open(z, origin, e)) == NULL)
		return (-1);
	nv = zz_props(props, nprops);
	r = zfs_clone(zhp, ds, nv);
	nvlist_free(nv);
	zfs_close(zhp);
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

int
sh_zfs_promote(struct sh_zfs *z, const char *ds, struct sh_err *e)
{
	char origin[ZFS_MAX_DATASET_NAME_LEN];
	char conflict[ZFS_MAX_DATASET_NAME_LEN];
	int r;

	if ((r = sh_zfs_origin(z, ds, origin, sizeof (origin), e)) != 1) {
		if (r == 0)
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

/* FS's receive_resume_token, into TOKEN: 1; 0 for none; -1. */
static int
zz_token(struct sh_zfs *z, const char *fs, char *token, size_t len,
    struct sh_err *e)
{
	zfs_handle_t *zhp;
	int r;

	if ((zhp = zz_open(z, fs, e)) == NULL)
		return (-1);
	r = zz_str(zhp, ZFS_PROP_RECEIVE_RESUME_TOKEN, token, len);
	zfs_close(zhp);
	return (r);
}

int
sh_zfs_resumable(struct sh_zfs *z, const char *ds, struct sh_err *e)
{
	char token[ZFS_MAXPROPLEN];

	return (zz_token(z, ds, token, sizeof (token), e));
}

/*
 * SNAP sent from FROM by lzc_send, or with RESUME by lzc_send_resume
 * from OBJ and OFF, in a child into a pipe; received as TO here by
 * lzc_receive_resumable, so a stream broken off leaves its token. The
 * receiver's errno names the refusal (dmu_recv.c: ENODEV the base not
 * FROM's counterpart, EEXIST the target there, ETXTBSY changed since
 * its newest snapshot), else the sender's (EXDEV: FROM no ancestor).
 */
static int
zz_stream(struct sh_zfs *z, const char *snap, const char *from,
    const char *to, const char *origin, enum lzc_send_flags flags,
    int resume, uint64_t obj, uint64_t off, struct sh_err *e)
{
	int fds[2], st, rr, sr;
	size_t n = strcspn(to, "@");
	pid_t pid;

	(void) z;
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
		/* a receiver that stops is EPIPE, not a signal */
		(void) close(fds[0]);
		(void) signal(SIGPIPE, SIG_IGN);
		sr = resume ? lzc_send_resume(snap, from, fds[1], flags, obj,
		    off) : lzc_send(snap, from, fds[1], flags);
		_exit(sr & 0xff);
	}
	(void) close(fds[1]);
	rr = lzc_receive_resumable(to, NULL, origin, B_FALSE, B_FALSE, fds[0]);
	(void) close(fds[0]);
	while (waitpid(pid, &st, 0) == -1 && errno == EINTR)
		;
	sr = WIFEXITED(st) ? WEXITSTATUS(st) : EINTR;
	if (rr == 0 && sr == 0)
		return (0);
	if (rr == ENODEV && origin != NULL)
		sh_errf(e, "%s: not %s's counterpart", origin, from);
	else if (rr == ENODEV)
		sh_errf(e, "%.*s: newest snapshot is not %s's counterpart",
		    (int)n, to, from);
	else if (rr == EEXIST)
		sh_errf(e, "%.*s: exists", (int)n, to);
	else if (rr == ETXTBSY)
		sh_errf(e, "%.*s: changed since its newest snapshot", (int)n,
		    to);
	else if (sr != 0 && sr != EPIPE)
		sh_errf(e, "%s: %s", snap, strerror(sr));
	else
		sh_errf(e, "%s: %s", to, strerror(rr));
	return (-1);
}

/* A walk of snapshots: the newest's name into ARG. */
static int
zz_last(void *arg, const char *snap)
{
	(void) snprintf(arg, ZFS_MAX_DATASET_NAME_LEN, "%s", snap);
	return (0);
}

/*
 * Whether a stream from FROM may go onto FS: with ORIGIN, ORIGIN is
 * FROM's counterpart (the same guid); without, FS's newest snapshot
 * is. 1, 0, or -1.
 */
static int
zz_base(struct sh_zfs *z, const char *fs, const char *from,
    const char *origin, struct sh_err *e)
{
	char last[ZFS_MAX_DATASET_NAME_LEN], name[ZFS_MAX_DATASET_NAME_LEN * 2];
	uint64_t gf, gb, t;

	if (origin != NULL) {
		(void) snprintf(name, sizeof (name), "%s", origin);
	} else {
		last[0] = '\0';
		if (sh_zfs_snapshots(z, fs, zz_last, last, e) != 0)
			return (-1);
		if (last[0] == '\0')
			return (0);
		(void) snprintf(name, sizeof (name), "%s@%s", fs, last);
	}
	if (sh_zfs_snapinfo(z, from, &gf, &t, e) != 0 ||
	    sh_zfs_snapinfo(z, name, &gb, &t, e) != 0)
		return (-1);
	return (gf == gb);
}

int
sh_zfs_send(struct sh_zfs *z, const char *snap, const char *from,
    const char *to, const char *origin, struct sh_err *e)
{
	const char *const named[] = { snap, from, origin };
	char fs[ZFS_MAX_DATASET_NAME_LEN], token[ZFS_MAXPROPLEN];
	const char *at = strchr(to, '@');
	size_t i;
	int r;

	for (i = 0; i < 3; i++) {
		if (named[i] != NULL && !zfs_dataset_exists(z->zz_hdl,
		    named[i], ZFS_TYPE_SNAPSHOT)) {
			sh_errf(e, "%s: no such snapshot", named[i]);
			return (-1);
		}
	}
	if (at == NULL) {
		sh_errf(e, "%s: not a snapshot's name", to);
		return (-1);
	}
	(void) snprintf(fs, sizeof (fs), "%.*s", (int)(at - to), to);
	if (zfs_dataset_exists(z->zz_hdl, fs, ZFS_TYPE_FILESYSTEM)) {
		if ((r = zz_token(z, fs, token, sizeof (token), e)) != 0) {
			if (r == 1)
				sh_errf(e, "%s: a receive to resume", fs);
			return (-1);
		}
		if (from == NULL || origin != NULL) {
			sh_errf(e, "%s: exists", fs);
			return (-1);
		}
	} else if (from != NULL && origin == NULL) {
		sh_errf(e, "%s: no such filesystem", fs);
		return (-1);
	}
	/* the base, by guid, as the beam's rule names it (plan 8.1) */
	if (from != NULL && (r = zz_base(z, fs, from, origin, e)) != 1) {
		if (r == 0 && origin != NULL)
			sh_errf(e, "%s: not %s's counterpart", origin, from);
		else if (r == 0)
			sh_errf(e, "%s: newest snapshot is not %s's "
			    "counterpart", fs, from);
		return (-1);
	}
	return (zz_stream(z, snap, from, to, origin,
	    LZC_SEND_FLAG_LARGE_BLOCK | LZC_SEND_FLAG_EMBED_DATA |
	    LZC_SEND_FLAG_COMPRESS, 0, 0, 0, e));
}

/* A walk of FS's snapshots for the one whose guid is the search's. */
struct zz_find {
	libzfs_handle_t	*zf_hdl;
	const char	*zf_fs;
	uint64_t	zf_guid;
	char		zf_name[ZFS_MAX_DATASET_NAME_LEN];
};

static int
zz_find(void *arg, const char *snap)
{
	struct zz_find *f = arg;
	zfs_handle_t *zhp;
	int found = 0;

	(void) snprintf(f->zf_name, sizeof (f->zf_name), "%s@%s", f->zf_fs,
	    snap);
	if ((zhp = zfs_open(f->zf_hdl, f->zf_name, ZFS_TYPE_SNAPSHOT)) !=
	    NULL) {
		found = zfs_prop_get_int(zhp, ZFS_PROP_GUID) == f->zf_guid;
		zfs_close(zhp);
	}
	return (found);
}

/*
 * The snapshot whose guid is GUID among those of SNAP's filesystem, or
 * the one that filesystem is a clone of, into F's name: 1, 0, or -1:
 * what lzc_send_resume needs of a token's fromguid.
 */
static int
zz_byguid(struct sh_zfs *z, const char *snap, uint64_t guid,
    struct zz_find *f, struct sh_err *e)
{
	char fs[ZFS_MAX_DATASET_NAME_LEN];
	uint64_t g, t;
	int r;

	(void) snprintf(fs, sizeof (fs), "%.*s", (int)strcspn(snap, "@"), snap);
	f->zf_hdl = z->zz_hdl;
	f->zf_fs = fs;
	f->zf_guid = guid;
	if ((r = sh_zfs_snapshots(z, fs, zz_find, f, e)) != 0)
		return (r);
	if ((r = sh_zfs_origin(z, fs, f->zf_name, sizeof (f->zf_name), e)) !=
	    1)
		return (r);
	if (sh_zfs_snapinfo(z, f->zf_name, &g, &t, e) != 0)
		return (-1);
	return (g == guid);
}

/*
 * The token decoded (zfs_send_resume_token_to_nvlist), its source
 * looked for as it was when the stream began (toname with toguid), and
 * the snapshot it was sent from found by its fromguid; the stream sent
 * on from object and offset with the flags it began with.
 */
int
sh_zfs_resume(struct sh_zfs *z, const char *ds, struct sh_err *e)
{
	char token[ZFS_MAXPROPLEN], to[ZFS_MAX_DATASET_NAME_LEN * 2];
	enum lzc_send_flags flags = 0;
	uint64_t toguid, fromguid = 0, obj, off, g, t;
	struct zz_find f;
	const char *toname;
	nvlist_t *nv;
	int r;

	if ((r = zz_token(z, ds, token, sizeof (token), e)) != 1) {
		if (r == 0)
			sh_errf(e, "%s: no receive to resume", ds);
		return (-1);
	}
	if ((nv = zfs_send_resume_token_to_nvlist(z->zz_hdl, token)) ==
	    NULL) {
		zz_err(z, e, ds);
		return (-1);
	}
	toname = fnvlist_lookup_string(nv, "toname");
	toguid = fnvlist_lookup_uint64(nv, "toguid");
	obj = fnvlist_lookup_uint64(nv, "object");
	off = fnvlist_lookup_uint64(nv, "offset");
	(void) nvlist_lookup_uint64(nv, "fromguid", &fromguid);
	if (nvlist_exists(nv, "largeblockok"))
		flags |= LZC_SEND_FLAG_LARGE_BLOCK;
	if (nvlist_exists(nv, "embedok"))
		flags |= LZC_SEND_FLAG_EMBED_DATA;
	if (nvlist_exists(nv, "compressok"))
		flags |= LZC_SEND_FLAG_COMPRESS;
	if (nvlist_exists(nv, "rawok"))
		flags |= LZC_SEND_FLAG_RAW;
	r = -1;
	if (!zfs_dataset_exists(z->zz_hdl, toname, ZFS_TYPE_SNAPSHOT) ||
	    sh_zfs_snapinfo(z, toname, &g, &t, e) != 0 || g != toguid)
		sh_errf(e, "%s: no such snapshot", toname);
	else if (fromguid != 0 && zz_byguid(z, toname, fromguid, &f, e) != 1)
		sh_errf(e, "%s: its base gone", toname);
	else {
		(void) snprintf(to, sizeof (to), "%s%s", ds,
		    strchr(toname, '@'));
		r = zz_stream(z, toname, fromguid != 0 ? f.zf_name : NULL, to,
		    NULL, flags, 1, obj, off, e);
	}
	nvlist_free(nv);
	return (r);
}

/*
 * As zfs receive -A (cmd/zfs/zfs_main.c): the hidden DS/%recv an
 * incremental was going into destroyed, or, where there is none, DS
 * itself, which a whole or clone stream was making.
 */
int
sh_zfs_abort(struct sh_zfs *z, const char *ds, struct sh_err *e)
{
	char token[ZFS_MAXPROPLEN], recv[ZFS_MAX_DATASET_NAME_LEN + 8];
	zfs_handle_t *zhp;
	int r;

	if ((r = zz_token(z, ds, token, sizeof (token), e)) != 1) {
		if (r == 0)
			sh_errf(e, "%s: no receive to resume", ds);
		return (-1);
	}
	(void) snprintf(recv, sizeof (recv), "%s/%%recv", ds);
	if ((zhp = zfs_open(z->zz_hdl, zfs_dataset_exists(z->zz_hdl, recv,
	    ZFS_TYPE_FILESYSTEM) ? recv : ds, ZFS_TYPE_FILESYSTEM)) == NULL) {
		zz_err(z, e, ds);
		return (-1);
	}
	if ((r = zfs_destroy(zhp, B_FALSE)) != 0)
		zz_err(z, e, ds);
	zfs_close(zhp);
	return (r != 0 ? -1 : 0);
}
