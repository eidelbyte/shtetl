/*
 * The beam: its looks, its passes of streams, and its timer.
 *
 * A look reads each space whole, every filesystem below its root with
 * its origin, whether a receive stands to resume on it, and its
 * snapshots with their guids, oldest first. The streams it plans are
 * added to the receiving space as the copies they will make, each
 * marked with the stream that makes it, so the next dataset's plan
 * finds them there and knows what it needs: a stream needs the one
 * that lands its base, and, for a copy not there yet, the one that
 * makes its parent.
 *
 * A pass runs the look's streams one at a time, in the order planned,
 * each a job of one forked call. One that fails is said; one that
 * needs a stream that failed is not run; neither holds up the rest. A
 * look follows each pass, planning nothing for a top whose stream
 * failed in this convergence, so the convergence ends, with the first
 * look that plans nothing.
 */

#include <sys/types.h>
#include <sys/event.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/beam.h"
#include "core/err.h"
#include "core/job.h"
#include "core/mem.h"
#include "core/zfs.h"

struct bstream;

/* A snapshot of a space: its name, ds@snap, and its guid. */
struct bsnap {
	char		*bs_name;
	uint64_t	bs_guid;
	struct bstream	*bs_by;		/* the stream landing it, or NULL */
};

/* A filesystem of a space, as a look reads it. */
struct bfs {
	char		*bf_name;
	char		*bf_origin;	/* the snapshot it is a clone of */
	int		bf_token;	/* a receive to resume on it */
	struct bsnap	*bf_snaps;	/* oldest first */
	size_t		bf_n;
	int		bf_done;	/* its streams planned */
	struct bstream	*bf_by;		/* the stream making it, or NULL */
};

/* A space's filesystems, each before its children. */
struct bspace {
	struct bfs	*sp_fs;
	size_t		sp_n;
};

/*
 * A stream of a pass: SNAP sent from FROM as TO, a clone of ORIGIN;
 * with SNAP NULL, the receive on TO resumed. KEY is its top's.
 */
struct bstream {
	struct sh_beam	*bt_beam;
	char		*bt_key;
	char		*bt_snap;
	char		*bt_from;
	char		*bt_to;
	char		*bt_origin;
	struct bstream	*bt_need[2];	/* the streams it needs landed */
	int		bt_failed;	/* or one it needs did */
	struct bstream	*bt_next;	/* the pass's, in order */
};

/* A convergence's failures: each one's top, and why. */
struct bfails {
	char	**bl_key;
	char	**bl_why;
	size_t	bl_n;
};

struct sh_beam {
	enum sh_ev		b_ev;		/* SH_EV_BEAM, first: udata */
	const struct sh_beamenv	*b_env;
	struct sh_job		b_job;		/* the stream under way's */
	int			b_busy;		/* converging */
	int			b_again;	/* told while converging */
	struct bfails		b_now;		/* this convergence's */
	struct bfails		b_last;		/* the last one's */
	long			b_wait;		/* ms to the next */
	struct bstream		*b_pass;
	struct bstream		**b_tail;
	struct bstream		*b_cur;		/* the stream under way */
	struct sh_err		b_lerr;		/* what it says, in its log */
};

/* The roots below both spaces' roots, the owner's space named. */
static const struct {
	int		bp_lake;	/* the lake owns what is below it */
	const char	*bp_name;
} pairs[] = {
	{ 1, "foundations" },
	{ 0, "containers" },
	{ 0, "systems" }
};

#define	NPAIRS	(sizeof (pairs) / sizeof (pairs[0]))

/* A, B and C, made one. */
static char *
join(const char *a, const char *b, const char *c)
{
	size_t n = strlen(a) + strlen(b) + strlen(c) + 1;
	char *s = sh_calloc(1, n);

	(void) snprintf(s, n, "%s%s%s", a, b, c);
	return (s);
}

static char *
dup0(const char *s)
{
	return (s != NULL ? sh_strdup(s) : NULL);
}

/* Whether NAME is below ROOT. */
static int
below(const char *name, const char *root)
{
	size_t n = strlen(root);

	return (strncmp(name, root, n) == 0 && name[n] == '/');
}

