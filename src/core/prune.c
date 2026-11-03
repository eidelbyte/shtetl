/*
 * The prune: both spaces read whole, every filesystem with its origin
 * and its snapshots with their guids and createtxgs; then the houses'
 * versions, the run's before the lake's copies, then the foundations',
 * the run's copies before the lake's, so that what one destroys frees
 * what the next would keep, within one prune.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/err.h"
#include "core/mem.h"
#include "core/prune.h"
#include "core/zfs.h"

/* A snapshot: ds@snap, its guid and createtxg. */
struct psnap {
	char		*ps_name;
	uint64_t	ps_guid;
	uint64_t	ps_txg;
};

/* A filesystem: the snapshot it is a clone of, or NULL; its own. */
struct pfs {
	char		*pf_name;
	char		*pf_origin;
	struct psnap	*pf_snaps;	/* oldest first */
	size_t		pf_n;
	int		pf_gone;	/* destroyed by this prune */
};

/* A space below its root, each filesystem before its children. */
struct pspace {
	struct pfs	*pp_fs;
	size_t		pp_n;
};

enum { LAKE, RUN };

struct prune {
	struct sh_zfs	*p_z;
	FILE		*p_log;
	struct sh_err	*p_e;
	const char	*p_root[2];
	struct pspace	p_sp[2];
	int		p_rc;
};

/* A house version below a pair's root: its house, its origin's txg. */
struct pver {
	struct pfs	*pv_top;
	char		*pv_house;
	uint64_t	pv_txg;
};

static const char *const kinds[] = { "containers", "systems" };

/* Names, as a walk finds them. */
struct names {
	char	**n_v;
	size_t	n_n;
};

static char *
join(const char *a, const char *b, const char *c)
{
	size_t n = strlen(a) + strlen(b) + strlen(c) + 1;
	char *s = sh_calloc(1, n);

	(void) snprintf(s, n, "%s%s%s", a, b, c);
	return (s);
}

static int
name_add(void *arg, const char *name)
{
	struct names *n = arg;

	n->n_v = sh_realloc(n->n_v, (n->n_n + 1) * sizeof (char *));
	n->n_v[n->n_n++] = sh_strdup(name);
	return (0);
}

static void
names_free(struct names *n)
{
	while (n->n_n > 0)
		free(n->n_v[--n->n_n]);
	free(n->n_v);
}

/* Whether NAME, a filesystem or a snapshot, is ROOT's or below it. */
static int
in(const char *name, const char *root)
{
	size_t n = strlen(root);

	return (strncmp(name, root, n) == 0 && (name[n] == '\0' ||
	    name[n] == '/' || name[n] == '@'));
}

/* Whether the filesystem NAME is a child of ROOT. */
static int
child(const char *name, const char *root)
{
	size_t n = strlen(root);

	return (strncmp(name, root, n) == 0 && name[n] == '/' &&
	    strchr(name + n + 1, '/') == NULL);
}

static int
fs_add(void *arg, const char *name)
{
	struct pspace *sp = arg;

	sp->pp_fs = sh_realloc(sp->pp_fs, (sp->pp_n + 1) *
	    sizeof (struct pfs));
	(void) memset(&sp->pp_fs[sp->pp_n], 0, sizeof (struct pfs));
	sp->pp_fs[sp->pp_n++].pf_name = sh_strdup(name);
	return (0);
}

/* A walk of a filesystem's snapshots, each read with its guid and txg. */
struct swalk {
	struct prune	*sw_p;
	struct pfs	*sw_f;
};

static int
snap_add(void *arg, const char *snap)
{
	struct swalk *w = arg;
	struct pfs *f = w->sw_f;
	struct psnap *s;

	f->pf_snaps = sh_realloc(f->pf_snaps, (f->pf_n + 1) * sizeof (*s));
	s = &f->pf_snaps[f->pf_n++];
	s->ps_name = join(f->pf_name, "@", snap);
	return (sh_zfs_snapinfo(w->sw_p->p_z, s->ps_name, &s->ps_guid,
	    &s->ps_txg, w->sw_p->p_e));
}

