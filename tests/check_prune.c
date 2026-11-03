/*
 * Family P of tests/MATRIX.md: the prune (src/core/prune.h) over the
 * fake. The spaces are made as the beam leaves them: g on the lake, its
 * run copy pulled and brought up to date by hand, house versions on the
 * run cloned from the copy, and their copies on the lake sent as clone
 * streams, each arrived when its top's snapshots hold shtetl:kind.
 */

#include <stdint.h>

#include "check.h"
#include "core/prune.h"
#include "core/spaces.h"
#include "core/zfs.h"
#include "zfake.h"

#define	LAKE	"tank/lake"
#define	RUN	"fast/run"
#define	LF	LAKE "/foundations"
#define	RF	RUN "/foundations"
#define	LC	LAKE "/containers"
#define	RC	RUN "/containers"
#define	RS	RUN "/systems"

static struct cap c;
static struct sh_zfs *z;
static FILE *logf;
static char *logbuf;
static size_t loglen, logmark;

/* The lake and the run, marked, their skeleton made; the log anew. */
static void
fresh(void)
{
	static const struct sh_zprop lake = { "shtetl:space", "lake" };
	static const struct sh_zprop run = { "shtetl:space", "run" };
	struct sh_spaces sp;

	zfake_reset();
	zfake_pool("tank");
	zfake_pool("fast");
	cap_reset(&c);
	CHECK(sh_zfs_create(z, LAKE, &lake, 1, &c.c_err) == 0);
	CHECK(sh_zfs_create(z, RUN, &run, 1, &c.c_err) == 0);
	CHECK(sh_spaces_find(z, &sp, &c.c_err) == 0);
	CHECK(sh_spaces_skeleton(z, &sp, &c.c_err) == 0);
	sh_spaces_free(&sp);
	(void) fflush(logf);
	logmark = loglen;
}

static int
is(const char *a, const char *b)
{
	return (strcmp(a, b) == 0);
}

static void
mk(const char *ds)
{
	CHECK(sh_zfs_create(z, ds, NULL, 0, &c.c_err) == 0);
}

/* Names, as a walk finds them. */
struct names {
	char	*v[64];
	size_t	n;
};

static int
name_add(void *arg, const char *name)
{
	struct names *n = arg;

	CHECK(n->n < 64);
	n->v[n->n++] = strdup(name);
	return (0);
}

static void
names_free(struct names *n)
{
	while (n->n > 0)
		free(n->v[--n->n]);
}

/* ROOT and every filesystem below it, each before its children. */
static void
tree(const char *root, struct names *n)
{
	size_t i;

	n->n = 0;
	(void) name_add(n, root);
	for (i = 0; i < n->n; i++)
		CHECK(sh_zfs_children(z, n->v[i], name_add, n, &c.c_err) == 0);
}

/* TOP's tree snapshotted at once as SNAP, shtetl:kind KIND. */
static void
take(const char *top, const char *snap, const char *kind)
{
	struct sh_zprop p = { "shtetl:kind", kind };
	char buf[64][256];
	const char *v[64];
	struct names t;
	size_t i;

	tree(top, &t);
	for (i = 0; i < t.n; i++) {
		(void) snprintf(buf[i], sizeof (buf[i]), "%s@%s", t.v[i], snap);
		v[i] = buf[i];
	}
	CHECK(sh_zfs_snapshot(z, v, t.n, &p, 1, &c.c_err) == 0);
	names_free(&t);
}

/* NAME's twin in the other space: RUN for LAKE, LAKE for RUN. */
static void
twin(const char *name, char *buf, size_t len)
{
	if (strncmp(name, RUN, strlen(RUN)) == 0)
		(void) snprintf(buf, len, "%s%s", LAKE, name + strlen(RUN));
	else
		(void) snprintf(buf, len, "%s%s", RUN, name + strlen(LAKE));
}

/*
 * What TOP's tree has that its twin lacks, sent, oldest first, as the
 * beam sends it: a clone stream where TOP's dataset is a clone, a full
 * stream where not, then incrementals. With ARRIVE, the twin's
 * snapshots given their owners' shtetl:kind, the top's last, and its
 * shtetl:house.
 */
