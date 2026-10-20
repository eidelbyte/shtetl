/*
 * The beam, src/core/beam.c: family B of tests/MATRIX.md, over the
 * fake, whose state the beam's forked streams share (Z18).
 *
 * The lake is tank/lake and the run fast/run, their skeleton made as
 * the daemon makes it. The foundation g is g, g/usr a placeholder,
 * g/usr/local and g/var; a house's versions are clones of its copy's.
 */

#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>

#include <stdint.h>
#include <time.h>

#include "check.h"
#include "core/beam.h"
#include "core/job.h"
#include "core/spaces.h"
#include "core/zfs.h"
#include "zfake.h"

#define	LAKE	"tank/lake"
#define	RUN	"fast/run"
#define	LF	LAKE "/foundations"
#define	RF	RUN "/foundations"
#define	LC	LAKE "/containers"
#define	RC	RUN "/containers"
#define	LS	LAKE "/systems"
#define	RS	RUN "/systems"

static struct cap c;
static struct sh_zfs *z;
static struct sh_beamenv env;
static struct sh_beam *b;
static FILE *logf;
static char *logbuf;
static size_t loglen, logmark;
static int timers;		/* the timer's events handed to the beam */
static struct timespec fired;	/* when the last was */

/* The log from here on is what since() reads. */
static void
mark(void)
{
	(void) fflush(logf);
	logmark = loglen;
}

static const char *
since(void)
{
	(void) fflush(logf);
	return (logbuf != NULL ? logbuf + logmark : "");
}

/* How many times S is in the log since the mark. */
static int
count(const char *s)
{
	const char *p = since();
	int n = 0;

	while ((p = strstr(p, s)) != NULL) {
		n++;
		p++;
	}
	return (n);
}

/* Whether A and B are in the log since the mark, A first. */
static int
before(const char *a, const char *b2)
{
	const char *pa = strstr(since(), a), *pb = strstr(since(), b2);

	return (pa != NULL && pb != NULL && pa < pb);
}

static int
is(const char *a, const char *b2)
{
	return (a != NULL && strcmp(a, b2) == 0);
}

/* One event of the kqueue handed on, within MS: 1, or 0 for none. */
static int
one(int ms)
{
	struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
	struct kevent ev;

	if (kevent(env.be_kq, NULL, 0, &ev, 1, &ts) != 1)
		return (0);
	if (*(enum sh_ev *)ev.udata == SH_EV_BEAM) {
		timers++;
		(void) clock_gettime(CLOCK_MONOTONIC, &fired);
		sh_beam_timer(ev.udata);
	} else {
		sh_job_event(ev.udata, env.be_kq, &ev);
	}
	return (1);
}

static const char *
status(void)
{
	static char *buf;
	size_t len = 0;
	FILE *f;

	free(buf);
	buf = NULL;
	CHECK((f = open_memstream(&buf, &len)) != NULL);
	sh_beam_status(b, f);
	(void) fclose(f);
	return (buf);
}

/* The convergence under way, if any, run to its end: its status. */
static const char *
settle(void)
{
	while (strncmp(status(), "beam converging", 15) == 0)
		CHECK(one(20000));
	return (status());
}

static void
begin(void)
{
	b = sh_beam_start(&env);
	(void) settle();
}

static const char *
again(void)
{
	sh_beam_converge(b);
	return (settle());
}

/* The lake and the run, marked, their skeleton made; the log marked. */
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
	mark();
}

static void
mk(const char *ds)
{
	CHECK(sh_zfs_create(z, ds, NULL, 0, &c.c_err) == 0);
}

static void
clone(const char *origin, const char *ds)
{
	CHECK(sh_zfs_clone(z, origin, ds, NULL, 0, &c.c_err) == 0);
}

/* Names, as a walk finds them. */
struct names {
	char	*v[128];
	size_t	n;
};

static int
name_add(void *arg, const char *name)
{
	struct names *n = arg;

	CHECK(n->n < 128);
	n->v[n->n++] = strdup(name);
	return (0);
}

