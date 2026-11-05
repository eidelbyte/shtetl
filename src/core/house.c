/*
 * The houses: their start from ZFS, a container's first version made
 * (m5-create), and its jail started (m5-jail). A create is a pull,
 * then, when the beam tells it the foundation's copy holds the version,
 * the version's clones made at once, inside that telling and so before
 * the convergence's prune; then a job: the version mounted at its own
 * path, the house's recipe stacked on the shaken foundation recipe read
 * from the clone, its own datasets, its packages and command pieces,
 * house.conf, the create snapshot, the version placed at
 * <containers>/<house>, and the start. A start is the jail's block
 * written from the shaken recipe in the active version's create or
 * update snapshot, then jail -cm; the actor's start runs one for each
 * house.
 */

#include <sys/param.h>
#include <sys/mount.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "core/cascade.h"
#include "core/commands.h"
#include "core/err.h"
#include "core/house.h"
#include "core/jail.h"
#include "core/job.h"
#include "core/mem.h"
#include "core/rebase.h"
#include "core/shake.h"
#include "core/show.h"
#include "core/site.h"
#include "core/syntax.h"
#include "core/tree.h"
#include "core/zfs.h"

/* A create under way. */
struct sh_hmake {
	struct sh_house		*hm_h;
	struct sh_house		***hm_list;	/* the houses, */
	size_t			*hm_n;		/* to leave */
	int			hm_pulling;
	int			hm_block;	/* the jail's block written */
	char			*hm_fds;	/* the lake's F */
	char			*hm_newest;	/* its version */
	char			*hm_config;
	struct sh_site		hm_site;
	const char		**hm_repos;
	char			*hm_root;	/* the version's */
	char			*hm_head;	/* where it is */
	struct sh_node		*hm_under;	/* foundation.conf */
	struct sh_recipe	hm_recipe;
	struct sh_commands	hm_cm;
};

/* Names, as a walk finds them. */
struct names {
	char	**n_v;
	size_t	n_n;
};

struct sh_hroll;

/* One dataset of a roll's new version, below its root: "" the root. */
struct hds {
	struct sh_hroll	*d_r;
	char		*d_rel;
	int		d_off;		/* a placeholder */
};

/* A roll under way (plan 7.2). */
struct sh_hroll {
	struct sh_house		*hr_h;
	struct sh_house		***hr_list;	/* the houses, another's */
	size_t			*hr_n;		/* roll on the copy */
	char			*hr_config;
	struct sh_site		hr_site;
	const char		**hr_repos;
	char			*hr_fds;	/* the foundation, the lake's */
	char			*hr_copy;	/* its copy on the run */
	char			*hr_next;	/* its version, after the @ */
	char			*hr_new;	/* the new version's root */
	char			hr_head[1024];	/* where it is mounted */
	struct names		hr_saves;	/* replayed, oldest first */
	struct hds		*hr_ds;
	size_t			hr_nds;
	struct sh_node		*hr_under;	/* the version's shaken */
	struct sh_node		*hr_had;	/* the old house.conf */
	char			*hr_ulayer;
	char			*hr_hlayer;
	struct sh_recipe	hr_recipe;
	struct sh_commands	hr_cm;
	int			hr_complete;	/* the update snapshot taken */
	char			*hr_why;	/* the stall, at a failure */
};

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
	n->n_v = NULL;
}

static int
names_have(const struct names *n, const char *name)
{
	size_t i;

	for (i = 0; i < n->n_n; i++) {
		if (strcmp(n->n_v[i], name) == 0)
			return (1);
	}
	return (0);
}

static char *
join(const char *a, const char *b, const char *c)
{
	size_t n = strlen(a) + strlen(b) + strlen(c) + 1;
	char *s = sh_calloc(1, n);

	(void) snprintf(s, n, "%s%s%s", a, b, c);
	return (s);
}

/* ROOT and every filesystem below it into N, each before its children. */
static int
tree(struct sh_zfs *z, const char *root, struct names *n, struct sh_err *e)
{
	size_t i;

	n->n_v = NULL;
	n->n_n = 0;
	(void) name_add(n, root);
	for (i = 0; i < n->n_n; i++) {
		if (sh_zfs_children(z, n->n_v[i], name_add, n, e) != 0)
			return (-1);
	}
	return (0);
}

/* ROOT's tree mounted where it says, a parent first, placeholders not. */
static int
tree_mount(struct sh_zfs *z, const char *root, struct sh_err *e)
{
	struct names t;
	char cm[16];
	size_t i;
	int r;

	r = tree(z, root, &t, e);
	for (i = 0; r == 0 && i < t.n_n; i++) {
		if ((r = sh_zfs_native(z, t.n_v[i], "canmount", cm,
		    sizeof (cm), e)) == 0 && strcmp(cm, "off") != 0)
			r = sh_zfs_mount(z, t.n_v[i], e);
	}
	names_free(&t);
	return (r);
}

/*
 * ROOT's tree unmounted, children first; first the devfs the commands'
 * chroot mounts on its dev (commands.h), which a failure part way
 * leaves there and which ZFS will not unmount ROOT under, released by
 * the system's call, as sh_foundation_idle releases a head's; where
 * there is none, it says so to no one.
 */
static int
tree_unmount(struct sh_zfs *z, const char *root, struct sh_err *e)
{
	struct names t;
	char at[1024], dev[1100];
	size_t i;
	int r;

	if ((r = sh_zfs_mounted(z, root, at, sizeof (at), e)) == 1) {
		(void) snprintf(dev, sizeof (dev), "%s/dev", at);
		(void) unmount(dev, MNT_FORCE);
	}
	if (r < 0)
		return (-1);
	r = tree(z, root, &t, e);
	for (i = t.n_n; r == 0 && i > 0; i--)
		r = sh_zfs_unmount(z, t.n_v[i - 1], e);
	names_free(&t);
	return (r);
}

/* ROOT's tree destroyed, unmounted, children first, snapshots first. */
static int
tree_destroy(struct sh_zfs *z, const char *root, struct sh_err *e)
{
	struct names t, s;
	char *snap;
	size_t i, k;
	int r;

	t.n_v = NULL;
	t.n_n = 0;
	if ((r = tree_unmount(z, root, e)) == 0)
		r = tree(z, root, &t, e);
	for (i = t.n_n; r == 0 && i > 0; i--) {
		s.n_v = NULL;
		s.n_n = 0;
		r = sh_zfs_snapshots(z, t.n_v[i - 1], name_add, &s, e);
		for (k = 0; r == 0 && k < s.n_n; k++) {
			snap = join(t.n_v[i - 1], "@", s.n_v[k]);
			r = sh_zfs_destroy(z, snap, e);
			free(snap);
		}
		names_free(&s);
		if (r == 0)
			r = sh_zfs_destroy(z, t.n_v[i - 1], e);
	}
	names_free(&t);
	return (r);
}

/*
 * Whether a snapshot of DS holds shtetl:kind create or update, its name
 * after the @ into SNAP: 1, 0, -1.
 */
static int
complete(struct sh_zfs *z, const char *ds, char *snap, size_t len,
    struct sh_err *e)
{
	struct names s = { NULL, 0 };
	char *name, kind[16];
	size_t k;
	int r, got = 0;

	r = sh_zfs_snapshots(z, ds, name_add, &s, e);
	for (k = 0; r == 0 && !got && k < s.n_n; k++) {
		name = join(ds, "@", s.n_v[k]);
		if ((r = sh_zfs_user(z, name, "shtetl:kind", kind,
		    sizeof (kind), e)) == 1) {
			r = 0;
		} else if (r == 0 && (strcmp(kind, "create") == 0 ||
		    strcmp(kind, "update") == 0)) {
			got = 1;
			(void) snprintf(snap, len, "%s", s.n_v[k]);
		}
		free(name);
	}
	names_free(&s);
	return (r < 0 ? -1 : got);
}

/* Where the house NAME is placed: <containers>/<name>, into BUF. */
static int
place(struct sh_zfs *z, const char *run, const char *name, char *buf,
    size_t len, struct sh_err *e)
{
	char *c = join(run, "/containers", ""), mp[1024];
	int r;

	if ((r = sh_zfs_native(z, c, "mountpoint", mp, sizeof (mp), e)) == 0)
		(void) snprintf(buf, len, "%s/%s", mp, name);
	free(c);
	return (r);
}

/* The house NAME in the N of H, or NULL. */
static struct sh_house *
house_of(struct sh_house **h, size_t n, const char *name)
{
	size_t i;

	for (i = 0; i < n; i++) {
		if (strcmp(h[i]->h_name, name) == 0)
			return (h[i]);
	}
	return (NULL);
}

/* A house NAME added to *HP in its place by name. */
static struct sh_house *
house_add(struct sh_house ***hp, size_t *np, const char *name,
    const struct sh_houseenv *env)
{
	struct sh_house *h = sh_calloc(1, sizeof (*h));
	size_t i;

	h->h_name = sh_strdup(name);
	h->h_env = env;
	*hp = sh_realloc(*hp, (*np + 1) * sizeof (**hp));
	for (i = *np; i > 0 && strcmp((*hp)[i - 1]->h_name, name) > 0; i--)
		(*hp)[i] = (*hp)[i - 1];
	(*hp)[i] = h;
	(*np)++;
	return (h);
}