/* Whether S is one of the N of V. */
static int
listed(char *const *v, size_t n, const char *s)
{
	while (n > 0) {
		if (strcmp(v[--n], s) == 0)
			return (1);
	}
	return (0);
}

/*
 * The top NAME is or is in below ROOT, the root's child, keyed by the
 * root's last word, PAIR: "foundations/g".
 */
static char *
topkey(const char *pair, const char *name, const char *root)
{
	const char *rel = name + strlen(root) + 1;
	int n = (int)strcspn(rel, "/@");
	size_t len = strlen(pair) + (size_t)n + 2;
	char *s = sh_calloc(1, len);

	(void) snprintf(s, len, "%s/%.*s", pair, n, rel);
	return (s);
}

static int
fs_add(void *arg, const char *name)
{
	struct bspace *sp = arg;

	sp->sp_fs = sh_realloc(sp->sp_fs, (sp->sp_n + 1) * sizeof (struct bfs));
	(void) memset(&sp->sp_fs[sp->sp_n], 0, sizeof (struct bfs));
	sp->sp_fs[sp->sp_n++].bf_name = sh_strdup(name);
	return (0);
}

/* The snapshot NAME, of guid GUID, added to F, landed by BY. */
static void
snap_put(struct bfs *f, char *name, uint64_t guid, struct bstream *by)
{
	f->bf_snaps = sh_realloc(f->bf_snaps, (f->bf_n + 1) *
	    sizeof (struct bsnap));
	f->bf_snaps[f->bf_n].bs_name = name;
	f->bf_snaps[f->bf_n].bs_guid = guid;
	f->bf_snaps[f->bf_n++].bs_by = by;
}

static int
snap_add(void *arg, const char *snap)
{
	struct bfs *f = arg;

	snap_put(f, join(f->bf_name, "@", snap), 0, NULL);
	return (0);
}

static void
space_free(struct bspace *sp)
{
	struct bfs *f;
	size_t i, k;

	for (i = 0; i < sp->sp_n; i++) {
		f = &sp->sp_fs[i];
		for (k = 0; k < f->bf_n; k++)
			free(f->bf_snaps[k].bs_name);
		free(f->bf_snaps);
		free(f->bf_name);
		free(f->bf_origin);
	}
	free(sp->sp_fs);
}

/* The space below ROOT read into SP. */
static int
space_read(struct sh_zfs *z, const char *root, struct bspace *sp,
    struct sh_err *e)
{
	char origin[512];
	struct bfs *f;
	uint64_t txg;
	size_t i, k;
	int r;

	(void) fs_add(sp, root);
	for (i = 0; i < sp->sp_n; i++) {
		/* the walk may move the array, not the name */
		if (sh_zfs_children(z, sp->sp_fs[i].bf_name, fs_add, sp,
		    e) != 0)
			return (-1);
		f = &sp->sp_fs[i];
		if ((r = sh_zfs_origin(z, f->bf_name, origin, sizeof (origin),
		    e)) < 0 ||
		    (f->bf_token = sh_zfs_resumable(z, f->bf_name, e)) < 0 ||
		    sh_zfs_snapshots(z, f->bf_name, snap_add, f, e) != 0)
			return (-1);
		if (r == 1)
			f->bf_origin = sh_strdup(origin);
		for (k = 0; k < f->bf_n; k++) {
			if (sh_zfs_snapinfo(z, f->bf_snaps[k].bs_name,
			    &f->bf_snaps[k].bs_guid, &txg, e) != 0)
				return (-1);
		}
	}
	return (0);
}

/* The filesystem of SP named NAME, or the one of the snapshot NAME. */
static struct bfs *
fs_of(const struct bspace *sp, const char *name)
{
	size_t i, n = strcspn(name, "@");

	for (i = 0; i < sp->sp_n; i++) {
		if (strncmp(sp->sp_fs[i].bf_name, name, n) == 0 &&
		    sp->sp_fs[i].bf_name[n] == '\0')
			return (&sp->sp_fs[i]);
	}
	return (NULL);
}

