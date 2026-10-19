/*
 * The fake ZFS of the tests (tests/zfake.h). Small and fixed in size:
 * a test makes a few pools and a few dozen filesystems. Filesystems are
 * walked in the order they were made, which is depth first as long as
 * a parent is made before its children, as ZFS requires.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/err.h"
#include "core/zfs.h"
#include "zfake.h"

#define	ZF_DS		64
#define	ZF_SNAPS	128
#define	ZF_PROPS	16
#define	ZF_WORDS	8
#define	ZF_NAME		256

struct zf_prop {
	char	zp_name[ZF_NAME];
	char	zp_val[ZF_NAME];
};

/* A filesystem or a snapshot: its properties and what it holds. */
struct zf_obj {
	char		zo_name[ZF_NAME];
	struct zf_prop	zo_props[ZF_PROPS];
	int		zo_nprops;
	char		zo_words[ZF_WORDS][32];
	int		zo_nwords;
	char		zo_at[ZF_NAME];	/* a filesystem: mounted there, or "" */
};

struct sh_zfs {
	int	zz_unused;
};

static struct zf_obj zf_ds[ZF_DS];
static int zf_n;
static struct zf_obj zf_snaps[ZF_SNAPS];	/* oldest first */
static int zf_nsnaps;
static char zf_failing[32];
static int zf_failin;		/* calls of zf_failing still to pass */
static struct sh_zfs zf_handle;

static void
zf_die(const char *what, const char *arg)
{
	(void) printf("zfake: %s: %s\n", what, arg);
	exit(1);
}

static struct zf_obj *
zf_find(struct zf_obj *set, int n, const char *name)
{
	int i;

	for (i = 0; i < n; i++) {
		if (strcmp(set[i].zo_name, name) == 0)
			return (&set[i]);
	}
	return (NULL);
}

static struct zf_obj *
zf_fs(const char *ds)
{
	return (zf_find(zf_ds, zf_n, ds));
}

static struct zf_obj *
zf_snap(const char *name)
{
	return (zf_find(zf_snaps, zf_nsnaps, name));
}

static const char *
zf_prop(const struct zf_obj *o, const char *name)
{
	int i;

	for (i = 0; i < o->zo_nprops; i++) {
		if (strcmp(o->zo_props[i].zp_name, name) == 0)
			return (o->zo_props[i].zp_val);
	}
	return (NULL);
}

static void
zf_put(struct zf_obj *o, const char *name, const char *val)
{
	if (o->zo_nprops == ZF_PROPS)
		zf_die("too many properties", o->zo_name);
	(void) snprintf(o->zo_props[o->zo_nprops].zp_name, ZF_NAME, "%s",
	    name);
	(void) snprintf(o->zo_props[o->zo_nprops++].zp_val, ZF_NAME, "%s",
	    val);
}

/* DS's parent into BUF: 1; 0 for a pool's root. */
static int
zf_parent(const char *ds, char *buf)
{
	const char *slash = strrchr(ds, '/');

	if (slash == NULL)
		return (0);
	(void) snprintf(buf, ZF_NAME, "%.*s", (int)(slash - ds), ds);
	return (1);
}

/* Whether this call of WHAT is the one set to fail; it fails once. */
static int
zf_fails(const char *what, struct sh_err *e)
{
	if (strcmp(zf_failing, what) != 0 || --zf_failin > 0)
		return (0);
	zf_failing[0] = '\0';
	sh_errf(e, "zfake: %s failed as asked", what);
	return (1);
}

void
zfake_reset(void)
{
	zf_n = 0;
	zf_nsnaps = 0;
	zf_failing[0] = '\0';
}

