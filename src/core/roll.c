/*
 * A foundation's roll: its steps, each a call of the job that adds the
 * next, and its end.
 */

#include <sys/types.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "core/builder.h"
#include "core/cascade.h"
#include "core/commands.h"
#include "core/err.h"
#include "core/foundation.h"
#include "core/install.h"
#include "core/job.h"
#include "core/mem.h"
#include "core/roll.h"
#include "core/root.h"
#include "core/shake.h"
#include "core/show.h"
#include "core/site.h"
#include "core/src.h"
#include "core/syntax.h"
#include "core/tree.h"
#include "core/zfs.h"

struct sh_roll {
	struct sh_job		r_job;		/* first: a kevent's udata */
	struct sh_foundation	*r_f;
	const struct sh_rollenv	*r_env;
	char			*r_config;
	char			*r_build;
	struct sh_site		r_site;
	const char		**r_repos;
	struct sh_recipe	r_recipe;
	struct sh_src		r_src;
	struct sh_branch	r_branch;
	char			*r_head;	/* where F is mounted */
	char			*r_srcdir;	/* the checkout */
	char			*r_obj;		/* where F's obj cache is */
	struct sh_install	r_install;
	struct sh_commands	r_commands;
	char			r_ver[48];	/* 15.1-releng */
	char			r_commit[72];
	char			*r_snap;	/* the version made */
	const char		*r_kind;	/* create, update or pkg */
	struct sh_node		*r_shaken;	/* a pkg roll's version's */
	int			r_pkg;		/* a pkg roll */
	int			r_first;	/* F had no version: a create */
	int			r_made;		/* F made by this roll */
	int			r_reset;	/* zbuild's reset under way */
	FILE			*r_log;		/* F.log, made anew */
	struct sh_err		r_lerr;		/* what the roll says, there */
};

/* Names a walk found. */
struct names {
	char	**n_v;
	size_t	n_n;
};

static int
roll_name(void *arg, const char *name)
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

/* DS and every filesystem below it into N, each before its children. */
static int
roll_tree(struct sh_zfs *z, const char *ds, struct names *n,
    struct sh_err *e)
{
	size_t i;

	(void) roll_name(n, ds);
	for (i = 0; i < n->n_n; i++) {
		if (sh_zfs_children(z, n->n_v[i], roll_name, n, e) != 0)
			return (-1);
	}
	return (0);
}

/* A property's value as ZFS takes it: true and false are on and off. */
static const char *
roll_value(const struct sh_node *v)
{
	if (v->sn_type == SH_BOOL)
		return (v->sn_bool ? "on" : "off");
	return (v->sn_str);
}

/*
 * The properties of DEF, those of OVER in place of the same names, and
 * OVER's others, into a new array of *N; either may be no object.
 */
static struct sh_zprop *
roll_props(const struct sh_node *def, const struct sh_node *over, size_t *n)
{
	const struct sh_node *sets[2] = { def, over };
	const struct sh_pair *pr;
	struct sh_zprop *p;
	size_t i, k, max = 0;

	for (k = 0; k < 2; k++) {
		if (sets[k] == NULL || sets[k]->sn_type != SH_OBJECT)
			sets[k] = NULL;
		else
			max += sets[k]->sn_obj.ob_n;
	}
	p = sh_calloc(max + 1, sizeof (*p));
	*n = 0;
	for (k = 0; k < 2; k++) {
		for (i = 0; sets[k] != NULL && i < sets[k]->sn_obj.ob_n; i++) {
			pr = &sets[k]->sn_obj.ob_v[i];
			if (k == 0 && sh_obj_get(sets[1], pr->pr_key) != NULL)
				continue;
			p[*n].zp_name = pr->pr_key;
			p[(*n)++].zp_value = roll_value(pr->pr_val);
		}
	}
	return (p);
}

/*
 * Every ver of the recipe allowing some version from LO to HI (major *
 * SH_VER_MINORS + minor), the versions NAMED: 0; -1 with the first
 * that does not.
 */
static int
roll_ver(const struct sh_roll *r, long lo, long hi, const char *named,
    struct sh_err *e)
{
	const struct sh_node *ver, *c;
	long clo, chi;
	size_t i;

