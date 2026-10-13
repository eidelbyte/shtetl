/*
 * What the build asks of ZFS, over the fake (src/core/zfs.h,
 * tests/zfake.c): tests/MATRIX.md family Z, Z1 - Z10.
 */

#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "core/zfs.h"
#include "zfake.h"

static struct cap c;
static struct sh_zfs *z;

static int
is(const char *got, const char *want)
{
	if (strcmp(got, want) != 0) {
		printf("got \"%s\", wanted \"%s\"\n", got, want);
		return (0);
	}
	return (1);
}

/* PROP of DS, with where it comes from in *SRC. */
static const char *
get(const char *ds, const char *prop, enum sh_zsrc *src)
{
	static char val[256];

	cap_reset(&c);
	if (sh_zfs_get(z, ds, prop, val, sizeof (val), src, &c.c_err) != 0)
		return ("(refused)");
	return (val);
}

/* A lake with a foundation of three filesystems. */
static void
world(void)
{
	zfake_reset();
	zfake_pool("tank");
	zfake_fs("tank/f");
	zfake_fs("tank/f/usr");
	zfake_fs("tank/f/var");
	zfake_set("tank/f", "shtetl:kind", "foundation");
}

static int
snap(const char *const *names, size_t n, const struct sh_zprop *p,
    size_t np)
{
	cap_reset(&c);
	return (sh_zfs_snapshot(z, names, n, p, np, &c.c_err));
}

/* What sh_zfs_snapshots walked, space-separated. */
static char walked[512];
static int stop_at;

static int
walk(void *arg, const char *s)
{
	size_t n = strlen(walked);

	(void) arg;
	(void) snprintf(walked + n, sizeof (walked) - n, "%s%s",
	    n > 0 ? " " : "", s);
	return (stop_at > 0 && --stop_at == 0 ? 7 : 0);
}

static const char *
snaps(const char *ds, int *rc)
{
	walked[0] = '\0';
	cap_reset(&c);
	*rc = sh_zfs_snapshots(z, ds, walk, NULL, &c.c_err);
	return (walked);
}

/* Z1, Z2, Z5: taking snapshots, and listing them. */
static void
test_snapshots(void)
{
	static const char *const v1[] = { "tank/f@v1", "tank/f/usr@v1",
	    "tank/f/var@v1" };
	static const char *const v2[] = { "tank/f@v2", "tank/f/usr@v2",
	    "tank/f/var@v2" };
	static const struct sh_zprop p[] = {
		{ "shtetl:kind", "version" },
		{ "shtetl:commit", "the first" }
	};
	const char *bad[3];
	char t1[64];
	enum sh_zsrc src;
	int rc;

	world();
	CHECK(snap(v1, 3, p, 2) == 0);
	(void) snprintf(t1, sizeof (t1), "%s", get("tank/f@v1", "createtxg",
	    NULL));
	CHECK(is(get("tank/f/usr@v1", "createtxg", NULL), t1));
	CHECK(is(get("tank/f/var@v1", "createtxg", NULL), t1));
	CHECK(is(get("tank/f/var@v1", "shtetl:kind", &src), "version"));
	CHECK(src == SH_ZSRC_LOCAL);
	CHECK(is(get("tank/f/usr@v1", "shtetl:commit", NULL), "the first"));
	CHECK(snap(v2, 3, p, 1) == 0);
	CHECK(strcmp(get("tank/f@v2", "createtxg", NULL), t1) != 0);
	CHECK(is(get("tank/f@v2", "shtetl:commit", &src), "-"));
	CHECK(src == SH_ZSRC_NONE);
	/* Z5: oldest first, its own only, the stop returned */
	CHECK(is(snaps("tank/f", &rc), "v1 v2") && rc == 0);
	CHECK(is(snaps("tank/f/usr", &rc), "v1 v2") && rc == 0);
	stop_at = 1;
	CHECK(is(snaps("tank/f", &rc), "v1") && rc == 7);
	CHECK(is(snaps("tank", &rc), "") && rc == 0);
	CHECK(is(snaps("tank/nope", &rc), "") && rc == -1);

	/* Z2: all or none */
	bad[0] = "tank/f@v3";
	bad[1] = "tank/f/usr@v2";		/* taken */
	CHECK(snap(bad, 2, NULL, 0) == -1);
	CHECK(cap_has(&c, "zfake: tank/f/usr@v2: taken"));
	CHECK(!zfake_snapshot("tank/f@v3"));
	bad[1] = "tank/g@v3";			/* no filesystem */
	CHECK(snap(bad, 2, NULL, 0) == -1);
	CHECK(cap_has(&c, "tank/g@v3: no such filesystem"));
	bad[1] = "tank/f@v3";			/* twice */
	CHECK(snap(bad, 2, NULL, 0) == -1);
	CHECK(cap_has(&c, "tank/f@v3: named twice"));
	bad[1] = "tank/f/usr";			/* malformed */
	CHECK(snap(bad, 2, NULL, 0) == -1);
	CHECK(cap_has(&c, "tank/f/usr: not a snapshot's name"));
	CHECK(!zfake_snapshot("tank/f@v3"));
	CHECK(is(snaps("tank/f", &rc), "v1 v2"));
}

