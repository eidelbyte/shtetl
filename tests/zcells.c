/*
 * The cells of family Z (tests/zcells.h). Names are written relative to
 * the top: "f/usr" is TOP/f/usr.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/err.h"
#include "core/zfs.h"
#include "zcells.h"

int (*zc_put)(const char *, const char *);
int (*zc_has)(const char *, const char *);
int (*zc_fill)(const char *);
int (*zc_cut)(const char *, const char *, const char *, const char *);

static struct sh_zfs *z;
static FILE *out;
static const char *top;
static int failed;

/* The sink every call reports to, read back by the cells. */
static struct sh_err e;
static FILE *ef;
static char *ebuf;
static size_t elen;

/* REL below the top, as a full name: one of eight buffers. */
static const char *
at(const char *rel)
{
	static char buf[8][512];
	static int n;

	n = (n + 1) % 8;
	(void) snprintf(buf[n], sizeof (buf[n]), "%s/%s", top, rel);
	return (buf[n]);
}

/* What the calls since the last cell reported. */
static const char *
heard(void)
{
	(void) fflush(ef);
	return (ebuf != NULL ? ebuf : "");
}

/* An empty sink. */
static void
hush(void)
{
	if (ef != NULL)
		(void) fclose(ef);
	free(ebuf);
	ebuf = NULL;
	elen = 0;
	if ((ef = open_memstream(&ebuf, &elen)) == NULL) {
		(void) fprintf(out, "zcells: open_memstream failed\n");
		exit(2);
	}
	sh_err_init(&e, ef, "");
}

static void
cell(const char *id, const char *what, int good)
{
	(void) fprintf(out, "%s %s %s\n", good ? "ok  " : "FAIL", id, what);
	if (!good) {
		failed++;
		(void) fprintf(out, "     heard: %s", *heard() != '\0' ?
		    heard() : "nothing\n");
	}
	hush();
}

/* A refusal: RC -1, and "REL: WHY" heard. */
static void
refused(const char *id, const char *what, int rc, const char *rel,
    const char *why)
{
	char want[640];

	(void) snprintf(want, sizeof (want), "%s: %s", at(rel), why);
	cell(id, what, rc == -1 && strstr(heard(), want) != NULL);
}

/* What a walk saw, each name relative to the top, and where to stop. */
static char seen[1024];
static int stop_at;

static int
see(void *arg, const char *name)
{
	size_t n = strlen(seen), t = strlen(top);

	(void) arg;
	if (strncmp(name, top, t) == 0 && name[t] == '/')
		name += t + 1;
	(void) snprintf(seen + n, sizeof (seen) - n, "%s%s", n > 0 ? " " : "",
	    name);
	return (stop_at > 0 && --stop_at == 0 ? 7 : 0);
}

static int
see_local(void *arg, const char *ds, const char *value)
{
	char buf[512];

	(void) snprintf(buf, sizeof (buf), "%s=%s", ds, value);
	return (see(arg, buf));
}

static int
cmp(const void *a, const void *b)
{
	return (strcmp(*(char *const *)a, *(char *const *)b));
}

/* What was seen, its words sorted: ZFS walks children in no order. */
static const char *
sorted(void)
{
	static char buf[1024];
	char *words[64], *w, *last;
	size_t i, n = 0;

	for (w = strtok_r(seen, " ", &last); w != NULL && n < 64;
	    w = strtok_r(NULL, " ", &last))
		words[n++] = w;
	qsort(words, n, sizeof (words[0]), cmp);
	buf[0] = '\0';
	for (i = 0; i < n; i++) {
		(void) strlcat(buf, i > 0 ? " " : "", sizeof (buf));
		(void) strlcat(buf, words[i], sizeof (buf));
	}
	return (buf);
}

static int
walked(int rc)
{
	seen[0] = '\0';
	return (rc);
}