/* The snapshot of SP whose guid is GUID, or NULL. */
static const struct bsnap *
held(const struct bspace *sp, uint64_t guid)
{
	size_t i, k;

	for (i = 0; i < sp->sp_n; i++) {
		for (k = 0; k < sp->sp_fs[i].bf_n; k++) {
			if (sp->sp_fs[i].bf_snaps[k].bs_guid == guid)
				return (&sp->sp_fs[i].bf_snaps[k]);
		}
	}
	return (NULL);
}

/*
 * The newest snapshot of F's history whose guid R holds: F's own,
 * newest first, then those of the filesystem it is a clone of from its
 * origin back, and so on; or NULL. *OWN, how many of F's own are it or
 * before it: the copy lacks those after.
 */
static const struct bsnap *
base(const struct bspace *s, const struct bfs *f, const struct bspace *r,
    size_t *own)
{
	const struct bfs *h = f;
	const char *o;
	size_t n = f->bf_n;

	*own = 0;
	for (;;) {
		while (n > 0) {
			if (held(r, h->bf_snaps[--n].bs_guid) != NULL) {
				if (h == f)
					*own = n + 1;
				return (&h->bf_snaps[n]);
			}
		}
		if ((o = h->bf_origin) == NULL || (h = fs_of(s, o)) == NULL)
			return (NULL);
		for (n = h->bf_n; n > 0 &&
		    strcmp(h->bf_snaps[n - 1].bs_name, o) != 0; n--)
			;
	}
}

/* Whether F's parent and origin, where they are below ROOT, are done. */
static int
ready(const struct bspace *s, const struct bfs *f, const char *root)
{
	const struct bfs *p;
	char *parent = sh_strdup(f->bf_name);

	*strrchr(parent, '/') = '\0';
	p = fs_of(s, parent);
	free(parent);
	if (below(p->bf_name, root) && !p->bf_done)
		return (0);
	return (f->bf_origin == NULL || !below(f->bf_origin, root) ||
	    fs_of(s, f->bf_origin)->bf_done);
}

/* Whether a receive to resume stands at TOP in R, or below it. */
static int
resuming(const struct bspace *r, const char *top)
{
	size_t i;

	for (i = 0; i < r->sp_n; i++) {
		if ((strcmp(r->sp_fs[i].bf_name, top) == 0 ||
		    below(r->sp_fs[i].bf_name, top)) && r->sp_fs[i].bf_token)
			return (1);
	}
	return (0);
}

/* Whether a stream of the top KEY failed in this convergence. */
static int
failed(const struct sh_beam *b, const char *key)
{
	return (listed(b->b_now.bl_key, b->b_now.bl_n, key));
}

static void
fails_free(struct bfails *l)
{
	while (l->bl_n > 0) {
		l->bl_n--;
		free(l->bl_key[l->bl_n]);
		free(l->bl_why[l->bl_n]);
	}
	free(l->bl_key);
	free(l->bl_why);
	(void) memset(l, 0, sizeof (*l));
}

/*
 * A failure of the top KEY: in the log; in syslog, which hears few
 * errors, only where the last convergence did not fail so.
 */
static void
fail(struct sh_beam *b, const char *key, const char *why)
{
	struct bfails *l = &b->b_now;

	sh_errf(&b->b_lerr, "failed: %s", why);
	if (!listed(b->b_last.bl_why, b->b_last.bl_n, why))
		sh_errf(b->b_env->be_err, "beam: %s", why);
	l->bl_key = sh_realloc(l->bl_key, (l->bl_n + 1) * sizeof (char *));
	l->bl_why = sh_realloc(l->bl_why, (l->bl_n + 1) * sizeof (char *));
	l->bl_key[l->bl_n] = sh_strdup(key);
	l->bl_why[l->bl_n++] = sh_strdup(why);
}

/* A stream added to the pass, of the top KEY. */
static struct bstream *
stream(struct sh_beam *b, const char *key, const char *snap,
    const char *from, const char *to, const char *origin)
{
	struct bstream *t = sh_calloc(1, sizeof (*t));

	t->bt_beam = b;
	t->bt_key = sh_strdup(key);
	t->bt_snap = dup0(snap);
	t->bt_from = dup0(from);
	t->bt_to = sh_strdup(to);
	t->bt_origin = dup0(origin);
	*b->b_tail = t;
	b->b_tail = &t->bt_next;
	return (t);
}