static void
house_free(struct sh_house *h)
{
	while (h->h_nclean > 0)
		free(h->h_clean[--h->h_nclean]);
	free(h->h_clean);
	free(h->h_name);
	free(h->h_active);
	free(h->h_stall);
	free(h);
}

/* H taken out of *HP, and freed. */
static void
house_drop(struct sh_house ***hp, size_t *np, struct sh_house *h)
{
	size_t i;

	for (i = 0; (*hp)[i] != h; i++)
		;
	(void) memmove(&(*hp)[i], &(*hp)[i + 1],
	    (*np - i - 1) * sizeof (**hp));
	(*np)--;
	house_free(h);
}

/* H's log, <logdir>/H.log, opened with MODE for an act. */
static int
house_log(struct sh_house *h, const char *mode, struct sh_err *e)
{
	char path[1024];

	(void) snprintf(path, sizeof (path), "%s/%s.log", h->h_env->he_logdir,
	    h->h_name);
	if ((h->h_log = fopen(path, mode)) == NULL) {
		sh_errf(e, "%s: %s", path, strerror(errno));
		return (-1);
	}
	(void) setvbuf(h->h_log, NULL, _IOLBF, 0);
	sh_err_init(&h->h_lerr, h->h_log, "# ");
	return (0);
}

/* H's act over: its log closed. */
static void
act_over(struct sh_house *h)
{
	(void) fclose(h->h_log);
	h->h_log = NULL;
}

/*
 * H's act ended, the last thing its end does: active, the daemon told,
 * which asks it to take the next version waiting (sh_house_next).
 */
static void
idle(struct sh_house *h)
{
	if (h->h_state == SH_H_ACTIVE)
		h->h_env->he_idle(h->h_env->he_parg, h);
}

/* Where H's jail's block is: <jails>/H.conf, into BUF. */
static void
block_path(const struct sh_house *h, char *buf, size_t len)
{
	(void) snprintf(buf, len, "%s/%s.conf", h->h_env->he_jails,
	    h->h_name);
}

/*
 * REL, a shaken recipe, of the snapshot SNAP of ROOT's tree, parsed
 * with LAYER, which outlives it, as its layer: read through the
 * snapshot of the dataset holding it, the one of the tree mounted
 * nearest above it; where ROOT is mounted into MNT.
 */
static struct sh_node *
read_shaken(struct sh_zfs *z, const char *root, const char *snap,
    const char *rel, const char *layer, char *mnt, size_t len,
    struct sh_err *e)
{
	char at[1024], best[1024], want[1100];
	struct names t;
	size_t i, k, n;
	int r;

	if ((r = sh_zfs_mounted(z, root, mnt, len, e)) < 0)
		return (NULL);
	if (r == 0) {
		sh_errf(e, "%s: not mounted", root);
		return (NULL);
	}
	(void) snprintf(want, sizeof (want), "%s/%s", mnt, rel);
	(void) snprintf(best, sizeof (best), "%s", mnt);
	n = strlen(mnt);
	r = tree(z, root, &t, e);
	for (i = 1; r == 0 && i < t.n_n; i++) {
		if ((r = sh_zfs_mounted(z, t.n_v[i], at, sizeof (at),
		    e)) == 1 && (k = strlen(at)) > n &&
		    strncmp(want, at, k) == 0 && want[k] == '/') {
			(void) snprintf(best, sizeof (best), "%s", at);
			n = k;
		}
		r = r < 0 ? -1 : 0;
	}
	names_free(&t);
	if (r != 0)
		return (NULL);
	return (sh_shaken_read(best, snap, want + n + 1, layer, e));
}

/*
 * REL, a shaken recipe in H's active version, read from its create or
 * update snapshot, which nothing in the jail could have written (plan
 * 5), as read_shaken reads it. Its layer, *LAYER, is freed after it;
 * where the version's root is mounted into ROOT.
 */
static struct sh_node *
shaken(struct sh_house *h, const char *rel, char **layer, char *root,
    size_t len, struct sh_err *e)
{
	struct sh_zfs *z = h->h_env->he_z;
	char snap[256];

	*layer = NULL;
	snap[0] = '\0';
	if (complete(z, h->h_active, snap, sizeof (snap), e) < 0)
		return (NULL);
	*layer = join(h->h_active, "@", snap);
	return (read_shaken(z, h->h_active, snap, rel, *layer, root, len, e));
}

/* The jail's block written from the active version's house.conf. */
static int
start_block(struct sh_job *j, void *arg, struct sh_err *e)
{
	struct sh_house *h = arg;
	char root[1024], file[1024], *layer;
	struct sh_node *n;
	int r = -1;

	(void) j;
	if ((n = shaken(h, SH_SHAKEN_HOUSE, &layer, root, sizeof (root),
	    e)) != NULL) {
		block_path(h, file, sizeof (file));
		r = sh_jail_write(file, h->h_name, root,
		    sh_obj_get(n, "jailconf"), e);
		if (r == 0 && h->h_make != NULL)
			h->h_make->hm_block = 1;
		sh_node_free(n);
	}
	free(layer);
	return (r);
}

/* A start's steps added to H's job: the block, then jail -cm. */
static void
start_steps(struct sh_house *h)
{
	char file[1024];

	block_path(h, file, sizeof (file));
	sh_job_call(&h->h_job, "block", start_block, h);
	sh_jail_start(&h->h_job, file, h->h_name);
}

/*
 * A start or an upgrade over: the house active, WHY said where it
 * failed; an upgrade's snapshot told to the beam.
 */
static void
act_end(struct sh_job *j, const char *why, void *arg)
{
	struct sh_house *h = arg;
	const struct sh_houseenv *env = h->h_env;

	(void) j;
	if (why != NULL) {
		sh_errf(env->he_err, "house %s: %s", h->h_name, why);
		sh_errf(&h->h_lerr, "failed: %s", why);
	} else if (h->h_state == SH_H_UPGRADING) {
		env->he_beam(env->he_parg);
	}
	h->h_state = SH_H_ACTIVE;
	act_over(h);
	idle(h);
}

/* A zfs_rebase run's record as a dataset itself holds it. */
struct record {
	int	rc_open;		/* zfs_rebase:tag there */
	char	rc_phase[32];
};

static int
record_one(void *arg, const char *prop, const char *value)
{
	struct record *rc = arg;

	if (strcmp(prop, SH_REBASE_TAG) == 0)
		rc->rc_open = 1;
	else if (strcmp(prop, SH_REBASE_PHASE) == 0)
		(void) snprintf(rc->rc_phase, sizeof (rc->rc_phase), "%s",
		    value);
	return (0);
}

/*
 * The steps aborting every open zfs_rebase run on the tree ROOT, found
 * by its record, zfs_rebase:tag set on a dataset itself, added to J:
 * one --abort, two where the run is in applying2, whose first --abort
 * steps back to the conflicts gate, holds and all (zfs_rebase(8)).
 */
static void
aborts(struct sh_job *j, struct sh_zfs *z, const char *root,
    struct sh_err *e)
{
	const char *argv[3] = { "--abort", NULL, NULL };
	struct record rc;
	struct names t;
	size_t i;

	if (tree(z, root, &t, e) == 0) {
		for (i = 0; i < t.n_n; i++) {
			(void) memset(&rc, 0, sizeof (rc));
			if (sh_zfs_props(z, t.n_v[i], record_one, &rc,
			    e) != 0 || !rc.rc_open)
				continue;
			argv[1] = t.n_v[i];
			sh_rebase(j, "abort", argv);
			if (strcmp(rc.rc_phase, "applying2") == 0)
				sh_rebase(j, "abort", argv);
		}
	}
	names_free(&t);
}

/* The versions a crash left incomplete destroyed, after their aborts. */
static int
house_clean(struct sh_job *j, void *arg, struct sh_err *e)
{
	struct sh_house *h = arg;
	int r = 0;

	(void) j;
	while (r == 0 && h->h_nclean > 0) {
		if ((r = tree_destroy(h->h_env->he_z,
		    h->h_clean[h->h_nclean - 1], e)) == 0)
			free(h->h_clean[--h->h_nclean]);
	}
	return (r);
}

/*
 * H's jail started from its active version, a job of its own; then
 * the versions a crash left incomplete, the open zfs_rebase runs on
 * them aborted first, destroyed.
 */
static void
house_start(struct sh_house *h)
{
	const struct sh_houseenv *env = h->h_env;
	size_t i;

	if (house_log(h, "a", env->he_err) != 0)
		return;
	h->h_state = SH_H_STARTING;
	sh_job_init(&h->h_job, h->h_log, act_end, h);
	start_steps(h);
	for (i = 0; i < h->h_nclean; i++)
		aborts(&h->h_job, env->he_z, h->h_clean[i], &h->h_lerr);
	if (h->h_nclean > 0)
		sh_job_call(&h->h_job, "clean", house_clean, h);
	sh_job_run(&h->h_job, env->he_kq);
}

/* What a house's start found of one version. */
struct hver {
	char		*hv_root;
	char		*hv_house;
	uint64_t	hv_txg;		/* its origin's */
};

/*
 * The version ROOT of its house HV made active: placed, mounted, where
 * a crash or a step left it at its own path or unmounted.
 */
