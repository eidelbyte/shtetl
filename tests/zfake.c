/*
 * The fake ZFS of the unit tests (tests/zfake.h). Small and fixed in
 * size: a test sets up a few pools and a few dozen filesystems.
 * Filesystems are walked in the order they were made, which is a
 * depth-first order as long as a test makes parents first, as ZFS
 * itself requires.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/err.h"
#include "core/zfs.h"
#include "zfake.h"

#define	ZF_DS		128
#define	ZF_SNAPS	256
#define	ZF_PROPS	32
#define	ZF_WRITES	16	/* zfake_write calls a filesystem */
#define	ZF_NAME		256
#define	ZF_VAL		256

struct zf_prop {
	char	zp_name[ZF_NAME];
	char	zp_val[ZF_VAL];
};

struct zf_ds {
	char		zd_name[ZF_NAME];
	struct zf_prop	zd_props[ZF_PROPS];
	int		zd_n;
	char		zd_at[ZF_VAL];		/* mounted there; "" if not */
	char		zd_origin[ZF_NAME];	/* a clone's, ds@snap; "" */
	unsigned long	zd_wtxg[ZF_WRITES];	/* zfake_write's, when */
	unsigned long	zd_wbytes[ZF_WRITES];	/* and how much */
	int		zd_nw;
	struct zf_prop	zd_pool[ZF_PROPS];	/* a pool's root: the pool's */
	int		zd_npool;
};

/* A snapshot: its properties, and the transaction that took it. */
struct zf_snap {
	char		zs_name[ZF_NAME];	/* ds@snap */
	struct zf_prop	zs_props[ZF_PROPS];
	int		zs_n;
	unsigned long	zs_txg;
	unsigned long	zs_guid;
	int		zs_holds;	/* zfake_hold's */
};

struct sh_zfs {
	int	zz_open;
};

static struct zf_ds zf_ds[ZF_DS];
static int zf_n;
static struct zf_snap zf_snaps[ZF_SNAPS];	/* oldest first */
static int zf_nsnaps;
static unsigned long zf_txg;
static char zf_rolled[ZF_NAME];		/* the last rollback, ds@snap */
static void (*zf_hook)(const char *, const char *, void *);
static void *zf_hookarg;
static char zf_failing[32];
static int zf_failin;		/* calls of zf_failing still to pass */
static struct sh_zfs zf_handle;

/* The native properties the fake knows, and their defaults. */
static const struct {
	const char	*n_name;
	const char	*n_default;
	int		n_inherit;
} zf_native[] = {
	{ "atime", "on", 1 },
	{ "compression", "on", 1 },
	{ "aclmode", "discard", 1 },
	{ "readonly", "off", 1 },
	{ "exec", "on", 1 },
	{ "setuid", "on", 1 },
	{ "recordsize", "128K", 1 },
	{ "mountpoint", NULL, 1 },
	{ "canmount", "on", 0 },
	{ "mounted", "no", 0 },
	{ NULL, NULL, 0 }
};

static void
zf_die(const char *what, const char *arg)
{
	printf("zfake: %s: %s\n", what, arg);
	exit(1);
}

static struct zf_ds *
zf_find(const char *ds)
{
	int i;

	for (i = 0; i < zf_n; i++) {
		if (strcmp(zf_ds[i].zd_name, ds) == 0)
			return (&zf_ds[i]);
	}
	return (NULL);
}

static const char *
zf_prop(const struct zf_prop *p, int n, const char *name)
{
	int i;

	for (i = 0; i < n; i++) {
		if (strcmp(p[i].zp_name, name) == 0)
			return (p[i].zp_val);
	}
	return (NULL);
}

static void
zf_put(struct zf_prop *p, int *n, const char *name, const char *val)
{
	int i;

	for (i = 0; i < *n; i++) {
		if (strcmp(p[i].zp_name, name) == 0)
			break;
	}
	if (i == ZF_PROPS)
		zf_die("too many properties", name);
	(void) snprintf(p[i].zp_name, ZF_NAME, "%s", name);
	(void) snprintf(p[i].zp_val, ZF_VAL, "%s", val);
	if (i == *n)
		(*n)++;
}

/* Whether a call of WHAT is the one set to fail; it fails once. */
static int
zf_fails(const char *what, struct sh_err *e)
{
	if (strcmp(zf_failing, what) != 0)
		return (0);
	if (zf_failin > 1) {
		zf_failin--;
		return (0);
	}
	zf_failing[0] = '\0';
	sh_errf(e, "zfake: %s failed as asked", what);
	return (1);
}

void
zfake_reset(void)
{
	(void) memset(zf_ds, 0, sizeof (zf_ds));
	zf_n = 0;
	(void) memset(zf_snaps, 0, sizeof (zf_snaps));
	zf_nsnaps = 0;
	zf_txg = 1;
	zf_rolled[0] = '\0';
	zf_failing[0] = '\0';
	zf_hook = NULL;
}

void
zfake_hook(void (*fn)(const char *op, const char *name, void *arg),
    void *arg)
{
	zf_hook = fn;
	zf_hookarg = arg;
}

/* What the tests' hook is told: OP done to NAME. */
static void
zf_told(const char *op, const char *name)
{
	if (zf_hook != NULL)
		zf_hook(op, name, zf_hookarg);
}

static void
zf_make(const char *ds)
{
	if (zf_n == ZF_DS)
		zf_die("too many filesystems", ds);
	if (zf_find(ds) != NULL)
		zf_die("already there", ds);
	(void) memset(&zf_ds[zf_n], 0, sizeof (zf_ds[zf_n]));
	(void) snprintf(zf_ds[zf_n].zd_name, ZF_NAME, "%s", ds);
	zf_n++;
}