static void
send(const char *top, int arrive)
{
	char to[300], from[300], org[300], tw[300], kind[64];
	struct names t, s;
	size_t i, k;
	uint64_t g, x;

	tree(top, &t);
	for (i = 0; i < t.n; i++) {
		twin(t.v[i], tw, sizeof (tw));
		s.n = 0;
		CHECK(sh_zfs_snapshots(z, t.v[i], name_add, &s, &c.c_err) ==
		    0);
		from[0] = '\0';
		if (sh_zfs_origin(z, t.v[i], from, sizeof (from), &c.c_err) ==
		    1)
			twin(from, org, sizeof (org));
		for (k = 0; k < s.n; k++) {
			(void) snprintf(to, sizeof (to), "%s@%s", tw, s.v[k]);
			if (sh_zfs_snapinfo(z, to, &g, &x, &c.c_err) != 0) {
				char snap[300];

				(void) snprintf(snap, sizeof (snap), "%s@%s",
				    t.v[i], s.v[k]);
				CHECK(sh_zfs_send(z, snap, from[0] != '\0' ?
				    from : NULL, to, k == 0 && from[0] !=
				    '\0' ? org : NULL, &c.c_err) == 0);
			}
			(void) snprintf(from, sizeof (from), "%s@%s", t.v[i],
			    s.v[k]);
		}
		cap_reset(&c);
		for (k = 0; arrive && k < s.n; k++) {
			(void) snprintf(from, sizeof (from), "%s@%s", t.v[i],
			    s.v[k]);
			(void) snprintf(to, sizeof (to), "%s@%s", tw, s.v[k]);
			if (sh_zfs_user(z, from, "shtetl:kind", kind,
			    sizeof (kind), &c.c_err) == 0)
				CHECK(sh_zfs_set(z, to, "shtetl:kind", kind,
				    &c.c_err) == 0);
		}
		if (arrive && i == 0 && sh_zfs_user(z, t.v[0], "shtetl:house",
		    kind, sizeof (kind), &c.c_err) == 0)
			CHECK(sh_zfs_set(z, tw, "shtetl:house", kind,
			    &c.c_err) == 0);
		names_free(&s);
	}
	names_free(&t);
	cap_reset(&c);
}

/* g on the lake, as a foundation roll makes it, its first version v1. */
static void
foundation(void)
{
	static const struct sh_zprop off = { "canmount", "off" };

	mk(LF "/g");
	CHECK(sh_zfs_create(z, LF "/g/usr", &off, 1, &c.c_err) == 0);
	mk(LF "/g/var");
	take(LF "/g", "v1", "create");
}

/*
 * The house HOUSE's version V on the run, cloned from g's copy at FV:
 * V and V/var the copy's, V/www the house's own, cloned from WWW where
 * a version came before; its update snapshot uN and a save sN.
 */
static void
version(const char *root, const char *v, const char *fv,
    const char *house, const char *www, int n)
{
	struct sh_zprop h = { "shtetl:house", house };
	char a[300], b[300];

	(void) snprintf(a, sizeof (a), RF "/g@%s", fv);
	(void) snprintf(b, sizeof (b), "%s/%s", root, v);
	CHECK(sh_zfs_clone(z, a, b, &h, 1, &c.c_err) == 0);
	(void) snprintf(a, sizeof (a), RF "/g/var@%s", fv);
	(void) snprintf(b, sizeof (b), "%s/%s/var", root, v);
	CHECK(sh_zfs_clone(z, a, b, NULL, 0, &c.c_err) == 0);
	(void) snprintf(b, sizeof (b), "%s/%s/www", root, v);
	if (www != NULL)
		CHECK(sh_zfs_clone(z, www, b, NULL, 0, &c.c_err) == 0);
	else
		mk(b);
	(void) snprintf(b, sizeof (b), "%s/%s", root, v);
	(void) snprintf(a, sizeof (a), "u%d", n);
	take(b, a, n == 1 ? "create" : "update");
	(void) snprintf(a, sizeof (a), "s%d", n);
	take(b, a, "save");
}

/*
 * g's versions v1 to vN on the lake and its copy on the run, pulled at
 * v1; the house h's versions V1 to VN on the run, each on its own, and
 * their copies on the lake, arrived.
 */
static void
chain(int n)
{
	char fv[8], v[8], www[64];
	int i;

	fresh();
	foundation();
	send(LF "/g", 0);
	for (i = 1; i <= n; i++) {
		(void) snprintf(fv, sizeof (fv), "v%d", i);
		if (i > 1) {
			take(LF "/g", fv, "update");
			send(LF "/g", 0);
		}
		(void) snprintf(v, sizeof (v), "V%d", i);
		(void) snprintf(www, sizeof (www), RC "/V%d/www@s%d", i - 1,
		    i - 1);
		version(RC, v, fv, "h", i > 1 ? www : NULL, i);
		(void) snprintf(www, sizeof (www), RC "/%s", v);
		send(www, 1);
	}
}