static void
pass_free(struct sh_beam *b)
{
	struct bstream *t;

	while ((t = b->b_pass) != NULL) {
		b->b_pass = t->bt_next;
		free(t->bt_key);
		free(t->bt_snap);
		free(t->bt_from);
		free(t->bt_to);
		free(t->bt_origin);
		free(t);
	}
	b->b_tail = &b->b_pass;
	b->b_cur = NULL;
}

/* Each receive to resume below ROOT in R, of a top not failed. */
static void
resumes(struct sh_beam *b, const struct bspace *r, const char *pair,
    const char *root)
{
	const struct bfs *f;
	char *key;
	size_t i;

	for (i = 0; i < r->sp_n; i++) {
		f = &r->sp_fs[i];
		if (!below(f->bf_name, root) || !f->bf_token)
			continue;
		key = topkey(pair, f->bf_name, root);
		if (!failed(b, key))
			(void) stream(b, key, NULL, NULL, f->bf_name, NULL);
		free(key);
	}
}

/*
 * The streams that bring F's copy TO, in R, to F, in S, of the top
 * KEY: added to the pass, and to R as the copies they make, with what
 * each needs.
 */
static void
plan(struct sh_beam *b, const struct bspace *s, struct bspace *r,
    const struct bfs *f, const char *key, const char *to)
{
	const struct bsnap *from, *at;
	struct bstream *t;
	struct bfs *c, *p;
	char *name, *parent;
	size_t i, own;

	from = base(s, f, r, &own);
	for (i = own; i < f->bf_n; i++) {
		name = join(to, strchr(f->bf_snaps[i].bs_name, '@'), "");
		c = fs_of(r, to);
		at = from != NULL ? held(r, from->bs_guid) : NULL;
		t = stream(b, key, f->bf_snaps[i].bs_name, from != NULL ?
		    from->bs_name : NULL, name, at != NULL && c == NULL ?
		    at->bs_name : NULL);
		t->bt_need[0] = at != NULL ? at->bs_by : NULL;
		if (c == NULL) {
			parent = sh_strdup(to);
			*strrchr(parent, '/') = '\0';
			p = fs_of(r, parent);
			free(parent);
			t->bt_need[1] = p != NULL ? p->bf_by : NULL;
			(void) fs_add(r, to);
			c = &r->sp_fs[r->sp_n - 1];
			c->bf_by = t;
		}
		snap_put(c, name, f->bf_snaps[i].bs_guid, t);
		from = &f->bf_snaps[i];
	}
}

/*
 * The streams that bring each copy below RROOT, in R, to its owner's
 * below SROOT, in S, a parent and an origin first; none for a top
 * that failed in this convergence or has a receive to resume.
 */
static void
streams(struct sh_beam *b, struct bspace *s, struct bspace *r,
    const char *pair, const char *sroot, const char *rroot)
{
	struct bfs *f;
	char *key, *to, *top;
	size_t k, placed;

	do {
		placed = 0;
		for (k = 0; k < s->sp_n; k++) {
			f = &s->sp_fs[k];
			if (f->bf_done || !below(f->bf_name, sroot) ||
			    !ready(s, f, sroot))
				continue;
			f->bf_done = 1;
			placed++;
			key = topkey(pair, f->bf_name, sroot);
			to = join(rroot, f->bf_name + strlen(sroot), "");
			top = join(rroot, key + strlen(pair), "");
			if (!failed(b, key) && !resuming(r, top))
				plan(b, s, r, f, key, to);
			free(key);
			free(to);
			free(top);
		}
	} while (placed > 0);
}

/* A walk of properties: the shtetl:* ones, "PROP=VALUE" each. */
struct plist {
	char	**pl_v;
	size_t	pl_n;
};

static int
prop_add(void *arg, const char *prop, const char *value)
{
	struct plist *l = arg;

	if (strncmp(prop, "shtetl:", 7) != 0)
		return (0);
	l->pl_v = sh_realloc(l->pl_v, (l->pl_n + 1) * sizeof (char *));
	l->pl_v[l->pl_n++] = join(prop, "=", value);
	return (0);
}