/* DS's parent, into BUF; 0 if DS is a pool's root. */
static int
zf_parent(const char *ds, char *buf)
{
	const char *slash = strrchr(ds, '/');

	if (slash == NULL)
		return (0);
	(void) snprintf(buf, ZF_NAME, "%.*s", (int)(slash - ds), ds);
	return (1);
}

static int zf_get(const char *ds, const char *prop, char *val, size_t len,
    enum sh_zsrc *src, struct sh_err *e);

/* PROP of D no longer set locally. */
static void
zf_drop(struct zf_ds *d, const char *prop)
{
	int i;

	for (i = 0; i < d->zd_n; i++) {
		if (strcmp(d->zd_props[i].zp_name, prop) == 0) {
			(void) memmove(&d->zd_props[i], &d->zd_props[i + 1],
			    (size_t)(d->zd_n - i - 1) *
			    sizeof (d->zd_props[0]));
			d->zd_n--;
			return;
		}
	}
}

/* Whether D takes its mountpoint from DS: DS, or below with none set. */
static int
zf_takes(const char *d, const char *ds)
{
	char cur[ZF_NAME], up[ZF_NAME];
	const struct zf_ds *x;
	size_t len = strlen(ds);

	if (strncmp(d, ds, len) != 0 || (d[len] != '\0' && d[len] != '/'))
		return (0);
	(void) snprintf(cur, sizeof (cur), "%s", d);
	while (strcmp(cur, ds) != 0) {
		x = zf_find(cur);
		if (zf_prop(x->zd_props, x->zd_n, "mountpoint") != NULL ||
		    !zf_parent(cur, up))
			return (0);
		(void) snprintf(cur, sizeof (cur), "%s", up);
	}
	return (1);
}

/*
 * DS's mountpoint set to VALUE, or inherited (NULL), with what zfs set
 * and zfs inherit do to mounts (lib/libzfs/libzfs_changelist.c): DS
 * and each dataset taking its mountpoint from DS are unmounted, the
 * property changed, and each then mounted, parents first, where its
 * mountpoint now says, if it was mounted or its canmount is on, so
 * long as canmount is not off and the mountpoint is not none or
 * legacy. Setting a mountpoint mounts a dataset that was not.
 */
static void
zf_remount(const char *ds, const char *value)
{
	static char was[ZF_DS];
	char mp[ZF_VAL], cm[ZF_VAL];
	struct zf_ds *d;
	int i;

	for (i = 0; i < zf_n; i++) {
		was[i] = 0;
		if (!zf_takes(zf_ds[i].zd_name, ds))
			continue;
		was[i] = zf_ds[i].zd_at[0] != '\0' ? 2 : 1;
		zf_ds[i].zd_at[0] = '\0';
	}
	d = zf_find(ds);
	if (value != NULL)
		zf_put(d->zd_props, &d->zd_n, "mountpoint", value);
	else
		zf_drop(d, "mountpoint");
	for (i = 0; i < zf_n; i++) {
		if (was[i] == 0)
			continue;
		(void) zf_get(zf_ds[i].zd_name, "mountpoint", mp, sizeof (mp),
		    NULL, NULL);
		(void) zf_get(zf_ds[i].zd_name, "canmount", cm, sizeof (cm),
		    NULL, NULL);
		if (mp[0] == '/' && strcmp(cm, "off") != 0 &&
		    (was[i] == 2 || strcmp(cm, "on") == 0))
			(void) snprintf(zf_ds[i].zd_at, ZF_VAL, "%s", mp);
	}
}

void
zfake_pool(const char *pool)
{
	struct zf_ds *d;

	zf_make(pool);
	d = &zf_ds[zf_n - 1];
	zf_put(d->zd_pool, &d->zd_npool, "ashift", "12");
	zf_put(d->zd_pool, &d->zd_npool, "autotrim", "off");
	zf_put(d->zd_pool, &d->zd_npool, "bootfs", "-");
	zf_put(d->zd_pool, &d->zd_npool, "altroot", "-");
}

void
zfake_fs(const char *ds)
{
	char parent[ZF_NAME];

	if (!zf_parent(ds, parent) || zf_find(parent) == NULL)
		zf_die("no parent", ds);
	zf_make(ds);
}

void
zfake_set(const char *ds, const char *prop, const char *value)
{
	struct zf_ds *d;

	if ((d = zf_find(ds)) == NULL)
		zf_die("no such filesystem", ds);
	/* mounted yes: where its mountpoint says */
	if (strcmp(prop, "mounted") == 0) {
		d->zd_at[0] = '\0';
		if (strcmp(value, "yes") == 0)
			(void) zf_get(ds, "mountpoint", d->zd_at, ZF_VAL, NULL,
			    NULL);
		return;
	}
	zf_put(d->zd_props, &d->zd_n, prop, value);
}

void
zfake_fail(const char *what)
{
	zfake_fail_nth(what, 1);
}

void
zfake_fail_nth(const char *what, int nth)
{
	(void) snprintf(zf_failing, sizeof (zf_failing), "%s", what);
	zf_failin = nth;
}

int
zfake_count(void)
{
	return (zf_n);
}

int
zfake_exists(const char *ds)
{
	return (zf_find(ds) != NULL);
}

static struct zf_snap *
zf_snap(const char *name)
{
	int i;

	for (i = 0; i < zf_nsnaps; i++) {
		if (strcmp(zf_snaps[i].zs_name, name) == 0)
			return (&zf_snaps[i]);
	}
	return (NULL);
}