	ver = sh_obj_get(r->r_recipe.sr_merged, "ver");
	for (i = 0; ver != NULL && i < ver->sn_arr.ar_n; i++) {
		c = ver->sn_arr.ar_v[i];
		sh_ver_range(c->sn_str, &clo, &chi);
		if (chi < lo || clo > hi) {
			sh_errf(e, "%s: ver: %s is not %s", c->sn_from, named,
			    c->sn_str);
			return (-1);
		}
	}
	return (0);
}

/*
 * The head unmounted, children first, and every dataset of it
 * snapshotted at once.
 */
static int
roll_snapshot(struct sh_job *j, void *arg, struct sh_err *e)
{
	struct sh_roll *r = arg;
	struct sh_zfs *z = r->r_env->re_z;
	struct names tree = { NULL, 0 }, snaps = { NULL, 0 };
	struct sh_zprop p[2];
	char snap[128], date[32], name[512];
	time_t now = time(NULL);
	struct tm tm;
	size_t i;
	int rc = -1;

	(void) j;
	if (roll_tree(z, r->r_f->f_ds, &tree, e) != 0)
		goto out;
	for (i = tree.n_n; i > 0; i--) {
		if (sh_zfs_unmount(z, tree.n_v[i - 1], e) != 0)
			goto out;
	}
	(void) strftime(date, sizeof (date), "%Y-%m-%dT%H_%M_%SZ",
	    gmtime_r(&now, &tm));
	(void) snprintf(snap, sizeof (snap), "%s-%s-%s", r->r_ver, date,
	    r->r_kind);
	for (i = 0; i < tree.n_n; i++) {
		(void) snprintf(name, sizeof (name), "%s@%s", tree.n_v[i],
		    snap);
		(void) roll_name(&snaps, name);
	}
	p[0].zp_name = "shtetl:kind";
	p[0].zp_value = r->r_kind;
	p[1].zp_name = "shtetl:commit";
	p[1].zp_value = r->r_commit;
	if ((rc = sh_zfs_snapshot(z, (const char *const *)snaps.n_v,
	    snaps.n_n, p, 2, e)) == 0)
		r->r_snap = sh_strdup(snap);
out:
	names_free(&snaps);
	names_free(&tree);
	return (rc);
}

/* foundation.conf: what the roll fetched, then the shaken recipe. */
static int
roll_inject(struct sh_job *j, void *arg, struct sh_err *e)
{
	struct sh_roll *r = arg;
	char *buf = NULL;
	size_t len = 0;
	FILE *f;
	int rc;

	f = sh_memstream(&buf, &len);
	rc = sh_src_record(f, &r->r_src, e);
	sh_show_recipe(f, &r->r_recipe);
	(void) fclose(f);
	if (rc == 0)
		rc = sh_shaken_write(r->r_head, SH_SHAKEN_FOUNDATION, buf, len,
		    r->r_env->re_flags, e);
	free(buf);
	if (rc == 0)
		sh_job_call(j, "snapshot", roll_snapshot, r);
	return (rc);
}

/*
 * F's obj cache made where missing, with the site's lake properties,
 * and mounted; the build and the install added, the packages and the
 * commands, then the inject.
 */
static int
roll_obj(struct sh_job *j, void *arg, struct sh_err *e)
{
	struct sh_roll *r = arg;
	struct sh_zfs *z = r->r_env->re_z;
	struct names have = { NULL, 0 };
	const struct sh_node *props, *jobs;
	struct sh_zprop *p;
	char ds[512], path[1024];
	size_t n;
	int rc;

	(void) snprintf(ds, sizeof (ds), "%s/%s", r->r_env->re_obj,
	    r->r_f->f_name);
	if ((rc = sh_zfs_children(z, r->r_env->re_obj, roll_name, &have,
	    e)) == 0 && !names_have(&have, ds)) {
		props = sh_obj_get(r->r_site.ss_merged, "properties");
		p = roll_props(sh_obj_get(props, "default"),
		    sh_obj_get(props, "lake"), &n);
		rc = sh_zfs_create(z, ds, p, n, e);
		free(p);
	}
	names_free(&have);
	if (rc != 0 || sh_zfs_mount(z, ds, e) != 0 ||
	    sh_zfs_mounted(z, ds, path, sizeof (path), e) != 1)
		return (-1);
	r->r_obj = sh_strdup(path);
	r->r_srcdir = sh_root_path(r->r_build, "src");
	jobs = sh_obj_get(sh_obj_get(r->r_site.ss_merged, "build"),
	    "make_jobs");
	r->r_install.in_src = r->r_srcdir;
	r->r_install.in_config = r->r_config;
	r->r_install.in_obj = r->r_obj;
	r->r_install.in_head = r->r_head;
	r->r_install.in_name = r->r_f->f_name;
	r->r_install.in_recipe = &r->r_recipe;
	r->r_install.in_jobs = jobs != NULL ? (long)jobs->sn_int : 0;
	r->r_install.in_update = !r->r_first;
	if (sh_install_steps(j, &r->r_install, e) != 0)
		return (-1);
	r->r_commands.cm_head = r->r_head;
	r->r_commands.cm_name = r->r_f->f_name;
	r->r_commands.cm_recipe = &r->r_recipe;
	r->r_commands.cm_pkgs = sh_obj_get(r->r_recipe.sr_merged, "pkg");
	r->r_commands.cm_resolv = r->r_env->re_resolv;
	sh_commands_steps(j, &r->r_commands);
	sh_job_call(j, "inject", roll_inject, r);
	return (0);
}