static int
activate(struct sh_zfs *z, const char *run, struct sh_house *h,
    const char *root, struct sh_err *e)
{
	char want[1024], mp[1024];
	int r;

	if ((r = place(z, run, h->h_name, want, sizeof (want), e)) == 0 &&
	    (r = sh_zfs_native(z, root, "mountpoint", mp, sizeof (mp),
	    e)) == 0 && strcmp(mp, want) != 0)
		r = sh_zfs_set(z, root, "mountpoint", want, e);
	if (r == 0)
		r = tree_mount(z, root, e);
	if (r == 0)
		h->h_active = sh_strdup(root);
	return (r);
}

/*
 * ROOT, a version of H not its newest, unmounted, since only the active
 * version is (plan 7.1), and given back its own path where a swap a
 * crash cut short left it at <containers>/<house> (W12, H14).
 */
static int
put_back(struct sh_zfs *z, const char *run, const struct sh_house *h,
    const char *root, struct sh_err *e)
{
	char want[1024], mp[1024];

	if (tree_unmount(z, root, e) != 0 ||
	    place(z, run, h->h_name, want, sizeof (want), e) != 0 ||
	    sh_zfs_native(z, root, "mountpoint", mp, sizeof (mp), e) != 0)
		return (-1);
	if (strcmp(mp, want) == 0)
		return (sh_zfs_inherit(z, root, "mountpoint", e));
	return (0);
}

/* The house NAME of *HP, added where it is not, failed: left out. */
static void
house_fail(struct sh_house ***hp, size_t *np, const char *name,
    const struct sh_houseenv *env)
{
	struct sh_house *h;

	if ((h = house_of(*hp, *np, name)) == NULL)
		h = house_add(hp, np, name, env);
	h->h_state = SH_H_FAILED;
}

int
sh_houses_start(const struct sh_houseenv *env, struct sh_house ***hp,
    size_t *np, struct sh_err *e)
{
	char *c, house[256], o[1024], snap[256], pfx[300], stall[1024];
	struct sh_zfs *z = env->he_z;
	struct names tops = { NULL, 0 }, cut = { NULL, 0 };
	struct names cuth = { NULL, 0 }, copies = { NULL, 0 };
	struct hver *v = NULL;
	struct sh_house *h;
	struct sh_err he;
	uint64_t guid, txg;
	size_t i, k, n = 0;
	int r, best, got;

	*hp = NULL;
	*np = 0;
	/* nothing rolls yet: a copy a roll left mounted is unmounted */
	c = join(env->he_run, "/foundations", "");
	if (sh_zfs_children(z, c, name_add, &copies, env->he_err) == 0) {
		for (i = 0; i < copies.n_n; i++)
			(void) tree_unmount(z, copies.n_v[i], env->he_err);
	}
	names_free(&copies);
	free(c);
	c = join(env->he_run, "/containers", "");
	r = sh_zfs_children(z, c, name_add, &tops, e);
	/*
	 * What ZFS refuses of one house is said once, to the sink, "house
	 * H: why", and the house is left out; the rest go on (ruled
	 * 2026-11-05, Q-H1).
	 */
	for (i = 0; r == 0 && i < tops.n_n; i++) {
		if (sh_zfs_user(z, tops.n_v[i], "shtetl:house", house,
		    sizeof (house), env->he_err) != 0 ||
		    ((h = house_of(*hp, *np, house)) != NULL &&
		    h->h_state == SH_H_FAILED))
			continue;
		(void) snprintf(pfx, sizeof (pfx), "%shouse %s: ",
		    env->he_err->se_prefix, house);
		sh_err_init(&he, env->he_err->se_out, pfx);
		txg = 0;
		if ((got = complete(z, tops.n_v[i], snap, sizeof (snap),
		    &he)) == 0) {
			/* a create or a roll a crash cut short */
			(void) name_add(&cut, tops.n_v[i]);
			(void) name_add(&cuth, house);
			continue;
		}
		if (got == 1 && (got = sh_zfs_origin(z, tops.n_v[i], o,
		    sizeof (o), &he)) == 1)
			got = sh_zfs_snapinfo(z, o, &guid, &txg, &he);
		if (got < 0) {
			house_fail(hp, np, house, env);
			continue;
		}
		v = sh_realloc(v, (n + 1) * sizeof (*v));
		v[n].hv_root = sh_strdup(tops.n_v[i]);
		v[n].hv_house = sh_strdup(house);
		v[n++].hv_txg = txg;
		if (h == NULL)
			(void) house_add(hp, np, house, env);
	}
	/*
	 * An incomplete version: a house's, a roll cut short, cleaned by its
	 * start's job, which can abort the runs open on it; one with no
	 * complete version beside it, a create cut short, which opened none,
	 * destroyed now
	 */
	for (i = 0; r == 0 && i < cut.n_n; i++) {
		if ((h = house_of(*hp, *np, cuth.n_v[i])) != NULL) {
			if (h->h_state != SH_H_FAILED) {
				h->h_clean = sh_realloc(h->h_clean,
				    (h->h_nclean + 1) * sizeof (char *));
				h->h_clean[h->h_nclean++] =
				    sh_strdup(cut.n_v[i]);
			}
			continue;
		}
		(void) snprintf(pfx, sizeof (pfx), "%shouse %s: ",
		    env->he_err->se_prefix, cuth.n_v[i]);
		sh_err_init(&he, env->he_err->se_out, pfx);
		(void) tree_destroy(z, cut.n_v[i], &he);
	}
	for (i = 0; r == 0 && i < *np; i++) {
		h = (*hp)[i];
		if (h->h_state == SH_H_FAILED)
			continue;
		best = -1;
		for (k = 0; k < n; k++) {
			if (strcmp(v[k].hv_house, h->h_name) == 0 &&
			    (best < 0 || v[k].hv_txg > v[best].hv_txg))
				best = (int)k;
		}
		(void) snprintf(pfx, sizeof (pfx), "%shouse %s: ",
		    env->he_err->se_prefix, h->h_name);
		sh_err_init(&he, env->he_err->se_out, pfx);
		got = 0;
		for (k = 0; got == 0 && k < n; k++) {
			if ((int)k != best &&
			    strcmp(v[k].hv_house, h->h_name) == 0)
				got = put_back(z, env->he_run, h,
				    v[k].hv_root, &he);
		}
		if (got == 0 &&
		    activate(z, env->he_run, h, v[best].hv_root, &he) == 0 &&
		    (got = sh_zfs_user(z, h->h_active, "shtetl:stall", stall,
		    sizeof (stall), &he)) >= 0) {
			if (got == 0)
				h->h_stall = sh_strdup(stall);
			house_start(h);
		} else {
			h->h_state = SH_H_FAILED;
		}
	}
	names_free(&cut);
	names_free(&cuth);
	for (k = 0; k < n; k++) {
		free(v[k].hv_root);
		free(v[k].hv_house);
	}
	free(v);
	names_free(&tops);
	free(c);
	return (r);
}

void
sh_houses_free(struct sh_house **h, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++) {
		sh_house_stop(h[i]);
		house_free(h[i]);
	}
	free(h);
}

static void
make_free(struct sh_hmake *m)
{
	free(m->hm_fds);
	free(m->hm_newest);
	free(m->hm_config);
	free(m->hm_repos);
	sh_site_free(&m->hm_site);
	free(m->hm_root);
	free(m->hm_head);
	sh_node_free(m->hm_under);
	sh_recipe_free(&m->hm_recipe);
	free(m);
}

/*
 * The create over: the house active and its jail started, or the
 * version and the block it wrote destroyed and the house taken away
 * again, WHY said.
 */
static void
make_end(struct sh_job *j, const char *why, void *arg)
{
	struct sh_hmake *m = arg;
	struct sh_house *h = m->hm_h;
	const struct sh_houseenv *env = h->h_env;
	char file[1024];

	(void) j;
	h->h_make = NULL;
	if (why == NULL) {
		sh_errf(&h->h_lerr, "version %s", h->h_active);
		h->h_state = SH_H_ACTIVE;
		act_over(h);
		make_free(m);
		/* the version on to the lake now, not at the next period */
		env->he_beam(env->he_parg);
		idle(h);
		return;
	}
	sh_errf(env->he_err, "house %s: %s", h->h_name, why);
	sh_errf(&h->h_lerr, "failed: %s", why);
	block_path(h, file, sizeof (file));
	if (m->hm_block && unlink(file) != 0)
		sh_errf(&h->h_lerr, "%s: %s", file, strerror(errno));
	if (m->hm_root != NULL &&
	    tree_destroy(env->he_z, m->hm_root, &h->h_lerr) != 0)
		sh_errf(&h->h_lerr, "not destroyed");
	act_over(h);
	house_drop(m->hm_list, m->hm_n, h);
	make_free(m);
}

/* The properties of a recipe's dataset DEF and canmount=noauto, *N. */
static struct sh_zprop *
make_props(const struct sh_node *def, size_t *n)
{
	const struct sh_pair *pr;
	struct sh_zprop *p;
	size_t i, max;

	max = def != NULL && def->sn_type == SH_OBJECT ? def->sn_obj.ob_n : 0;
	p = sh_calloc(max + 1, sizeof (*p));
	for (i = 0; i < max; i++) {
		pr = &def->sn_obj.ob_v[i];
		p[i].zp_name = pr->pr_key;
		p[i].zp_value = pr->pr_val->sn_type == SH_BOOL ?
		    (pr->pr_val->sn_bool ? "on" : "off") : pr->pr_val->sn_str;
	}
	p[max].zp_name = "canmount";
	p[max].zp_value = "noauto";
	*n = max + 1;
	return (p);
}