int
zfake_snapshot(const char *name)
{
	return (zf_snap(name) != NULL);
}

const char *
zfake_last_snapshot(void)
{
	return (zf_nsnaps > 0 ? zf_snaps[zf_nsnaps - 1].zs_name : NULL);
}

const char *
zfake_last_rollback(void)
{
	return (zf_rolled[0] != '\0' ? zf_rolled : NULL);
}

/* NAME, ds@snap, as a snapshot's: why not, or NULL; its DS into BUF. */
static const char *
zf_snapname(const char *name, char *buf)
{
	const char *at;

	if ((at = strchr(name, '@')) == NULL || at[1] == '\0' ||
	    strchr(at + 1, '@') != NULL || strchr(at + 1, '/') != NULL)
		return ("not a snapshot's name");
	(void) snprintf(buf, ZF_NAME, "%.*s", (int)(at - name), name);
	if (zf_find(buf) == NULL)
		return ("no such filesystem");
	if (zf_snap(name) != NULL)
		return ("taken");
	return (NULL);
}

int
sh_zfs_snapshot(struct sh_zfs *z, const char *const *names, size_t n,
    const struct sh_zprop *props, size_t nprops, struct sh_err *e)
{
	struct zf_snap *s;
	char ds[ZF_NAME];
	const char *why;
	size_t i, j, k;

	(void) z;
	if (zf_fails("snapshot", e))
		return (-1);
	if (n == 0 || zf_nsnaps + (int)n > ZF_SNAPS) {
		sh_errf(e, "zfake: no snapshot, or too many");
		return (-1);
	}
	/* all of them, or none */
	for (i = 0; i < n; i++) {
		why = zf_snapname(names[i], ds);
		for (j = 0; why == NULL && j < i; j++) {
			if (strcmp(names[j], names[i]) == 0)
				why = "named twice";
		}
		if (why != NULL) {
			sh_errf(e, "zfake: %s: %s", names[i], why);
			return (-1);
		}
	}
	zf_txg++;
	for (i = 0; i < n; i++) {
		s = &zf_snaps[zf_nsnaps++];
		(void) memset(s, 0, sizeof (*s));
		(void) snprintf(s->zs_name, ZF_NAME, "%s", names[i]);
		s->zs_txg = zf_txg;
		s->zs_guid = 0x5eed0000UL + (unsigned long)zf_nsnaps * 7919UL;
		for (k = 0; k < nprops; k++)
			zf_put(s->zs_props, &s->zs_n, props[k].zp_name,
			    props[k].zp_value);
	}
	for (i = 0; i < n; i++)
		zf_told("snapshot", names[i]);
	return (0);
}

int
sh_zfs_snapshots(struct sh_zfs *z, const char *ds,
    int (*fn)(void *, const char *), void *arg, struct sh_err *e)
{
	size_t len = strlen(ds);
	int i, r;

	(void) z;
	if (zf_fails("snapshots", e))
		return (-1);
	if (zf_find(ds) == NULL) {
		sh_errf(e, "zfake: %s: no such filesystem", ds);
		return (-1);
	}
	for (i = 0; i < zf_nsnaps; i++) {
		if (strncmp(zf_snaps[i].zs_name, ds, len) == 0 &&
		    zf_snaps[i].zs_name[len] == '@' &&
		    (r = fn(arg, zf_snaps[i].zs_name + len + 1)) != 0)
			return (r);
	}
	return (0);
}

int
sh_zfs_rollback(struct sh_zfs *z, const char *ds, const char *snap,
    struct sh_err *e)
{
	size_t len = strlen(ds);
	int i;

	(void) z;
	if (zf_fails("rollback", e))
		return (-1);
	if (zf_find(ds) == NULL) {
		sh_errf(e, "zfake: %s: no such filesystem", ds);
		return (-1);
	}
	for (i = zf_nsnaps - 1; i >= 0; i--) {
		if (strncmp(zf_snaps[i].zs_name, ds, len) == 0 &&
		    zf_snaps[i].zs_name[len] == '@')
			break;
	}
	if (i < 0 || strcmp(zf_snaps[i].zs_name + len + 1, snap) != 0) {
		sh_errf(e, "zfake: %s@%s: not the newest snapshot", ds, snap);
		return (-1);
	}
	(void) snprintf(zf_rolled, sizeof (zf_rolled), "%s",
	    zf_snaps[i].zs_name);
	zf_told("rollback", zf_rolled);
	return (0);
}