/* Whether L holds P, "PROP=VALUE", or with LEN, PROP of any value. */
static int
prop_has(const struct plist *l, const char *p, size_t len)
{
	size_t i;

	for (i = 0; i < l->pl_n; i++) {
		if (len > 0 ? strncmp(l->pl_v[i], p, len) == 0 &&
		    l->pl_v[i][len] == '=' : strcmp(l->pl_v[i], p) == 0)
			return (1);
	}
	return (0);
}

static void
plist_free(struct plist *l)
{
	while (l->pl_n > 0)
		free(l->pl_v[--l->pl_n]);
	free(l->pl_v);
}

/*
 * The shtetl:* properties of COPY made those of OWN, each a filesystem
 * or each a snapshot: 1 where COPY had none and has them now, else 0;
 * or -1.
 */
static int
same(struct sh_zfs *z, const char *own, const char *copy, struct sh_err *e)
{
	struct plist o = { NULL, 0 }, c = { NULL, 0 };
	char *p, *eq;
	size_t i;
	int rc = -1;

	if (sh_zfs_props(z, own, prop_add, &o, e) != 0 ||
	    sh_zfs_props(z, copy, prop_add, &c, e) != 0)
		goto out;
	for (i = 0; i < o.pl_n; i++) {
		if (prop_has(&c, o.pl_v[i], 0))
			continue;
		p = sh_strdup(o.pl_v[i]);
		eq = strchr(p, '=');
		*eq = '\0';
		rc = sh_zfs_set(z, copy, p, eq + 1, e);
		free(p);
		if (rc != 0)
			goto out;
	}
	for (i = 0; i < c.pl_n; i++) {
		if (!prop_has(&o, c.pl_v[i], strcspn(c.pl_v[i], "="))) {
			c.pl_v[i][strcspn(c.pl_v[i], "=")] = '\0';
			if (sh_zfs_inherit(z, copy, c.pl_v[i], e) != 0)
				goto out;
		}
	}
	rc = c.pl_n == 0 && o.pl_n > 0;
out:
	plist_free(&o);
	plist_free(&c);
	return (rc);
}

/*
 * Whether all of the version SNAP of F is in R: every snapshot of its
 * name in F's top, the dataset below ROOT F is or is in, and below it.
 */
static int
whole(const struct bspace *s, const struct bspace *r, const char *root,
    const struct bfs *f, const char *snap)
{
	const char *v = strchr(snap, '@');
	size_t i, k, n = strlen(root) + 1;
	const struct bfs *g;

	n += strcspn(f->bf_name + n, "/");
	for (i = 0; i < s->sp_n; i++) {
		g = &s->sp_fs[i];
		if (strncmp(g->bf_name, f->bf_name, n) != 0 ||
		    (g->bf_name[n] != '\0' && g->bf_name[n] != '/'))
			continue;
		for (k = 0; k < g->bf_n; k++) {
			if (strcmp(strchr(g->bf_snaps[k].bs_name, '@'), v) ==
			    0 && held(r, g->bf_snaps[k].bs_guid) == NULL)
				return (0);
		}
	}
	return (1);
}

/*
 * The properties of F, below SROOT in S, and of its versions all there
 * in R, made its owner's on its copy TO; a top's snapshot that had
 * none, arrived.
 */
static int
fs_props(struct sh_beam *b, const struct bspace *s, const struct bspace *r,
    const char *sroot, const struct bfs *f, const char *to,
    struct sh_err *e)
{
	struct sh_zfs *z = b->b_env->be_z;
	int top = strchr(f->bf_name + strlen(sroot) + 1, '/') == NULL;
	const struct bsnap *h;
	size_t i;
	int got;

	if (same(z, f->bf_name, to, e) < 0)
		return (-1);
	for (i = 0; i < f->bf_n; i++) {
		if ((h = held(r, f->bf_snaps[i].bs_guid)) == NULL ||
		    !whole(s, r, sroot, f, f->bf_snaps[i].bs_name))
			continue;
		if ((got = same(z, f->bf_snaps[i].bs_name, h->bs_name, e)) < 0)
			return (-1);
		if (got == 1 && top)
			sh_errf(&b->b_lerr, "arrived %s", h->bs_name);
	}
	return (0);
}