/*
 * The house's own datasets: each the recipe gives that the version
 * lacks, made with its properties, a parent it does not give made
 * first as a placeholder, canmount=off; each mounted.
 */
static int
make_datasets(struct sh_job *j, void *arg, struct sh_err *e)
{
	static const struct sh_zprop off = { "canmount", "off" };
	struct sh_hmake *m = arg;
	struct sh_zfs *z = m->hm_h->h_env->he_z;
	const struct sh_node *dss;
	const struct sh_pair *pr;
	struct sh_zprop *p;
	struct names have;
	char name[1024];
	size_t i, k, n;
	int r;

	(void) j;
	dss = sh_obj_get(m->hm_recipe.sr_merged, "datasets");
	r = tree(z, m->hm_root, &have, e);
	for (i = 0; r == 0 && dss != NULL && i < dss->sn_obj.ob_n; i++) {
		pr = &dss->sn_obj.ob_v[i];
		for (k = 0; r == 0; k++) {
			if (pr->pr_key[k] != '/' && pr->pr_key[k] != '\0')
				continue;
			(void) snprintf(name, sizeof (name), "%s/%.*s",
			    m->hm_root, (int)k, pr->pr_key);
			if (!names_have(&have, name)) {
				if (pr->pr_key[k] == '\0') {
					p = make_props(pr->pr_val, &n);
					if ((r = sh_zfs_create(z, name, p, n,
					    e)) == 0)
						r = sh_zfs_mount(z, name, e);
					free(p);
				} else {
					r = sh_zfs_create(z, name, &off, 1, e);
				}
				(void) name_add(&have, name);
			}
			if (pr->pr_key[k] == '\0')
				break;
		}
	}
	names_free(&have);
	return (r);
}

/* house.conf in the version at HEAD: REC shaken, read-only, FLAGS. */
static int
inject(const char *head, struct sh_recipe *rec, unsigned long flags,
    struct sh_err *e)
{
	char *buf = NULL;
	size_t len = 0;
	FILE *f;
	int r;

	f = sh_memstream(&buf, &len);
	sh_show_recipe(f, rec);
	(void) fclose(f);
	r = sh_shaken_write(head, SH_SHAKEN_HOUSE, buf, len, flags, e);
	free(buf);
	return (r);
}

/* house.conf: the shaken house recipe, read-only and schg. */
static int
make_inject(struct sh_job *j, void *arg, struct sh_err *e)
{
	struct sh_hmake *m = arg;

	(void) j;
	return (inject(m->hm_head, &m->hm_recipe, m->hm_h->h_env->he_flags,
	    e));
}

/*
 * The container NAME's recipe resolved into REC on UNDER, the shaken
 * recipe of the foundation version it is made on, its layer named as
 * that version is: its ver held against UNDER's src_branch.
 */
static int
resolve(const char *config, const char **repos, const char *name,
    struct sh_node *under, struct sh_recipe *rec, struct sh_err *e)
{
	const struct sh_node *branch;
	struct sh_branch b;
	char target[512];

	if ((branch = sh_obj_get(under, "src_branch")) == NULL ||
	    branch->sn_type != SH_STR ||
	    sh_syn_branch(branch->sn_str, &b) != NULL) {
		sh_errf(e, "%s: src_branch: invalid", under->sn_from);
		return (-1);
	}
	(void) snprintf(target, sizeof (target), "containers/%s", name);
	if (sh_recipe_resolve(config, repos, target, under, rec, e,
	    NULL) != 0 || sh_recipe_branch(rec, &b, branch->sn_str, e) != 0)
		return (-1);
	return (0);
}

/*
 * ROOT's tree snapshotted at once, every dataset of it, <date>-KIND,
 * shtetl:kind KIND: a create's, a save, a pkg upgrade's.
 */
static int
take(struct sh_zfs *z, const char *root, const char *kind,
    struct sh_err *e)
{
	struct sh_zprop p = { "shtetl:kind", kind };
	struct names t, s = { NULL, 0 };
	char date[32], name[1100];
	time_t now = time(NULL);
	struct tm tm;
	size_t i;
	int r;

	(void) strftime(date, sizeof (date), "%Y-%m-%dT%H_%M_%SZ",
	    gmtime_r(&now, &tm));
	r = tree(z, root, &t, e);
	for (i = 0; r == 0 && i < t.n_n; i++) {
		(void) snprintf(name, sizeof (name), "%s@%s-%s", t.n_v[i],
		    date, kind);
		(void) name_add(&s, name);
	}
	if (r == 0)
		r = sh_zfs_snapshot(z, (const char *const *)s.n_v, s.n_n,
		    &p, 1, e);
	names_free(&s);
	names_free(&t);
	return (r);
}

/* The create snapshot, -r over the version, kind create, taken last. */
static int
make_snapshot(struct sh_job *j, void *arg, struct sh_err *e)
{
	struct sh_hmake *m = arg;

	(void) j;
	return (take(m->hm_h->h_env->he_z, m->hm_root, "create", e));
}

/*
 * The version placed at <containers>/<house>, moved there mounted: the
 * house's active version, which its start reads.
 */
static int
make_place(struct sh_job *j, void *arg, struct sh_err *e)
{
	struct sh_hmake *m = arg;
	const struct sh_houseenv *env = m->hm_h->h_env;
	char want[1024];

	(void) j;
	if (place(env->he_z, env->he_run, m->hm_h->h_name, want,
	    sizeof (want), e) != 0 ||
	    sh_zfs_set(env->he_z, m->hm_root, "mountpoint", want, e) != 0)
		return (-1);
	m->hm_h->h_active = sh_strdup(m->hm_root);
	return (0);
}

/*
 * The house's recipe resolved on the shaken foundation recipe read
 * from the clone, which nothing has run yet: ver held, the recipe's
 * words where not; then the rest of the steps.
 */
static int
make_recipe(struct sh_job *j, void *arg, struct sh_err *e)
{
	struct sh_hmake *m = arg;
	const struct sh_houseenv *env = m->hm_h->h_env;
	char target[512];

	/* the layer named as the foundation version is: its pieces ran */
	(void) snprintf(target, sizeof (target), "foundations/%s@%s",
	    strrchr(m->hm_fds, '/') + 1, m->hm_newest);
	if ((m->hm_under = sh_shaken_read(m->hm_head, NULL,
	    SH_SHAKEN_FOUNDATION, target, e)) == NULL ||
	    resolve(m->hm_config, m->hm_repos, m->hm_h->h_name, m->hm_under,
	    &m->hm_recipe, e) != 0)
		return (-1);
	sh_job_call(j, "datasets", make_datasets, m);
	m->hm_cm.cm_head = m->hm_head;
	m->hm_cm.cm_name = m->hm_h->h_name;
	m->hm_cm.cm_kind = "container";
	m->hm_cm.cm_ran = m->hm_under->sn_from;
	m->hm_cm.cm_recipe = &m->hm_recipe;
	m->hm_cm.cm_pkgs = sh_obj_get(m->hm_recipe.sr_merged, "pkg");
	m->hm_cm.cm_resolv = env->he_resolv;
	sh_commands_steps(j, &m->hm_cm);
	sh_job_call(j, "house.conf", make_inject, m);
	sh_job_call(j, "snapshot", make_snapshot, m);
	sh_job_call(j, "place", make_place, m);
	start_steps(m->hm_h);
	return (0);
}

/* The version mounted at its own path, and where that is. */
static int
make_mount(struct sh_job *j, void *arg, struct sh_err *e)
{
	struct sh_hmake *m = arg;
	struct sh_zfs *z = m->hm_h->h_env->he_z;
	char path[1024];

	if (tree_mount(z, m->hm_root, e) != 0 ||
	    sh_zfs_mounted(z, m->hm_root, path, sizeof (path), e) != 1)
		return (-1);
	m->hm_head = sh_strdup(path);
	sh_job_call(j, "recipe", make_recipe, m);
	return (0);
}

/*
 * The root of the house HOUSE's version made from the foundation
 * snapshot SNAP, below <run>/containers: SNAP's name, its kind left
 * off, then HOUSE, names being for people; or NULL, said to E.
 */
static char *
version_name(struct sh_zfs *z, const char *run, const char *snap,
    const char *house, struct sh_err *e)
{
	char kind[16], *base, *root;
	size_t n, k;

	if (sh_zfs_user(z, snap, "shtetl:kind", kind, sizeof (kind), e) != 0)
		return (NULL);
	base = sh_strdup(strchr(snap, '@') + 1);
	k = strlen(kind) + 1;
	if ((n = strlen(base)) > k && base[n - k] == '-' &&
	    strcmp(base + n - k + 1, kind) == 0)
		base[n - k] = '\0';
	root = sh_calloc(1, strlen(run) + strlen(base) + strlen(house) + 32);
	(void) sprintf(root, "%s/containers/%s-%s", run, base, house);
	free(base);
	return (root);
}