static void
names_free(struct names *n)
{
	while (n->n > 0)
		free(n->v[--n->n]);
}

/* ROOT and every filesystem below it into N, each before its children. */
static void
tree(const char *root, struct names *n)
{
	size_t i;

	n->n = 0;
	(void) name_add(n, root);
	for (i = 0; i < n->n; i++)
		CHECK(sh_zfs_children(z, n->v[i], name_add, n, &c.c_err) == 0);
}

/* Every filesystem of TREE's snapshotted at once as SNAP, kind KIND. */
static void
take(const char *top, const char *snap, const char *kind)
{
	struct sh_zprop p[2] = {
		{ "shtetl:kind", kind }, { "shtetl:commit", snap }
	};
	struct names t;
	char buf[128][256];
	const char *v[128];
	size_t i;

	tree(top, &t);
	for (i = 0; i < t.n; i++) {
		(void) snprintf(buf[i], sizeof (buf[i]), "%s@%s", t.v[i], snap);
		v[i] = buf[i];
	}
	CHECK(sh_zfs_snapshot(z, v, t.n, p, 2, &c.c_err) == 0);
	names_free(&t);
}

/* TOP's tree destroyed, its snapshots first, children before parents. */
static void
destroy(const char *top)
{
	struct names t, s;
	char name[512];
	size_t i, k;

	tree(top, &t);
	for (i = t.n; i > 0; i--) {
		s.n = 0;
		CHECK(sh_zfs_snapshots(z, t.v[i - 1], name_add, &s, &c.c_err) ==
		    0);
		for (k = 0; k < s.n; k++) {
			(void) snprintf(name, sizeof (name), "%s@%s",
			    t.v[i - 1], s.v[k]);
			CHECK(sh_zfs_destroy(z, name, &c.c_err) == 0);
		}
		names_free(&s);
		CHECK(sh_zfs_destroy(z, t.v[i - 1], &c.c_err) == 0);
	}
	names_free(&t);
}

/* The snapshot SNAP of every filesystem of TOP's that has it destroyed. */
static void
unsnap(const char *top, const char *snap)
{
	struct names t;
	char name[512];
	uint64_t g, x;
	size_t i;

	tree(top, &t);
	for (i = 0; i < t.n; i++) {
		(void) snprintf(name, sizeof (name), "%s@%s", t.v[i], snap);
		if (sh_zfs_snapinfo(z, name, &g, &x, &c.c_err) == 0)
			CHECK(sh_zfs_destroy(z, name, &c.c_err) == 0);
	}
	names_free(&t);
	cap_reset(&c);
}

static uint64_t
guid(const char *snap)
{
	uint64_t g = 0, x;

	return (sh_zfs_snapinfo(z, snap, &g, &x, &c.c_err) == 0 ? g : 0);
}

/* Whether the snapshots A and B are there and share a guid. */
static int
same(const char *a, const char *b2)
{
	return (guid(a) != 0 && guid(a) == guid(b2));
}

/* Whether NAME, a filesystem or a snapshot, is there. */
static int
there(const char *name)
{
	int r = strchr(name, '@') != NULL ? guid(name) != 0 :
	    sh_zfs_resumable(z, name, &c.c_err) >= 0;

	cap_reset(&c);
	return (r);
}

/* The snapshot DS is a clone of, or "". */
static const char *
origin(const char *ds)
{
	static char buf[512];

	if (sh_zfs_origin(z, ds, buf, sizeof (buf), &c.c_err) != 1)
		buf[0] = '\0';
	return (buf);
}

/* The user property PROP of NAME, or "(unset)". */
static const char *
user(const char *name, const char *prop)
{
	static char buf[4][256];
	static int n;

	n = (n + 1) % 4;
	if (sh_zfs_user(z, name, prop, buf[n], sizeof (buf[n]), &c.c_err) != 0)
		return ("(unset)");
	return (buf[n]);
}