/* The user property PROP of REL, or "(unset)", or "(refused)". */
static const char *
user(const char *rel, const char *prop)
{
	static char val[4][256];
	static int n;
	int r;

	n = (n + 1) % 4;
	if ((r = sh_zfs_user(z, at(rel), prop, val[n], sizeof (val[n]),
	    &e)) != 0)
		return (r == 1 ? "(unset)" : "(refused)");
	return (val[n]);
}

/* Where REL is mounted, below the altroot: "" when it is not. */
static const char *
where(const char *rel)
{
	static char path[1024];
	const char *slash;

	if (sh_zfs_mounted(z, at(rel), path, sizeof (path), &e) != 1)
		return ("");
	/* a pool imported with an altroot mounts below it: the tail */
	slash = strstr(path, "/zc-");
	return (slash != NULL ? slash : path);
}

/* N snapshots, REL@SNAP each, none of them with properties. */
static int
snap(const char *a, const char *b, const char *c)
{
	const char *names[3];
	size_t n = 0;

	names[n++] = at(a);
	if (b != NULL)
		names[n++] = at(b);
	if (c != NULL)
		names[n++] = at(c);
	return (sh_zfs_snapshot(z, names, n, NULL, 0, &e));
}

static void
cells_create(void)
{
	static const struct sh_zprop f = { "shtetl:zcheck", "f" };
	static const struct sh_zprop g = { "shtetl:zcheck", "g" };

	cell("Z1", "created, a property from its birth",
	    sh_zfs_create(z, at("f"), &f, 1, &e) == 0 &&
	    strcmp(user("f", "shtetl:zcheck"), "f") == 0);
	cell("Z1", "children and a sibling",
	    sh_zfs_create(z, at("f/usr"), NULL, 0, &e) == 0 &&
	    sh_zfs_create(z, at("f/var"), NULL, 0, &e) == 0 &&
	    sh_zfs_create(z, at("f/usr/local"), NULL, 0, &e) == 0 &&
	    sh_zfs_create(z, at("g"), &g, 1, &e) == 0);
	refused("Z1", "a name taken: refused",
	    sh_zfs_create(z, at("f"), NULL, 0, &e), "f", "exists");
	refused("Z1", "no parent: refused",
	    sh_zfs_create(z, at("q/r"), NULL, 0, &e), "q/r", "no parent");
}

static void
cells_user(void)
{
	cell("Z2", "set on it: its value",
	    strcmp(user("g", "shtetl:zcheck"), "g") == 0);
	cell("Z2", "set above it: inherited",
	    strcmp(user("f/usr/local", "shtetl:zcheck"), "f") == 0);
	cell("Z2", "set nowhere: 1",
	    strcmp(user("f", "shtetl:nothing"), "(unset)") == 0);
	cell("Z2", "a native property is no user property: 1",
	    strcmp(user("f", "canmount"), "(unset)") == 0);
	refused("Z2", "no such filesystem: refused",
	    sh_zfs_user(z, at("nope"), "shtetl:zcheck", seen, sizeof (seen),
	    &e), "nope", "no such filesystem");
}

static void
cells_local(void)
{
	cell("Z3", "set locally: each with its value, not where inherited",
	    walked(0) == 0 &&
	    sh_zfs_local(z, "shtetl:zcheck", see_local, NULL, &e) == 0 &&
	    strcmp(sorted(), "f=f g=g") == 0);
	stop_at = 1;
	cell("Z3", "the walk stopped, FN's value returned",
	    walked(0) == 0 &&
	    sh_zfs_local(z, "shtetl:zcheck", see_local, NULL, &e) == 7 &&
	    strchr(seen, ' ') == NULL);
	stop_at = 0;
}