/*
 * Whether REL, below the foundation FDS on the lake, is a placeholder
 * there: 1, 0, or -1. The lake's owner says: every copy is noauto.
 */
static int
placeholder(struct sh_zfs *z, const char *fds, const char *rel,
    struct sh_err *e)
{
	char cm[16], *own = join(fds, rel, "");
	int r;

	r = sh_zfs_native(z, own, "canmount", cm, sizeof (cm), e);
	free(own);
	return (r != 0 ? -1 : strcmp(cm, "off") == 0);
}

/*
 * The copy's tree at the version cloned below <run>/containers, at
 * once: the version named as version_name names it; each clone
 * canmount=noauto, off where the lake's owner is a placeholder; the
 * root shtetl:house.
 */
static int
make_clone(struct sh_hmake *m, const char *copy, struct sh_err *e)
{
	const struct sh_houseenv *env = m->hm_h->h_env;
	struct sh_zfs *z = env->he_z;
	struct sh_zprop p[2];
	struct names t;
	char *top, *at, *to, *own;
	size_t i;
	int r;

	if ((m->hm_root = version_name(z, env->he_run, copy, m->hm_h->h_name,
	    e)) == NULL)
		return (-1);
	top = sh_strdup(copy);
	*strchr(top, '@') = '\0';
	at = strchr(copy, '@');
	r = tree(z, top, &t, e);
	for (i = 0; r == 0 && i < t.n_n; i++) {
		if ((r = placeholder(z, m->hm_fds, t.n_v[i] + strlen(top),
		    e)) < 0)
			break;
		to = join(m->hm_root, t.n_v[i] + strlen(top), "");
		p[0].zp_name = "canmount";
		p[0].zp_value = r ? "off" : "noauto";
		p[1].zp_name = "shtetl:house";
		p[1].zp_value = m->hm_h->h_name;
		own = join(t.n_v[i], at, "");
		r = sh_zfs_clone(z, own, to, p, i == 0 ? 2 : 1, e);
		free(own);
		free(to);
	}
	names_free(&t);
	free(top);
	return (r);
}

/*
 * The beam's word on the pull: the clones made at once, before the
 * convergence's prune, and the job begun; or the create's end.
 */
static void
make_pulled(void *arg, const char *copy, const char *why)
{
	struct sh_hmake *m = arg;
	struct sh_house *h = m->hm_h;
	char *buf = NULL, *w;
	struct sh_err e;
	size_t len = 0;
	FILE *f;
	int r;

	m->hm_pulling = 0;
	sh_job_init(&h->h_job, h->h_log, make_end, m);
	if (why != NULL) {
		w = join("pull: ", why, "");
		make_end(&h->h_job, w, m);
		free(w);
		return;
	}
	f = sh_memstream(&buf, &len);
	sh_err_init(&e, f, "");
	r = make_clone(m, copy, &e);
	(void) fclose(f);
	if (r != 0) {
		buf[strcspn(buf, "\n")] = '\0';
		w = join("clone: ", buf, "");
		make_end(&h->h_job, w, m);
		free(w);
		free(buf);
		return;
	}
	free(buf);
	sh_job_call(&h->h_job, "mount", make_mount, m);
	sh_job_run(&h->h_job, h->h_env->he_kq);
}

int
sh_house_create(struct sh_house ***hp, size_t *np, const char *name,
    const char *fds, const char *newest, const struct sh_houseenv *env,
    const char *config, struct sh_err *e)
{
	struct sh_recipe check;
	struct sh_hmake *m;
	struct sh_house *h;
	char path[1024], *snap;
	int r;

	if ((h = house_of(*hp, *np, name)) != NULL) {
		sh_errf(e, "%s: %s", name, h->h_state == SH_H_CREATING ?
		    "creating" : "exists");
		return (-1);
	}
	m = sh_calloc(1, sizeof (*m));
	if (sh_site_load(config, &m->hm_site, e) != 0) {
		free(m);
		return (-1);
	}
	m->hm_repos = sh_site_repos(&m->hm_site);
	(void) snprintf(path, sizeof (path), "containers/%s", name);
	r = sh_recipe_resolve(config, m->hm_repos, path, NULL, &check, e,
	    NULL);
	sh_recipe_free(&check);
	if (r == 0) {
		h = house_add(hp, np, name, env);
		if ((r = house_log(h, "w", e)) != 0)
			house_drop(hp, np, h);
	}
	if (r != 0) {
		free(m->hm_repos);
		sh_site_free(&m->hm_site);
		free(m);
		return (-1);
	}
	m->hm_list = hp;
	m->hm_n = np;
	m->hm_fds = sh_strdup(fds);
	m->hm_newest = sh_strdup(newest);
	m->hm_config = sh_strdup(config);
	m->hm_pulling = 1;
	h->h_state = SH_H_CREATING;
	h->h_make = m;
	m->hm_h = h;
	snap = join(fds, "@", newest);
	sh_errf(&h->h_lerr, "pull %s", snap);
	env->he_pull(env->he_parg, snap, make_pulled, m);
	free(snap);
	return (0);
}

/* A house's state, as status and a refusal say it. */
static const char *const states[] = {
	[SH_H_ACTIVE] = "active",
	[SH_H_CREATING] = "creating",
	[SH_H_STARTING] = "starting",
	[SH_H_FAILED] = "failed",
	[SH_H_UPGRADING] = "upgrading",
	[SH_H_ROLLING] = "rolling"
};

/* H not active, said to E, "H: upgrading": -1; or 0. */
static int
busy(const struct sh_house *h, struct sh_err *e)
{
	if (h->h_state == SH_H_ACTIVE)
		return (0);
	sh_errf(e, "%s: %s", h->h_name, states[h->h_state]);
	return (-1);
}

int
sh_house_save(struct sh_house *h, struct sh_err *e)
{
	const struct sh_houseenv *env = h->h_env;

	if (busy(h, e) != 0 || take(env->he_z, h->h_active, "save", e) != 0)
		return (-1);
	env->he_beam(env->he_parg);
	return (0);
}

/* The pkg upgrade's snapshot, as a save's, kind pkg. */
static int
pkg_snapshot(struct sh_job *j, void *arg, struct sh_err *e)
{
	struct sh_house *h = arg;

	(void) j;
	return (take(h->h_env->he_z, h->h_active, "pkg", e));
}

/*
 * The house's own packages, its house.conf's less its foundation.conf's
 * (ruled 2026-11-05), upgraded in its running jail, then the snapshot.
 * With none, no pkg: pkg upgrade with no names upgrades everything.
 */
static int
pkg_packages(struct sh_job *j, void *arg, struct sh_err *e)
{
	static const char *const words[] = { "env", "ASSUME_ALWAYS_YES=yes",
	    "pkg", "-j", NULL, "upgrade", "-y", "--" };
	struct sh_house *h = arg;
	const struct sh_node *own, *ran;
	struct sh_node *hn, *fn = NULL;
	char root[1024], *hl, *fl = NULL;
	const char **argv;
	size_t i, k, n;
	int r = -1;

	if ((hn = shaken(h, SH_SHAKEN_HOUSE, &hl, root, sizeof (root),
	    e)) != NULL && (fn = shaken(h, SH_SHAKEN_FOUNDATION, &fl, root,
	    sizeof (root), e)) != NULL) {
		own = sh_obj_get(hn, "pkg");
		ran = sh_obj_get(fn, "pkg");
		n = sizeof (words) / sizeof (words[0]);
		argv = sh_calloc(n + 1 + (own != NULL ? own->sn_obj.ob_n : 0),
		    sizeof (*argv));
		(void) memcpy(argv, words, sizeof (words));
		argv[4] = h->h_name;
		for (i = 0, k = n; own != NULL && i < own->sn_obj.ob_n; i++) {
			if (ran == NULL ||
			    sh_obj_get(ran, own->sn_obj.ob_v[i].pr_key) == NULL)
				argv[k++] = own->sn_obj.ob_v[i].pr_key;
		}
		if (k > n)
			sh_job_child(j, "pkg", argv);
		free(argv);
		sh_job_call(j, "snapshot", pkg_snapshot, h);
		r = 0;
	}
	sh_node_free(fn);
	sh_node_free(hn);
	free(fl);
	free(hl);
	return (r);
}

int
sh_house_pkg(struct sh_house *h, struct sh_err *e)
{
	if (busy(h, e) != 0 || house_log(h, "a", e) != 0)
		return (-1);
	h->h_state = SH_H_UPGRADING;
	sh_job_init(&h->h_job, h->h_log, act_end, h);
	sh_job_call(&h->h_job, "packages", pkg_packages, h);
	sh_job_run(&h->h_job, h->h_env->he_kq);
	return (0);
}

static void
roll_free(struct sh_hroll *r)
{
	size_t i;

	free(r->hr_config);
	free(r->hr_repos);
	sh_site_free(&r->hr_site);
	free(r->hr_fds);
	free(r->hr_copy);
	free(r->hr_next);
	free(r->hr_new);
	names_free(&r->hr_saves);
	for (i = 0; i < r->hr_nds; i++)
		free(r->hr_ds[i].d_rel);
	free(r->hr_ds);
	sh_node_free(r->hr_under);
	sh_node_free(r->hr_had);
	free(r->hr_ulayer);
	free(r->hr_hlayer);
	sh_recipe_free(&r->hr_recipe);
	free(r->hr_why);
	free(r);
}