int
sh_zfs_destroy(struct sh_zfs *z, const char *name, struct sh_err *e)
{
	struct zf_snap *s;
	struct zf_ds *d;
	char parent[ZF_NAME];
	size_t len;
	int i;

	(void) z;
	if (zf_fails("destroy", e))
		return (-1);
	if ((s = zf_snap(name)) != NULL) {
		/* dsl_destroy_snapshot_check_impl: EEXIST cloned, EBUSY held */
		if (s->zs_holds > 0) {
			sh_errf(e, "zfake: %s: has holds", name);
			return (-1);
		}
		for (i = 0; i < zf_n; i++) {
			if (strcmp(zf_ds[i].zd_origin, name) == 0) {
				sh_errf(e, "zfake: %s: has clones", name);
				return (-1);
			}
		}
		i = (int)(s - zf_snaps);
		(void) memmove(&zf_snaps[i], &zf_snaps[i + 1],
		    (size_t)(zf_nsnaps - i - 1) * sizeof (zf_snaps[0]));
		zf_nsnaps--;
		zf_told("destroy", name);
		return (0);
	}
	if (strchr(name, '@') != NULL || (d = zf_find(name)) == NULL) {
		sh_errf(e, "zfake: %s: no such dataset", name);
		return (-1);
	}
	/* its mount, then snapshots, as dsl_destroy_head_check_impl looks */
	if (d->zd_at[0] != '\0') {
		sh_errf(e, "zfake: %s: mounted", name);
		return (-1);
	}
	len = strlen(name);
	for (i = 0; i < zf_nsnaps; i++) {
		if (strncmp(zf_snaps[i].zs_name, name, len) == 0 &&
		    zf_snaps[i].zs_name[len] == '@') {
			sh_errf(e, "zfake: %s: has snapshots", name);
			return (-1);
		}
	}
	for (i = 0; i < zf_n; i++) {
		if (zf_parent(zf_ds[i].zd_name, parent) &&
		    strcmp(parent, name) == 0) {
			sh_errf(e, "zfake: %s: has children", name);
			return (-1);
		}
	}
	i = (int)(d - zf_ds);
	(void) memmove(&zf_ds[i], &zf_ds[i + 1],
	    (size_t)(zf_n - i - 1) * sizeof (zf_ds[0]));
	zf_n--;
	return (0);
}

int
sh_zfs_inherit(struct sh_zfs *z, const char *ds, const char *prop,
    struct sh_err *e)
{
	struct zf_snap *sp;
	struct zf_ds *d;
	int i;

	(void) z;
	if (zf_fails("inherit", e))
		return (-1);
	/* a snapshot's user property, its own no longer */
	if (strchr(ds, '@') != NULL) {
		if ((sp = zf_snap(ds)) == NULL) {
			sh_errf(e, "zfake: %s: no such snapshot", ds);
			return (-1);
		}
		for (i = 0; i < sp->zs_n; i++) {
			if (strcmp(sp->zs_props[i].zp_name, prop) != 0)
				continue;
			(void) memmove(&sp->zs_props[i], &sp->zs_props[i + 1],
			    (size_t)(sp->zs_n - i - 1) *
			    sizeof (sp->zs_props[0]));
			sp->zs_n--;
			break;
		}
		return (0);
	}
	if ((d = zf_find(ds)) == NULL) {
		sh_errf(e, "zfake: %s: no such filesystem", ds);
		return (-1);
	}
	if (strcmp(prop, "mountpoint") == 0)
		zf_remount(ds, NULL);
	else
		zf_drop(d, prop);
	return (0);
}

int
sh_zfs_unmount(struct sh_zfs *z, const char *ds, struct sh_err *e)
{
	(void) z;
	if (zf_fails("unmount", e))
		return (-1);
	if (zf_find(ds) == NULL) {
		sh_errf(e, "zfake: %s: no such filesystem", ds);
		return (-1);
	}
	zf_find(ds)->zd_at[0] = '\0';
	return (0);
}

/* Mounted where its mountpoint says already: nothing to do. */
int
sh_zfs_mount(struct sh_zfs *z, const char *ds, struct sh_err *e)
{
	char mp[ZF_VAL], cm[ZF_VAL];
	struct zf_ds *d;

	if (zf_fails("mount", e))
		return (-1);
	if (sh_zfs_get(z, ds, "mountpoint", mp, sizeof (mp), NULL, e) != 0 ||
	    sh_zfs_get(z, ds, "canmount", cm, sizeof (cm), NULL, e) != 0)
		return (-1);
	d = zf_find(ds);
	if (mp[0] != '/') {
		sh_errf(e, "zfake: %s: no mountpoint", ds);
		return (-1);
	}
	if (strcmp(cm, "off") == 0) {
		sh_errf(e, "zfake: %s: canmount is off", ds);
		return (-1);
	}
	if (d->zd_at[0] != '\0' && strcmp(d->zd_at, mp) != 0) {
		sh_errf(e, "zfake: %s: mounted at %s, not its mountpoint", ds,
		    d->zd_at);
		return (-1);
	}
	(void) snprintf(d->zd_at, ZF_VAL, "%s", mp);
	return (0);
}

int
sh_zfs_clone(struct sh_zfs *z, const char *origin, const char *ds,
    const struct sh_zprop *props, size_t nprops, struct sh_err *e)
{
	char parent[ZF_NAME];
	size_t i;

	(void) z;
	if (zf_fails("clone", e))
		return (-1);
	if (zf_snap(origin) == NULL) {
		sh_errf(e, "zfake: %s: no such snapshot", origin);
		return (-1);
	}
	if (zf_find(ds) != NULL) {
		sh_errf(e, "zfake: %s: exists", ds);
		return (-1);
	}
	if (!zf_parent(ds, parent) || zf_find(parent) == NULL) {
		sh_errf(e, "zfake: %s: no parent", ds);
		return (-1);
	}
	zf_make(ds);
	(void) snprintf(zf_find(ds)->zd_origin, ZF_NAME, "%s", origin);
	for (i = 0; i < nprops; i++)
		zfake_set(ds, props[i].zp_name, props[i].zp_value);
	return (0);
}

/* DS's newest snapshot, or NULL. */
static struct zf_snap *
zf_newest(const char *ds)
{
	size_t len = strlen(ds);
	int i;

	for (i = zf_nsnaps - 1; i >= 0; i--) {
		if (strncmp(zf_snaps[i].zs_name, ds, len) == 0 &&
		    zf_snaps[i].zs_name[len] == '@')
			return (&zf_snaps[i]);
	}
	return (NULL);
}

/*
 * A send as zfs.h has it: the snapshot TO made in the fake, the guid
 * SNAP's, no property, the filesystem made or taken as asked.
 */
