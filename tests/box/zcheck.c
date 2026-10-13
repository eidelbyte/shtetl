/*
 * tests/MATRIX.md Z11: the cells of family Z (tests/check_zfs.c, over
 * the fake) against libzfs on real pools. tests/box/run.sh runs it
 * with a dataset of its own to work under; each cell prints a line,
 * "ok   Zn what" or "FAIL Zn what", which run.sh counts with its own.
 * A refusal is read as check_zfs.c reads one, by its words: the name
 * refused and why, as the fake says it.
 *
 * usage: box_zcheck POOL/DATASET OTHER/DATASET
 *        (made by run.sh, empty; OTHER another pool, for Z15 - Z17)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/err.h"
#include "core/zfs.h"

static struct sh_zfs *z;
static struct sh_err e;
static int failed;
static char top[256], other[256];

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

/* REL below the other pool's dataset, as a full name. */
static const char *
oth(const char *rel)
{
	static char buf[4][512];
	static int n;

	n = (n + 1) % 4;
	(void) snprintf(buf[n], sizeof (buf[n]), "%s/%s", other, rel);
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

/*
 * The sink of a call whose refusal a cell reads: in memory, emptied
 * here first; refused() passes what it heard on to stderr.
 */
static struct sh_err q;
static FILE *qf;
static char *qbuf;
static size_t qlen;
static int refusals, misheard;

static struct sh_err *
ask(void)
{
	if (qf != NULL)
		(void) fclose(qf);
	free(qbuf);
	qbuf = NULL;
	qlen = 0;
	if ((qf = open_memstream(&qbuf, &qlen)) == NULL) {
		(void) fprintf(stderr, "zcheck: open_memstream failed\n");
		exit(2);
	}
	sh_err_init(&q, qf, "");
	return (&q);
}

/* RC is -1, and what was reported holds "NAME: WHY". */
static void
refusedn(const char *id, const char *what, int rc, const char *name,
    const char *why)
{
	char want[768];
	int good;

	(void) fflush(qf);
	(void) snprintf(want, sizeof (want), "%s: %s", name, why);
	good = rc == -1 && qbuf != NULL && strstr(qbuf, want) != NULL;
	(void) fprintf(stderr, "zcheck: %s", qbuf != NULL &&
	    qbuf[0] != '\0' ? qbuf : "(nothing reported)\n");
	if (!good) {
		(void) fprintf(stderr, "zcheck: wanted %s\n", want);
		misheard++;
	}
	refusals++;
	cell(id, what, good);
}

/* RC is -1, and what was reported holds "REL: WHY", REL as at()'s. */
static void
refused(const char *id, const char *what, int rc, const char *rel,
    const char *why)
{
	refusedn(id, what, rc, at(rel), why);
}

/* Z18: a snapshot's own user properties, walked into these. */
static char props[512];

static int
walkprop(void *arg, const char *prop, const char *value)
{
	size_t n = strlen(props);

	(void) arg;
	(void) snprintf(props + n, sizeof (props) - n, "%s%s=%s",
	    n > 0 ? " " : "", prop, value);
	return (0);
}

int
main(int argc, char **argv)
{
	static const struct sh_zprop p[] = {
		{ "shtetl:kind", "version" },
		{ "shtetl:commit", "the first" }
	};
	static const struct sh_zprop kind = { "shtetl:kind", "foundation" };
	static const struct sh_zprop off = { "canmount", "off" };
	static const struct sh_zprop none = { "mountpoint", "none" };
	static const struct sh_zprop kver = { "shtetl:kind", "version" };
	static const struct sh_zprop zstd = { "compression", "zstd" };
	const char *v1[3], *v2[3], *bad[2];
	char t1[64], name[512], g3[512], tn[512], want[640];
	const char *sv1, *sv2, *sv3, *xs1, *ms;
	enum sh_zsrc src;
	int rc;

	if (argc != 3) {
		(void) fprintf(stderr, "usage: box_zcheck POOL/DATASET "
		    "OTHER/DATASET\n");
		return (2);
	}
	(void) snprintf(top, sizeof (top), "%s", argv[1]);
	(void) snprintf(other, sizeof (other), "%s", argv[2]);
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
	refused("Z5", "no such filesystem refused",
	    sh_zfs_snapshots(z, at("nope"), walk, NULL, ask()), "nope",
	    "no such filesystem");

	/* Z2: all or none, the one refused named */
	(void) snprintf(name, sizeof (name), "%s@v3", at("f"));
	bad[0] = name;
	bad[1] = v2[1];
	refused("Z2", "one taken: refused",
	    sh_zfs_snapshot(z, bad, 2, NULL, 0, ask()), "f/usr@v2", "taken");
	(void) snprintf(g3, sizeof (g3), "%s@v3", at("g"));
	bad[1] = g3;
	refused("Z2", "a filesystem missing: refused",
	    sh_zfs_snapshot(z, bad, 2, NULL, 0, ask()), "g@v3",
	    "no such filesystem");
	bad[1] = name;
	refused("Z2", "a name twice: refused",
	    sh_zfs_snapshot(z, bad, 2, NULL, 0, ask()), "f@v3",
	    "named twice");
	bad[1] = at("f/usr");
	refused("Z2", "a malformed name: refused",
	    sh_zfs_snapshot(z, bad, 2, NULL, 0, ask()), "f/usr",
	    "not a snapshot's name");
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
	refused("Z4", "a native property on a snapshot refused",
	    sh_zfs_set(z, v2[0], "compression", "lz4", ask()), "f@v2",
	    "this property can not be modified for snapshots");
	(void) snprintf(name, sizeof (name), "%s@v9", at("f"));
	refused("Z4", "a missing snapshot's property refused",
	    sh_zfs_set(z, name, "shtetl:x", "y", ask()), "f@v9",
	    "no such snapshot");
	cell("Z4", "a missing snapshot: 1, as a missing filesystem",
	    sh_zfs_get(z, name, "type", t1, sizeof (t1), NULL, &e) == 1);

	/* Z6: rollback to the newest only */
	refused("Z6", "rollback to an older one refused",
	    sh_zfs_rollback(z, at("f"), "v1", ask()), "f@v1",
	    "not the newest snapshot");
	refused("Z6", "rollback of a missing filesystem refused",
	    sh_zfs_rollback(z, at("nope"), "v1", ask()), "nope",
	    "no such filesystem");
	cell("Z6", "rollback to the newest",
	    sh_zfs_rollback(z, at("f"), "v2", &e) == 0);

	/* Z7: destroy */
	cell("Z7", "a snapshot destroyed",
	    sh_zfs_destroy(z, v2[0], &e) == 0 &&
	    strcmp(snaps(at("f"), &rc), "v1") == 0);
	/* f has children and v1: ZFS looks at the snapshots first */
	refused("Z7", "children and a snapshot: refused for the snapshot",
	    sh_zfs_destroy(z, at("f"), ask()), "f", "has snapshots");
	refused("Z7", "a filesystem with snapshots refused",
	    sh_zfs_destroy(z, at("f/var"), ask()), "f/var",
	    "has snapshots");
	cell("Z7", "its snapshots, then it",
	    sh_zfs_destroy(z, v2[2], &e) == 0 &&
	    sh_zfs_destroy(z, v1[2], &e) == 0 &&
	    sh_zfs_destroy(z, at("f/var"), &e) == 0 &&
	    sh_zfs_get(z, at("f/var"), "type", t1, sizeof (t1), NULL,
	    &e) == 1);
	refused("Z7", "a missing filesystem refused",
	    sh_zfs_destroy(z, at("f/var"), ask()), "f/var",
	    "no such dataset");
	(void) snprintf(name, sizeof (name), "%s@v9", at("f"));
	refused("Z7", "a missing snapshot refused",
	    sh_zfs_destroy(z, name, ask()), "f@v9", "no such dataset");
	cell("Z7", "f's last snapshot destroyed",
	    sh_zfs_destroy(z, v1[0], &e) == 0);
	refused("Z7", "a filesystem with children refused",
	    sh_zfs_destroy(z, at("f"), ask()), "f", "has children");

	/*
	 * Z8, Z9: inherit; unmount and mount. f sets the mountpoint f/usr
	 * inherits, as in check_zfs.c: nothing above f does, so what f
	 * inherits is the default. The values carry run.sh's altroot, so
	 * f/usr's is read against f's.
	 */
	rc = sh_zfs_set(z, at("f"), "mountpoint", "/zcheck-f", &e);
	rc |= sh_zfs_set(z, at("f/usr"), "mountpoint", "/zcheck-usr", &e);
	(void) get(at("f/usr"), "mountpoint", &src);
	cell("Z8", "a mountpoint set locally", rc == 0 &&
	    src == SH_ZSRC_LOCAL);
	rc = sh_zfs_inherit(z, at("f/usr"), "mountpoint", &e);
	(void) snprintf(name, sizeof (name), "%s/usr",
	    get(at("f"), "mountpoint", NULL));
	cell("Z8", "and inherited again, f's below it", rc == 0 &&
	    strcmp(get(at("f/usr"), "mountpoint", &src), name) == 0 &&
	    src == SH_ZSRC_INHERITED);
	rc = sh_zfs_inherit(z, at("f"), "mountpoint", &e);
	(void) get(at("f"), "mountpoint", &src);
	cell("Z8", "f's inherited where none is set: the default",
	    rc == 0 && src == SH_ZSRC_DEFAULT);
	cell("Z9", "unmounted",
	    sh_zfs_unmount(z, at("f/usr"), &e) == 0 &&
	    strcmp(get(at("f/usr"), "mounted", NULL), "no") == 0);
	cell("Z9", "mounted again",
	    sh_zfs_mount(z, at("f/usr"), &e) == 0 &&
	    strcmp(get(at("f/usr"), "mounted", NULL), "yes") == 0);

	/*
	 * Z12, Z13: a mountpoint mounts as zfs set does, which the first
	 * real build met (a mount after it failed busy); mount. f and
	 * f/usr unmounted first, f/off and f/none made beside them.
	 */
	rc = sh_zfs_unmount(z, at("f/usr"), &e) | sh_zfs_unmount(z, at("f"),
	    &e) | sh_zfs_create(z, at("f/off"), &off, 1, &e) |
	    sh_zfs_create(z, at("f/none"), &none, 1, &e);
	rc |= sh_zfs_set(z, at("f"), "mountpoint", "/zcheck-m", &e);
	cell("Z12", "a mountpoint set: f mounted, and f/usr below it",
	    rc == 0 && strcmp(get(at("f"), "mounted", NULL), "yes") == 0 &&
	    strcmp(get(at("f/usr"), "mounted", NULL), "yes") == 0);
	cell("Z12", "canmount off, or a mountpoint of its own: not",
	    strcmp(get(at("f/off"), "mounted", NULL), "no") == 0 &&
	    strcmp(get(at("f/none"), "mounted", NULL), "no") == 0);
	cell("Z13", "mounted there already: nothing to do",
	    sh_zfs_mount(z, at("f"), &e) == 0 &&
	    sh_zfs_mount(z, at("f/usr"), &e) == 0);
	refused("Z13", "canmount off refused",
	    sh_zfs_mount(z, at("f/off"), ask()), "f/off", "canmount is off");
	refused("Z13", "no mountpoint refused",
	    sh_zfs_mount(z, at("f/none"), ask()), "f/none", "no mountpoint");
	rc = sh_zfs_inherit(z, at("f"), "mountpoint", &e);
	cell("Z12", "inherited: moved where the default says",
	    rc == 0 && strcmp(get(at("f"), "mounted", NULL), "yes") == 0 &&
	    sh_zfs_mount(z, at("f"), &e) == 0);
	/* Z20: f/m mounted below f, a snapshot of it too */
	(void) snprintf(name, sizeof (name), "%s@s", at("f/m"));
	ms = name;
	cell("Z20", "a filesystem mounted, with a snapshot",
	    sh_zfs_create(z, at("f/m"), NULL, 0, &e) == 0 &&
	    sh_zfs_mount(z, at("f/m"), &e) == 0 &&
	    sh_zfs_snapshot(z, &ms, 1, NULL, 0, &e) == 0);
	refused("Z20", "destroyed while mounted: refused, before its snapshots",
	    sh_zfs_destroy(z, at("f/m"), ask()), "f/m", "mounted");
	cell("Z20", "unmounted, its snapshot destroyed, then it",
	    sh_zfs_unmount(z, at("f/m"), &e) == 0 &&
	    sh_zfs_destroy(z, name, &e) == 0 &&
	    sh_zfs_destroy(z, at("f/m"), &e) == 0);

	/*
	 * Z14 - Z19: clones and sends, s and its snapshots the source,
	 * x a clone of it, the other pool the target
	 */
	(void) snprintf(name, sizeof (name), "%s@v1", at("s"));
	sv1 = strdup(name);
	cell("Z14", "the source made, with a version",
	    sh_zfs_create(z, at("s"), NULL, 0, &e) == 0 &&
	    sh_zfs_snapshot(z, &sv1, 1, &kver, 1, &e) == 0);
	cell("Z14", "a clone, a property from its birth, its origin",
	    sh_zfs_clone(z, sv1, at("x"), &zstd, 1, &e) == 0 &&
	    strcmp(get(at("x"), "origin", NULL), sv1) == 0 &&
	    strcmp(get(at("x"), "compression", &src), "zstd") == 0 &&
	    src == SH_ZSRC_LOCAL &&
	    strcmp(get(at("x"), "mounted", NULL), "no") == 0);
	(void) snprintf(name, sizeof (name), "%s@v9", at("s"));
	refusedn("Z14", "no such snapshot",
	    sh_zfs_clone(z, name, at("y"), NULL, 0, ask()), name,
	    "no such snapshot");
	refused("Z14", "the name taken",
	    sh_zfs_clone(z, sv1, at("x"), NULL, 0, ask()), "x", "exists");
	refused("Z14", "no parent",
	    sh_zfs_clone(z, sv1, at("q/r"), NULL, 0, ask()), "q/r",
	    "no parent");
	refusedn("Z14", "its origin not destroyed",
	    sh_zfs_destroy(z, sv1, ask()), sv1, "has clones");
	(void) snprintf(name, sizeof (name), "%s@v1", oth("s"));
	cell("Z15", "a whole send into the other pool, nothing but data",
	    sh_zfs_send(z, sv1, NULL, name, NULL, &e) == 0 &&
	    strcmp(get(name, "guid", NULL), get(sv1, "guid", NULL)) == 0 &&
	    strcmp(get(name, "shtetl:kind", NULL), "-") == 0 &&
	    strcmp(get(oth("s"), "mounted", NULL), "no") == 0);
	refusedn("Z15", "the target there already",
	    sh_zfs_send(z, sv1, NULL, name, NULL, ask()), oth("s"),
	    "exists");
	(void) snprintf(name, sizeof (name), "%s@v2", at("s"));
	sv2 = strdup(name);
	(void) snprintf(name, sizeof (name), "%s@v2", oth("s"));
	cell("Z16", "an incremental send",
	    sh_zfs_snapshot(z, &sv2, 1, NULL, 0, &e) == 0 &&
	    sh_zfs_send(z, sv2, sv1, name, NULL, &e) == 0 &&
	    strcmp(snaps(oth("s"), &rc), "v1 v2") == 0);
	(void) snprintf(name, sizeof (name), "%s@v2", oth("nope"));
	refusedn("Z16", "the target missing",
	    sh_zfs_send(z, sv2, sv1, name, NULL, ask()), oth("nope"),
	    "no such filesystem");
	(void) snprintf(name, sizeof (name), "%s@v3", at("s"));
	sv3 = strdup(name);
	(void) snprintf(tn, sizeof (tn), "%s@v3", oth("s"));
	(void) snprintf(want, sizeof (want), "newest snapshot is not %s's "
	    "counterpart", sv1);
	refusedn("Z16", "its newest not the base's counterpart",
	    sh_zfs_snapshot(z, &sv3, 1, NULL, 0, &e) == 0 ?
	    sh_zfs_send(z, sv3, sv1, tn, NULL, ask()) : 0, oth("s"),
	    want);
	(void) snprintf(name, sizeof (name), "%s@v1", oth("s"));
	(void) snprintf(want, sizeof (want), "not an earlier snapshot of %s",
	    sv1);
	refusedn("Z16", "a base that is no earlier",
	    sh_zfs_send(z, sv1, sv3, name, NULL, ask()), sv3, want);
	(void) snprintf(name, sizeof (name), "%s@s1", at("x"));
	xs1 = strdup(name);
	(void) snprintf(name, sizeof (name), "%s@s1", oth("x"));
	(void) snprintf(tn, sizeof (tn), "%s@v1", oth("s"));
	cell("Z17", "a clone stream, received as a clone of the counterpart",
	    sh_zfs_snapshot(z, &xs1, 1, NULL, 0, &e) == 0 &&
	    sh_zfs_send(z, xs1, sv1, name, tn, &e) == 0 &&
	    strcmp(get(oth("x"), "origin", NULL), tn) == 0);
	(void) snprintf(name, sizeof (name), "%s@s1", oth("y"));
	(void) snprintf(tn, sizeof (tn), "%s@v9", oth("s"));
	refusedn("Z17", "the origin missing",
	    sh_zfs_send(z, xs1, sv1, name, tn, ask()), tn,
	    "no such snapshot");
	(void) snprintf(tn, sizeof (tn), "%s@v2", oth("s"));
	(void) snprintf(want, sizeof (want), "not %s's counterpart", sv1);
	refusedn("Z17", "the origin not the base's counterpart",
	    sh_zfs_send(z, xs1, sv1, name, tn, ask()), tn, want);
	props[0] = '\0';
	(void) snprintf(name, sizeof (name), "%s@v1", oth("s"));
	cell("Z18", "a snapshot's own user properties, copied over",
	    sh_zfs_userprops(z, sv1, walkprop, NULL, &e) == 0 &&
	    strcmp(props, "shtetl:kind=version") == 0 &&
	    sh_zfs_set(z, name, "shtetl:kind", "version", &e) == 0 &&
	    strcmp(get(name, "shtetl:kind", &src), "version") == 0 &&
	    src == SH_ZSRC_LOCAL);

	/* Z10, Z19: each refusal above in the fake's words */
	cell("Z10", "every refusal above said why, as the fake does",
	    refusals > 0 && misheard == 0);

	sh_zfs_close(z);
	if (qf != NULL)
		(void) fclose(qf);
	free(qbuf);
	return (failed);
}