/* REVISION, N.M, as the checkout's sys/conf/newvers.sh sets it. */
static int
roll_revision(const struct sh_roll *r, long *major, long *minor,
    struct sh_err *e)
{
	char *path, *line = NULL;
	size_t cap = 0;
	int found = 0;
	FILE *f;

	path = sh_root_path(r->r_build, "src/sys/conf/newvers.sh");
	f = fopen(path, "r");
	free(path);
	if (f == NULL) {
		sh_errf(e, "sys/conf/newvers.sh: %s", strerror(errno));
		return (-1);
	}
	while (!found && getline(&line, &cap, f) > 0)
		found = sscanf(line, "REVISION=\"%ld.%ld\"", major, minor) == 2;
	free(line);
	(void) fclose(f);
	if (!found) {
		sh_errf(e, "sys/conf/newvers.sh: no REVISION");
		return (-1);
	}
	return (0);
}

/* The version: named, held to ver, its commit read. */
static int
roll_version(struct sh_job *j, void *arg, struct sh_err *e)
{
	struct sh_roll *r = arg;
	const struct sh_branch *b = &r->r_branch;
	char named[48], *src;
	long major, minor, v;
	int rc;

	if (roll_revision(r, &major, &minor, e) != 0)
		return (-1);
	(void) snprintf(named, sizeof (named), "%ld.%ld", major, minor);
	v = major * SH_VER_MINORS + minor;
	if (roll_ver(r, v, v, named, e) != 0)
		return (-1);
	if (b->br_type == SH_BR_RELENG)
		(void) snprintf(r->r_ver, sizeof (r->r_ver), "%ld.%ld-releng",
		    b->br_major, b->br_minor);
	else if (b->br_type == SH_BR_STABLE)
		(void) snprintf(r->r_ver, sizeof (r->r_ver), "%ld-stable",
		    b->br_major);
	else
		(void) snprintf(r->r_ver, sizeof (r->r_ver), "%ld-current",
		    major);
	src = sh_root_path(r->r_build, "src");
	rc = sh_src_commit(src, r->r_commit, sizeof (r->r_commit), e);
	free(src);
	if (rc == 0)
		sh_job_call(j, "obj", roll_obj, r);
	return (rc);
}

static int
by_key(const void *a, const void *b)
{
	return (strcmp(((const struct sh_pair *)a)->pr_key,
	    ((const struct sh_pair *)b)->pr_key));
}

/*
 * The recipe's dataset PR made below F where missing, with its
 * properties, each parent the recipe does not give made first as a
 * placeholder, canmount off; then mounted. HAVE is F's tree.
 */
static int
roll_dataset(struct sh_roll *r, const struct sh_pair *pr,
    struct names *have, struct sh_err *e)
{
	static const struct sh_zprop off = { "canmount", "off" };
	struct sh_zfs *z = r->r_env->re_z;
	struct sh_zprop *p;
	char name[512];
	size_t k, n;
	int rc = 0;

