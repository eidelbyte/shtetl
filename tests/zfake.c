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
#define	ZF_PROPS	32
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
	struct zf_prop	zd_pool[ZF_PROPS];	/* a pool's root: the pool's */
	int		zd_npool;
};

struct sh_zfs {
	int	zz_open;
};

static struct zf_ds zf_ds[ZF_DS];
static int zf_n;
static char zf_snaps[ZF_DS][ZF_NAME];
static int zf_nsnaps;
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
	zf_nsnaps = 0;
	zf_failing[0] = '\0';
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

int
zfake_snapshot(const char *name)
{
	int i;

	for (i = 0; i < zf_nsnaps; i++) {
		if (strcmp(zf_snaps[i], name) == 0)
			return (1);
	}
	return (0);
}

const char *
zfake_last_snapshot(void)
{
	return (zf_nsnaps > 0 ? zf_snaps[zf_nsnaps - 1] : NULL);
}

int
sh_zfs_snapshot(struct sh_zfs *z, const char *name,
    const struct sh_zprop *props, size_t nprops, struct sh_err *e)
{
	char ds[ZF_NAME];
	const char *at;

	(void) z;
	(void) props;
	(void) nprops;
	if (zf_fails("snapshot", e))
		return (-1);
	if ((at = strchr(name, '@')) == NULL || zf_nsnaps == ZF_DS) {
		sh_errf(e, "zfake: %s: not a snapshot's name", name);
		return (-1);
	}
	(void) snprintf(ds, sizeof (ds), "%.*s", (int)(at - name), name);
	if (zf_find(ds) == NULL || zfake_snapshot(name)) {
		sh_errf(e, "zfake: %s: no such filesystem, or taken", name);
		return (-1);
	}
	(void) snprintf(zf_snaps[zf_nsnaps++], ZF_NAME, "%s", name);
	return (0);
}

int
sh_zfs_mount(struct sh_zfs *z, const char *ds, struct sh_err *e)
{
	char mp[ZF_VAL];

	if (zf_fails("mount", e))
		return (-1);
	if (sh_zfs_get(z, ds, "mountpoint", mp, sizeof (mp), NULL, e) != 0)
		return (-1);
	if (mp[0] != '/') {
		sh_errf(e, "zfake: %s: no mountpoint", ds);
		return (-1);
	}
	zfake_set(ds, "mounted", "yes");
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
int
sh_zfs_get(struct sh_zfs *z, const char *ds, const char *prop, char *val,
    size_t len, enum sh_zsrc *src, struct sh_err *e)
{
	const struct zf_ds *d, *a;
	char up[ZF_NAME], cur[ZF_NAME];
	const char *v;
	enum sh_zsrc s;
	int i, user, inherit;

	(void) z;
	if (zf_fails("get", e))
		return (-1);
	if ((d = zf_find(ds)) == NULL)
		return (1);
	if (strcmp(prop, "type") == 0 || strcmp(prop, "name") == 0) {
		(void) snprintf(val, len, "%s", prop[0] == 't' ?
		    "filesystem" : ds);
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
	(void) z;
	if (zf_fails("set", e))
		return (-1);
	if (zf_find(ds) == NULL) {
		sh_errf(e, "zfake: %s: no such filesystem", ds);
		return (-1);
	}
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