/* Whether a house besides R's rolls from R's copy of the foundation. */
static int
copy_shared(const struct sh_hroll *r)
{
	const struct sh_house *o;
	size_t i;

	for (i = 0; i < *r->hr_n; i++) {
		o = (*r->hr_list)[i];
		if (o != r->hr_h && o->h_roll != NULL &&
		    strcmp(o->h_roll->hr_copy, r->hr_copy) == 0)
			return (1);
	}
	return (0);
}

/* Whether NAME, a filesystem or a snapshot, is there, nothing said. */
static int
exists(struct sh_zfs *z, const char *name)
{
	char cm[16], *buf = NULL;
	struct sh_err q;
	uint64_t g, txg;
	size_t len = 0;
	FILE *f;
	int r;

	f = sh_memstream(&buf, &len);
	sh_err_init(&q, f, "");
	if (strchr(name, '@') != NULL)
		r = sh_zfs_snapinfo(z, name, &g, &txg, &q) == 0;
	else
		r = sh_zfs_native(z, name, "canmount", cm, sizeof (cm),
		    &q) == 0;
	(void) fclose(f);
	free(buf);
	return (r);
}

/*
 * The next version of the foundation the run's copy holds for R's
 * house (plan 7.2): the oldest snapshot of the copy newer than its
 * active version's origin that has arrived, its shtetl:kind set, the
 * beam's last word on it, and whose shtetl:branch the house's ver
 * allows, its recipe resolved from zconfig as it is now. 0; 1 where
 * none waits; -1 said to E.
 */
static int
roll_next(struct sh_hroll *r, struct sh_err *e)
{
	const struct sh_house *h = r->hr_h;
	struct sh_zfs *z = h->h_env->he_z;
	struct names s = { NULL, 0 };
	struct sh_recipe rec;
	struct sh_branch b;
	char o[1024], kind[16], branch[64], target[300], *snap, *buf = NULL;
	uint64_t g, at, txg;
	struct sh_err q;
	size_t i, len = 0;
	FILE *f;
	int rc;

	/* a version that is a clone of none, a hand's, has none waiting */
	if ((rc = sh_zfs_origin(z, h->h_active, o, sizeof (o), e)) != 1)
		return (rc == 0 ? 1 : -1);
	if (sh_zfs_snapinfo(z, o, &g, &at, e) != 0)
		return (-1);
	r->hr_copy = sh_strdup(o);
	*strchr(r->hr_copy, '@') = '\0';
	r->hr_fds = join(h->h_env->he_lake, "/foundations",
	    strrchr(r->hr_copy, '/'));
	(void) snprintf(target, sizeof (target), "containers/%s", h->h_name);
	if (sh_recipe_resolve(r->hr_config, r->hr_repos, target, NULL, &rec,
	    e, NULL) != 0) {
		sh_recipe_free(&rec);
		return (-1);
	}
	/* what a version not taken says is no one's */
	f = sh_memstream(&buf, &len);
	sh_err_init(&q, f, "");
	rc = sh_zfs_snapshots(z, r->hr_copy, name_add, &s, e);
	for (i = 0; rc == 0 && r->hr_next == NULL && i < s.n_n; i++) {
		snap = join(r->hr_copy, "@", s.n_v[i]);
		if ((rc = sh_zfs_snapinfo(z, snap, &g, &txg, e)) == 0 &&
		    txg > at && sh_zfs_user(z, snap, "shtetl:kind", kind,
		    sizeof (kind), &q) == 0 && sh_zfs_user(z, snap,
		    "shtetl:branch", branch, sizeof (branch), &q) == 0 &&
		    sh_syn_branch(branch, &b) == NULL &&
		    sh_recipe_branch(&rec, &b, branch, &q) == 0)
			r->hr_next = sh_strdup(s.n_v[i]);
		free(snap);
	}
	(void) fclose(f);
	free(buf);
	names_free(&s);
	sh_recipe_free(&rec);
	if (rc != 0)
		return (-1);
	return (r->hr_next == NULL);
}

/* The final save, on the active version, the jail stopped. */
static int
roll_save(struct sh_job *j, void *arg, struct sh_err *e)
{
	struct sh_hroll *r = arg;

	(void) j;
	return (take(r->hr_h->h_env->he_z, r->hr_h->h_active, "save", e));
}

/*
 * A dataset of R's new version, below its root, as a step's argument:
 * the next of R_DS, made as large as the copy's tree before the first,
 * since the steps keep where each one is.
 */
static struct hds *
roll_ds(struct sh_hroll *r, const char *rel, int off)
{
	struct hds *d = &r->hr_ds[r->hr_nds++];

	d->d_r = r;
	d->d_rel = sh_strdup(rel);
	d->d_off = off;
	return (d);
}

/*
 * A placeholder of the foundation's next version, or a dataset the old
 * version lacks, cloned from it as a create clones one: canmount off,
 * or noauto and mounted below the new version's root.
 */
static int
roll_clone(struct sh_job *j, void *arg, struct sh_err *e)
{
	struct hds *d = arg;
	struct sh_hroll *r = d->d_r;
	struct sh_zfs *z = r->hr_h->h_env->he_z;
	struct sh_zprop p = { "canmount", d->d_off ? "off" : "noauto" };
	char *from, *snap, *to;
	int rc;

	(void) j;
	from = join(r->hr_copy, d->d_rel, "");
	snap = join(from, "@", r->hr_next);
	to = join(r->hr_new, d->d_rel, "");
	if ((rc = sh_zfs_clone(z, snap, to, &p, 1, e)) == 0 && !d->d_off)
		rc = sh_zfs_mount(z, to, e);
	free(to);
	free(snap);
	free(from);
	return (rc);
}

/*
 * The clone form's result placed (plan 7.2 step 3): canmount noauto,
 * readonly and mountpoint taken from its parent, at its own path, and
 * mounted; the root shtetl:house, and where it is mounted noted.
 */
static int
roll_place(struct sh_job *j, void *arg, struct sh_err *e)
{
	struct hds *d = arg;
	struct sh_hroll *r = d->d_r;
	struct sh_zfs *z = r->hr_h->h_env->he_z;
	char *ds = join(r->hr_new, d->d_rel, "");
	int rc;

	(void) j;
	/* noauto first, or libzfs mounts it as the mountpoint changes */
	rc = sh_zfs_set(z, ds, "canmount", "noauto", e);
	if (rc == 0)
		rc = sh_zfs_inherit(z, ds, "readonly", e);
	if (rc == 0)
		rc = sh_zfs_inherit(z, ds, "mountpoint", e);
	if (rc == 0 && d->d_rel[0] == '\0')
		rc = sh_zfs_set(z, ds, "shtetl:house", r->hr_h->h_name, e);
	if (rc == 0)
		rc = sh_zfs_mount(z, ds, e);
	if (rc == 0 && d->d_rel[0] == '\0' && sh_zfs_mounted(z, ds,
	    r->hr_head, sizeof (r->hr_head), e) != 1)
		rc = -1;
	free(ds);
	return (rc);
}

/*
 * The replay's steps for REL, a dataset both the old version and the
 * foundation's next version have: the oldest save by the clone form
 * onto the version, the result placed, each later save by the dataset
 * form, -u, -b the save before it and the pre-apply snapshot named
 * for it, -m always (plan 7.2 step 3; probe P6).
 */
static void
roll_replay(struct sh_job *j, struct sh_hroll *r, struct hds *d)
{
	const char *argv[12];
	char *from, *onto, *res, *prev, *next;
	size_t k;

	from = join(r->hr_h->h_active, d->d_rel, "");
	res = join(r->hr_new, d->d_rel, "");
	prev = join(from, "@", r->hr_saves.n_v[0]);
	next = join(r->hr_copy, d->d_rel, "");
	onto = join(next, "@", r->hr_next);
	argv[0] = "-m";
	argv[1] = "--from";
	argv[2] = prev;
	argv[3] = "--onto";
	argv[4] = onto;
	argv[5] = "--result";
	argv[6] = res;
	argv[7] = NULL;
	sh_rebase(j, "replay", argv);
	free(onto);
	free(next);
	sh_job_call(j, "place", roll_place, d);
	for (k = 1; k < r->hr_saves.n_n; k++) {
		next = join(from, "@", r->hr_saves.n_v[k]);
		argv[0] = "-m";
		argv[1] = "-u";
		argv[2] = "-b";
		argv[3] = prev;
		argv[4] = "--from";
		argv[5] = next;
		argv[6] = "--onto";
		argv[7] = res;
		argv[8] = "--result";
		argv[9] = r->hr_saves.n_v[k - 1];
		argv[10] = NULL;
		sh_rebase(j, "replay", argv);
		free(prev);
		prev = next;
	}
	free(prev);
	free(res);
	free(from);
}

/* house.conf in the new version: the recipe shaken, schg. */
static int
roll_inject(struct sh_job *j, void *arg, struct sh_err *e)
{
	struct sh_hroll *r = arg;

	(void) j;
	return (inject(r->hr_head, &r->hr_recipe, r->hr_h->h_env->he_flags,
	    e));
}