/* Z3, Z4: a snapshot's properties. */
static void
test_snapshot_props(void)
{
	static const char *const v1[] = { "tank/f@v1" };
	enum sh_zsrc src;

	world();
	CHECK(snap(v1, 1, NULL, 0) == 0);
	CHECK(is(get("tank/f@v1", "type", NULL), "snapshot"));
	CHECK(is(get("tank/f@v1", "name", NULL), "tank/f@v1"));
	CHECK(strtoul(get("tank/f@v1", "createtxg", NULL), NULL, 10) > 0);
	CHECK(strtoul(get("tank/f@v1", "guid", NULL), NULL, 10) > 0);
	/* the filesystem's own user property, inherited */
	CHECK(is(get("tank/f@v1", "shtetl:kind", &src), "foundation"));
	CHECK(src == SH_ZSRC_INHERITED);
	/* Z4 */
	cap_reset(&c);
	CHECK(sh_zfs_set(z, "tank/f@v1", "shtetl:kind", "version",
	    &c.c_err) == 0);
	CHECK(is(get("tank/f@v1", "shtetl:kind", &src), "version"));
	CHECK(src == SH_ZSRC_LOCAL);
	CHECK(is(get("tank/f", "shtetl:kind", NULL), "foundation"));
	CHECK(sh_zfs_set(z, "tank/f@v1", "compression", "lz4", &c.c_err) ==
	    -1);
	/* libzfs's words, which src/fbsd/zfs.c passes on */
	CHECK(cap_has(&c, "tank/f@v1: this property can not be modified "
	    "for snapshots"));
	CHECK(sh_zfs_set(z, "tank/f@v9", "shtetl:x", "y", &c.c_err) == -1);
	CHECK(cap_has(&c, "tank/f@v9: no such snapshot"));
	/* a missing snapshot is 1, as a missing filesystem is */
	cap_reset(&c);
	CHECK(sh_zfs_get(z, "tank/f@v9", "type", NULL, 0, NULL, &c.c_err) ==
	    1);
	CHECK(is(get("tank/f@v1", "compression", NULL), "(refused)"));
}