static void
cells_children(void)
{
	cell("Z4", "the filesystems directly below",
	    walked(0) == 0 &&
	    sh_zfs_children(z, at("f"), see, NULL, &e) == 0 &&
	    strcmp(sorted(), "f/usr f/var") == 0);
	stop_at = 1;
	cell("Z4", "the walk stopped, FN's value returned",
	    walked(0) == 0 &&
	    sh_zfs_children(z, at("f"), see, NULL, &e) == 7 &&
	    strchr(seen, ' ') == NULL);
	stop_at = 0;
	refused("Z4", "no such filesystem: refused",
	    sh_zfs_children(z, at("nope"), see, NULL, &e), "nope",
	    "no such filesystem");
}

static void
cells_snapshot(void)
{
	static const struct sh_zprop kind = { "shtetl:kind", "create" };
	const char *v1[3];

	v1[0] = at("f@v1");
	v1[1] = at("f/usr@v1");
	v1[2] = at("f/var@v1");
	cell("Z5", "three at once, a property on each from its birth",
	    sh_zfs_snapshot(z, v1, 3, &kind, 1, &e) == 0 &&
	    strcmp(user("f/var@v1", "shtetl:kind"), "create") == 0 &&
	    strcmp(user("f@v1", "shtetl:kind"), "create") == 0);
	cell("Z5", "a snapshot inherits its filesystem's",
	    strcmp(user("f/usr@v1", "shtetl:zcheck"), "f") == 0);
	refused("Z5", "one taken: refused",
	    snap("f@v2", "f/usr@v1", NULL), "f/usr@v1", "taken");
	refused("Z5", "a filesystem missing: refused",
	    snap("f@v2", "nope@v2", NULL), "nope@v2", "no such filesystem");
	refused("Z5", "a name twice: refused",
	    snap("f@v2", "f@v2", NULL), "f@v2", "named twice");
	refused("Z5", "a malformed name: refused",
	    snap("f@v2", "f/usr", NULL), "f/usr", "not a snapshot's name");
	cell("Z5", "and none of them taken",
	    walked(0) == 0 &&
	    sh_zfs_snapshots(z, at("f"), see, NULL, &e) == 0 &&
	    strcmp(seen, "v1") == 0);
	refused("Z5", "no such snapshot: refused",
	    sh_zfs_user(z, at("f@v9"), "shtetl:kind", seen, sizeof (seen),
	    &e), "f@v9", "no such snapshot");
}

static void
cells_snapshots(void)
{
	cell("Z6", "a second: oldest first",
	    snap("f@v2", NULL, NULL) == 0 && walked(0) == 0 &&
	    sh_zfs_snapshots(z, at("f"), see, NULL, &e) == 0 &&
	    strcmp(seen, "v1 v2") == 0);
	cell("Z6", "its own, not its parent's",
	    walked(0) == 0 &&
	    sh_zfs_snapshots(z, at("f/usr"), see, NULL, &e) == 0 &&
	    strcmp(seen, "v1") == 0);
	stop_at = 1;
	cell("Z6", "the walk stopped, FN's value returned",
	    walked(0) == 0 &&
	    sh_zfs_snapshots(z, at("f"), see, NULL, &e) == 7 &&
	    strcmp(seen, "v1") == 0);
	stop_at = 0;
	refused("Z6", "no such filesystem: refused",
	    sh_zfs_snapshots(z, at("nope"), see, NULL, &e), "nope",
	    "no such filesystem");
}