/* The space below ROOT, read whole into SP: 0, or -1. */
static int
space_read(struct prune *p, const char *root, struct pspace *sp)
{
	struct swalk w = { p, NULL };
	char origin[1024];
	size_t i;
	int r;

	(void) fs_add(sp, root);
	for (i = 0; i < sp->pp_n; i++) {
		if (sh_zfs_children(p->p_z, sp->pp_fs[i].pf_name, fs_add, sp,
		    p->p_e) != 0)
			return (-1);
	}
	for (i = 0; i < sp->pp_n; i++) {
		w.sw_f = &sp->pp_fs[i];
		if ((r = sh_zfs_origin(p->p_z, w.sw_f->pf_name, origin,
		    sizeof (origin), p->p_e)) < 0 ||
		    sh_zfs_snapshots(p->p_z, w.sw_f->pf_name, snap_add, &w,
		    p->p_e) != 0)
			return (-1);
		if (r == 1)
			w.sw_f->pf_origin = sh_strdup(origin);
	}
	return (0);
}

static void
space_free(struct pspace *sp)
{
	struct pfs *f;
	size_t i, k;

	for (i = 0; i < sp->pp_n; i++) {
		f = &sp->pp_fs[i];
		for (k = 0; k < f->pf_n; k++)
			free(f->pf_snaps[k].ps_name);
		free(f->pf_snaps);
		free(f->pf_origin);
		free(f->pf_name);
	}
	free(sp->pp_fs);
}

/* The filesystem NAME of SP, not destroyed by this prune: or NULL. */
static struct pfs *
fs_of(const struct pspace *sp, const char *name)
{
	size_t i;

	for (i = 0; i < sp->pp_n; i++) {
		if (!sp->pp_fs[i].pf_gone &&
		    strcmp(sp->pp_fs[i].pf_name, name) == 0)
			return (&sp->pp_fs[i]);
	}
	return (NULL);
}

/* The snapshot NAME of SP: or NULL. */
static const struct psnap *
snap_of(const struct pspace *sp, const char *name)
{
	const struct pfs *f;
	size_t i, k, n = strcspn(name, "@");

	for (i = 0; i < sp->pp_n; i++) {
		f = &sp->pp_fs[i];
		if (strncmp(f->pf_name, name, n) != 0 || f->pf_name[n] != '\0')
			continue;
		for (k = 0; k < f->pf_n; k++) {
			if (strcmp(f->pf_snaps[k].ps_name, name) == 0)
				return (&f->pf_snaps[k]);
		}
	}
	return (NULL);
}

/* The snapshot of ROOT's tree in SP whose guid is GUID: or NULL. */
static const struct psnap *
by_guid(const struct pspace *sp, const char *root, uint64_t guid)
{
	const struct pfs *f;
	size_t i, k;

	for (i = 0; i < sp->pp_n; i++) {
		f = &sp->pp_fs[i];
		for (k = 0; in(f->pf_name, root) && k < f->pf_n; k++) {
			if (f->pf_snaps[k].ps_guid == guid)
				return (&f->pf_snaps[k]);
		}
	}
	return (NULL);
}

/*
 * TOP's own snapshot of the version SNAP, of a dataset of its tree, is
 * of: the one of its name, as a roll snapshots a tree at once.
 */
static const struct psnap *
top_snap(const struct pfs *top, const char *snap)
{
	const char *at = strchr(snap, '@');
	size_t k;

	for (k = 0; k < top->pf_n; k++) {
		if (strcmp(strchr(top->pf_snaps[k].ps_name, '@'), at) == 0)
			return (&top->pf_snaps[k]);
	}
	return (NULL);
}