void
zfake_pool(const char *pool)
{
	if (zf_n == ZF_DS || zf_fs(pool) != NULL)
		zf_die("cannot make pool", pool);
	(void) memset(&zf_ds[zf_n], 0, sizeof (zf_ds[0]));
	(void) snprintf(zf_ds[zf_n++].zo_name, ZF_NAME, "%s", pool);
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

/*
 * DS's mountpoint into BUF, as ZFS has it: set on DS, or inherited from
 * the nearest ancestor that sets one, the path below that ancestor
 * added unless it is none or legacy; or the default, "/" and DS.
 */
static void
zf_mountpoint(const char *ds, char *buf)
{
	char cur[ZF_NAME], up[ZF_NAME];
	const char *v;

	(void) snprintf(cur, sizeof (cur), "%s", ds);
	for (;;) {
		if ((v = zf_prop(zf_fs(cur), "mountpoint")) != NULL)
			break;
		if (!zf_parent(cur, up)) {
			(void) snprintf(buf, ZF_NAME, "/%s", ds);
			return;
		}
		(void) snprintf(cur, sizeof (cur), "%s", up);
	}
	if (v[0] != '/')
		(void) snprintf(buf, ZF_NAME, "%s", v);
	else
		(void) snprintf(buf, ZF_NAME, "%s%s", strcmp(v, "/") == 0 ?
		    "" : v, ds + strlen(cur));
}

int
zfake_put(const char *ds, const char *word)
{
	struct zf_obj *d = zf_fs(ds);
	int i;

	if (d == NULL || d->zo_at[0] == '\0')
		return (-1);
	for (i = 0; i < d->zo_nwords; i++) {
		if (strcmp(d->zo_words[i], word) == 0)
			return (0);
	}
	if (d->zo_nwords == ZF_WORDS)
		zf_die("too many words", ds);
	(void) snprintf(d->zo_words[d->zo_nwords++], 32, "%s", word);
	return (0);
}

int
zfake_has(const char *ds, const char *word)
{
	struct zf_obj *d = zf_fs(ds);
	int i;

	if (d == NULL || d->zo_at[0] == '\0')
		return (-1);
	for (i = 0; i < d->zo_nwords; i++) {
		if (strcmp(d->zo_words[i], word) == 0)
			return (1);
	}
	return (0);
}

int
sh_zfs_open(struct sh_zfs **zp, struct sh_err *e)
{
	if (zf_fails("open", e))
		return (-1);
	*zp = &zf_handle;
	return (0);
}

void
sh_zfs_close(struct sh_zfs *z)
{
	(void) z;
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
		if ((v = zf_prop(&zf_ds[i], prop)) != NULL &&
		    (r = fn(arg, zf_ds[i].zo_name, v)) != 0)
			return (r);
	}
	return (0);
}

int
sh_zfs_user(struct sh_zfs *z, const char *name, const char *prop,
    char *val, size_t len, struct sh_err *e)
{
	const struct zf_obj *o;
	char cur[ZF_NAME], up[ZF_NAME];
	const char *v;

	(void) z;
	if (zf_fails("user", e))
		return (-1);
	if (strchr(name, '@') != NULL ? (o = zf_snap(name)) == NULL :
	    (o = zf_fs(name)) == NULL) {
		sh_errf(e, "zfake: %s: no such %s", name,
		    strchr(name, '@') != NULL ? "snapshot" : "filesystem");
		return (-1);
	}
	/* its own; else its filesystem's, and up from there */
	if ((v = zf_prop(o, prop)) == NULL) {
		(void) snprintf(cur, sizeof (cur), "%.*s",
		    (int)strcspn(name, "@"), name);
		while ((v = zf_prop(zf_fs(cur), prop)) == NULL) {
			if (!zf_parent(cur, up))
				return (1);
			(void) snprintf(cur, sizeof (cur), "%s", up);
		}
	}
	(void) snprintf(val, len, "%s", v);
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
	if (zf_fs(ds) == NULL) {
		sh_errf(e, "zfake: %s: no such filesystem", ds);
		return (-1);
	}
	for (i = 0; i < zf_n; i++) {
		if (zf_parent(zf_ds[i].zo_name, parent) &&
		    strcmp(parent, ds) == 0 &&
		    (r = fn(arg, zf_ds[i].zo_name)) != 0)
			return (r);
	}
	return (0);
}

/* As zfs_create looks: the parent first, then the name. */
int
sh_zfs_create(struct sh_zfs *z, const char *ds,
    const struct sh_zprop *props, size_t nprops, struct sh_err *e)
{
	char parent[ZF_NAME];
	struct zf_obj *d;
	size_t i;

	(void) z;
	if (zf_fails("create", e))
		return (-1);
	if (!zf_parent(ds, parent) || zf_fs(parent) == NULL) {
		sh_errf(e, "zfake: %s: no parent", ds);
		return (-1);
	}
	if (zf_fs(ds) != NULL) {
		sh_errf(e, "zfake: %s: exists", ds);
		return (-1);
	}
	if (zf_n == ZF_DS)
		zf_die("too many filesystems", ds);
	d = &zf_ds[zf_n++];
	(void) memset(d, 0, sizeof (*d));
	(void) snprintf(d->zo_name, ZF_NAME, "%s", ds);
	for (i = 0; i < nprops; i++)
		zf_put(d, props[i].zp_name, props[i].zp_value);
	return (0);
}