	for (k = 0; rc == 0; k++) {
		if (pr->pr_key[k] != '/' && pr->pr_key[k] != '\0')
			continue;
		(void) snprintf(name, sizeof (name), "%s/%.*s", r->r_f->f_ds,
		    (int)k, pr->pr_key);
		if (!names_have(have, name)) {
			if (pr->pr_key[k] == '\0') {
				p = roll_props(pr->pr_val, NULL, &n);
				rc = sh_zfs_create(z, name, p, n, e);
				free(p);
			} else {
				rc = sh_zfs_create(z, name, &off, 1, e);
			}
			(void) roll_name(have, name);
		}
		if (pr->pr_key[k] == '\0')
			break;
	}
	return (rc == 0 ? sh_zfs_mount(z, name, e) : -1);
}

/*
 * F mounted, and the datasets DSS gives made where they are missing
 * and mounted, a parent before its children; where F is, the head.
 */
static int
roll_mount(struct sh_roll *r, const struct sh_node *dss, struct sh_err *e)
{
	struct sh_zfs *z = r->r_env->re_z;
	struct names have = { NULL, 0 };
	struct sh_pair *v = NULL;
	char path[1024];
	size_t i, n;
	int rc = -1;

	if (roll_tree(z, r->r_f->f_ds, &have, e) != 0 ||
	    sh_zfs_mount(z, r->r_f->f_ds, e) != 0)
		goto out;
	n = dss != NULL ? dss->sn_obj.ob_n : 0;
	v = sh_calloc(n + 1, sizeof (*v));
	if (n > 0)
		(void) memcpy(v, dss->sn_obj.ob_v, n * sizeof (*v));
	/* a parent sorts before its children */
	qsort(v, n, sizeof (*v), by_key);
	for (i = 0; i < n; i++) {
		if (roll_dataset(r, &v[i], &have, e) != 0)
			goto out;
	}
	if (sh_zfs_mounted(z, r->r_f->f_ds, path, sizeof (path), e) != 1)
		goto out;
	r->r_head = sh_strdup(path);
	rc = 0;
out:
	free(v);
	names_free(&have);
	return (rc);
}

/*
 * The head: F made on a first roll, the recipe's datasets where they
 * are missing, a parent before its children; F and they mounted.
 */
static int
roll_head(struct sh_job *j, void *arg, struct sh_err *e)
{
	struct sh_roll *r = arg;
	const struct sh_node *props;
	struct sh_zprop *p;
	size_t n;
	int rc;

	(void) j;
	if (r->r_first) {
		props = sh_obj_get(r->r_site.ss_merged, "properties");
		p = roll_props(sh_obj_get(props, "default"),
		    sh_obj_get(props, "lake"), &n);
		rc = sh_zfs_create(r->r_env->re_z, r->r_f->f_ds, p, n, e);
		free(p);
		if (rc != 0)
			return (-1);
		r->r_made = 1;
	}
	return (roll_mount(r, sh_obj_get(r->r_recipe.sr_merged, "datasets"),
	    e));
}

/*
 * A pkg roll's head: F mounted, its newest version's foundation.conf
 * read from it, and the datasets that gives mounted; then pkg upgrade
 * of the packages it gives, in a chroot (commands.h), and the
 * snapshot. The version is the newest's, VER of its name
 * VER-DATE-KIND, and so is the commit.
 */
static int
roll_pkg(struct sh_job *j, void *arg, struct sh_err *e)
{
	struct sh_roll *r = arg;
	struct sh_zfs *z = r->r_env->re_z;
	const char *v = r->r_f->f_newest, *kind = strrchr(v, '-');
	char path[1024], snap[512];
	size_t n = kind != NULL ? (size_t)(kind - v) : 0;
	int rc;

	/* the date is 2026-10-15T09_30_12Z: twenty, and a dash before */
	if (n < 22) {
		sh_errf(e, "%s: not a version's name", v);
		return (-1);
	}
	(void) snprintf(r->r_ver, sizeof (r->r_ver), "%.*s", (int)(n - 21),
	    v);
	(void) snprintf(snap, sizeof (snap), "%s@%s", r->r_f->f_ds, v);
	if ((rc = sh_zfs_user(z, snap, "shtetl:commit", r->r_commit,
	    sizeof (r->r_commit), e)) != 0) {
		if (rc == 1)
			sh_errf(e, "%s: no shtetl:commit", snap);
		return (-1);
	}
	if (sh_zfs_mount(z, r->r_f->f_ds, e) != 0 ||
	    sh_zfs_mounted(z, r->r_f->f_ds, path, sizeof (path), e) != 1 ||
	    (r->r_shaken = sh_shaken_read(path, NULL, SH_SHAKEN_FOUNDATION,
	    SH_SHAKEN_FOUNDATION, e)) == NULL ||
	    roll_mount(r, sh_obj_get(r->r_shaken, "datasets"), e) != 0)
		return (-1);
	r->r_commands.cm_head = r->r_head;
	r->r_commands.cm_name = r->r_f->f_name;
	r->r_commands.cm_resolv = r->r_env->re_resolv;
	r->r_commands.cm_pkgs = sh_obj_get(r->r_shaken, "pkg");
	r->r_commands.cm_upgrade = 1;
	sh_commands_steps(j, &r->r_commands);
	sh_job_call(j, "snapshot", roll_snapshot, r);
	return (0);
}