/* Whether SNAP holds shtetl:kind; with MADE, create or update. */
static int
kind(struct prune *p, const char *snap, int made)
{
	char v[64];
	int r;

	if ((r = sh_zfs_user(p->p_z, snap, "shtetl:kind", v, sizeof (v),
	    p->p_e)) < 0)
		p->p_rc = -1;
	return (r == 0 && (!made || strcmp(v, "create") == 0 ||
	    strcmp(v, "update") == 0));
}

/* F, a filesystem of the prune's, destroyed, its snapshots first. */
static int
fs_destroy(struct prune *p, struct pfs *f)
{
	struct names n = { NULL, 0 };
	char *s;
	size_t i;
	int r;

	r = sh_zfs_snapshots(p->p_z, f->pf_name, name_add, &n, p->p_e);
	for (i = 0; r == 0 && i < n.n_n; i++) {
		s = join(f->pf_name, "@", n.n_v[i]);
		r = sh_zfs_destroy(p->p_z, s, p->p_e);
		free(s);
	}
	if (r == 0 && (r = sh_zfs_destroy(p->p_z, f->pf_name, p->p_e)) == 0)
		f->pf_gone = 1;
	names_free(&n);
	return (r);
}

/* TOP's tree in SP destroyed, children first, and said: 0, or -1. */
static int
tree_destroy(struct prune *p, struct pspace *sp, const char *top)
{
	struct pfs *f;
	size_t k;

	for (k = sp->pp_n; k > 0; k--) {
		f = &sp->pp_fs[k - 1];
		if (!f->pf_gone && in(f->pf_name, top) && fs_destroy(p, f) != 0)
			return (-1);
	}
	(void) fprintf(p->p_log, "# prune %s\n", top);
	return (0);
}

/*
 * The house version TOP of SP pruned: each filesystem of SP outside it
 * that is a clone of one of its snapshots promoted, then it destroyed.
 */
static void
house_prune(struct prune *p, struct pspace *sp, struct pfs *top)
{
	struct pfs *f;
	size_t i;

	for (i = 0; i < sp->pp_n; i++) {
		f = &sp->pp_fs[i];
		if (!f->pf_gone && f->pf_origin != NULL &&
		    in(f->pf_origin, top->pf_name) &&
		    !in(f->pf_name, top->pf_name) &&
		    sh_zfs_promote(p->p_z, f->pf_name, p->p_e) != 0) {
			p->p_rc = -1;
			return;
		}
	}
	if (tree_destroy(p, sp, top->pf_name) != 0)
		p->p_rc = -1;
}

/* The complete house versions of SP below the pair's root ROOT. */
static size_t
versions(struct prune *p, struct pspace *sp, const char *root,
    struct pver **vp)
{
	struct pver *v = NULL;
	const struct psnap *o;
	struct pfs *f;
	char house[256];
	size_t i, k, n = 0;
	int r;

	for (i = 0; i < sp->pp_n; i++) {
		f = &sp->pp_fs[i];
		if (!child(f->pf_name, root) || f->pf_origin == NULL ||
		    (o = snap_of(sp, f->pf_origin)) == NULL)
			continue;
		if ((r = sh_zfs_user(p->p_z, f->pf_name, "shtetl:house", house,
		    sizeof (house), p->p_e)) != 0) {
			if (r < 0)
				p->p_rc = -1;
			continue;
		}
		for (k = 0; k < f->pf_n && !kind(p, f->pf_snaps[k].ps_name, 1);
		    k++)
			;
		if (k == f->pf_n)
			continue;
		v = sh_realloc(v, (n + 1) * sizeof (*v));
		v[n].pv_top = f;
		v[n].pv_house = sh_strdup(house);
		v[n++].pv_txg = o->ps_txg;
	}
	*vp = v;
	return (n);
}

static void
versions_free(struct pver *v, size_t n)
{
	while (n > 0)
		free(v[--n].pv_house);
	free(v);
}