int
sh_zfs_send(struct sh_zfs *z, const char *snap, const char *from,
    const char *to, const char *origin, struct sh_err *e)
{
	struct zf_snap *sp, *fp, *op, *np;
	char sfs[ZF_NAME], ffs[ZF_NAME], tofs[ZF_NAME], parent[ZF_NAME];
	const char *at;

	(void) z;
	if (zf_fails("send", e))
		return (-1);
	if ((sp = zf_snap(snap)) == NULL) {
		sh_errf(e, "zfake: %s: no such snapshot", snap);
		return (-1);
	}
	fp = NULL;
	if (from != NULL && (fp = zf_snap(from)) == NULL) {
		sh_errf(e, "zfake: %s: no such snapshot", from);
		return (-1);
	}
	if ((at = strchr(to, '@')) == NULL) {
		sh_errf(e, "zfake: %s: not a snapshot's name", to);
		return (-1);
	}
	(void) snprintf(tofs, sizeof (tofs), "%.*s", (int)(at - to), to);
	(void) snprintf(sfs, sizeof (sfs), "%.*s",
	    (int)(strchr(snap, '@') - snap), snap);
	if (fp != NULL) {
		(void) snprintf(ffs, sizeof (ffs), "%.*s",
		    (int)(strchr(from, '@') - from), from);
		/* an earlier one of its own, or what it is a clone of */
		if (!((strcmp(ffs, sfs) == 0 && fp->zs_txg < sp->zs_txg) ||
		    strcmp(zf_find(sfs)->zd_origin, from) == 0)) {
			sh_errf(e, "zfake: %s: not an earlier snapshot of %s",
			    from, snap);
			return (-1);
		}
	}
	if (fp == NULL || origin != NULL) {
		if (origin != NULL && (op = zf_snap(origin)) == NULL) {
			sh_errf(e, "zfake: %s: no such snapshot", origin);
			return (-1);
		}
		if (zf_find(tofs) != NULL) {
			sh_errf(e, "zfake: %s: exists", tofs);
			return (-1);
		}
		if (!zf_parent(tofs, parent) || zf_find(parent) == NULL) {
			sh_errf(e, "zfake: %s: no parent", tofs);
			return (-1);
		}
		if (origin != NULL && (fp == NULL ||
		    zf_snap(origin)->zs_guid != fp->zs_guid)) {
			sh_errf(e, "zfake: %s: not %s's counterpart", origin,
			    from != NULL ? from : snap);
			return (-1);
		}
	} else {
		if (zf_find(tofs) == NULL) {
			sh_errf(e, "zfake: %s: no such filesystem", tofs);
			return (-1);
		}
		if ((np = zf_newest(tofs)) == NULL ||
		    np->zs_guid != fp->zs_guid) {
			sh_errf(e, "zfake: %s: newest snapshot is not %s's "
			    "counterpart", tofs, from);
			return (-1);
		}
	}
	if (zf_nsnaps == ZF_SNAPS) {
		sh_errf(e, "zfake: too many snapshots");
		return (-1);
	}
	if (zf_find(tofs) == NULL) {
		zf_make(tofs);
		if (origin != NULL)
			(void) snprintf(zf_find(tofs)->zd_origin, ZF_NAME,
			    "%s", origin);
	}
	np = &zf_snaps[zf_nsnaps++];
	(void) memset(np, 0, sizeof (*np));
	(void) snprintf(np->zs_name, ZF_NAME, "%s", to);
	np->zs_txg = ++zf_txg;
	np->zs_guid = sp->zs_guid;
	return (0);
}

void
zfake_hold(const char *snap)
{
	struct zf_snap *s;

	if ((s = zf_snap(snap)) == NULL)
		zf_die("no such snapshot", snap);
	s->zs_holds++;
}

void
zfake_write(const char *ds, unsigned long bytes)
{
	struct zf_ds *d;

	if ((d = zf_find(ds)) == NULL)
		zf_die("no such filesystem", ds);
	if (d->zd_nw == ZF_WRITES)
		zf_die("too many writes", ds);
	d->zd_wtxg[d->zd_nw] = ++zf_txg;
	d->zd_wbytes[d->zd_nw++] = bytes;
}

/* Whether NAME is FS, a snapshot of it, or below it. */
static int
zf_under(const char *name, const char *fs)
{
	size_t n = strlen(fs);

	return (strncmp(name, fs, n) == 0 && (name[n] == '\0' ||
	    name[n] == '/' || name[n] == '@'));
}

/*
 * DS, a clone of O@S, promoted: O's snapshots up to S by transaction
 * become DS's, clones of them following them; DS takes O's origin and
 * O becomes a clone of DS@S (dsl_dataset_promote).
 */