static int
there(const char *name)
{
	uint64_t g, x;
	struct names t;
	int r;

	if (strchr(name, '@') != NULL) {
		r = sh_zfs_snapinfo(z, name, &g, &x, &c.c_err) == 0;
	} else {
		t.n = 0;
		r = sh_zfs_children(z, name, name_add, &t, &c.c_err) == 0;
		names_free(&t);
	}
	cap_reset(&c);
	return (r);
}

static const char *
origin(const char *ds)
{
	static char buf[300];

	buf[0] = '\0';
	CHECK(sh_zfs_origin(z, ds, buf, sizeof (buf), &c.c_err) >= 0);
	return (buf);
}

static int
prune(int64_t run_keep, int64_t lake_keep)
{
	int r = sh_prune(z, LAKE, RUN, run_keep, lake_keep, logf, &c.c_err);

	(void) fflush(logf);
	return (r);
}

static int
logged(const char *line)
{
	char want[300];

	(void) snprintf(want, sizeof (want), "# prune %s\n", line);
	return (strstr(logbuf + logmark, want) != NULL);
}

int
main(void)
{
	cap_open(&c);
	CHECK((logf = open_memstream(&logbuf, &loglen)) != NULL);
	CHECK(sh_zfs_open(&z, &c.c_err) == 0);

	/*
	 * P1, P2, P3, P4: the run keeps 1; V3 the newest, V2's copy short
	 * of its newest snapshot, V1's arrived, V4 not complete
	 */
	chain(3);
	take(RC "/V2", "late", "save");
	CHECK(sh_zfs_clone(z, RF "/g@v3", RC "/V4", NULL, 0, &c.c_err) == 0);
	CHECK(sh_zfs_set(z, RC "/V4", "shtetl:house", "h", &c.c_err) == 0);
	CHECK(prune(1, 5) == 0 && is(cap_text(&c), ""));
	CHECK(!there(RC "/V1") && there(RC "/V2") && there(RC "/V3") &&
	    there(RC "/V4"));
	/* P4: V2's www promoted, the snapshots it was cloned from its own */
	CHECK(is(origin(RC "/V2/www"), "") && there(RC "/V2/www@s1") &&
	    there(RC "/V2/www@u1"));
	CHECK(logged(RC "/V1") && !logged(RC "/V2"));
	/* P6: the lake's copies within its keep, kept */
	CHECK(there(LC "/V1") && there(LC "/V2") && there(LC "/V3"));
	/* P9, P8: g's copy from v2 on, V2 the oldest on it; v1 gone */
	CHECK(!there(RF "/g@v1") && !there(RF "/g/var@v1") &&
	    there(RF "/g@v2") && logged(RF "/g@v1"));
	/* P13: the lake's v1, which LC/V1 is a clone of, kept */
	CHECK(there(LF "/g@v1") && there(LF "/g@v2"));

	/*
	 * P5, P16: the lake keeps 2; V1 beyond it and gone from the run:
	 * promoted, destroyed; v1 then nothing's clone and older than the
	 * run's newest: destroyed on every dataset
	 */
	CHECK(sh_zfs_destroy(z, RC "/V2@late", &c.c_err) == 0);
	CHECK(prune(1, 2) == 0 && is(cap_text(&c), ""));
	CHECK(!there(LC "/V1") && there(LC "/V2") && is(origin(LC "/V2/www"),
	    "") && there(LC "/V2/www@s1"));
	CHECK(!there(RC "/V2") && there(RC "/V3"));
	CHECK(!there(LF "/g@v1") && !there(LF "/g/var@v1") &&
	    !there(LF "/g/usr@v1") && logged(LF "/g@v1"));
	CHECK(there(LF "/g@v2") && there(LF "/g@v3"));

	/*
	 * P2: V1's copy holds its newest snapshot, its properties not yet
	 * made the owner's: not arrived, V1 kept
	 */
	chain(2);
	CHECK(sh_zfs_inherit(z, LC "/V1@s1", "shtetl:kind", &c.c_err) == 0);
	CHECK(prune(1, 5) == 0 && there(RC "/V1"));

	/* P6: a copy beyond the lake's keep the run still holds: kept */
	chain(3);
	take(RC "/V1", "late", "save");
	CHECK(prune(1, 2) == 0);
	CHECK(there(RC "/V1") && there(LC "/V1") && !there(RC "/V2") &&
	    there(LC "/V2"));

	/*
	 * P7: a copy not arrived is neither counted nor pruned: the run's
	 * V1 and V2 gone, LC/V1's kinds taken away, the lake keeping 2
	 */
	chain(3);
	CHECK(prune(1, 5) == 0 && !there(RC "/V1") && !there(RC "/V2"));
	CHECK(sh_zfs_inherit(z, LC "/V1@u1", "shtetl:kind", &c.c_err) == 0 &&
	    sh_zfs_inherit(z, LC "/V1@s1", "shtetl:kind", &c.c_err) == 0);
	CHECK(prune(1, 2) == 0);
	CHECK(there(LC "/V1") && there(LC "/V2") && there(LC "/V3"));

	/* P10, P19: k's one version on v1 keeps v1 and v2 on the run */
	chain(3);
	version(RC, "W1", "v1", "k", NULL, 1);
	send(RC "/W1", 1);
	CHECK(prune(1, 5) == 0);
	CHECK(!there(RC "/V1") && !there(RC "/V2") && there(RC "/V3") &&
	    there(RC "/W1"));
	CHECK(there(RF "/g@v1") && there(RF "/g@v2") && there(RF "/g@v3"));

	/*
	 * P11, P12, P16: no house: the run's copy destroyed whole; the
	 * lake's newest kept, the rest gone
	 */
	fresh();
	foundation();
	send(LF "/g", 0);
	take(LF "/g", "v2", "update");
	send(LF "/g", 0);
	CHECK(prune(2, 5) == 0 && is(cap_text(&c), ""));
	CHECK(!there(RF "/g") && logged(RF "/g"));
	CHECK(!there(LF "/g@v1") && there(LF "/g@v2") && there(LF "/g/var@v2"));
	/* P12: the newest alone, kept */
	CHECK(prune(2, 5) == 0 && there(LF "/g@v2"));

	/*
	 * P14: a house version on the run not yet on the lake keeps its
	 * clone stream's origin there
	 */
	fresh();
	foundation();
	send(LF "/g", 0);
	take(LF "/g", "v2", "update");
	send(LF "/g", 0);
	version(RC, "V1", "v1", "h", NULL, 1);
	CHECK(prune(2, 5) == 0);
	CHECK(there(LF "/g@v1") && there(RF "/g@v1"));

	/*
	 * P15: the run's copy behind: every lake version not older than
	 * its newest kept, the base of its next stream
	 */
	fresh();
	foundation();
	send(LF "/g", 0);
	version(RC, "V1", "v1", "h", NULL, 1);
	send(RC "/V1", 1);
	take(LF "/g", "v2", "update");
	take(LF "/g", "v3", "update");
	CHECK(prune(2, 5) == 0);
	CHECK(there(LF "/g@v1") && there(LF "/g@v2") && there(LF "/g@v3"));

	/*
	 * P17: the order by the origins' createtxg: Z, on v1, is older
	 * than A, on v2, though its name sorts after
	 */
	fresh();
	foundation();
	send(LF "/g", 0);
	take(LF "/g", "v2", "update");
	send(LF "/g", 0);
	version(RC, "Z", "v1", "h", NULL, 1);
	send(RC "/Z", 1);
	version(RC, "A", "v2", "h", RC "/Z/www@s1", 2);
	send(RC "/A", 1);
	CHECK(prune(1, 5) == 0);
	CHECK(!there(RC "/Z") && there(RC "/A"));

	/* P18: a system's versions, as a container's */
	fresh();
	foundation();
	send(LF "/g", 0);
	take(LF "/g", "v2", "update");
	send(LF "/g", 0);
	version(RS, "S1", "v1", "box", NULL, 1);
	send(RS "/S1", 1);
	version(RS, "S2", "v2", "box", RS "/S1/www@s1", 2);
	send(RS "/S2", 1);
	CHECK(prune(1, 5) == 0);
	CHECK(!there(RS "/S1") && there(RS "/S2") &&
	    there(LAKE "/systems/S1") && is(origin(RS "/S2/www"), ""));

	/*
	 * P20: a destroy refused part way: said, -1, what is left left;
	 * the next prune takes it up
	 */
	fresh();
	foundation();
	send(LF "/g", 0);
	take(LF "/g", "v2", "update");
	send(LF "/g", 0);
	zfake_fail("destroy");
	CHECK(prune(2, 5) == -1 && strstr(cap_text(&c), "destroy") != NULL);
	CHECK(there(RF "/g"));
	cap_reset(&c);
	CHECK(prune(2, 5) == 0 && !there(RF "/g") && !there(LF "/g@v1"));

	sh_zfs_close(z);
	(void) fclose(logf);
	free(logbuf);
	cap_close(&c);
	return (0);
}