/* Whether V[I] is older than the newest KEEP of its house's N. */
static int
beyond(const struct pver *v, size_t n, size_t i, int64_t keep)
{
	int64_t newer = 0;
	size_t k;

	for (k = 0; k < n; k++) {
		if (strcmp(v[k].pv_house, v[i].pv_house) == 0 &&
		    v[k].pv_txg > v[i].pv_txg)
			newer++;
	}
	return (newer >= keep);
}

/*
 * The houses' versions: the run's beyond RUN_KEEP whose copy has
 * arrived at their newest, then the lake's copies beyond LAKE_KEEP
 * that the run holds no more.
 */
static void
houses(struct prune *p, int64_t run_keep, int64_t lake_keep)
{
	struct pspace *lake = &p->p_sp[LAKE], *run = &p->p_sp[RUN];
	const struct psnap *last, *s;
	const struct pfs *copy;
	char *root[2], *name;
	struct pver *v;
	size_t i, k, n;

	for (k = 0; k < sizeof (kinds) / sizeof (kinds[0]); k++) {
		root[LAKE] = join(p->p_root[LAKE], "/", kinds[k]);
		root[RUN] = join(p->p_root[RUN], "/", kinds[k]);
		n = versions(p, run, root[RUN], &v);
		for (i = 0; i < n; i++) {
			last = &v[i].pv_top->pf_snaps[v[i].pv_top->pf_n - 1];
			name = join(root[LAKE], v[i].pv_top->pf_name +
			    strlen(root[RUN]), "");
			if (beyond(v, n, i, run_keep) &&
			    (copy = fs_of(lake, name)) != NULL &&
			    (s = by_guid(lake, copy->pf_name, last->ps_guid)) !=
			    NULL && kind(p, s->ps_name, 0))
				house_prune(p, run, v[i].pv_top);
			free(name);
		}
		versions_free(v, n);
		n = versions(p, lake, root[LAKE], &v);
		for (i = 0; i < n; i++) {
			name = join(root[RUN], v[i].pv_top->pf_name +
			    strlen(root[LAKE]), "");
			if (beyond(v, n, i, lake_keep) &&
			    fs_of(run, name) == NULL)
				house_prune(p, lake, v[i].pv_top);
			free(name);
		}
		versions_free(v, n);
		free(root[LAKE]);
		free(root[RUN]);
	}
}

/* The version AT, "@snap", of TOP's tree in SP destroyed, and said. */
static int
version_destroy(struct prune *p, struct pspace *sp, const char *top,
    const char *at)
{
	const struct pfs *f;
	size_t i, k;

	for (i = 0; i < sp->pp_n; i++) {
		f = &sp->pp_fs[i];
		for (k = 0; !f->pf_gone && in(f->pf_name, top) && k < f->pf_n;
		    k++) {
			if (strcmp(strchr(f->pf_snaps[k].ps_name, '@'), at) ==
			    0 && sh_zfs_destroy(p->p_z, f->pf_snaps[k].ps_name,
			    p->p_e) != 0)
				return (-1);
		}
	}
	(void) fprintf(p->p_log, "# prune %s%s\n", top, at);
	return (0);
}

/*
 * The run's copies of the foundations: each version older than the
 * oldest one a filesystem of the run is a clone of; with none, the
 * copy whole.
 */