/* m mounted, holding a; s1; b; s2; c. */
static void
cells_rollback(void)
{
	static const struct sh_zprop mp = { "mountpoint", "/zc-m" };

	cell("Z7", "m made, mounted, written, snapshotted twice",
	    sh_zfs_create(z, at("m"), &mp, 1, &e) == 0 &&
	    sh_zfs_mount(z, at("m"), &e) == 0 &&
	    zc_put(at("m"), "a") == 0 && snap("m@s1", NULL, NULL) == 0 &&
	    zc_put(at("m"), "b") == 0 && snap("m@s2", NULL, NULL) == 0 &&
	    zc_put(at("m"), "c") == 0);
	refused("Z7", "to an older one: refused",
	    sh_zfs_rollback(z, at("m"), "s1", &e), "m@s1",
	    "not the newest snapshot");
	refused("Z7", "to one that is not there: refused",
	    sh_zfs_rollback(z, at("m"), "s9", &e), "m@s9",
	    "not the newest snapshot");
	cell("Z7", "to the newest, mounted: what came after it gone",
	    sh_zfs_rollback(z, at("m"), "s2", &e) == 0 &&
	    zc_has(at("m"), "a") == 1 && zc_has(at("m"), "b") == 1 &&
	    zc_has(at("m"), "c") == 0);
	cell("Z7", "and still mounted", strcmp(where("m"), "/zc-m") == 0);
	refused("Z7", "no such filesystem: refused",
	    sh_zfs_rollback(z, at("nope"), "s2", &e), "nope",
	    "no such filesystem");
	refused("Z7", "a filesystem with no snapshot: refused",
	    sh_zfs_rollback(z, at("g"), "s1", &e), "g@s1",
	    "not the newest snapshot");
}

static void
cells_mount(void)
{
	static const struct sh_zprop off[] = {
		{ "canmount", "off" }, { "mountpoint", "/zc-off" }
	};
	static const struct sh_zprop none = { "mountpoint", "none" };

	cell("Z8", "mounted there already: nothing to do",
	    sh_zfs_mount(z, at("m"), &e) == 0 &&
	    strcmp(where("m"), "/zc-m") == 0);
	cell("Z8", "a child mounted where it inherits",
	    sh_zfs_create(z, at("m/k"), NULL, 0, &e) == 0 &&
	    strcmp(where("m/k"), "") == 0 &&
	    sh_zfs_mount(z, at("m/k"), &e) == 0 &&
	    strcmp(where("m/k"), "/zc-m/k") == 0);
	refused("Z8", "a filesystem with one mounted inside: busy",
	    sh_zfs_unmount(z, at("m"), &e), "m", "busy");
	cell("Z8", "unmounted, and again: nothing to do",
	    sh_zfs_unmount(z, at("m/k"), &e) == 0 &&
	    strcmp(where("m/k"), "") == 0 &&
	    sh_zfs_unmount(z, at("m/k"), &e) == 0);
	refused("Z8", "canmount off: refused",
	    sh_zfs_create(z, at("off"), off, 2, &e) == 0 ?
	    sh_zfs_mount(z, at("off"), &e) : 0, "off", "canmount is off");
	refused("Z8", "mountpoint none: refused",
	    sh_zfs_create(z, at("none"), &none, 1, &e) == 0 ?
	    sh_zfs_mount(z, at("none"), &e) : 0, "none", "no mountpoint");
	refused("Z8", "where a missing one is mounted: refused",
	    sh_zfs_mounted(z, at("nope"), seen, sizeof (seen), &e), "nope",
	    "no such filesystem");
}

static void
cells_destroy(void)
{
	refused("Z9", "mounted, with snapshots: refused for the mount",
	    sh_zfs_destroy(z, at("m"), &e), "m", "mounted");
	refused("Z9", "unmounted, with children and snapshots: the snapshots",
	    sh_zfs_unmount(z, at("m"), &e) == 0 ?
	    sh_zfs_destroy(z, at("m"), &e) : 0, "m", "has snapshots");
	cell("Z9", "its snapshots destroyed",
	    sh_zfs_destroy(z, at("m@s2"), &e) == 0 &&
	    sh_zfs_destroy(z, at("m@s1"), &e) == 0 &&
	    walked(0) == 0 &&
	    sh_zfs_snapshots(z, at("m"), see, NULL, &e) == 0 &&
	    seen[0] == '\0');
	refused("Z9", "with a child: refused",
	    sh_zfs_destroy(z, at("m"), &e), "m", "has children");
	cell("Z9", "the child, then it",
	    sh_zfs_destroy(z, at("m/k"), &e) == 0 &&
	    sh_zfs_destroy(z, at("m"), &e) == 0);
	refused("Z9", "a filesystem not there: refused",
	    sh_zfs_destroy(z, at("m"), &e), "m", "no such dataset");
	refused("Z9", "a snapshot not there: refused",
	    sh_zfs_destroy(z, at("f@v9"), &e), "f@v9", "no such dataset");
}