/*
 * Why NAME cannot be taken, or NULL: its shape and a name twice first,
 * before anything is asked, then what ZFS looks at.
 */
static const char *
zf_why(const char *const *names, size_t i, int asked)
{
	char ds[ZF_NAME];
	const char *at = strchr(names[i], '@');
	size_t j;

	if (!asked) {
		if (at == NULL || at == names[i] || at[1] == '\0' ||
		    strchr(at + 1, '@') != NULL || strchr(at + 1, '/') != NULL)
			return ("not a snapshot's name");
		for (j = 0; j < i; j++) {
			if (strcmp(names[j], names[i]) == 0)
				return ("named twice");
		}
		return (NULL);
	}
	(void) snprintf(ds, sizeof (ds), "%.*s", (int)(at - names[i]),
	    names[i]);
	if (zf_fs(ds) == NULL)
		return ("no such filesystem");
	if (zf_snap(names[i]) != NULL)
		return ("taken");
	return (NULL);
}

int
sh_zfs_snapshot(struct sh_zfs *z, const char *const *names, size_t n,
    const struct sh_zprop *props, size_t nprops, struct sh_err *e)
{
	struct zf_obj *s;
	char ds[ZF_NAME];
	const char *why;
	size_t i, k;
	int asked;

	(void) z;
	if (zf_fails("snapshot", e))
		return (-1);
	for (asked = 0; asked < 2; asked++) {
		for (i = 0; i < n; i++) {
			if ((why = zf_why(names, i, asked)) != NULL) {
				sh_errf(e, "zfake: %s: %s", names[i], why);
				return (-1);
			}
		}
	}
	if (zf_nsnaps + (int)n > ZF_SNAPS)
		zf_die("too many snapshots", names[0]);
	for (i = 0; i < n; i++) {
		(void) snprintf(ds, sizeof (ds), "%.*s",
		    (int)strcspn(names[i], "@"), names[i]);
		s = &zf_snaps[zf_nsnaps++];
		*s = *zf_fs(ds);
		(void) snprintf(s->zo_name, ZF_NAME, "%s", names[i]);
		s->zo_nprops = 0;
		s->zo_at[0] = '\0';
		for (k = 0; k < nprops; k++)
			zf_put(s, props[k].zp_name, props[k].zp_value);
	}
	return (0);
}

/* Whether the snapshot S is one of DS's. */
static int
zf_of(const struct zf_obj *s, const char *ds)
{
	size_t len = strlen(ds);

	return (strncmp(s->zo_name, ds, len) == 0 && s->zo_name[len] == '@');
}

int
sh_zfs_snapshots(struct sh_zfs *z, const char *ds,
    int (*fn)(void *, const char *), void *arg, struct sh_err *e)
{
	int i, r;

	(void) z;
	if (zf_fails("snapshots", e))
		return (-1);
	if (zf_fs(ds) == NULL) {
		sh_errf(e, "zfake: %s: no such filesystem", ds);
		return (-1);
	}
	for (i = 0; i < zf_nsnaps; i++) {
		if (zf_of(&zf_snaps[i], ds) &&
		    (r = fn(arg, strchr(zf_snaps[i].zo_name, '@') + 1)) != 0)
			return (r);
	}
	return (0);
}

int
sh_zfs_rollback(struct sh_zfs *z, const char *ds, const char *snap,
    struct sh_err *e)
{
	struct zf_obj *d;
	int i;

	(void) z;
	if (zf_fails("rollback", e))
		return (-1);
	if ((d = zf_fs(ds)) == NULL) {
		sh_errf(e, "zfake: %s: no such filesystem", ds);
		return (-1);
	}
	for (i = zf_nsnaps - 1; i >= 0 && !zf_of(&zf_snaps[i], ds); i--)
		;
	if (i < 0 || strcmp(strchr(zf_snaps[i].zo_name, '@') + 1, snap) != 0) {
		sh_errf(e, "zfake: %s@%s: not the newest snapshot", ds, snap);
		return (-1);
	}
	(void) memcpy(d->zo_words, zf_snaps[i].zo_words, sizeof (d->zo_words));
	d->zo_nwords = zf_snaps[i].zo_nwords;
	return (0);
}