/*
 * The properties of each copy below RROOT, in R, made those of its
 * owner below SROOT, in S: a child before its parent, so that of a
 * version its top's go last.
 */
static int
props(struct sh_beam *b, const struct bspace *s, const struct bspace *r,
    const char *sroot, const char *rroot, struct sh_err *e)
{
	const struct bfs *f;
	char *to;
	size_t k;
	int rc = 0;

	for (k = s->sp_n; rc == 0 && k > 0; k--) {
		f = &s->sp_fs[k - 1];
		if (!below(f->bf_name, sroot))
			continue;
		to = join(rroot, f->bf_name + strlen(sroot), "");
		if (fs_of(r, to) != NULL)
			rc = fs_props(b, s, r, sroot, f, to, e);
		free(to);
	}
	return (rc);
}

static int
beam_send(struct sh_job *j, void *arg, struct sh_err *e)
{
	struct bstream *t = arg;

	(void) j;
	return (sh_zfs_send(t->bt_beam->b_env->be_z, t->bt_snap, t->bt_from,
	    t->bt_to, t->bt_origin, e));
}

static int
beam_resume(struct sh_job *j, void *arg, struct sh_err *e)
{
	struct bstream *t = arg;

	(void) j;
	return (sh_zfs_resume(t->bt_beam->b_env->be_z, t->bt_to, e));
}

static void beam_go(struct sh_beam *b);
static void beam_look(struct sh_beam *b);

/*
 * The convergence over: the next one's wait, and the next, at once
 * when B was told meanwhile.
 */
static void
beam_end(struct sh_beam *b)
{
	const struct sh_beamenv *env = b->b_env;
	struct kevent ev;

	b->b_busy = 0;
	if (b->b_now.bl_n == 0)
		b->b_wait = env->be_period;
	else if (b->b_last.bl_n == 0)
		b->b_wait = env->be_retry;
	else if ((b->b_wait *= 2) > env->be_period)
		b->b_wait = env->be_period;
	fails_free(&b->b_last);
	b->b_last = b->b_now;
	(void) memset(&b->b_now, 0, sizeof (b->b_now));
	if (b->b_again) {
		b->b_again = 0;
		beam_go(b);
		return;
	}
	EV_SET(&ev, (uintptr_t)b, EVFILT_TIMER, EV_ADD | EV_ONESHOT, 0,
	    b->b_wait, b);
	(void) kevent(env->be_kq, &ev, 1, NULL, 0, NULL);
}

static void stream_end(struct sh_job *j, const char *why, void *arg);

/*
 * The pass's next stream begun, those needing one that failed passed
 * over; the pass over, a look.
 */
static void
beam_next(struct sh_beam *b)
{
	struct bstream *t;
	char *step;

	while ((t = b->b_cur) != NULL &&
	    ((t->bt_need[0] != NULL && t->bt_need[0]->bt_failed) ||
	    (t->bt_need[1] != NULL && t->bt_need[1]->bt_failed))) {
		t->bt_failed = 1;
		b->b_cur = t->bt_next;
	}
	if (t == NULL) {
		pass_free(b);
		beam_look(b);
		return;
	}
	step = join(t->bt_snap != NULL ? "send " : "resume ",
	    t->bt_snap != NULL ? t->bt_snap : t->bt_to, "");
	sh_job_init(&b->b_job, b->b_env->be_log, stream_end, b);
	sh_job_fork(&b->b_job, step, t->bt_snap != NULL ? beam_send :
	    beam_resume, t);
	free(step);
	sh_job_run(&b->b_job, b->b_env->be_kq);
}

static void
stream_end(struct sh_job *j, const char *why, void *arg)
{
	struct sh_beam *b = arg;
	struct bstream *t = b->b_cur;

	(void) j;
	if (why != NULL) {
		t->bt_failed = 1;
		fail(b, t->bt_key, why);
	}
	b->b_cur = t->bt_next;
	beam_next(b);
}