/* Last: f/usr set and inherited, g's own let go. */
static void
cells_set(void)
{
	cell("Z10", "set: its value, and inherited below it",
	    sh_zfs_set(z, at("f/usr"), "shtetl:zcheck", "u", &e) == 0 &&
	    strcmp(user("f/usr", "shtetl:zcheck"), "u") == 0 &&
	    strcmp(user("f/usr/local", "shtetl:zcheck"), "u") == 0);
	cell("Z10", "set again: replaced",
	    sh_zfs_set(z, at("f/usr"), "shtetl:zcheck", "v", &e) == 0 &&
	    strcmp(user("f/usr", "shtetl:zcheck"), "v") == 0);
	cell("Z10", "inherited: its parent's again",
	    sh_zfs_inherit(z, at("f/usr"), "shtetl:zcheck", &e) == 0 &&
	    strcmp(user("f/usr", "shtetl:zcheck"), "f") == 0);
	cell("Z10", "inherited where not set there: nothing done",
	    sh_zfs_inherit(z, at("f/usr"), "shtetl:zcheck", &e) == 0 &&
	    strcmp(user("f/usr", "shtetl:zcheck"), "f") == 0);
	cell("Z10", "inherited with nothing above: none",
	    sh_zfs_inherit(z, at("g"), "shtetl:zcheck", &e) == 0 &&
	    strcmp(user("g", "shtetl:zcheck"), "(unset)") == 0);
	refused("Z10", "set on no such filesystem: refused",
	    sh_zfs_set(z, at("nope"), "shtetl:zcheck", "x", &e), "nope",
	    "no such filesystem");
	refused("Z10", "inherited on no such filesystem: refused",
	    sh_zfs_inherit(z, at("nope"), "shtetl:zcheck", &e), "nope",
	    "no such filesystem");
}

/* Whether the snapshots A and B, below the top, share a guid. */
static int
same(const char *a, const char *b)
{
	uint64_t ga, gb, t;

	return (sh_zfs_snapinfo(z, at(a), &ga, &t, &e) == 0 &&
	    sh_zfs_snapinfo(z, at(b), &gb, &t, &e) == 0 && ga == gb);
}

/* REL's snapshots, oldest first, as a walk sees them. */
static const char *
snaps(const char *rel)
{
	if (walked(0) != 0 || sh_zfs_snapshots(z, at(rel), see, NULL, &e) != 0)
		return ("(refused)");
	return (seen);
}

/* Whether REL is a clone of the snapshot ORIGIN, both below the top. */
static int
origin_is(const char *rel, const char *origin)
{
	char buf[512];

	return (sh_zfs_origin(z, at(rel), buf, sizeof (buf), &e) == 1 &&
	    strcmp(buf, at(origin)) == 0);
}

/*
 * The streams, last: s written and snapshotted, sent whole and on to
 * t; streams cut and resumed, taken away; clones, a clone stream, and
 * a promote.
 */