int
sh_zfs_promote(struct sh_zfs *z, const char *ds, struct sh_err *e)
{
	struct zf_ds *d, *o;
	const struct zf_snap *at;
	char oname[ZF_NAME], oorigin[ZF_NAME], moved[ZF_NAME];
	char renamed[ZF_NAME], self[ZF_NAME];
	size_t olen;
	int i, j;

	(void) z;
	if (zf_fails("promote", e))
		return (-1);
	if ((d = zf_find(ds)) == NULL) {
		sh_errf(e, "zfake: %s: no such filesystem", ds);
		return (-1);
	}
	if (d->zd_origin[0] == '\0') {
		sh_errf(e, "zfake: %s: not a clone", ds);
		return (-1);
	}
	olen = (size_t)(strchr(d->zd_origin, '@') - d->zd_origin);
	(void) snprintf(oname, sizeof (oname), "%.*s", (int)olen,
	    d->zd_origin);
	o = zf_find(oname);
	at = zf_snap(d->zd_origin);
	for (i = 0; i < zf_nsnaps; i++) {
		if (strncmp(zf_snaps[i].zs_name, oname, olen) != 0 ||
		    zf_snaps[i].zs_name[olen] != '@' ||
		    zf_snaps[i].zs_txg > at->zs_txg)
			continue;
		(void) snprintf(renamed, sizeof (renamed), "%s%s", ds,
		    zf_snaps[i].zs_name + olen);
		if (zf_snap(renamed) != NULL) {
			sh_errf(e, "zfake: %s: %s is the origin's too", ds,
			    zf_snaps[i].zs_name + olen + 1);
			return (-1);
		}
	}
	(void) snprintf(oorigin, sizeof (oorigin), "%s", o->zd_origin);
	(void) snprintf(self, sizeof (self), "%s%s", ds,
	    d->zd_origin + olen);
	for (i = 0; i < zf_nsnaps; i++) {
		if (strncmp(zf_snaps[i].zs_name, oname, olen) != 0 ||
		    zf_snaps[i].zs_name[olen] != '@' ||
		    zf_snaps[i].zs_txg > at->zs_txg)
			continue;
		(void) snprintf(moved, sizeof (moved), "%s",
		    zf_snaps[i].zs_name);
		(void) snprintf(zf_snaps[i].zs_name, ZF_NAME, "%s%s", ds,
		    moved + olen);
		for (j = 0; j < zf_n; j++) {
			if (strcmp(zf_ds[j].zd_origin, moved) == 0)
				(void) snprintf(zf_ds[j].zd_origin, ZF_NAME,
				    "%s%s", ds, moved + olen);
		}
	}
	(void) snprintf(d->zd_origin, ZF_NAME, "%s", oorigin);
	(void) snprintf(o->zd_origin, ZF_NAME, "%s", self);
	return (0);
}

/*
 * FROM, everything below it and their snapshots renamed TO, origins
 * naming them following; what was mounted unmounted first and mounted
 * after where its mountpoint now says, as zfs rename's changelist does.
 */
int
sh_zfs_rename(struct sh_zfs *z, const char *from, const char *to,
    struct sh_err *e)
{
	static char was[ZF_DS];
	char parent[ZF_NAME], mp[ZF_VAL], cm[ZF_VAL], buf[ZF_NAME];
	size_t flen = strlen(from);
	int i;

	(void) z;
	if (zf_fails("rename", e))
		return (-1);
	if (zf_find(from) == NULL) {
		sh_errf(e, "zfake: %s: no such filesystem", from);
		return (-1);
	}
	if (zf_find(to) != NULL) {
		sh_errf(e, "zfake: %s: exists", to);
		return (-1);
	}
	if (!zf_parent(to, parent) || zf_find(parent) == NULL) {
		sh_errf(e, "zfake: %s: no parent", to);
		return (-1);
	}
	for (i = 0; i < zf_n; i++) {
		was[i] = 0;
		if (!zf_under(zf_ds[i].zd_name, from))
			continue;
		was[i] = zf_ds[i].zd_at[0] != '\0';
		zf_ds[i].zd_at[0] = '\0';
		(void) snprintf(buf, sizeof (buf), "%s%s", to,
		    zf_ds[i].zd_name + flen);
		(void) snprintf(zf_ds[i].zd_name, ZF_NAME, "%s", buf);
	}
	for (i = 0; i < zf_nsnaps; i++) {
		if (!zf_under(zf_snaps[i].zs_name, from))
			continue;
		(void) snprintf(buf, sizeof (buf), "%s%s", to,
		    zf_snaps[i].zs_name + flen);
		(void) snprintf(zf_snaps[i].zs_name, ZF_NAME, "%s", buf);
	}
	for (i = 0; i < zf_n; i++) {
		if (zf_ds[i].zd_origin[0] == '\0' ||
		    !zf_under(zf_ds[i].zd_origin, from))
			continue;
		(void) snprintf(buf, sizeof (buf), "%s%s", to,
		    zf_ds[i].zd_origin + flen);
		(void) snprintf(zf_ds[i].zd_origin, ZF_NAME, "%s", buf);
	}
	for (i = 0; i < zf_n; i++) {
		if (!was[i])
			continue;
		(void) zf_get(zf_ds[i].zd_name, "mountpoint", mp, sizeof (mp),
		    NULL, NULL);
		(void) zf_get(zf_ds[i].zd_name, "canmount", cm, sizeof (cm),
		    NULL, NULL);
		if (mp[0] == '/' && strcmp(cm, "off") != 0)
			(void) snprintf(zf_ds[i].zd_at, ZF_VAL, "%s", mp);
	}
	return (0);
}

int
sh_zfs_userprops(struct sh_zfs *z, const char *name,
    int (*fn)(void *, const char *, const char *), void *arg,
    struct sh_err *e)
{
	const struct zf_prop *pp;
	struct zf_snap *sp;
	struct zf_ds *d;
	int i, n, r;

	(void) z;
	if (zf_fails("userprops", e))
		return (-1);
	if (strchr(name, '@') != NULL) {
		if ((sp = zf_snap(name)) == NULL) {
			sh_errf(e, "zfake: %s: no such snapshot", name);
			return (-1);
		}
		pp = sp->zs_props;
		n = sp->zs_n;
	} else {
		if ((d = zf_find(name)) == NULL) {
			sh_errf(e, "zfake: %s: no such filesystem", name);
			return (-1);
		}
		pp = d->zd_props;
		n = d->zd_n;
	}
	for (i = 0; i < n; i++) {
		if (strchr(pp[i].zp_name, ':') == NULL)
			continue;
		if ((r = fn(arg, pp[i].zp_name, pp[i].zp_val)) != 0)
			return (r);
	}
	return (0);
}