/*
 * A look: both spaces read; the receives to resume and the properties
 * made the owner's, then the streams the copies lack, a pass of them;
 * with none, the convergence over.
 */
static void
beam_look(struct sh_beam *b)
{
	const struct sh_beamenv *env = b->b_env;
	const char *root[2] = { env->be_run, env->be_lake };
	struct bspace sp[2];	/* the run's, the lake's: by bp_lake */
	char *own[NPAIRS], *copy[NPAIRS], *buf = NULL, *why;
	size_t i, len = 0;
	struct sh_err e;
	FILE *f;
	int o, rc = -1;

	(void) memset(sp, 0, sizeof (sp));
	for (i = 0; i < NPAIRS; i++) {
		o = pairs[i].bp_lake;
		own[i] = join(root[o], "/", pairs[i].bp_name);
		copy[i] = join(root[!o], "/", pairs[i].bp_name);
	}
	f = sh_memstream(&buf, &len);
	sh_err_init(&e, f, "");
	if (space_read(env->be_z, root[0], &sp[0], &e) != 0 ||
	    space_read(env->be_z, root[1], &sp[1], &e) != 0)
		goto out;
	for (i = 0; i < NPAIRS; i++)
		resumes(b, &sp[!pairs[i].bp_lake], pairs[i].bp_name, copy[i]);
	/* the properties of the copies there before this look's streams */
	for (i = 0; i < NPAIRS; i++) {
		o = pairs[i].bp_lake;
		if (props(b, &sp[o], &sp[!o], own[i], copy[i], &e) != 0)
			goto out;
	}
	for (i = 0; i < NPAIRS; i++) {
		o = pairs[i].bp_lake;
		streams(b, &sp[o], &sp[!o], pairs[i].bp_name, own[i], copy[i]);
	}
	rc = 0;
out:
	(void) fclose(f);
	if (rc != 0) {
		buf[strcspn(buf, "\n")] = '\0';
		why = join("look: ", buf, "");
		fail(b, "", why);
		free(why);
		pass_free(b);
	}
	free(buf);
	for (i = 0; i < NPAIRS; i++) {
		free(own[i]);
		free(copy[i]);
	}
	space_free(&sp[0]);
	space_free(&sp[1]);
	if (b->b_pass == NULL) {
		beam_end(b);
		return;
	}
	b->b_cur = b->b_pass;
	beam_next(b);
}

static void
beam_go(struct sh_beam *b)
{
	b->b_busy = 1;
	beam_look(b);
}

struct sh_beam *
sh_beam_start(const struct sh_beamenv *env)
{
	struct sh_beam *b = sh_calloc(1, sizeof (*b));

	b->b_ev = SH_EV_BEAM;
	b->b_env = env;
	b->b_tail = &b->b_pass;
	sh_err_init(&b->b_lerr, env->be_log, "# ");
	beam_go(b);
	return (b);
}

void
sh_beam_converge(struct sh_beam *b)
{
	if (b->b_busy)
		b->b_again = 1;
	else
		beam_go(b);
}

void
sh_beam_timer(struct sh_beam *b)
{
	/* one under way sets the timer again at its end */
	if (!b->b_busy)
		beam_go(b);
}

void
sh_beam_status(const struct sh_beam *b, FILE *out)
{
	size_t i;

	if (b->b_busy)
		(void) fprintf(out, "beam converging %s\n",
		    sh_job_step(&b->b_job));
	for (i = 0; !b->b_busy && i < b->b_last.bl_n; i++)
		(void) fprintf(out, "beam failing %s\n", b->b_last.bl_why[i]);
	if (!b->b_busy && b->b_last.bl_n == 0)
		(void) fprintf(out, "beam converged\n");
}

void
sh_beam_stop(struct sh_beam *b)
{
	struct kevent ev;

	if (b->b_busy)
		sh_job_stop(&b->b_job);
	pass_free(b);
	EV_SET(&ev, (uintptr_t)b, EVFILT_TIMER, EV_DELETE, 0, 0, NULL);
	(void) kevent(b->b_env->be_kq, &ev, 1, NULL, 0, NULL);
	fails_free(&b->b_now);
	fails_free(&b->b_last);
	free(b);
}