/* The update snapshot, -r over the new version, taken last. */
static int
roll_update(struct sh_job *j, void *arg, struct sh_err *e)
{
	struct sh_hroll *r = arg;

	(void) j;
	if (take(r->hr_h->h_env->he_z, r->hr_new, "update", e) != 0)
		return (-1);
	r->hr_complete = 1;
	return (0);
}

/*
 * The swap (plan 7.3): the old version unmounted and given back its own
 * path, then the new one's mountpoint set to <containers>/<house>,
 * which moves it there mounted; it is the active version.
 */
static int
roll_swap(struct sh_job *j, void *arg, struct sh_err *e)
{
	struct sh_hroll *r = arg;
	struct sh_house *h = r->hr_h;
	const struct sh_houseenv *env = h->h_env;
	char want[1024];

	(void) j;
	if (tree_unmount(env->he_z, h->h_active, e) != 0 ||
	    sh_zfs_inherit(env->he_z, h->h_active, "mountpoint", e) != 0 ||
	    place(env->he_z, env->he_run, h->h_name, want, sizeof (want),
	    e) != 0 ||
	    sh_zfs_set(env->he_z, r->hr_new, "mountpoint", want, e) != 0)
		return (-1);
	free(h->h_active);
	h->h_active = sh_strdup(r->hr_new);
	return (0);
}

/* The copy unmounted, where no other house's roll is on it. */
static int
roll_copy(struct sh_job *j, void *arg, struct sh_err *e)
{
	struct sh_hroll *r = arg;

	(void) j;
	if (copy_shared(r))
		return (0);
	return (tree_unmount(r->hr_h->h_env->he_z, r->hr_copy, e));
}

/*
 * What the house's recipe gained since its version was made (plan 7.2
 * step 4): the recipe resolved on the foundation version's shaken
 * recipe as the copy's snapshot of that version holds it, not as the
 * new tree, which the replay wrote into, does; a command piece the old
 * version's house.conf holds alike not run again; its own packages
 * installed. Then house.conf, the update snapshot, the swap, the start
 * and the copy unmounted.
 */
static int
roll_recipe(struct sh_job *j, void *arg, struct sh_err *e)
{
	struct sh_hroll *r = arg;
	struct sh_house *h = r->hr_h;
	const struct sh_houseenv *env = h->h_env;
	char mnt[1024], *f;

	f = join("foundations", strrchr(r->hr_copy, '/'), "");
	r->hr_ulayer = join(f, "@", r->hr_next);
	free(f);
	if ((r->hr_under = read_shaken(env->he_z, r->hr_copy, r->hr_next,
	    SH_SHAKEN_FOUNDATION, r->hr_ulayer, mnt, sizeof (mnt), e)) ==
	    NULL || (r->hr_had = shaken(h, SH_SHAKEN_HOUSE, &r->hr_hlayer,
	    mnt, sizeof (mnt), e)) == NULL || resolve(r->hr_config,
	    r->hr_repos, h->h_name, r->hr_under, &r->hr_recipe, e) != 0)
		return (-1);
	r->hr_cm.cm_head = r->hr_head;
	r->hr_cm.cm_name = h->h_name;
	r->hr_cm.cm_kind = "container";
	r->hr_cm.cm_ran = r->hr_under->sn_from;
	r->hr_cm.cm_had = r->hr_had;
	r->hr_cm.cm_recipe = &r->hr_recipe;
	r->hr_cm.cm_pkgs = sh_obj_get(r->hr_recipe.sr_merged, "pkg");
	r->hr_cm.cm_resolv = env->he_resolv;
	sh_commands_steps(j, &r->hr_cm);
	sh_job_call(j, "house.conf", roll_inject, r);
	sh_job_call(j, "update", roll_update, r);
	sh_job_call(j, "swap", roll_swap, r);
	start_steps(h);
	sh_job_call(j, "copy", roll_copy, r);
	return (0);
}

/*
 * Whether the old version's dataset DS came from the foundation: a
 * clone of a snapshot of R's copy. 1, 0, -1.
 */
static int
from_foundation(struct sh_hroll *r, const char *ds, struct sh_err *e)
{
	size_t n = strlen(r->hr_copy);
	char o[1024];
	int rc;

	if ((rc = sh_zfs_origin(r->hr_h->h_env->he_z, ds, o, sizeof (o),
	    e)) != 1)
		return (rc);
	return (strncmp(o, r->hr_copy, n) == 0 &&
	    (o[n] == '@' || o[n] == '/'));
}

/*
 * The house's own datasets, those of the old version that came from no
 * foundation, cloned from the final save, canmount noauto and mounted
 * (plan 7.2 step 6; probe P5), their placeholders made again,
 * canmount off: before what the recipe gained runs, as a create makes
 * them before its commands.
 */
static int
roll_own(struct sh_job *j, void *arg, struct sh_err *e)
{
	static const struct sh_zprop off = { "canmount", "off" };
	static const struct sh_zprop noauto = { "canmount", "noauto" };
	struct sh_hroll *r = arg;
	struct sh_house *h = r->hr_h;
	struct sh_zfs *z = h->h_env->he_z;
	const char *final = r->hr_saves.n_v[r->hr_saves.n_n - 1];
	struct names old;
	char cm[16], *to, *snap;
	size_t i, n = strlen(h->h_active);
	int rc;

	(void) j;
	rc = tree(z, h->h_active, &old, e);
	for (i = 1; rc == 0 && i < old.n_n; i++) {
		if ((rc = from_foundation(r, old.n_v[i], e)) != 0) {
			rc = rc < 0 ? -1 : 0;
			continue;
		}
		to = join(r->hr_new, old.n_v[i] + n, "");
		if ((rc = sh_zfs_native(z, old.n_v[i], "canmount", cm,
		    sizeof (cm), e)) == 0 && strcmp(cm, "off") == 0) {
			rc = sh_zfs_create(z, to, &off, 1, e);
		} else if (rc == 0) {
			snap = join(old.n_v[i], "@", final);
			if ((rc = sh_zfs_clone(z, snap, to, &noauto, 1,
			    e)) == 0)
				rc = sh_zfs_mount(z, to, e);
			free(snap);
		}
		free(to);
	}
	names_free(&old);
	return (rc);
}

/*
 * The plan, once the old version holds its final save: the saves,
 * roll.depth deep; the new version named; then, for each dataset of
 * the foundation's next version, parent first, its copy mounted, since
 * zfs_rebase reads every side through a mounted dataset, and its steps;
 * then the rest of the roll. A placeholder's copy is not mounted: a
 * copy is noauto, its owner's placeholders too, and one mounted would
 * hide the directories below it that its children mount on (build run
 * 13). A dataset the old version took from the foundation that the
 * next version lacks stops it here, before anything is made.
 */
static int
roll_plan(struct sh_job *j, void *arg, struct sh_err *e)
{
	struct sh_hroll *r = arg;
	struct sh_house *h = r->hr_h;
	struct sh_zfs *z = h->h_env->he_z;
	struct names all = { NULL, 0 }, old = { NULL, 0 }, cp = { NULL, 0 };
	int64_t count, since, when;
	const char *rel;
	char *name, *snap;
	size_t i, k, n = strlen(h->h_active);
	int rc, off;

	if (sh_zfs_snapshots(z, h->h_active, name_add, &all, e) != 0) {
		names_free(&all);
		return (-1);
	}
	sh_site_depth(&r->hr_site, (int64_t)time(NULL), &count, &since);
	rc = 0;
	for (i = 0; rc == 0 && i < all.n_n; i++) {
		if (count > 0 && all.n_n - i > (size_t)count)
			continue;
		name = join(h->h_active, "@", all.n_v[i]);
		if (since > 0 && (rc = sh_zfs_created(z, name, &when,
		    e)) == 0 && when < since) {
			free(name);
			continue;
		}
		free(name);
		if (rc == 0)
			(void) name_add(&r->hr_saves, all.n_v[i]);
	}
	names_free(&all);
	snap = join(r->hr_copy, "@", r->hr_next);
	if (rc != 0 || (r->hr_new = version_name(z, h->h_env->he_run, snap,
	    h->h_name, e)) == NULL || tree(z, h->h_active, &old, e) != 0 ||
	    tree(z, r->hr_copy, &cp, e) != 0)
		rc = -1;
	free(snap);
	/* the version's tree: a dataset it dropped stays while it is used */
	for (i = k = 0; rc == 0 && i < cp.n_n; i++) {
		snap = join(cp.n_v[i], "@", r->hr_next);
		if (exists(z, snap))
			cp.n_v[k++] = cp.n_v[i];
		else
			free(cp.n_v[i]);
		free(snap);
	}
	if (rc == 0)
		cp.n_n = k;
	for (i = 0; rc == 0 && i < old.n_n; i++) {
		name = join(r->hr_copy, old.n_v[i] + n, "");
		if ((rc = from_foundation(r, old.n_v[i], e)) == 1 &&
		    !names_have(&cp, name)) {
			sh_errf(e, "%s: not in %s@%s", old.n_v[i] + n + 1,
			    r->hr_copy, r->hr_next);
			rc = -1;
		}
		rc = rc < 0 ? -1 : 0;
		free(name);
	}
	if (rc == 0)
		r->hr_ds = sh_calloc(cp.n_n, sizeof (*r->hr_ds));
	for (i = 0; rc == 0 && i < cp.n_n; i++) {
		rel = cp.n_v[i] + strlen(r->hr_copy);
		name = join(h->h_active, rel, "");
		if ((off = placeholder(z, r->hr_fds, rel, e)) < 0 ||
		    (!off && sh_zfs_mount(z, cp.n_v[i], e) != 0))
			rc = -1;
		else if (!off && names_have(&old, name))
			roll_replay(j, r, roll_ds(r, rel, 0));
		else
			sh_job_call(j, "clone", roll_clone, roll_ds(r, rel,
			    off));
		free(name);
	}
	names_free(&cp);
	names_free(&old);
	if (rc == 0) {
		sh_job_call(j, "own", roll_own, r);
		sh_job_call(j, "recipe", roll_recipe, r);
	}
	return (rc);
}