int
sh_zfs_open(struct sh_zfs **zp, struct sh_err *e)
{
	if (zf_fails("open", e)) {
		*zp = NULL;
		return (-1);
	}
	zf_handle.zz_open = 1;
	*zp = &zf_handle;
	return (0);
}

void
sh_zfs_close(struct sh_zfs *z)
{
	z->zz_open = 0;
}

int
sh_zfs_local(struct sh_zfs *z, const char *prop,
    int (*fn)(void *, const char *, const char *), void *arg,
    struct sh_err *e)
{
	const char *v;
	int i, r;

	(void) z;
	if (zf_fails("local", e))
		return (-1);
	for (i = 0; i < zf_n; i++) {
		v = zf_prop(zf_ds[i].zd_props, zf_ds[i].zd_n, prop);
		if (v != NULL && (r = fn(arg, zf_ds[i].zd_name, v)) != 0)
			return (r);
	}
	return (0);
}

/*
 * PROP of DS as ZFS would report it: local; else inherited from the
 * nearest ancestor that sets it, a mountpoint gaining the path below
 * that ancestor; else the default.
 */
static int
zf_get(const char *ds, const char *prop, char *val, size_t len,
    enum sh_zsrc *src, struct sh_err *e)
{
	const struct zf_ds *d, *a;
	const struct zf_snap *sn;
	char up[ZF_NAME], cur[ZF_NAME];
	const char *v;
	enum sh_zsrc s;
	unsigned long w;
	int i, user, inherit;

	if ((d = zf_find(ds)) == NULL)
		return (1);
	if (strcmp(prop, "type") == 0 || strcmp(prop, "name") == 0) {
		(void) snprintf(val, len, "%s", prop[0] == 't' ?
		    "filesystem" : ds);
		if (src != NULL)
			*src = SH_ZSRC_NONE;
		return (0);
	}
	if (strcmp(prop, "mounted") == 0) {
		(void) snprintf(val, len, "%s", d->zd_at[0] != '\0' ? "yes" :
		    "no");
		if (src != NULL)
			*src = SH_ZSRC_NONE;
		return (0);
	}
	if (strcmp(prop, "origin") == 0) {
		(void) snprintf(val, len, "%s", d->zd_origin[0] != '\0' ?
		    d->zd_origin : "-");
		if (src != NULL)
			*src = SH_ZSRC_NONE;
		return (0);
	}
	/* written@SNAP: zfake_write's bytes since SNAP's transaction */
	if (strncmp(prop, "written@", 8) == 0) {
		(void) snprintf(cur, sizeof (cur), "%s@%s", ds, prop + 8);
		if ((sn = zf_snap(cur)) == NULL) {
			sh_errf(e, "zfake: %s: no such snapshot", cur);
			return (-1);
		}
		for (i = 0, w = 0; i < d->zd_nw; i++) {
			if (d->zd_wtxg[i] > sn->zs_txg)
				w += d->zd_wbytes[i];
		}
		(void) snprintf(val, len, "%lu", w);
		if (src != NULL)
			*src = SH_ZSRC_NONE;
		return (0);
	}
	user = strchr(prop, ':') != NULL;
	for (i = 0; !user && zf_native[i].n_name != NULL; i++) {
		if (strcmp(zf_native[i].n_name, prop) == 0)
			break;
	}
	if (!user && zf_native[i].n_name == NULL) {
		sh_errf(e, "zfake: %s: no property %s", ds, prop);
		return (-1);
	}
	inherit = user || zf_native[i].n_inherit;
	if ((v = zf_prop(d->zd_props, d->zd_n, prop)) != NULL) {
		(void) snprintf(val, len, "%s", v);
		s = SH_ZSRC_LOCAL;
		goto out;
	}
	(void) snprintf(cur, sizeof (cur), "%s", ds);
	while (inherit && zf_parent(cur, up)) {
		(void) snprintf(cur, sizeof (cur), "%s", up);
		a = zf_find(cur);
		if ((v = zf_prop(a->zd_props, a->zd_n, prop)) == NULL)
			continue;
		if (strcmp(prop, "mountpoint") == 0 &&
		    strcmp(v, "none") != 0 && strcmp(v, "legacy") != 0)
			(void) snprintf(val, len, "%s%s",
			    strcmp(v, "/") == 0 ? "" : v, ds + strlen(cur));
		else
			(void) snprintf(val, len, "%s", v);
		s = SH_ZSRC_INHERITED;
		goto out;
	}
	if (user) {
		(void) snprintf(val, len, "-");
		s = SH_ZSRC_NONE;
	} else if (strcmp(prop, "mountpoint") == 0) {
		(void) snprintf(val, len, "/%s", ds);
		s = SH_ZSRC_DEFAULT;
	} else {
		(void) snprintf(val, len, "%s", zf_native[i].n_default);
		s = SH_ZSRC_DEFAULT;
	}
out:
	if (src != NULL)
		*src = s;
	return (0);
}

/*
 * PROP of the snapshot NAME: what identifies it, and user properties,
 * its own or else its filesystem's, inherited.
 */
static int
zf_get_snap(const char *name, const char *prop, char *val, size_t len,
    enum sh_zsrc *src, struct sh_err *e)
{
	const struct zf_snap *s;
	char ds[ZF_NAME];
	const char *v;
	enum sh_zsrc fs;
	int r;