/* Every snapshot's guid below ROOT into G. */
static size_t
guids(const char *root, uint64_t *g)
{
	struct names t, s;
	char name[512];
	size_t i, k, n = 0;

	tree(root, &t);
	for (i = 0; i < t.n; i++) {
		s.n = 0;
		CHECK(sh_zfs_snapshots(z, t.v[i], name_add, &s, &c.c_err) ==
		    0);
		for (k = 0; k < s.n; k++) {
			(void) snprintf(name, sizeof (name), "%s@%s", t.v[i],
			    s.v[k]);
			CHECK(n < 512);
			g[n++] = guid(name);
		}
		names_free(&s);
	}
	names_free(&t);
	return (n);
}

/* Whether the space RECV holds every snapshot below OWN, by guid. */
static int
mirrored(const char *own, const char *recv)
{
	uint64_t want[512], have[512];
	size_t nw = guids(own, want), nh = guids(recv, have), i, k;

	for (i = 0; i < nw; i++) {
		for (k = 0; k < nh && have[k] != want[i]; k++)
			;
		if (k == nh)
			return (0);
	}
	return (nw > 0);
}

static void
done(void)
{
	sh_beam_stop(b);
	b = NULL;
}

/* g on the lake, as a foundation roll makes it. */
static void
foundation(void)
{
	static const struct sh_zprop off = { "canmount", "off" };

	mk(LF "/g");
	CHECK(sh_zfs_create(z, LF "/g/usr", &off, 1, &c.c_err) == 0);
	mk(LF "/g/usr/local");
	mk(LF "/g/var");
}

/*
 * The house's first version on the run, V1, from the foundation's v1:
 * V1 and V1/var clones of its copy's, V1/www the house's own; created,
 * then saved.
 */
static void
house1(void)
{
	clone(RF "/g@v1", RC "/V1");
	clone(RF "/g/var@v1", RC "/V1/var");
	mk(RC "/V1/www");
	take(RC "/V1", "c", "create");
	take(RC "/V1", "s1", "save");
}

/* V2, from v2; its www cloned from V1's final save. */
static void
house2(void)
{
	clone(RF "/g@v2", RC "/V2");
	clone(RF "/g/var@v2", RC "/V2/var");
	clone(RC "/V1/www@s1", RC "/V2/www");
	take(RC "/V2", "u", "update");
}

/* The foundation g with versions v1 and v2, beamed. */
static void
beamed(void)
{
	fresh();
	foundation();
	take(LF "/g", "v1", "create");
	take(LF "/g", "v2", "update");
	begin();
	CHECK(is(status(), "beam converged\n"));
}

/*
 * B12: a convergence cut at its Kth stream, for each K of its N: it
 * fails with the stream's words, and the next resumes the receive and
 * goes on to the end. SETUP makes the fixture up to the convergence.
 */
static void
cuts(void (*setup)(void), int n, const char *own, const char *recv,
    int arrivals)
{
	int k;

	for (k = 1; k <= n; k++) {
		setup();
		mark();
		zfake_cut_nth(k);
		CHECK(strstr(again(), "the stream broken off") != NULL);
		CHECK(strstr(again(), "beam converged") != NULL);
		CHECK(count("# resume ") == 1 && count("# failed: ") == 1);
		CHECK(mirrored(own, recv) && count("# arrived ") == arrivals);
		done();
	}
}

/* The fixtures B12 cuts. */
static void
first_v1(void)
{
	fresh();
	foundation();
	begin();
	take(LF "/g", "v1", "create");
}

static void
later(void)
{
	beamed();
	take(LF "/g", "v3", "update");
	take(LF "/g", "v4", "update");
}

static void
newer_dataset(void)
{
	beamed();
	mk(LF "/g/new");
	take(LF "/g", "v3", "update");
}

static void
h1(void)
{
	beamed();
	house1();
}

static void
h2(void)
{
	beamed();
	house1();
	CHECK(is(again(), "beam converged\n"));
	house2();
}