/*
 * The recipe resolved and its ver held to src_branch; then the head,
 * the src and the patches, and the version.
 */
static int
roll_recipe(struct sh_job *j, void *arg, struct sh_err *e)
{
	struct sh_roll *r = arg;
	const struct sh_branch *b = &r->r_branch;
	const char *branch;
	char target[512];
	long v;

	(void) snprintf(target, sizeof (target), "foundations/%s",
	    r->r_f->f_name);
	if (sh_recipe_resolve(r->r_config, r->r_repos, target, NULL,
	    &r->r_recipe, e, NULL) != 0)
		return (-1);
	/* resolved, a foundation's src_branch is there and well formed */
	branch = sh_obj_get(r->r_recipe.sr_merged, "src_branch")->sn_str;
	(void) sh_syn_branch(branch, &r->r_branch);
	v = b->br_major * SH_VER_MINORS;
	if ((b->br_type == SH_BR_RELENG && roll_ver(r, v + b->br_minor,
	    v + b->br_minor, branch, e) != 0) ||
	    (b->br_type == SH_BR_STABLE && roll_ver(r, v,
	    v + SH_VER_MINORS - 1, branch, e) != 0))
		return (-1);
	sh_job_call(j, "head", roll_head, r);
	r->r_src.sc_build = r->r_build;
	r->r_src.sc_config = r->r_config;
	r->r_src.sc_site = &r->r_site;
	r->r_src.sc_recipe = &r->r_recipe;
	sh_src_steps(j, &r->r_src);
	sh_job_call(j, "version", roll_version, r);
	return (0);
}

/* shtetl.conf read; the recipe repositories missing cloned. */
static int
roll_site(struct sh_job *j, void *arg, struct sh_err *e)
{
	struct sh_roll *r = arg;

	if (sh_site_load(r->r_config, &r->r_site, e) != 0)
		return (-1);
	r->r_repos = sh_site_repos(&r->r_site);
	sh_src_recipes(j, r->r_config, &r->r_site);
	sh_job_call(j, "recipe", roll_recipe, r);
	return (0);
}

static void
roll_free(struct sh_roll *r)
{
	free(r->r_config);
	free(r->r_build);
	free(r->r_repos);
	sh_site_free(&r->r_site);
	sh_recipe_free(&r->r_recipe);
	free(r->r_head);
	free(r->r_srcdir);
	free(r->r_obj);
	free(r->r_snap);
	sh_node_free(r->r_shaken);
	(void) fclose(r->r_log);
	free(r);
}

/* The roll over: F let go of it, the builder's turn given back. */
static void
roll_done(struct sh_roll *r)
{
	struct sh_builder *b = r->r_env->re_builder;
	struct sh_foundation *f = r->r_f;

	/* rolling until here, so that no roll is asked under this one */
	f->f_state = f->f_stall != NULL ? SH_F_STALLED : SH_F_IDLE;
	f->f_roll = NULL;
	roll_free(r);
	sh_builder_give(b);
}

/*
 * F put back where its newest version left it, a first roll's undone,
 * and its stall recorded, WHY, where it has a version to stall at; it
 * shows stalled once the roll is over (roll_done).
 */
static void
roll_back(struct sh_roll *r, const char *why)
{
	const struct sh_rollenv *env = r->r_env;
	struct sh_foundation *f = r->r_f;

	if ((!r->r_first || r->r_made) &&
	    sh_foundation_idle(env->re_z, f->f_ds, &r->r_lerr) != 0)
		sh_errf(&r->r_lerr, "not put back");
	if (r->r_first)
		return;
	(void) sh_zfs_set(env->re_z, f->f_ds, "shtetl:stall", why,
	    &r->r_lerr);
	f->f_stall = sh_strdup(why);
}

