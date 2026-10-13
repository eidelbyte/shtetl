/*
 * tests/MATRIX.md Z11: the cells of family Z (tests/check_zfs.c, over
 * the fake) against libzfs on real pools. tests/box/run.sh runs it
 * with a dataset of its own to work under; each cell prints a line,
 * "ok   Zn what" or "FAIL Zn what", which run.sh counts with its own.
 *
 * usage: box_zcheck POOL/DATASET    (made by run.sh, empty)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/err.h"
#include "core/zfs.h"

static struct sh_zfs *z;
static struct sh_err e;
static int failed;
static char top[256];

static void
cell(const char *id, const char *what, int good)
{
	(void) printf("%s %s %s\n", good ? "ok  " : "FAIL", id, what);
	if (!good)
		failed = 1;
}

/* REL below the top, as a full name; one of four buffers. */
static const char *
at(const char *rel)
{
	static char buf[4][512];
	static int n;

	n = (n + 1) % 4;
	(void) snprintf(buf[n], sizeof (buf[n]), "%s/%s", top, rel);
	return (buf[n]);
}

/* PROP of NAME, or "(refused)"; where it comes from into *SRC. */
static const char *
get(const char *name, const char *prop, enum sh_zsrc *src)
{
	static char val[4][512];
	static int n;

	n = (n + 1) % 4;
	if (sh_zfs_get(z, name, prop, val[n], sizeof (val[n]), src, &e) != 0)
		return ("(refused)");
	return (val[n]);
}

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
	*rc = sh_zfs_snapshots(z, ds, walk, NULL, &e);
	return (walked);
}

