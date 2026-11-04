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

#include <sys/types.h>

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
#include "core/shake.h"
#include "core/show.h"
#include "core/site.h"
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

/* ROOT's tree destroyed, unmounted, children first, snapshots first. */
static int
tree_destroy(struct sh_zfs *z, const char *root, struct sh_err *e)
{
	struct names t, s;
	char *snap;
	size_t i, k;
	int r;

	r = tree(z, root, &t, e);
	for (i = t.n_n; r == 0 && i > 0; i--)
		r = sh_zfs_unmount(z, t.n_v[i - 1], e);
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
	free(h->h_name);
	free(h->h_active);
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

/* Where H's jail's block is: <jails>/H.conf, into BUF. */
static void
block_path(const struct sh_house *h, char *buf, size_t len)
{
	(void) snprintf(buf, len, "%s/%s.conf", h->h_env->he_jails,
	    h->h_name);
}

/*
 * The jail's block written from the active version's shaken recipe,
 * read from its create or update snapshot, which nothing in the jail
 * could have written (plan 5): through the snapshot of the dataset
 * holding var/db/shtetl, the one mounted nearest above it.
 */
static int
start_block(struct sh_job *j, void *arg, struct sh_err *e)
{
	struct sh_house *h = arg;
	struct sh_zfs *z = h->h_env->he_z;
	char snap[256], root[1024], at[1024], best[1024], want[1100];
	char file[1024], *layer;
	struct sh_node *n;
	struct names t;
	size_t i, k, len;
	int r;

	(void) j;
	snap[0] = '\0';
	if (complete(z, h->h_active, snap, sizeof (snap), e) < 0 ||
	    (r = sh_zfs_mounted(z, h->h_active, root, sizeof (root), e)) < 0)
		return (-1);
	if (r == 0) {
		sh_errf(e, "%s: not mounted", h->h_active);
		return (-1);
	}
	(void) snprintf(want, sizeof (want), "%s/%s", root, SH_SHAKEN_HOUSE);
	(void) snprintf(best, sizeof (best), "%s", root);
	len = strlen(root);
	r = tree(z, h->h_active, &t, e);
	for (i = 1; r == 0 && i < t.n_n; i++) {
		if ((r = sh_zfs_mounted(z, t.n_v[i], at, sizeof (at),
		    e)) == 1 && (k = strlen(at)) > len &&
		    strncmp(want, at, k) == 0 && want[k] == '/') {
			(void) snprintf(best, sizeof (best), "%s", at);
			len = k;
		}
		r = r < 0 ? -1 : 0;
	}
	names_free(&t);
	if (r != 0)
		return (-1);
	/* the tree's layer, which outlives it */
	layer = join(h->h_active, "@", snap);
	r = -1;
	if ((n = sh_shaken_read(best, snap, want + len + 1, layer,
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

/* The start over: the house active, WHY said where it failed. */
static void
start_end(struct sh_job *j, const char *why, void *arg)
{
	struct sh_house *h = arg;

	(void) j;
	h->h_state = SH_H_ACTIVE;
	if (why != NULL) {
		sh_errf(h->h_env->he_err, "house %s: %s", h->h_name, why);
		sh_errf(&h->h_lerr, "failed: %s", why);
	}
	act_over(h);
}

/* H's jail started from its active version, a job of its own. */
static void
house_start(struct sh_house *h)
{
	const struct sh_houseenv *env = h->h_env;

	if (house_log(h, "a", env->he_err) != 0)
		return;
	h->h_state = SH_H_STARTING;
	sh_job_init(&h->h_job, h->h_log, start_end, h);
	start_steps(h);
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

int
sh_houses_start(const struct sh_houseenv *env, struct sh_house ***hp,
    size_t *np, struct sh_err *e)
{
	char *c = join(env->he_run, "/containers", ""), house[256], o[1024];
	char snap[256];
	struct sh_zfs *z = env->he_z;
	struct names tops = { NULL, 0 };
	struct hver *v = NULL;
	struct sh_house *h;
	uint64_t guid, txg;
	size_t i, k, n = 0;
	int r, best;

	*hp = NULL;
	*np = 0;
	r = sh_zfs_children(z, c, name_add, &tops, e);
	for (i = 0; r == 0 && i < tops.n_n; i++) {
		if ((r = sh_zfs_user(z, tops.n_v[i], "shtetl:house", house,
		    sizeof (house), e)) == 1) {
			r = 0;
			continue;
		}
		if (r != 0 || (r = complete(z, tops.n_v[i], snap,
		    sizeof (snap), e)) < 0)
			break;
		if (r == 0) {
			/* a create a crash cut short */
			r = tree_destroy(z, tops.n_v[i], e);
			continue;
		}
		r = 0;
		txg = 0;
		if (sh_zfs_origin(z, tops.n_v[i], o, sizeof (o), e) == 1)
			r = sh_zfs_snapinfo(z, o, &guid, &txg, e);
		v = sh_realloc(v, (n + 1) * sizeof (*v));
		v[n].hv_root = sh_strdup(tops.n_v[i]);
		v[n].hv_house = sh_strdup(house);
		v[n++].hv_txg = txg;
		if ((h = house_of(*hp, *np, house)) == NULL)
			(void) house_add(hp, np, house, env);
	}
	for (i = 0; r == 0 && i < *np; i++) {
		h = (*hp)[i];
		best = -1;
		for (k = 0; k < n; k++) {
			if (strcmp(v[k].hv_house, h->h_name) == 0 &&
			    (best < 0 || v[k].hv_txg > v[best].hv_txg))
				best = (int)k;
		}
		if ((r = activate(z, env->he_run, h, v[best].hv_root,
		    e)) == 0)
			house_start(h);
	}
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

/* house.conf: the shaken house recipe, read-only and schg. */
static int
make_inject(struct sh_job *j, void *arg, struct sh_err *e)
{
	struct sh_hmake *m = arg;
	char *buf = NULL;
	size_t len = 0;
	FILE *f;
	int r;

	(void) j;
	f = sh_memstream(&buf, &len);
	sh_show_recipe(f, &m->hm_recipe);
	(void) fclose(f);
	r = sh_shaken_write(m->hm_head, SH_SHAKEN_HOUSE, buf, len,
	    m->hm_h->h_env->he_flags, e);
	free(buf);
	return (r);
}

/* The create snapshot, -r over the version, kind create, taken last. */
static int
make_snapshot(struct sh_job *j, void *arg, struct sh_err *e)
{
	static const struct sh_zprop kind = { "shtetl:kind", "create" };
	struct sh_hmake *m = arg;
	struct sh_zfs *z = m->hm_h->h_env->he_z;
	struct names t, s = { NULL, 0 };
	char date[32], name[1100];
	time_t now = time(NULL);
	struct tm tm;
	size_t i;
	int r;

	(void) j;
	(void) strftime(date, sizeof (date), "%Y-%m-%dT%H_%M_%SZ",
	    gmtime_r(&now, &tm));
	r = tree(z, m->hm_root, &t, e);
	for (i = 0; r == 0 && i < t.n_n; i++) {
		(void) snprintf(name, sizeof (name), "%s@%s-create", t.n_v[i],
		    date);
		(void) name_add(&s, name);
	}
	if (r == 0)
		r = sh_zfs_snapshot(z, (const char *const *)s.n_v, s.n_n,
		    &kind, 1, e);
	names_free(&s);
	names_free(&t);
	return (r);
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
	const struct sh_node *branch;
	struct sh_branch b;
	char target[512];

	/* the layer named as the foundation version is: its pieces ran */
	(void) snprintf(target, sizeof (target), "foundations/%s@%s",
	    strrchr(m->hm_fds, '/') + 1, m->hm_newest);
	if ((m->hm_under = sh_shaken_read(m->hm_head, NULL,
	    SH_SHAKEN_FOUNDATION, target, e)) == NULL)
		return (-1);
	if ((branch = sh_obj_get(m->hm_under, "src_branch")) == NULL ||
	    branch->sn_type != SH_STR ||
	    sh_syn_branch(branch->sn_str, &b) != NULL) {
		sh_errf(e, "%s: src_branch: invalid", target);
		return (-1);
	}
	(void) snprintf(target, sizeof (target), "containers/%s",
	    m->hm_h->h_name);
	if (sh_recipe_resolve(m->hm_config, m->hm_repos, target, m->hm_under,
	    &m->hm_recipe, e, NULL) != 0 ||
	    sh_recipe_branch(&m->hm_recipe, &b, branch->sn_str, e) != 0)
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
 * The copy's tree at the version cloned below <run>/containers, at
 * once: the version's name the foundation snapshot's, its kind left
 * off, then the house's; each clone canmount=noauto, off where the
 * lake's owner is a placeholder; the root shtetl:house.
 */
static int
make_clone(struct sh_hmake *m, const char *copy, struct sh_err *e)
{
	const struct sh_houseenv *env = m->hm_h->h_env;
	struct sh_zfs *z = env->he_z;
	struct sh_zprop p[2];
	struct names t;
	char kind[16], cm[16], *top, *at, *base, *to, *own;
	size_t i, n, k;
	int r;

	top = sh_strdup(copy);
	*strchr(top, '@') = '\0';
	at = strchr(copy, '@');
	if ((r = sh_zfs_user(z, copy, "shtetl:kind", kind, sizeof (kind),
	    e)) != 0) {
		free(top);
		return (-1);
	}
	/* the snapshot's name without its kind: names are for people */
	base = sh_strdup(at + 1);
	k = strlen(kind) + 1;
	if ((n = strlen(base)) > k && base[n - k] == '-' &&
	    strcmp(base + n - k + 1, kind) == 0)
		base[n - k] = '\0';
	m->hm_root = sh_calloc(1, strlen(env->he_run) + strlen(base) +
	    strlen(m->hm_h->h_name) + 32);
	(void) sprintf(m->hm_root, "%s/containers/%s-%s", env->he_run, base,
	    m->hm_h->h_name);
	free(base);
	r = tree(z, top, &t, e);
	for (i = 0; r == 0 && i < t.n_n; i++) {
		own = join(m->hm_fds, t.n_v[i] + strlen(top), "");
		r = sh_zfs_native(z, own, "canmount", cm, sizeof (cm), e);
		free(own);
		if (r != 0)
			break;
		to = join(m->hm_root, t.n_v[i] + strlen(top), "");
		p[0].zp_name = "canmount";
		p[0].zp_value = strcmp(cm, "off") == 0 ? "off" : "noauto";
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

void
sh_house_status(const struct sh_house *h, FILE *out)
{
	const char *step;

	if (h->h_state == SH_H_ACTIVE) {
		(void) fprintf(out, "house %s active %s\n", h->h_name,
		    strrchr(h->h_active, '/') + 1);
		return;
	}
	step = h->h_make != NULL && h->h_make->hm_pulling ? "pull" :
	    sh_job_step(&h->h_job);
	(void) fprintf(out, "house %s %s %s\n", h->h_name,
	    h->h_state == SH_H_CREATING ? "creating" : "starting",
	    step != NULL ? step : "");
}

void
sh_house_stop(struct sh_house *h)
{
	if (h->h_state == SH_H_ACTIVE)
		return;
	if (h->h_make == NULL || !h->h_make->hm_pulling)
		sh_job_stop(&h->h_job);
	if (h->h_make != NULL)
		make_free(h->h_make);
	h->h_make = NULL;
	act_over(h);
}