static void
run_foundations(struct prune *p)
{
	struct pspace *run = &p->p_sp[RUN];
	char *root = join(p->p_root[RUN], "/foundations", "");
	const struct psnap *o;
	struct pfs *t, *f;
	uint64_t least;
	size_t i, k;

	for (i = 0; i < run->pp_n; i++) {
		t = &run->pp_fs[i];
		if (t->pf_gone || !child(t->pf_name, root))
			continue;
		least = UINT64_MAX;
		for (k = 0; k < run->pp_n; k++) {
			f = &run->pp_fs[k];
			if (!f->pf_gone && f->pf_origin != NULL &&
			    in(f->pf_origin, t->pf_name) &&
			    (o = top_snap(t, f->pf_origin)) != NULL &&
			    o->ps_txg < least)
				least = o->ps_txg;
		}
		if (least == UINT64_MAX) {
			if (tree_destroy(p, run, t->pf_name) != 0)
				p->p_rc = -1;
			continue;
		}
		for (k = 0; k < t->pf_n && t->pf_snaps[k].ps_txg < least; k++) {
			if (version_destroy(p, run, t->pf_name,
			    strchr(t->pf_snaps[k].ps_name, '@')) != 0) {
				p->p_rc = -1;
				break;
			}
		}
	}
	free(root);
}

/*
 * Whether a filesystem of either space is a clone of the version AT of
 * TOP, a foundation of the lake: of its snapshot of that name, or of
 * a copy with its guid.
 */
static int
cloned(const struct prune *p, const char *top, const char *at)
{
	const struct psnap *o, *s;
	const struct pfs *f;
	size_t w, i;

	for (w = 0; w < 2; w++) {
		for (i = 0; i < p->p_sp[w].pp_n; i++) {
			f = &p->p_sp[w].pp_fs[i];
			if (!f->pf_gone && f->pf_origin != NULL &&
			    (o = snap_of(&p->p_sp[w], f->pf_origin)) != NULL &&
			    (s = by_guid(&p->p_sp[LAKE], top, o->ps_guid)) !=
			    NULL && strcmp(strchr(s->ps_name, '@'), at) == 0)
				return (1);
		}
	}
	return (0);
}

/*
 * The lake's foundations: each version but the newest, older than the
 * newest the run's copy holds, that nothing in either space is a clone
 * of.
 */
static void
lake_foundations(struct prune *p)
{
	struct pspace *lake = &p->p_sp[LAKE];
	char *root = join(p->p_root[LAKE], "/foundations", ""), *name;
	const struct psnap *s;
	const struct pfs *c;
	const char *at;
	struct pfs *t;
	uint64_t base;
	size_t i, k;

	for (i = 0; i < lake->pp_n; i++) {
		t = &lake->pp_fs[i];
		if (!child(t->pf_name, root))
			continue;
		base = UINT64_MAX;
		name = join(p->p_root[RUN], t->pf_name +
		    strlen(p->p_root[LAKE]), "");
		if ((c = fs_of(&p->p_sp[RUN], name)) != NULL && c->pf_n > 0 &&
		    (s = by_guid(lake, t->pf_name,
		    c->pf_snaps[c->pf_n - 1].ps_guid)) != NULL)
			base = s->ps_txg;
		free(name);
		for (k = 0; k + 1 < t->pf_n; k++) {
			at = strchr(t->pf_snaps[k].ps_name, '@');
			if (t->pf_snaps[k].ps_txg < base &&
			    !cloned(p, t->pf_name, at) &&
			    version_destroy(p, lake, t->pf_name, at) != 0) {
				p->p_rc = -1;
				break;
			}
		}
	}
	free(root);
}

int
sh_prune(struct sh_zfs *z, const char *lake, const char *run,
    int64_t run_keep, int64_t lake_keep, FILE *log, struct sh_err *e)
{
	struct prune p;

	(void) memset(&p, 0, sizeof (p));
	p.p_z = z;
	p.p_log = log;
	p.p_e = e;
	p.p_root[LAKE] = lake;
	p.p_root[RUN] = run;
	if (space_read(&p, lake, &p.p_sp[LAKE]) == 0 &&
	    space_read(&p, run, &p.p_sp[RUN]) == 0) {
		houses(&p, run_keep, lake_keep);
		run_foundations(&p);
		lake_foundations(&p);
	} else {
		p.p_rc = -1;
	}
	space_free(&p.p_sp[LAKE]);
	space_free(&p.p_sp[RUN]);
	return (p.p_rc);
}