/*
 * The undoing of a roll that failed before its update snapshot: every
 * open zfs_rebase run on the new version already aborted, the new
 * version destroyed, the copy unmounted where no other house's roll
 * is on it, and the house stalled on its old version, shtetl:stall the
 * step and why (plan 7.4).
 */
static int
roll_undo(struct sh_job *j, void *arg, struct sh_err *e)
{
	struct sh_hroll *r = arg;
	struct sh_house *h = r->hr_h;
	struct sh_zfs *z = h->h_env->he_z;

	(void) j;
	/* stalled first: a refusal after it leaves H stalled, not idle */
	if (sh_zfs_set(z, h->h_active, "shtetl:stall", r->hr_why, e) != 0)
		return (-1);
	free(h->h_stall);
	h->h_stall = sh_strdup(r->hr_why);
	if ((r->hr_new != NULL && exists(z, r->hr_new) &&
	    tree_destroy(z, r->hr_new, e) != 0) ||
	    (!copy_shared(r) && tree_unmount(z, r->hr_copy, e) != 0))
		return (-1);
	return (0);
}

/* The undoing over: the old version started again, stalled. */
static void
undo_end(struct sh_job *j, const char *why, void *arg)
{
	struct sh_hroll *r = arg;
	struct sh_house *h = r->hr_h;

	(void) j;
	if (why != NULL) {
		sh_errf(h->h_env->he_err, "house %s: %s", h->h_name, why);
		sh_errf(&h->h_lerr, "failed: %s", why);
	}
	/* where ZFS refused the stall, H is stalled until the next start */
	if (h->h_stall == NULL)
		h->h_stall = sh_strdup(r->hr_why);
	h->h_state = SH_H_ACTIVE;
	h->h_roll = NULL;
	act_over(h);
	roll_free(r);
	idle(h);
}

/*
 * The roll over: the house active on its new version, the beam told;
 * or, failed before the update snapshot, its undoing begun and then
 * the old version's start, the job taken on again; or, failed after
 * it, said, the next start taking the version as it finds it.
 */
static void
roll_end(struct sh_job *j, const char *why, void *arg)
{
	struct sh_hroll *r = arg;
	struct sh_house *h = r->hr_h;
	const struct sh_houseenv *env = h->h_env;

	(void) j;
	if (why == NULL) {
		sh_errf(&h->h_lerr, "version %s", h->h_active);
		h->h_state = SH_H_ACTIVE;
		h->h_roll = NULL;
		act_over(h);
		roll_free(r);
		env->he_beam(env->he_parg);
		idle(h);
		return;
	}
	sh_errf(env->he_err, "house %s: %s", h->h_name, why);
	sh_errf(&h->h_lerr, "failed: %s", why);
	if (r->hr_complete) {
		/* the swap not done, the next start does it */
		h->h_state = strcmp(h->h_active, r->hr_new) == 0 ?
		    SH_H_ACTIVE : SH_H_FAILED;
		h->h_roll = NULL;
		act_over(h);
		roll_free(r);
		idle(h);
		return;
	}
	r->hr_why = sh_strdup(why);
	sh_job_init(&h->h_job, h->h_log, undo_end, r);
	if (r->hr_new != NULL && exists(env->he_z, r->hr_new))
		aborts(&h->h_job, env->he_z, r->hr_new, &h->h_lerr);
	sh_job_call(&h->h_job, "undo", roll_undo, r);
	start_steps(h);
	sh_job_run(&h->h_job, env->he_kq);
}

/*
 * A roll of H, active, begun onto the next version waiting (roll_next):
 * 0; 1 where none waits; -1 said to E.
 */
static int
roll_begin(struct sh_house ***hp, size_t *np, struct sh_house *h,
    const char *config, struct sh_err *e)
{
	const struct sh_houseenv *env = h->h_env;
	struct sh_hroll *r;
	char file[1024];
	int rc;

	r = sh_calloc(1, sizeof (*r));
	r->hr_h = h;
	r->hr_list = hp;
	r->hr_n = np;
	r->hr_config = sh_strdup(config);
	if (sh_site_load(config, &r->hr_site, e) != 0) {
		free(r->hr_config);
		free(r);
		return (-1);
	}
	r->hr_repos = sh_site_repos(&r->hr_site);
	if ((rc = roll_next(r, e)) != 0 || house_log(h, "a", e) != 0) {
		roll_free(r);
		return (rc == 1 ? 1 : -1);
	}
	h->h_state = SH_H_ROLLING;
	h->h_roll = r;
	sh_job_init(&h->h_job, h->h_log, roll_end, r);
	block_path(h, file, sizeof (file));
	sh_jail_stop(&h->h_job, file, h->h_name);
	sh_job_call(&h->h_job, "save", roll_save, r);
	sh_job_call(&h->h_job, "replay", roll_plan, r);
	sh_job_run(&h->h_job, env->he_kq);
	return (0);
}

int
sh_house_roll(struct sh_house ***hp, size_t *np, struct sh_house *h,
    const char *config, struct sh_err *e)
{
	int rc;

	if (busy(h, e) != 0)
		return (-1);
	/* any stall lifted first, a freeze among them (plan 7.4) */
	if (h->h_stall != NULL) {
		if (sh_zfs_inherit(h->h_env->he_z, h->h_active, "shtetl:stall",
		    e) != 0)
			return (-1);
		free(h->h_stall);
		h->h_stall = NULL;
	}
	if ((rc = roll_begin(hp, np, h, config, e)) == 1)
		sh_errf(e, "%s: up to date", h->h_name);
	return (rc == 0 ? 0 : -1);
}

int
sh_house_freeze(struct sh_house *h, struct sh_err *e)
{
	if (busy(h, e) != 0 || sh_zfs_set(h->h_env->he_z, h->h_active,
	    "shtetl:stall", "frozen", e) != 0)
		return (-1);
	free(h->h_stall);
	h->h_stall = sh_strdup("frozen");
	return (0);
}

void
sh_house_next(struct sh_house ***hp, size_t *np, struct sh_house *h,
    const char *config)
{
	const struct sh_houseenv *env = h->h_env;
	char *buf = NULL, *why, pfx[300];
	struct sh_err q, he;
	size_t len = 0;
	FILE *f;

	if (h->h_state != SH_H_ACTIVE || h->h_stall != NULL)
		return;
	f = sh_memstream(&buf, &len);
	sh_err_init(&q, f, "");
	if (roll_begin(hp, np, h, config, &q) < 0) {
		(void) fflush(f);
		why = sh_calloc(1, len + 8);
		(void) snprintf(why, len + 8, "next: %.*s",
		    (int)strcspn(buf, "\n"), buf);
		(void) snprintf(pfx, sizeof (pfx), "%shouse %s: ",
		    env->he_err->se_prefix, h->h_name);
		sh_err_init(&he, env->he_err->se_out, pfx);
		sh_errf(&he, "%s", why);
		/* stalled, not asked again; in ZFS too, where it takes it */
		(void) sh_zfs_set(env->he_z, h->h_active, "shtetl:stall", why,
		    &he);
		h->h_stall = why;
	}
	(void) fclose(f);
	free(buf);
}

void
sh_house_status(const struct sh_house *h, FILE *out)
{
	const char *step;

	if (h->h_state == SH_H_ACTIVE && h->h_stall != NULL) {
		(void) fprintf(out, "house %s stalled %s\n", h->h_name,
		    h->h_stall);
		return;
	}
	if (h->h_state == SH_H_ACTIVE) {
		(void) fprintf(out, "house %s active %s\n", h->h_name,
		    strrchr(h->h_active, '/') + 1);
		return;
	}
	if (h->h_state == SH_H_FAILED) {
		(void) fprintf(out, "house %s failed\n", h->h_name);
		return;
	}
	step = h->h_make != NULL && h->h_make->hm_pulling ? "pull" :
	    sh_job_step(&h->h_job);
	(void) fprintf(out, "house %s %s %s\n", h->h_name,
	    states[h->h_state], step != NULL ? step : "");
}

void
sh_house_stop(struct sh_house *h)
{
	if (h->h_state == SH_H_ACTIVE || h->h_state == SH_H_FAILED)
		return;
	if (h->h_make == NULL || !h->h_make->hm_pulling)
		sh_job_stop(&h->h_job, h->h_env->he_kq);
	if (h->h_make != NULL)
		make_free(h->h_make);
	h->h_make = NULL;
	if (h->h_roll != NULL)
		roll_free(h->h_roll);
	h->h_roll = NULL;
	act_over(h);
}