int
main(int argc, char **argv)
{
	static const struct sh_zprop p[] = {
		{ "shtetl:kind", "version" },
		{ "shtetl:commit", "the first" }
	};
	static const struct sh_zprop kind = { "shtetl:kind", "foundation" };
	const char *v1[3], *v2[3], *bad[2];
	char t1[64], name[512];
	enum sh_zsrc src;
	int rc;

	if (argc != 2) {
		(void) fprintf(stderr, "usage: box_zcheck POOL/DATASET\n");
		return (2);
	}
	(void) snprintf(top, sizeof (top), "%s", argv[1]);
	sh_err_init(&e, stderr, "zcheck: ");
	if (sh_zfs_open(&z, &e) != 0)
		return (2);
	if (sh_zfs_create(z, at("f"), &kind, 1, &e) != 0 ||
	    sh_zfs_create(z, at("f/usr"), NULL, 0, &e) != 0 ||
	    sh_zfs_create(z, at("f/var"), NULL, 0, &e) != 0) {
		cell("Z0", "the datasets to work on made", 0);
		return (1);
	}

	/* Z1: several at once, one transaction, the properties on each */
	(void) snprintf(name, sizeof (name), "%s@v1", at("f"));
	v1[0] = strdup(name);
	(void) snprintf(name, sizeof (name), "%s@v1", at("f/usr"));
	v1[1] = strdup(name);
	(void) snprintf(name, sizeof (name), "%s@v1", at("f/var"));
	v1[2] = strdup(name);
	cell("Z1", "three snapshots at once",
	    sh_zfs_snapshot(z, v1, 3, p, 2, &e) == 0);
	(void) snprintf(t1, sizeof (t1), "%s", get(v1[0], "createtxg",
	    NULL));
	cell("Z1", "one createtxg for all three",
	    strcmp(get(v1[1], "createtxg", NULL), t1) == 0 &&
	    strcmp(get(v1[2], "createtxg", NULL), t1) == 0);
	cell("Z1", "the properties on each, local",
	    strcmp(get(v1[2], "shtetl:kind", &src), "version") == 0 &&
	    src == SH_ZSRC_LOCAL &&
	    strcmp(get(v1[1], "shtetl:commit", NULL), "the first") == 0);
	(void) snprintf(name, sizeof (name), "%s@v2", at("f"));
	v2[0] = strdup(name);
	(void) snprintf(name, sizeof (name), "%s@v2", at("f/usr"));
	v2[1] = strdup(name);
	(void) snprintf(name, sizeof (name), "%s@v2", at("f/var"));
	v2[2] = strdup(name);
	cell("Z1", "a second version",
	    sh_zfs_snapshot(z, v2, 3, NULL, 0, &e) == 0);

	/* Z5: a dataset's own, oldest first, the stop returned */
	cell("Z5", "f: v1 v2", strcmp(snaps(at("f"), &rc), "v1 v2") == 0 &&
	    rc == 0);
	cell("Z5", "f/usr: v1 v2, not f's",
	    strcmp(snaps(at("f/usr"), &rc), "v1 v2") == 0);
	stop_at = 1;
	cell("Z5", "the walk stopped, its value returned",
	    strcmp(snaps(at("f"), &rc), "v1") == 0 && rc == 7);
	(void) snaps(at("nope"), &rc);
	cell("Z5", "no such filesystem refused", rc == -1);

	/* Z2: all or none */
	(void) snprintf(name, sizeof (name), "%s@v3", at("f"));
	bad[0] = name;
	bad[1] = v2[1];
	cell("Z2", "one taken: refused",
	    sh_zfs_snapshot(z, bad, 2, NULL, 0, &e) == -1);
	cell("Z2", "and none taken",
	    sh_zfs_get(z, name, "type", t1, sizeof (t1), NULL, &e) == 1);

	/* Z3, Z4: a snapshot's properties */
	cell("Z3", "type snapshot",
	    strcmp(get(v1[0], "type", NULL), "snapshot") == 0);
	cell("Z3", "a guid", strtoull(get(v1[0], "guid", NULL), NULL,
	    10) > 0);
	cell("Z3", "the filesystem's user property, inherited",
	    strcmp(get(v2[0], "shtetl:kind", &src), "foundation") == 0 &&
	    src == SH_ZSRC_INHERITED);
	cell("Z4", "a user property set on a snapshot",
	    sh_zfs_set(z, v2[0], "shtetl:kind", "version", &e) == 0 &&
	    strcmp(get(v2[0], "shtetl:kind", &src), "version") == 0 &&
	    src == SH_ZSRC_LOCAL);
	cell("Z4", "the filesystem's own unchanged",
	    strcmp(get(at("f"), "shtetl:kind", NULL), "foundation") == 0);
	cell("Z4", "a native property on a snapshot refused",
	    sh_zfs_set(z, v2[0], "compression", "lz4", &e) == -1);
	(void) snprintf(name, sizeof (name), "%s@v9", at("f"));
	cell("Z4", "a missing snapshot: 1, as a missing filesystem",
	    sh_zfs_get(z, name, "type", t1, sizeof (t1), NULL, &e) == 1);

	/* Z6: rollback to the newest only */
	cell("Z6", "rollback to an older one refused",
	    sh_zfs_rollback(z, at("f"), "v1", &e) == -1);
	cell("Z6", "rollback to the newest",
	    sh_zfs_rollback(z, at("f"), "v2", &e) == 0);

	/* Z7: destroy */
	cell("Z7", "a snapshot destroyed",
	    sh_zfs_destroy(z, v2[0], &e) == 0 &&
	    strcmp(snaps(at("f"), &rc), "v1") == 0);
	cell("Z7", "a filesystem with children refused",
	    sh_zfs_destroy(z, at("f"), &e) == -1);
	cell("Z7", "a filesystem with snapshots refused",
	    sh_zfs_destroy(z, at("f/var"), &e) == -1);
	cell("Z7", "its snapshots, then it",
	    sh_zfs_destroy(z, v2[2], &e) == 0 &&
	    sh_zfs_destroy(z, v1[2], &e) == 0 &&
	    sh_zfs_destroy(z, at("f/var"), &e) == 0 &&
	    sh_zfs_get(z, at("f/var"), "type", t1, sizeof (t1), NULL,
	    &e) == 1);

	/* Z8, Z9: inherit; unmount and mount */
	rc = sh_zfs_set(z, at("f/usr"), "mountpoint", "/zcheck-usr", &e);
	(void) get(at("f/usr"), "mountpoint", &src);
	cell("Z8", "a mountpoint set locally", rc == 0 &&
	    src == SH_ZSRC_LOCAL);
	rc = sh_zfs_inherit(z, at("f/usr"), "mountpoint", &e);
	(void) get(at("f/usr"), "mountpoint", &src);
	cell("Z8", "and inherited again", rc == 0 &&
	    src == SH_ZSRC_INHERITED);
	cell("Z9", "unmounted",
	    sh_zfs_unmount(z, at("f/usr"), &e) == 0 &&
	    strcmp(get(at("f/usr"), "mounted", NULL), "no") == 0);
	cell("Z9", "mounted again",
	    sh_zfs_mount(z, at("f/usr"), &e) == 0 &&
	    strcmp(get(at("f/usr"), "mounted", NULL), "yes") == 0);

	/* Z10: refusals carry ZFS's reason */
	/* six refusals above: Z5, Z2, Z4, Z6 and Z7 twice */
	cell("Z10", "every refusal above was reported", e.se_count >= 6);

	sh_zfs_close(z);
	return (failed);
}