/* Z6 - Z9: rollback, destroy, inherit, unmount. */
static void
test_changes(void)
{
	static const char *const v1[] = { "tank/f@v1" };
	static const char *const v2[] = { "tank/f@v2" };
	static const char *const vu[] = { "tank/f/var@x" };
	enum sh_zsrc src;
	int rc;

	world();
	CHECK(snap(v1, 1, NULL, 0) == 0);
	CHECK(snap(v2, 1, NULL, 0) == 0);
	/* Z6 */
	cap_reset(&c);
	CHECK(sh_zfs_rollback(z, "tank/f", "v1", &c.c_err) == -1);
	CHECK(cap_has(&c, "tank/f@v1: not the newest snapshot"));
	CHECK(zfake_last_rollback() == NULL);
	CHECK(sh_zfs_rollback(z, "tank/nope", "v1", &c.c_err) == -1);
	CHECK(cap_has(&c, "tank/nope: no such filesystem"));
	CHECK(sh_zfs_rollback(z, "tank/f", "v2", &c.c_err) == 0);
	CHECK(is(zfake_last_rollback(), "tank/f@v2"));
	/* Z7 */
	cap_reset(&c);
	CHECK(sh_zfs_destroy(z, "tank/f@v2", &c.c_err) == 0);
	CHECK(is(snaps("tank/f", &rc), "v1"));
	CHECK(sh_zfs_rollback(z, "tank/f", "v1", &c.c_err) == 0);
	/* children and a snapshot: the snapshot, as ZFS looks first */
	CHECK(sh_zfs_destroy(z, "tank/f", &c.c_err) == -1);
	CHECK(cap_has(&c, "tank/f: has snapshots"));
	CHECK(snap(vu, 1, NULL, 0) == 0);
	CHECK(sh_zfs_destroy(z, "tank/f/var", &c.c_err) == -1);
	CHECK(cap_has(&c, "tank/f/var: has snapshots"));
	CHECK(sh_zfs_destroy(z, "tank/f/var@x", &c.c_err) == 0);
	CHECK(sh_zfs_destroy(z, "tank/f/var", &c.c_err) == 0);
	CHECK(!zfake_exists("tank/f/var") && zfake_exists("tank/f/usr"));
	cap_reset(&c);
	CHECK(sh_zfs_destroy(z, "tank/f/var", &c.c_err) == -1);
	CHECK(cap_has(&c, "tank/f/var: no such dataset"));
	CHECK(sh_zfs_destroy(z, "tank/f@v9", &c.c_err) == -1);
	CHECK(cap_has(&c, "tank/f@v9: no such dataset"));
	CHECK(sh_zfs_destroy(z, "tank/f@v1", &c.c_err) == 0);
	CHECK(sh_zfs_destroy(z, "tank/f", &c.c_err) == -1);
	CHECK(cap_has(&c, "tank/f: has children"));
	/* Z8 */
	zfake_set("tank/f", "mountpoint", "/build/f");
	zfake_set("tank/f/usr", "mountpoint", "/elsewhere");
	CHECK(is(get("tank/f/usr", "mountpoint", &src), "/elsewhere"));
	CHECK(src == SH_ZSRC_LOCAL);
	CHECK(sh_zfs_inherit(z, "tank/f/usr", "mountpoint", &c.c_err) == 0);
	CHECK(is(get("tank/f/usr", "mountpoint", &src), "/build/f/usr"));
	CHECK(src == SH_ZSRC_INHERITED);
	CHECK(sh_zfs_inherit(z, "tank/f", "mountpoint", &c.c_err) == 0);
	CHECK(is(get("tank/f", "mountpoint", &src), "/tank/f"));
	CHECK(src == SH_ZSRC_DEFAULT);
	/* Z9 */
	cap_reset(&c);
	CHECK(sh_zfs_mount(z, "tank/f", &c.c_err) == 0);
	CHECK(is(get("tank/f", "mounted", NULL), "yes"));
	CHECK(sh_zfs_unmount(z, "tank/f", &c.c_err) == 0);
	CHECK(is(get("tank/f", "mounted", NULL), "no"));
}

/* Z10: ZFS refusing. */
static void
test_refusals(void)
{
	static const char *const what[] = { "snapshot", "snapshots",
	    "rollback", "destroy", "inherit", "unmount" };
	static const char *const v1[] = { "tank/f@v1" };
	size_t i;
	int r, rc;

	for (i = 0; i < sizeof (what) / sizeof (what[0]); i++) {
		world();
		CHECK(snap(v1, 1, NULL, 0) == 0);
		zfake_fail(what[i]);
		cap_reset(&c);
		switch (i) {
		case 0:
			r = snap(v1, 1, NULL, 0);
			break;
		case 1:
			(void) snaps("tank/f", &rc);
			r = rc;
			break;
		case 2:
			r = sh_zfs_rollback(z, "tank/f", "v1", &c.c_err);
			break;
		case 3:
			r = sh_zfs_destroy(z, "tank/f@v1", &c.c_err);
			break;
		case 4:
			r = sh_zfs_inherit(z, "tank/f", "shtetl:kind",
			    &c.c_err);
			break;
		default:
			r = sh_zfs_unmount(z, "tank/f", &c.c_err);
			break;
		}
		CHECK(r == -1);
		CHECK(cap_has(&c, what[i]) && cap_has(&c, "failed as asked"));
	}
}

int
main(void)
{
	cap_open(&c);
	CHECK(sh_zfs_open(&z, &c.c_err) == 0);
	test_snapshots();
	test_snapshot_props();
	test_changes();
	test_refusals();
	sh_zfs_close(z);
	cap_close(&c);
	printf("check_zfs: %d checks\n", checks);
	return (0);
}