	if ((s = zf_snap(name)) == NULL)
		return (1);
	if (src != NULL)
		*src = SH_ZSRC_NONE;
	if (strcmp(prop, "type") == 0) {
		(void) snprintf(val, len, "snapshot");
		return (0);
	}
	if (strcmp(prop, "name") == 0) {
		(void) snprintf(val, len, "%s", name);
		return (0);
	}
	if (strcmp(prop, "createtxg") == 0 || strcmp(prop, "guid") == 0) {
		(void) snprintf(val, len, "%lu", prop[0] == 'c' ? s->zs_txg :
		    s->zs_guid);
		return (0);
	}
	if (strcmp(prop, "userrefs") == 0) {
		(void) snprintf(val, len, "%d", s->zs_holds);
		return (0);
	}
	if (strchr(prop, ':') == NULL) {
		sh_errf(e, "zfake: %s: no property %s", name, prop);
		return (-1);
	}
	if ((v = zf_prop(s->zs_props, s->zs_n, prop)) != NULL) {
		(void) snprintf(val, len, "%s", v);
		if (src != NULL)
			*src = SH_ZSRC_LOCAL;
		return (0);
	}
	(void) snprintf(ds, sizeof (ds), "%.*s",
	    (int)(strchr(name, '@') - name), name);
	if ((r = zf_get(ds, prop, val, len, &fs, e)) != 0)
		return (r);
	if (src != NULL)
		*src = fs == SH_ZSRC_NONE ? SH_ZSRC_NONE : SH_ZSRC_INHERITED;
	return (0);
}

int
sh_zfs_get(struct sh_zfs *z, const char *ds, const char *prop, char *val,
    size_t len, enum sh_zsrc *src, struct sh_err *e)
{
	(void) z;
	if (zf_fails("get", e))
		return (-1);
	if (strchr(ds, '@') != NULL)
		return (zf_get_snap(ds, prop, val, len, src, e));
	return (zf_get(ds, prop, val, len, src, e));
}

int
sh_zfs_children(struct sh_zfs *z, const char *ds,
    int (*fn)(void *, const char *), void *arg, struct sh_err *e)
{
	char parent[ZF_NAME];
	int i, r;

	(void) z;
	if (zf_fails("children", e))
		return (-1);
	if (zf_find(ds) == NULL) {
		sh_errf(e, "zfake: %s: no such filesystem", ds);
		return (-1);
	}
	for (i = 0; i < zf_n; i++) {
		if (zf_parent(zf_ds[i].zd_name, parent) &&
		    strcmp(parent, ds) == 0 &&
		    (r = fn(arg, zf_ds[i].zd_name)) != 0)
			return (r);
	}
	return (0);
}

int
sh_zfs_create(struct sh_zfs *z, const char *ds,
    const struct sh_zprop *props, size_t nprops, struct sh_err *e)
{
	char parent[ZF_NAME];
	size_t i;

	(void) z;
	if (zf_fails("create", e))
		return (-1);
	if (zf_find(ds) != NULL) {
		sh_errf(e, "zfake: %s: already there", ds);
		return (-1);
	}
	if (!zf_parent(ds, parent) || zf_find(parent) == NULL) {
		sh_errf(e, "zfake: %s: no parent", ds);
		return (-1);
	}
	zf_make(ds);
	for (i = 0; i < nprops; i++)
		zfake_set(ds, props[i].zp_name, props[i].zp_value);
	return (0);
}

int
sh_zfs_set(struct sh_zfs *z, const char *ds, const char *prop,
    const char *value, struct sh_err *e)
{
	struct zf_snap *s;

	(void) z;
	if (zf_fails("set", e))
		return (-1);
	if (strchr(ds, '@') != NULL) {
		if ((s = zf_snap(ds)) == NULL) {
			sh_errf(e, "zfake: %s: no such snapshot", ds);
			return (-1);
		}
		if (strchr(prop, ':') == NULL) {
			sh_errf(e, "zfake: %s: this property can not be "
			    "modified for snapshots", ds);
			return (-1);
		}
		zf_put(s->zs_props, &s->zs_n, prop, value);
		return (0);
	}
	if (zf_find(ds) == NULL) {
		sh_errf(e, "zfake: %s: no such filesystem", ds);
		return (-1);
	}
	if (strcmp(prop, "mountpoint") == 0)
		zf_remount(ds, value);
	else
		zfake_set(ds, prop, value);
	return (0);
}

int
sh_zpool_get(struct sh_zfs *z, const char *pool, const char *prop,
    char *val, size_t len, struct sh_err *e)
{
	const struct zf_ds *d;
	const char *v;

	(void) z;
	if (zf_fails("pool_get", e))
		return (-1);
	if ((d = zf_find(pool)) == NULL || strchr(pool, '/') != NULL ||
	    (v = zf_prop(d->zd_pool, d->zd_npool, prop)) == NULL) {
		sh_errf(e, "zfake: pool %s: no property %s", pool, prop);
		return (-1);
	}
	(void) snprintf(val, len, "%s", v);
	return (0);
}

int
sh_zpool_set(struct sh_zfs *z, const char *pool, const char *prop,
    const char *value, struct sh_err *e)
{
	struct zf_ds *d;

	(void) z;
	if (zf_fails("pool_set", e))
		return (-1);
	if ((d = zf_find(pool)) == NULL || strchr(pool, '/') != NULL) {
		sh_errf(e, "zfake: no pool %s", pool);
		return (-1);
	}
	zf_put(d->zd_pool, &d->zd_npool, prop, value);
	return (0);
}