static void
cells_send(void)
{
	static const struct sh_zprop mp = { "mountpoint", "/zc-s" };
	static const struct sh_zprop cp[] = {
		{ "shtetl:zcheck", "c" }, { "mountpoint", "/zc-c" }
	};
	char why[600], buf[512];
	uint64_t g1, t1, g2, t2;

	cell("Z11", "two snapshots: their guids apart, createtxg in order",
	    sh_zfs_create(z, at("s"), &mp, 1, &e) == 0 &&
	    sh_zfs_mount(z, at("s"), &e) == 0 && zc_fill(at("s")) == 0 &&
	    snap("s@1", NULL, NULL) == 0 && zc_fill(at("s")) == 0 &&
	    snap("s@2", NULL, NULL) == 0 &&
	    sh_zfs_snapinfo(z, at("s@1"), &g1, &t1, &e) == 0 &&
	    sh_zfs_snapinfo(z, at("s@2"), &g2, &t2, &e) == 0 &&
	    g1 != g2 && t1 < t2);
	refused("Z11", "no such snapshot: refused",
	    sh_zfs_snapinfo(z, at("s@9"), &g1, &t1, &e), "s@9",
	    "no such snapshot");

	cell("Z14", "whole: t made, its @1 the source's guid",
	    sh_zfs_send(z, at("s@1"), NULL, at("t@1"), NULL, &e) == 0 &&
	    same("t@1", "s@1"));
	refused("Z14", "whole onto a filesystem there: refused",
	    sh_zfs_send(z, at("s@2"), NULL, at("t@2"), NULL, &e), "t",
	    "exists");
	cell("Z14", "incremental: t@2 the source's",
	    sh_zfs_send(z, at("s@2"), at("s@1"), at("t@2"), NULL, &e) == 0 &&
	    same("t@2", "s@2"));
	(void) snprintf(why, sizeof (why), "newest snapshot is not %s's "
	    "counterpart", at("s@1"));
	refused("Z14", "onto a newest snapshot that is no counterpart",
	    sh_zfs_send(z, at("s@2"), at("s@1"), at("t@3"), NULL, &e), "t",
	    why);
	refused("Z14", "incremental onto no filesystem: refused",
	    sh_zfs_send(z, at("s@2"), at("s@1"), at("u@2"), NULL, &e), "u",
	    "no such filesystem");
	refused("Z14", "a source not there: refused",
	    sh_zfs_send(z, at("s@9"), NULL, at("u@9"), NULL, &e), "s@9",
	    "no such snapshot");

	cell("Z15", "a cut incremental: a receive to resume, t still at @2",
	    zc_fill(at("s")) == 0 && snap("s@3", NULL, NULL) == 0 &&
	    zc_cut(at("s@3"), at("s@2"), at("t@3"), NULL) == 0 &&
	    sh_zfs_resumable(z, at("t"), &e) == 1 &&
	    strcmp(snaps("t"), "1 2") == 0);
	refused("Z15", "a fresh stream while it stands: refused",
	    sh_zfs_send(z, at("s@3"), at("s@2"), at("t@3"), NULL, &e), "t",
	    "a receive to resume");
	cell("Z15", "resumed: t@3 the source's, nothing to resume",
	    sh_zfs_resume(z, at("t"), &e) == 0 && same("t@3", "s@3") &&
	    sh_zfs_resumable(z, at("t"), &e) == 0);
	refused("Z15", "nothing to resume: refused",
	    sh_zfs_resume(z, at("t"), &e), "t", "no receive to resume");
	cell("Z15", "a cut whole: w made, a receive to resume on it",
	    zc_cut(at("s@3"), NULL, at("w@3"), NULL) == 0 &&
	    sh_zfs_resumable(z, at("w"), &e) == 1);
	cell("Z15", "resumed: w@3 the source's",
	    sh_zfs_resume(z, at("w"), &e) == 0 && same("w@3", "s@3"));
	cell("Z15", "a cut incremental, its source destroyed after",
	    zc_fill(at("s")) == 0 && snap("s@4", NULL, NULL) == 0 &&
	    zc_cut(at("s@4"), at("s@3"), at("t@4"), NULL) == 0 &&
	    sh_zfs_destroy(z, at("s@4"), &e) == 0 &&
	    sh_zfs_resumable(z, at("t"), &e) == 1);
	refused("Z15", "its source gone: refused",
	    sh_zfs_resume(z, at("t"), &e), "s@4", "no such snapshot");
	cell("Z15", "taken away: nothing to resume, t at @3",
	    sh_zfs_abort(z, at("t"), &e) == 0 &&
	    sh_zfs_resumable(z, at("t"), &e) == 0 &&
	    strcmp(snaps("t"), "1 2 3") == 0);
	refused("Z15", "taken away again: refused",
	    sh_zfs_abort(z, at("t"), &e), "t", "no receive to resume");
	cell("Z15", "a cut whole taken away: what it made gone",
	    zc_cut(at("s@3"), NULL, at("x@3"), NULL) == 0 &&
	    sh_zfs_abort(z, at("x"), &e) == 0 &&
	    sh_zfs_resumable(z, at("x"), &e) == -1);
	hush();
	cell("Z15", "the next stream lands",
	    zc_fill(at("s")) == 0 && snap("s@5", NULL, NULL) == 0 &&
	    sh_zfs_send(z, at("s@5"), at("s@3"), at("t@5"), NULL, &e) == 0 &&
	    same("t@5", "s@5"));

	cell("Z12", "c a clone of s@5, a property from its birth",
	    sh_zfs_clone(z, at("s@5"), at("c"), cp, 2, &e) == 0 &&
	    origin_is("c", "s@5") &&
	    strcmp(user("c", "shtetl:zcheck"), "c") == 0);
	cell("Z12", "no clone: no origin",
	    sh_zfs_origin(z, at("s"), buf, sizeof (buf), &e) == 0);
	refused("Z12", "of no snapshot: refused",
	    sh_zfs_clone(z, at("s@9"), at("c2"), NULL, 0, &e), "s@9",
	    "no such snapshot");
	refused("Z12", "onto a name taken: refused",
	    sh_zfs_clone(z, at("s@5"), at("c"), NULL, 0, &e), "c", "exists");
	refused("Z12", "no parent: refused",
	    sh_zfs_clone(z, at("s@5"), at("q/c"), NULL, 0, &e), "q/c",
	    "no parent");
	refused("Z9", "a snapshot with a clone: refused",
	    sh_zfs_destroy(z, at("s@5"), &e), "s@5", "has clones");

	cell("Z14", "a clone stream: d a clone of t@5, its @x1 c@x1's",
	    sh_zfs_mount(z, at("c"), &e) == 0 && zc_fill(at("c")) == 0 &&
	    snap("c@x1", NULL, NULL) == 0 &&
	    sh_zfs_send(z, at("c@x1"), at("s@5"), at("d@x1"), at("t@5"),
	    &e) == 0 && origin_is("d", "t@5") && same("d@x1", "c@x1"));
	(void) snprintf(why, sizeof (why), "not %s's counterpart", at("s@5"));
	refused("Z14", "onto an origin no counterpart: refused",
	    sh_zfs_send(z, at("c@x1"), at("s@5"), at("v@x1"), at("t@3"), &e),
	    "t@3", why);

	cell("Z13", "c promoted: s's snapshots to @5 c's, s a clone of c@5",
	    sh_zfs_promote(z, at("c"), &e) == 0 &&
	    sh_zfs_origin(z, at("c"), buf, sizeof (buf), &e) == 0 &&
	    origin_is("s", "c@5") && strcmp(snaps("c"), "1 2 3 5 x1") == 0 &&
	    strcmp(snaps("s"), "") == 0);
	refused("Z13", "no clone: refused",
	    sh_zfs_promote(z, at("t"), &e), "t", "not a clone");
}

int
zcells(FILE *o, const char *t)
{
	out = o;
	top = t;
	failed = 0;
	hush();
	if (sh_zfs_open(&z, &e) != 0) {
		cell("Z0", "the layer opened", 0);
		return (1);
	}
	cells_create();
	cells_user();
	cells_local();
	cells_children();
	cells_snapshot();
	cells_snapshots();
	cells_rollback();
	cells_mount();
	cells_destroy();
	cells_set();
	cells_send();
	sh_zfs_close(z);
	(void) fclose(ef);
	free(ebuf);
	ef = NULL;
	ebuf = NULL;
	return (failed);
}