/* What dsl_destroy_head_check_impl looks at: the mount, snapshots, children. */
int
sh_zfs_destroy(struct sh_zfs *z, const char *name, struct sh_err *e)
{
	struct zf_obj *o;
	char parent[ZF_NAME];
	int i, snap = strchr(name, '@') != NULL;

	(void) z;
	if (zf_fails("destroy", e))
		return (-1);
	if ((o = snap ? zf_snap(name) : zf_fs(name)) == NULL) {
		sh_errf(e, "zfake: %s: no such dataset", name);
		return (-1);
	}
	if (snap) {
		i = (int)(o - zf_snaps);
		(void) memmove(o, o + 1, (size_t)(zf_nsnaps - i - 1) *
		    sizeof (*o));
		zf_nsnaps--;
		return (0);
	}
	if (o->zo_at[0] != '\0') {
		sh_errf(e, "zfake: %s: mounted", name);
		return (-1);
	}
	for (i = 0; i < zf_nsnaps; i++) {
		if (zf_of(&zf_snaps[i], name)) {
			sh_errf(e, "zfake: %s: has snapshots", name);
			return (-1);
		}
	}
	for (i = 0; i < zf_n; i++) {
		if (zf_parent(zf_ds[i].zo_name, parent) &&
		    strcmp(parent, name) == 0) {
			sh_errf(e, "zfake: %s: has children", name);
			return (-1);
		}
	}
	i = (int)(o - zf_ds);
	(void) memmove(o, o + 1, (size_t)(zf_n - i - 1) * sizeof (*o));
	zf_n--;
	return (0);
}

int
sh_zfs_mount(struct sh_zfs *z, const char *ds, struct sh_err *e)
{
	struct zf_obj *d;
	char mp[ZF_NAME];
	const char *cm;

	(void) z;
	if (zf_fails("mount", e))
		return (-1);
	if ((d = zf_fs(ds)) == NULL) {
		sh_errf(e, "zfake: %s: no such filesystem", ds);
		return (-1);
	}
	zf_mountpoint(ds, mp);
	cm = zf_prop(d, "canmount");
	if (mp[0] != '/') {
		sh_errf(e, "zfake: %s: no mountpoint", ds);
		return (-1);
	}
	if (cm != NULL && strcmp(cm, "off") == 0) {
		sh_errf(e, "zfake: %s: canmount is off", ds);
		return (-1);
	}
	if (d->zo_at[0] != '\0' && strcmp(d->zo_at, mp) != 0) {
		sh_errf(e, "zfake: %s: mounted at %s, not its mountpoint", ds,
		    d->zo_at);
		return (-1);
	}
	(void) snprintf(d->zo_at, ZF_NAME, "%s", mp);
	return (0);
}

/* A filesystem with another mounted inside it is busy, as umount says. */
int
sh_zfs_unmount(struct sh_zfs *z, const char *ds, struct sh_err *e)
{
	struct zf_obj *d;
	size_t len;
	int i;

	(void) z;
	if (zf_fails("unmount", e))
		return (-1);
	if ((d = zf_fs(ds)) == NULL) {
		sh_errf(e, "zfake: %s: no such filesystem", ds);
		return (-1);
	}
	len = strlen(d->zo_at);
	for (i = 0; len > 0 && i < zf_n; i++) {
		if (&zf_ds[i] != d &&
		    strncmp(zf_ds[i].zo_at, d->zo_at, len) == 0 &&
		    (len == 1 || zf_ds[i].zo_at[len] == '/')) {
			sh_errf(e, "zfake: %s: busy", ds);
			return (-1);
		}
	}
	d->zo_at[0] = '\0';
	return (0);
}

int
sh_zfs_mounted(struct sh_zfs *z, const char *ds, char *path, size_t len,
    struct sh_err *e)
{
	struct zf_obj *d;

	(void) z;
	if (zf_fails("mounted", e))
		return (-1);
	if ((d = zf_fs(ds)) == NULL) {
		sh_errf(e, "zfake: %s: no such filesystem", ds);
		return (-1);
	}
	if (d->zo_at[0] == '\0')
		return (0);
	(void) snprintf(path, len, "%s", d->zo_at);
	return (1);
}