static void
roll_end(struct sh_job *j, const char *why, void *arg)
{
	struct sh_roll *r = arg;
	const struct sh_rollenv *env = r->r_env;
	struct sh_foundation *f = r->r_f;

	if (r->r_reset) {
		if (why != NULL)
			sh_errf(&r->r_lerr, "zbuild not reset: %s", why);
		roll_done(r);
		return;
	}
	if (why == NULL) {
		sh_errf(&r->r_lerr, "version %s", r->r_snap);
		free(f->f_newest);
		f->f_newest = r->r_snap;
		r->r_snap = NULL;
		roll_done(r);
		return;
	}
	/* the one line of a roll syslog hears (ruled 2026-10-20) */
	sh_errf(env->re_err, "foundation %s: %s", f->f_name, why);
	sh_errf(&r->r_lerr, "failed: %s", why);
	roll_back(r, why);
	/* a pkg roll never touched zbuild */
	if (r->r_pkg) {
		roll_done(r);
		return;
	}
	/* zbuild put back too, the turn still held */
	r->r_reset = 1;
	sh_job_init(j, r->r_log, roll_end, r);
	sh_src_reset(j, r->r_build);
	sh_job_run(j, env->re_kq);
}

/* The builder's turn: the roll begun. */
static void
roll_go(void *arg)
{
	struct sh_roll *r = arg;
	const struct sh_rollenv *env = r->r_env;

	r->r_f->f_state = SH_F_ROLLING;
	sh_job_init(&r->r_job, r->r_log, roll_end, r);
	if (r->r_pkg)
		sh_job_call(&r->r_job, "head", roll_pkg, r);
	else
		sh_job_call(&r->r_job, "site", roll_site, r);
	sh_job_run(&r->r_job, env->re_kq);
}

int
sh_roll_ask(struct sh_foundation *f, const struct sh_rollenv *env,
    const char *config, const char *build, int pkg, struct sh_err *e)
{
	struct sh_roll *r;
	char path[1024];
	FILE *log;

	if (f->f_state == SH_F_WAITING || f->f_state == SH_F_ROLLING) {
		sh_errf(e, "%s: rolling", f->f_name);
		return (-1);
	}
	if (pkg && f->f_newest == NULL) {
		sh_errf(e, "%s: no version", f->f_name);
		return (-1);
	}
	(void) snprintf(path, sizeof (path), "%s/%s.log", env->re_logdir,
	    f->f_name);
	if ((log = fopen(path, "w")) == NULL) {
		sh_errf(e, "%s: %s", path, strerror(errno));
		return (-1);
	}
	/* a line at a time, so the file is the roll as far as it has come */
	(void) setvbuf(log, NULL, _IOLBF, 0);
	if (f->f_state == SH_F_STALLED) {
		if (sh_zfs_inherit(env->re_z, f->f_ds, "shtetl:stall", e) !=
		    0) {
			(void) fclose(log);
			return (-1);
		}
		free(f->f_stall);
		f->f_stall = NULL;
	}
	r = sh_calloc(1, sizeof (*r));
	r->r_log = log;
	sh_err_init(&r->r_lerr, log, "# ");
	r->r_f = f;
	r->r_env = env;
	r->r_config = config != NULL ? sh_strdup(config) : NULL;
	r->r_build = build != NULL ? sh_strdup(build) : NULL;
	r->r_first = f->f_newest == NULL;
	r->r_pkg = pkg;
	r->r_kind = pkg ? "pkg" : r->r_first ? "create" : "update";
	f->f_roll = r;
	f->f_state = SH_F_WAITING;
	sh_builder_ask(env->re_builder, roll_go, r);
	return (0);
}

void
sh_roll_stop(struct sh_foundation *f)
{
	if (f->f_roll == NULL)
		return;
	if (f->f_state == SH_F_ROLLING)
		sh_job_stop(&f->f_roll->r_job);
	roll_free(f->f_roll);
	f->f_roll = NULL;
}

const char *
sh_roll_step(const struct sh_roll *r)
{
	const char *step = sh_job_step(&r->r_job);

	return (step != NULL ? step : "");
}