static void
h2promoted(void)
{
	h2();
	CHECK(sh_zfs_promote(z, RC "/V2/www", &c.c_err) == 0);
}

int
main(void)
{
	struct timespec t0;
	long ms;
	int i;

	cap_open(&c);
	CHECK((logf = open_memstream(&logbuf, &loglen)) != NULL);
	CHECK(sh_zfs_open(&z, &c.c_err) == 0);
	CHECK((env.be_kq = kqueue()) >= 0);
	env.be_z = z;
	env.be_lake = LAKE;
	env.be_run = RUN;
	env.be_log = logf;
	env.be_period = 60000;
	env.be_retry = 60000;

	/* B1, B23, B24: a foundation's first version, full streams */
	fresh();
	foundation();
	take(LF "/g", "v1", "create");
	b = sh_beam_start(&env);
	CHECK(is(status(), "beam converging send " LF "/g@v1\n"));
	CHECK(is(settle(), "beam converged\n"));
	CHECK(same(LF "/g@v1", RF "/g@v1") &&
	    same(LF "/g/usr@v1", RF "/g/usr@v1") &&
	    same(LF "/g/usr/local@v1", RF "/g/usr/local@v1") &&
	    same(LF "/g/var@v1", RF "/g/var@v1"));
	CHECK(is(origin(RF "/g"), "") && is(origin(RF "/g/var"), ""));
	CHECK(is(user(RF "/g/var@v1", "shtetl:kind"), "create") &&
	    is(user(RF "/g@v1", "shtetl:commit"), "v1"));
	CHECK(before("# send " LF "/g@v1\n", "# send " LF "/g/usr@v1\n") &&
	    before("# send " LF "/g/usr@v1\n",
	    "# send " LF "/g/usr/local@v1\n"));
	CHECK(count("# arrived " RF "/g@v1\n") == 1 &&
	    count("# arrived ") == 1);

	/* B3, B17: up to date: nothing sent, said, or arrived again */
	mark();
	CHECK(is(again(), "beam converged\n") && is(since(), ""));

	/* B2: later versions, oldest first, incrementals */
	take(LF "/g", "v2", "update");
	take(LF "/g", "v3", "update");
	mark();
	CHECK(is(again(), "beam converged\n"));
	CHECK(same(LF "/g@v3", RF "/g@v3") &&
	    same(LF "/g/usr/local@v2", RF "/g/usr/local@v2"));
	CHECK(before("# send " LF "/g@v2\n", "# send " LF "/g@v3\n"));
	CHECK(before("# arrived " RF "/g@v2\n", "# arrived " RF "/g@v3\n") &&
	    count("# arrived ") == 2);

	/* B4: a dataset new in a version: a full stream of it */
	mk(LF "/g/new");
	take(LF "/g", "v4", "update");
	mark();
	CHECK(is(again(), "beam converged\n"));
	CHECK(same(LF "/g/new@v4", RF "/g/new@v4") &&
	    same(LF "/g@v4", RF "/g@v4") && is(origin(RF "/g/new"), ""));
	CHECK(count("# arrived " RF "/g@v4\n") == 1);

	/* B15: a filesystem's shtetl:* made the owner's, the rest left */
	CHECK(sh_zfs_set(z, LF "/g", "shtetl:stall", "patches: x",
	    &c.c_err) == 0);
	CHECK(sh_zfs_set(z, RF "/g", "shtetl:x", "y", &c.c_err) == 0);
	CHECK(sh_zfs_set(z, RF "/g", "org.example:note", "kept",
	    &c.c_err) == 0);
	CHECK(is(again(), "beam converged\n"));
	CHECK(is(zfake_local(RF "/g", "shtetl:stall"), "patches: x") &&
	    zfake_local(RF "/g", "shtetl:x") == NULL &&
	    is(zfake_local(RF "/g", "org.example:note"), "kept"));
	CHECK(sh_zfs_inherit(z, LF "/g", "shtetl:stall", &c.c_err) == 0);
	CHECK(is(again(), "beam converged\n"));
	CHECK(zfake_local(RF "/g", "shtetl:stall") == NULL);

	/* B11: a snapshot pruned on the copy, older than its newest */
	unsnap(RF "/g", "v1");
	mark();
	CHECK(is(again(), "beam converged\n") && is(since(), "") &&
	    !there(RF "/g@v1"));

	/* B16: a version cut after its top's stream: not arrived */
	take(LF "/g", "v5", "update");
	zfake_cut_nth(2);
	mark();
	CHECK(is(again(), "beam failing send " LF "/g/usr@v5: zfake: "
	    RF "/g/usr@v5: the stream broken off\n"));
	CHECK(count("# failed: send " LF "/g/usr@v5: zfake: ") == 1 &&
	    count("# arrived ") == 0);
	CHECK(there(RF "/g@v5") && is(user(RF "/g@v5", "shtetl:kind"),
	    "(unset)") && sh_zfs_resumable(z, RF "/g/usr", &c.c_err) == 1);
	CHECK(is(again(), "beam converged\n"));
	CHECK(before("# resume " RF "/g/usr\n", "# send " LF "/g/var@v5\n"));
	CHECK(before("# send " LF "/g/usr/local@v5\n",
	    "# arrived " RF "/g@v5\n"));
	CHECK(is(user(RF "/g@v5", "shtetl:kind"), "update") &&
	    count("# arrived " RF "/g@v5\n") == 1);

	/* B16: a version's properties failing part way: its top's last */
	take(LF "/g", "v8", "update");
	zfake_fail_nth("set", 2);
	CHECK(strncmp(again(), "beam failing look: zfake: set failed", 36) ==
	    0);
	CHECK(is(user(RF "/g@v8", "shtetl:kind"), "(unset)") &&
	    is(user(RF "/g/usr/local@v8", "shtetl:kind"), "update"));
	mark();
	CHECK(is(again(), "beam converged\n") &&
	    count("# arrived " RF "/g@v8\n") == 1);

	/* B13: a receive to resume whose stream's snapshot is gone */
	take(LF "/g", "v6", "update");
	zfake_cut();
	CHECK(strstr(again(), "the stream broken off") != NULL);
	unsnap(LF "/g", "v6");
	CHECK(is(again(), "beam failing resume " RF "/g: zfake: " LF
	    "/g@v6: no such snapshot\n"));
	CHECK(sh_zfs_resumable(z, RF "/g", &c.c_err) == 1);
	/* the person's receive -A: then it converges */
	CHECK(sh_zfs_abort(z, RF "/g", &c.c_err) == 0);
	CHECK(is(again(), "beam converged\n"));

	/* B14: a copy with a snapshot of its own: refused, nothing more */
	{
		const char *mine[] = { RF "/g@mine" };

		CHECK(sh_zfs_snapshot(z, mine, 1, NULL, 0, &c.c_err) == 0);
	}
	take(LF "/g", "v7", "update");
	CHECK(is(again(), "beam failing send " LF "/g@v7: zfake: " RF
	    "/g: newest snapshot is not " LF "/g@v8's counterpart\n"));
	CHECK(!there(RF "/g/var@v7") && there(RF "/g@mine"));
	CHECK(sh_zfs_destroy(z, RF "/g@mine", &c.c_err) == 0);
	CHECK(is(again(), "beam converged\n") &&
	    same(LF "/g/var@v7", RF "/g/var@v7"));
	done();

	/* B12: a cut at each stream of B1, B2 and B4 */
	cuts(first_v1, 4, LF, RUN, 1);
	cuts(later, 8, LF, RUN, 2);
	cuts(newer_dataset, 5, LF, RUN, 1);

	/* B5: a house version: clone streams onto the lake's foundation */
	h1();
	mark();
	CHECK(is(again(), "beam converged\n"));
	CHECK(is(origin(LC "/V1"), LF "/g@v1") &&
	    is(origin(LC "/V1/var"), LF "/g/var@v1") &&
	    is(origin(LC "/V1/www"), ""));
	CHECK(same(RC "/V1@s1", LC "/V1@s1") &&
	    same(RC "/V1/www@c", LC "/V1/www@c") &&
	    is(user(LC "/V1/www@s1", "shtetl:kind"), "save"));
	CHECK(count("# arrived " LC "/V1@c\n") == 1 &&
	    count("# arrived " LC "/V1@s1\n") == 1);

	/* B6: its own dataset cloned from the version before's */
	house2();
	CHECK(is(again(), "beam converged\n"));
	CHECK(is(origin(LC "/V2/www"), LC "/V1/www@s1") &&
	    is(origin(LC "/V2"), LF "/g@v2") &&
	    same(RC "/V2/www@u", LC "/V2/www@u"));

	/* B8: a promote on the run after the beam: nothing again */
	CHECK(sh_zfs_promote(z, RC "/V2/www", &c.c_err) == 0);
	mark();
	CHECK(is(again(), "beam converged\n") && count("# send ") == 0);
	take(RC "/V2", "s2", "save");
	CHECK(is(again(), "beam converged\n") &&
	    same(RC "/V2/www@s2", LC "/V2/www@s2"));

	/* B9: the lake's copy promoted: nothing again; the next lands */
	CHECK(sh_zfs_promote(z, LC "/V2/www", &c.c_err) == 0);
	mark();
	CHECK(is(again(), "beam converged\n") && count("# send ") == 0);
	take(RC "/V2", "s3", "save");
	CHECK(is(again(), "beam converged\n") &&
	    same(RC "/V2/www@s3", LC "/V2/www@s3"));

	/* B10: the old version destroyed on the run after the beam */
	destroy(RC "/V1");
	mark();
	CHECK(is(again(), "beam converged\n") && count("# send ") == 0 &&
	    there(LC "/V1@c") && there(LC "/V1/var@s1"));

	/* B26: both pairs lacking: the foundation's streams first */
	take(LF "/g", "v3", "update");
	take(RC "/V2", "s4", "save");
	mark();
	CHECK(is(again(), "beam converged\n") &&
	    before("# send " LF "/g@v3\n", "# send " RC "/V2@s4\n"));
	done();

	/* B7, B10: promoted, the old version destroyed, before the beam */
	h2promoted();
	destroy(RC "/V1");
	mark();
	CHECK(is(again(), "beam converged\n"));
	CHECK(is(origin(LC "/V2/www"), LC "/V1/www@s1") &&
	    same(RC "/V2/www@u", LC "/V2/www@u"));
	CHECK(count("# send " RC "/V2/www@u\n") == 1 &&
	    count("# send " RC "/V2/www@s1\n") == 0 &&
	    count("# send " RC "/V2/www@c\n") == 0 && there(LC "/V1@s1"));
	done();

	/* B29: a clone the walk finds before its origin's dataset */
	h1();
	clone(RC "/V1/www@s1", RC "/W");
	take(RC "/W", "c", "create");
	CHECK(is(again(), "beam converged\n"));
	CHECK(is(origin(LC "/W"), LC "/V1/www@s1") &&
	    same(RC "/W@c", LC "/W@c"));
	done();

	/* B12: a cut at each stream of B5, B6 and B7 */
	cuts(h1, 6, RC, LAKE, 2);
	cuts(h2, 3, RC, LAKE, 1);
	cuts(h2promoted, 3, RC, LAKE, 1);

	/* B25: a receive to resume outside the beam's roots: left */
	beamed();
	zfake_cut();
	CHECK(sh_zfs_send(z, LF "/g@v1", NULL, RUN "/elsewhere@v1", NULL,
	    &c.c_err) == -1);
	CHECK(sh_zfs_resumable(z, RUN "/elsewhere", &c.c_err) == 1);
	mark();
	CHECK(is(again(), "beam converged\n") && is(since(), ""));
	done();

	/* B27: a system version, as a house's */
	beamed();
	clone(RF "/g@v2", RS "/S1");
	take(RS "/S1", "c", "create");
	CHECK(is(again(), "beam converged\n"));
	CHECK(is(origin(LS "/S1"), LF "/g@v2") &&
	    same(RS "/S1@c", LS "/S1@c"));
	done();

	/* B18: told while converging: once more at its end */
	fresh();
	foundation();
	take(LF "/g", "v1", "create");
	zfake_fail("send");
	mark();
	b = sh_beam_start(&env);
	sh_beam_converge(b);
	CHECK(is(settle(), "beam converged\n") && same(LF "/g@v1", RF "/g@v1"));
	CHECK(count("# failed: send " LF "/g@v1: zfake: send failed as "
	    "asked\n") == 1);
	done();

	/* B19: the timer, after the period; told meanwhile, at once */
	env.be_period = 300;
	env.be_retry = 100;
	beamed();
	(void) clock_gettime(CLOCK_MONOTONIC, &t0);
	take(LF "/g", "v3", "update");
	timers = 0;
	while (timers == 0)
		CHECK(one(5000));
	ms = (fired.tv_sec - t0.tv_sec) * 1000 +
	    (fired.tv_nsec - t0.tv_nsec) / 1000000;
	CHECK(ms >= 290 && is(settle(), "beam converged\n") &&
	    same(LF "/g@v3", RF "/g@v3"));
	take(LF "/g", "v4", "update");
	CHECK(is(again(), "beam converged\n") && timers == 1 &&
	    same(LF "/g@v4", RF "/g@v4"));
	done();

	/*
	 * B20, B21: the run's pool gone: failing, the retry doubled each
	 * time to the period; back, converged, and the period again
	 */
	env.be_retry = 50;
	env.be_period = 400;
	fresh();
	foundation();
	take(LF "/g", "v1", "create");
	zfake_export("fast");
	(void) clock_gettime(CLOCK_MONOTONIC, &t0);
	b = sh_beam_start(&env);
	CHECK(is(status(), "beam failing look: zfake: " RUN
	    ": no such filesystem\n"));
	{
		static const long least[] = { 50, 100, 200, 400, 400 };

		for (i = 0, timers = 0; i < 5; i++) {
			while (timers == i)
				CHECK(one(5000));
			ms = (fired.tv_sec - t0.tv_sec) * 1000 +
			    (fired.tv_nsec - t0.tv_nsec) / 1000000;
			CHECK(ms >= least[i] - 10 && (i < 4 || ms < 800));
			t0 = fired;
			CHECK(strncmp(status(), "beam failing look: ", 19) ==
			    0);
		}
	}
	zfake_import();
	while (timers == 5)
		CHECK(one(5000));
	CHECK(is(settle(), "beam converged\n") && same(LF "/g@v1", RF "/g@v1"));
	(void) clock_gettime(CLOCK_MONOTONIC, &t0);
	while (timers == 6)
		CHECK(one(5000));
	ms = (fired.tv_sec - t0.tv_sec) * 1000 +
	    (fired.tv_nsec - t0.tv_nsec) / 1000000;
	CHECK(ms >= 390);
	(void) settle();
	done();
	env.be_period = 60000;
	env.be_retry = 60000;

	/* B22: stopped while converging: nothing ended; begun again */
	fresh();
	foundation();
	take(LF "/g", "v1", "create");
	mark();
	b = sh_beam_start(&env);
	CHECK(strncmp(status(), "beam converging send ", 21) == 0);
	done();
	CHECK(count("# failed") == 0);
	/* what the stopped job left queued names it: the kqueue anew */
	(void) close(env.be_kq);
	CHECK((env.be_kq = kqueue()) >= 0);
	begin();
	CHECK(is(status(), "beam converged\n") && mirrored(LF, RUN));
	done();

	(void) close(env.be_kq);
	sh_zfs_close(z);
	(void) fclose(logf);
	free(logbuf);
	cap_close(&c);
	return (0);
}
