/*
 * shtetl build F (planning.md 7.1).
 *
 * The plan: every check made before anything is done, and what will
 * be done shown in full, as bootstrap's is (4.2). The run: each step
 * that changes something recorded, so that a failure anywhere undoes
 * all of it.
 *
 * Decided here, beyond the plan (PROPOSAL): the target is the
 * foundation's name or its recipe's path; a build refuses one already
 * under way, the build root's shtetl:build naming it; the datasets of
 * F are F, each dataset of the recipe, and a placeholder
 * (canmount=off, holding nothing, 4.7) for every parent the recipe
 * does not name, parents first.
 */

#include <sys/types.h>
#include <sys/stat.h>
#if defined(__FreeBSD__) || defined(__APPLE__)
#include <sys/sysctl.h>
#endif
#include <sys/utsname.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "core/build.h"
#include "core/cascade.h"
#include "core/err.h"
#include "core/patch.h"
#include "core/run.h"
#include "core/spaces.h"
#include "core/syntax.h"
#include "core/tree.h"
#include "core/version.h"
#include "core/zfs.h"

static int fb_kid(void *arg, const char *name);

int
sh_build_target(const char *target, char *name, size_t len,
    struct sh_err *e)
{
	const char *n, *why;
	size_t l;

	n = target;
	if (strchr(target, '/') != NULL) {
		if (strncmp(target, "foundations/", 12) != 0) {
			sh_errf(e, "%s: invalid foundation: use F or "
			    "foundations/F", target);
			return (-1);
		}
		n = target + 12;
	}
	l = strlen(n);
	if (l > 5 && strcmp(n + l - 5, ".conf") == 0)
		l -= 5;
	(void) snprintf(name, len, "%.*s", (int)l, n);
	if ((why = sh_syn_name(name)) != NULL) {
		sh_errf(e, "%s: invalid foundation name: %s", target, why);
		return (-1);
	}
	return (0);
}

/* The dataset REL below F in P's list, or NULL. */
static struct sh_fds *
fb_find(struct sh_fplan *p, const char *rel)
{
	size_t i;

	for (i = 0; i < p->fp_nds; i++) {
		if (strcmp(p->fp_dss[i].fd_rel, rel) == 0)
			return (&p->fp_dss[i]);
	}
	return (NULL);
}

static int
fb_add(struct sh_fplan *p, const char *rel, int placeholder,
    const struct sh_node *props, struct sh_err *e)
{
	struct sh_fds *d;

	if (p->fp_nds == SH_BUILD_DS) {
		sh_errf(e, "%s: more than %d datasets", p->fp_recipe.sr_file,
		    SH_BUILD_DS);
		return (-1);
	}
	d = &p->fp_dss[p->fp_nds++];
	(void) snprintf(d->fd_rel, sizeof (d->fd_rel), "%s", rel);
	(void) snprintf(d->fd_ds, sizeof (d->fd_ds), "%s%s%s", p->fp_ds,
	    rel[0] != '\0' ? "/" : "", rel);
	d->fd_placeholder = placeholder;
	d->fd_props = props;
	return (0);
}

/*
 * F's datasets, parents first: F, then each of the recipe's in its
 * order, every parent it does not name a placeholder before it.
 */
static int
fb_datasets(struct sh_fplan *p, struct sh_err *e)
{
	const struct sh_node *dss, *v;
	const char *key, *slash;
	struct sh_fds *d;
	char rel[SH_BUILD_NAME];
	size_t i;

	if (fb_add(p, "", 0, NULL, e) != 0)
		return (-1);
	dss = sh_obj_get(p->fp_recipe.sr_merged, "datasets");
	for (i = 0; dss != NULL && i < dss->sn_obj.ob_n; i++) {
		key = dss->sn_obj.ob_v[i].pr_key;
		v = dss->sn_obj.ob_v[i].pr_val;
		for (slash = key; slash != NULL; ) {
			slash = strchr(slash, '/');
			(void) snprintf(rel, sizeof (rel), "%.*s",
			    slash != NULL ? (int)(slash - key) :
			    (int)strlen(key), key);
			if (slash != NULL)
				slash++;
			if ((d = fb_find(p, rel)) == NULL) {
				if (fb_add(p, rel, slash != NULL, NULL, e) != 0)
					return (-1);
				d = &p->fp_dss[p->fp_nds - 1];
			}
			if (slash == NULL) {
				d->fd_placeholder = 0;
				d->fd_props = v->sn_type == SH_OBJECT ? v :
				    NULL;
			}
		}
	}
	return (0);
}

/*
 * Where the build root is mounted, into P. Refused: no mountpoint,
 * or not mounted.
 */
static int
fb_srcdir(struct sh_zfs *z, struct sh_fplan *p, struct sh_err *e)
{
	char mounted[SH_ZFS_VALMAX];

	if (sh_zfs_get(z, p->fp_src, "mountpoint", p->fp_srcdir,
	    sizeof (p->fp_srcdir), NULL, e) != 0 ||
	    sh_zfs_get(z, p->fp_src, "mounted", mounted, sizeof (mounted),
	    NULL, e) != 0)
		return (-1);
	if (p->fp_srcdir[0] != '/' || strcmp(mounted, "yes") != 0) {
		sh_errf(e, "build root %s: %s", p->fp_src,
		    p->fp_srcdir[0] != '/' ? "no mountpoint" : "not mounted");
		return (-1);
	}
	return (0);
}

/* A walk of snapshots: each one's name into ARG, the newest last. */
static int
fb_lastone(void *arg, const char *snap)
{
	(void) snprintf(arg, SH_BUILD_NAME, "%s", snap);
	return (0);
}

/* DS's newest snapshot, its name after the @, into LAST; "": none. */
static int
fb_last(struct sh_zfs *z, const char *ds, char *last, struct sh_err *e)
{
	last[0] = '\0';
	return (sh_zfs_snapshots(z, ds, fb_lastone, last, e) == 0 ? 0 : -1);
}

/* Every filesystem below DS, each by its full name, added to *ALL. */
static int
fb_walk(struct sh_zfs *z, const char *ds, char ***all, struct sh_err *e)
{
	char **kids;
	size_t i;
	int r;

	if ((kids = calloc(1, sizeof (*kids))) == NULL) {
		sh_err_nomem(e);
		return (-1);
	}
	r = sh_zfs_children(z, ds, fb_kid, &kids, e);
	for (i = 0; r == 0 && kids[i] != NULL; i++) {
		if (fb_kid(all, kids[i]) != 0) {
			sh_err_nomem(e);
			r = -1;
		} else {
			r = fb_walk(z, kids[i], all, e);
		}
	}
	for (i = 0; kids[i] != NULL; i++)
		free(kids[i]);
	free(kids);
	return (r);
}

/*
 * An update's datasets (7.2): F's, fixed for its life, which the
 * recipe's must be: each the recipe has that F has not, and each F
 * has that the recipe has not, refused.
 */
static void
fb_samesets(struct sh_zfs *z, struct sh_fplan *p, struct sh_err *e)
{
	char **have;
	size_t i, j;

	if ((have = calloc(1, sizeof (*have))) == NULL) {
		sh_err_nomem(e);
		return;
	}
	if (fb_walk(z, p->fp_ds, &have, e) == 0) {
		for (i = 1; i < p->fp_nds; i++) {
			for (j = 0; have[j] != NULL &&
			    strcmp(have[j], p->fp_dss[i].fd_ds) != 0; j++)
				;
			if (have[j] == NULL)
				sh_errf(e, "%s: datasets: %s: not in "
				    "foundation %s", p->fp_recipe.sr_file,
				    p->fp_dss[i].fd_rel, p->fp_name);
		}
		for (j = 0; have[j] != NULL; j++) {
			for (i = 1; i < p->fp_nds &&
			    strcmp(have[j], p->fp_dss[i].fd_ds) != 0; i++)
				;
			if (i == p->fp_nds)
				sh_errf(e, "%s: not in the recipe", have[j]);
		}
	}
	for (j = 0; have[j] != NULL; j++)
		free(have[j]);
	free(have);
}

/*
 * F there: the build is its next version (7.2), from its last, at a
 * securelevel where installworld's schg flags can be cleared, into
 * the datasets it has.
 */
static void
fb_updatable(struct sh_zfs *z, struct sh_fplan *p,
    const struct sh_tools *t, struct sh_err *e)
{
	int lvl;

	p->fp_update = 1;
	if (fb_last(z, p->fp_ds, p->fp_last, e) != 0)
		return;
	if (p->fp_last[0] == '\0') {
		sh_errf(e, "foundation %s exists without a version",
		    p->fp_name);
		return;
	}
	if ((lvl = t->tl_securelevel()) > 0)
		sh_errf(e, "securelevel %d: schg flags cannot be cleared", lvl);
	fb_samesets(z, p, e);
}

/* Whether the run directory of the build of NAME holds a pause. */
static int
fb_paused(const char *runbase, const char *name)
{
	char path[SH_BUILD_PATH * 2];

	(void) snprintf(path, sizeof (path), "%s/%s/" SH_BUILD_PAUSED,
	    runbase, name);
	return (access(path, F_OK) == 0);
}

/* The recipe resolved from ROOT, and what the plan takes from it. */
static int
fb_recipe(struct sh_fplan *p, const struct sh_node *site, int check,
    struct sh_err *e, struct sh_err *w)
{
	char rpath[SH_BUILD_NAME + 32];
	const struct sh_node *v;

	(void) snprintf(rpath, sizeof (rpath), "foundations/%s", p->fp_name);
	if (sh_recipe_resolve(p->fp_root, rpath, &p->fp_recipe, e, w) != 0)
		return (-1);
	p->fp_src_branch = sh_obj_get(p->fp_recipe.sr_merged,
	    "src_branch")->sn_str;
	(void) sh_syn_branch(p->fp_src_branch, &p->fp_branch);
	if (check)
		(void) sh_version_check_branch(&p->fp_branch,
		    sh_obj_get(p->fp_recipe.sr_merged, "ver"),
		    p->fp_recipe.sr_file, e);
	(void) fb_datasets(p, e);
	v = sh_obj_get(p->fp_recipe.sr_merged, "patches");
	if (v != NULL && v->sn_obj.ob_n > SH_PATCH_SRCS)
		sh_errf(e, "%s: patches: more than %d sources",
		    p->fp_recipe.sr_file, SH_PATCH_SRCS);
	v = sh_obj_get(sh_obj_get(site, "build"), "make_jobs");
	p->fp_jobs = v != NULL && v->sn_type == SH_INT ? (long)v->sn_int : 0;
	return (0);
}

/*
 * A paused build carried on: its record read, the recipe from the
 * config snapshot it began with (ROOT's .zfs/snapshot/<name>).
 */
static void
fb_resumable(struct sh_zfs *z, struct sh_fplan *p, const char *root,
    const struct sh_node *site, struct sh_err *e, struct sh_err *w)
{
	char path[SH_BUILD_PATH * 2], val[SH_ZFS_VALMAX];
	const char *at;

	(void) snprintf(path, sizeof (path), "%s/" SH_BUILD_PAUSED,
	    p->fp_rundir);
	if (sh_pause_read(path, &p->fp_pause, e) != 0)
		return;
	(void) fb_srcdir(z, p, e);
	if (sh_zfs_get(z, p->fp_ds, "type", val, sizeof (val), NULL, e) != 0)
		sh_errf(e, "foundation %s missing", p->fp_name);
	else if (fb_last(z, p->fp_ds, p->fp_last, e) == 0 &&
	    p->fp_last[0] != '\0')
		p->fp_update = 1;
	p->fp_obj_there = 1;
	if (p->fp_verb == SH_BV_ABORT)
		return;
	if ((at = strchr(p->fp_pause.pz_config, '@')) == NULL) {
		sh_errf(e, "%s: not a pause record", path);
		return;
	}
	(void) snprintf(p->fp_root, sizeof (p->fp_root),
	    "%s/.zfs/snapshot/%s", root, at + 1);
	(void) fb_recipe(p, site, 0, e, w);
}

int
sh_build_plan(struct sh_zfs *z, const struct sh_spaces *s,
    const struct sh_node *site, const char *root, const char *target,
    const char *runbase, const struct sh_tools *t, enum sh_bverb verb,
    struct sh_fplan *p, struct sh_err *e, struct sh_err *w)
{
	char val[SH_ZFS_VALMAX];
	enum sh_zsrc src;
	unsigned before;
	int r, sp, mine;

	(void) memset(p, 0, sizeof (*p));
	p->fp_verb = verb;
	before = e->se_count;
	if (sh_build_target(target, p->fp_name, sizeof (p->fp_name), e) != 0)
		return (-1);
	for (sp = 0; sp < SH_SP_N; sp++) {
		if (sp != SH_SP_RUN && s->ss_root[sp] == NULL)
			sh_errf(e, "no %s root", sh_space_name(sp));
	}
	if (e->se_count != before)
		return (-1);
	(void) snprintf(p->fp_ds, sizeof (p->fp_ds), "%s/foundations/%s",
	    s->ss_root[SH_SP_LAKE], p->fp_name);
	(void) snprintf(p->fp_obj, sizeof (p->fp_obj),
	    "%s/foundation-builds/%s", s->ss_root[SH_SP_LAKE], p->fp_name);
	(void) snprintf(p->fp_src, sizeof (p->fp_src), "%s",
	    s->ss_root[SH_SP_BUILD]);
	(void) snprintf(p->fp_config, sizeof (p->fp_config), "%s",
	    s->ss_root[SH_SP_CONFIG]);
	(void) snprintf(p->fp_root, sizeof (p->fp_root), "%s", root);
	(void) snprintf(p->fp_rundir, sizeof (p->fp_rundir), "%s/%s",
	    runbase, p->fp_name);

	/* one build at a time: the build root says which */
	r = sh_zfs_get(z, p->fp_src, SH_BUILD_PROP, val, sizeof (val), &src,
	    e);
	if (r != 0)
		return (-1);
	mine = src == SH_ZSRC_LOCAL && strcmp(val, p->fp_name) == 0;
	if (verb != SH_BV_NEW) {
		if (!mine || !fb_paused(runbase, p->fp_name))
			sh_errf(e, "no build of %s paused", p->fp_name);
		else
			fb_resumable(z, p, root, site, e, w);
		return (e->se_count == before ? 0 : -1);
	}
	if (src == SH_ZSRC_LOCAL)
		sh_errf(e, "build of %s %s", val, fb_paused(runbase, val) ?
		    "paused" : "in progress");
	else
		(void) fb_srcdir(z, p, e);
	if (fb_recipe(p, site, 1, e, w) != 0)
		return (-1);
	r = sh_zfs_get(z, p->fp_ds, "type", val, sizeof (val), NULL, e);
	if (r == 0)
		fb_updatable(z, p, t, e);
	r = sh_zfs_get(z, p->fp_obj, "type", val, sizeof (val), NULL, e);
	p->fp_obj_there = r == 0;
	return (e->se_count == before ? 0 : -1);
}

/* A dataset's properties, as the plan shows them: k=v, k=v. */
static void
fb_props(FILE *out, const struct sh_fds *d)
{
	const struct sh_pair *pr;
	const char *v;
	size_t i;

	if (d->fd_placeholder) {
		(void) fprintf(out, "  canmount=off");
		return;
	}
	for (i = 0; d->fd_props != NULL && i < d->fd_props->sn_obj.ob_n;
	    i++) {
		pr = &d->fd_props->sn_obj.ob_v[i];
		if (pr->pr_val->sn_type == SH_BOOL)
			v = pr->pr_val->sn_bool ? "on" : "off";
		else
			v = pr->pr_val->sn_str;
		(void) fprintf(out, "%s%s=%s", i == 0 ? "  " : ", ",
		    pr->pr_key, v);
	}
}

/* The patch sources, each with its ref and dir: a (ref r), b. */
static void
fb_sources(FILE *out, const struct sh_fplan *p)
{
	const struct sh_node *c, *ref, *dir;
	size_t i;

	c = sh_obj_get(p->fp_recipe.sr_merged, "patches");
	for (i = 0; c != NULL && i < c->sn_obj.ob_n; i++) {
		ref = sh_obj_get(c->sn_obj.ob_v[i].pr_val, "ref");
		dir = sh_obj_get(c->sn_obj.ob_v[i].pr_val, "dir");
		(void) fprintf(out, "%s%s", i == 0 ? "" : ", ",
		    c->sn_obj.ob_v[i].pr_key);
		if (ref != NULL || dir != NULL)
			(void) fprintf(out, " (%s%s%s%s%s)", ref != NULL ?
			    "ref " : "", ref != NULL ? ref->sn_str : "",
			    ref != NULL && dir != NULL ? ", " : "",
			    dir != NULL ? "dir " : "", dir != NULL ?
			    dir->sn_str : "");
	}
	if (c == NULL || c->sn_obj.ob_n == 0)
		(void) fprintf(out, "none");
}

/* The keys of the merged recipe's KEY, a collection: a, b, c. */
static void
fb_keys(FILE *out, const struct sh_fplan *p, const char *key)
{
	const struct sh_node *c;
	size_t i;

	c = sh_obj_get(p->fp_recipe.sr_merged, key);
	for (i = 0; c != NULL && i < c->sn_obj.ob_n; i++)
		(void) fprintf(out, "%s%s", i == 0 ? "" : ", ",
		    c->sn_obj.ob_v[i].pr_key);
	if (c == NULL || c->sn_obj.ob_n == 0)
		(void) fprintf(out, "none");
}

void
sh_build_show(FILE *out, const struct sh_fplan *p)
{
	static const char *const kind[] = { "current", "stable", "releng" };
	const struct sh_node *kc, *v;
	size_t i;

	if (p->fp_update)
		(void) fprintf(out, "foundation %s, an update of @%s, from "
		    "%s\n", p->fp_name, p->fp_last, p->fp_src_branch);
	else
		(void) fprintf(out, "foundation %s, new, from %s\n",
		    p->fp_name, p->fp_src_branch);
	if (p->fp_branch.br_type == SH_BR_RELENG)
		(void) fprintf(out, "  version %ld.%ld-releng-<day>-<commit>\n",
		    p->fp_branch.br_major, p->fp_branch.br_minor);
	else if (p->fp_branch.br_type == SH_BR_STABLE)
		(void) fprintf(out, "  version %ld-stable-<day>-<commit>\n",
		    p->fp_branch.br_major);
	else
		(void) fprintf(out, "  version <major>-%s-<day>-<commit>\n",
		    kind[p->fp_branch.br_type]);
	for (i = 0; i < p->fp_nds; i++) {
		/* an update's datasets are F's, as they were made */
		if (p->fp_update) {
			(void) fprintf(out, "  = %s\n", p->fp_dss[i].fd_ds);
			continue;
		}
		(void) fprintf(out, "  + %s", p->fp_dss[i].fd_ds);
		fb_props(out, &p->fp_dss[i]);
		(void) fputc('\n', out);
	}
	if (!p->fp_obj_there)
		(void) fprintf(out, "  + %s\n", p->fp_obj);
	(void) fprintf(out, "  checkout %s in %s (%s)\n", p->fp_src_branch,
	    p->fp_src, p->fp_srcdir);
	(void) fprintf(out, "  patches: ");
	fb_sources(out, p);
	(void) fputc('\n', out);
	kc = sh_obj_get(p->fp_recipe.sr_merged, "kernconf");
	v = sh_obj_get(kc, "file");
	(void) fprintf(out, "  kernel %s", v != NULL ? v->sn_str :
	    "GENERIC");
	v = sh_obj_get(kc, "lines");
	if (v != NULL)
		(void) fprintf(out, ", lines from %zu file%s",
		    v->sn_obj.ob_n, v->sn_obj.ob_n == 1 ? "" : "s");
	(void) fputc('\n', out);
	v = sh_obj_get(p->fp_recipe.sr_merged, "buildopts");
	if (v != NULL)
		(void) fprintf(out, "  src.conf from %zu file%s\n",
		    v->sn_obj.ob_n, v->sn_obj.ob_n == 1 ? "" : "s");
	if (p->fp_jobs > 0)
		(void) fprintf(out, "  make -j%ld buildworld buildkernel\n",
		    p->fp_jobs);
	else
		(void) fprintf(out, "  make -j<hw.ncpu> buildworld "
		    "buildkernel\n");
	if (p->fp_update)
		(void) fprintf(out, "  make delete-old delete-old-libs, "
		    "then\n");
	(void) fprintf(out, "  install world, kernel and distribution "
	    "(install -C)\n");
	if (p->fp_update)
		(void) fprintf(out, "  installworld's backups removed\n");
	(void) fprintf(out, "  pkg: ");
	fb_keys(out, p, "pkg");
	(void) fprintf(out, "\n  commands: ");
	fb_keys(out, p, "commands");
	(void) fprintf(out, "\n  snapshot -r %s@<version>\n", p->fp_ds);
}

/* ---- the run ---------------------------------------------------- */

/* kern.securelevel; 0 where there is none to read. */
static int
fb_securelevel(void)
{
#if defined(__FreeBSD__) || defined(__APPLE__)
	size_t len;
	int lvl;

	len = sizeof (lvl);
	if (sysctlbyname("kern.securelevel", &lvl, &len, NULL, 0) == 0)
		return (lvl);
#endif
	return (0);
}

const struct sh_tools sh_build_tools = {
	"git", "make", "pkg", "chroot", "mount", "umount", "sh", "jail",
	"jexec", fb_securelevel
};

/* What a run has done, to be undone, newest last. */
enum fu_kind {
	FU_UNLOCK,	/* the build root's shtetl:build inherited */
	FU_DESTROY,	/* a dataset made, destroyed */
	FU_UNMOUNT,	/* a dataset mounted, unmounted */
	FU_INHERIT,	/* a dataset's mountpoint set, inherited */
	FU_UMOUNT,	/* a path mounted (devfs), unmounted */
	FU_UNLINK,	/* a file put in F, removed */
	FU_PATCHES,	/* the build root back to @shtetl-patch-0 */
	FU_ROLLBACK	/* an update's dataset back to the last version */
};

struct fu {
	enum fu_kind	fu_kind;
	int		fu_fail;	/* undone only on a failure */
	char		fu_what[SH_BUILD_PATH];
};

#define	FB_UNDO		(SH_BUILD_DS * 3 + 16)
#define	FB_CAP		4096	/* what a captured program may print */
#define	FB_PSNAP	"shtetl-patch-"	/* the build root's, per patch */

/* One patch, in the order they are applied. */
struct fb_pa {
	size_t		pa_src;		/* its source, from 0 */
	char		*pa_name;
	char		pa_path[SH_BUILD_PATH];
};

/* One run. */
struct fb {
	struct sh_zfs		*fb_z;
	struct sh_fplan		*fb_p;
	const struct sh_tools	*fb_t;
	FILE			*fb_out;
	struct sh_err		*fb_err;
	struct fu		fb_undo[FB_UNDO];
	size_t			fb_nundo;
	char			fb_log[SH_BUILD_PATH];
	char			fb_rootdir[SH_BUILD_PATH];	/* F, mounted */
	char			fb_objdir[SH_BUILD_PATH];
	char			fb_ident[SH_BUILD_NAME];
	char			fb_kernconf[SH_BUILD_PATH];
	char			fb_srcconf[SH_BUILD_PATH];
	char			fb_sha[64];
	char			fb_version[SH_VERSION_MAX];
	char			fb_cfgsnap[SH_BUILD_NAME + 64];
	char			fb_cfgguid[64];
	char			fb_rev[32];	/* REVISION, N.M */
	struct sh_pause		fb_pz;		/* the patches, so far */
	struct fb_pa		*fb_pa;
	size_t			fb_npa;
	size_t			fb_psnaps;	/* the last N taken */
	const char		*fb_msg;	/* -m's, or NULL */
	const char		*fb_dest;	/* DESTDIR if not F's */
	char			fb_prec[SH_ZFS_VALMAX];	/* carried over */
};

static void
fb_push(struct fb *b, enum fu_kind k, int fail, const char *what)
{
	struct fu *u;

	if (b->fb_nundo == FB_UNDO)
		return;
	u = &b->fb_undo[b->fb_nundo++];
	u->fu_kind = k;
	u->fu_fail = fail;
	(void) snprintf(u->fu_what, sizeof (u->fu_what), "%s", what);
}

static int fb_run(struct fb *b, const char *const *words,
    const char *const *env, size_t cap, char **outp, size_t *lenp,
    int quiet);
static void fb_unpatch(struct fb *b);
static void fb_unrecord(struct fb *b);

static int
fb_exec(struct fb *b, const char *const *words, const char *const *env,
    size_t cap, char **outp, int quiet)
{
	return (fb_run(b, words, env, cap, outp, NULL, quiet));
}

/*
 * Undone, newest first, down to MARK: all of it on a FAILED run;
 * otherwise what is undone on success too, the rest forgotten.
 */
static void
fb_undo(struct fb *b, size_t mark, int failed)
{
	const char *words[4];
	struct fu *u;

	while (b->fb_nundo > mark) {
		u = &b->fb_undo[--b->fb_nundo];
		if (u->fu_fail && !failed)
			continue;
		switch (u->fu_kind) {
		case FU_UNLOCK:
			(void) sh_zfs_inherit(b->fb_z, u->fu_what,
			    SH_BUILD_PROP, b->fb_err);
			break;
		case FU_DESTROY:
			if (sh_zfs_destroy(b->fb_z, u->fu_what,
			    b->fb_err) == 0)
				(void) fprintf(b->fb_out, "destroyed %s\n",
				    u->fu_what);
			break;
		case FU_UNMOUNT:
			(void) sh_zfs_unmount(b->fb_z, u->fu_what, b->fb_err);
			break;
		case FU_INHERIT:
			(void) sh_zfs_inherit(b->fb_z, u->fu_what,
			    "mountpoint", b->fb_err);
			break;
		case FU_UMOUNT:
			words[0] = b->fb_t->tl_umount;
			words[1] = u->fu_what;
			words[2] = NULL;
			(void) fb_exec(b, words, NULL, 0, NULL, 0);
			break;
		case FU_UNLINK:
			if (unlink(u->fu_what) != 0 && errno != ENOENT)
				sh_errf(b->fb_err, "%s: %s", u->fu_what,
				    strerror(errno));
			break;
		case FU_PATCHES:
			fb_unpatch(b);
			break;
		case FU_ROLLBACK:
			if (sh_zfs_rollback(b->fb_z, u->fu_what,
			    b->fb_p->fp_last, b->fb_err) == 0)
				(void) fprintf(b->fb_out, "rolled back %s to "
				    "@%s\n", u->fu_what, b->fb_p->fp_last);
			break;
		}
	}
}

/* Forgets what is undone only on a failure: the run has succeeded. */
static void
fb_commit(struct fb *b)
{
	size_t i, n;

	for (i = n = 0; i < b->fb_nundo; i++) {
		if (!b->fb_undo[i].fu_fail)
			b->fb_undo[n++] = b->fb_undo[i];
	}
	b->fb_nundo = n;
}

/*
 * WORDS run, its output to the run's log, or with CAP its stdout into
 * *OUTP (the caller frees it), *LENP bytes. ENV, or the environment
 * shtetl has. QUIET: a failure is not reported (the caller reads the
 * status). The exit status is 0, or -1.
 */
static int
fb_run(struct fb *b, const char *const *words, const char *const *env,
    size_t cap, char **outp, size_t *lenp, int quiet)
{
	extern char **environ;
	struct sh_err q, *e;
	struct sh_run r;
	char **argv, **envp;
	size_t i, n, m;
	int fd, rc;
	FILE *null;

	for (n = 0; words[n] != NULL; n++)
		;
	for (m = 0; env != NULL && env[m] != NULL; m++)
		;
	argv = calloc(n + 1, sizeof (*argv));
	envp = env != NULL ? calloc(m + 1, sizeof (*envp)) : NULL;
	rc = -1;
	null = NULL;
	fd = -1;
	if (argv == NULL || (env != NULL && envp == NULL))
		goto nomem;
	for (i = 0; i < n; i++) {
		if ((argv[i] = strdup(words[i])) == NULL)
			goto nomem;
	}
	for (i = 0; i < m; i++) {
		if ((envp[i] = strdup(env[i])) == NULL)
			goto nomem;
	}
	e = b->fb_err;
	if (quiet) {
		if ((null = fopen("/dev/null", "w")) == NULL)
			goto nomem;
		sh_err_init(&q, null, "");
		e = &q;
	}
	if ((fd = open(b->fb_log, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC,
	    0644)) == -1) {
		sh_errf(b->fb_err, "%s: %s", b->fb_log, strerror(errno));
		goto out;
	}
	(void) memset(&r, 0, sizeof (r));
	r.ru_argv = argv;
	r.ru_env = env != NULL ? envp : environ;
	r.ru_log = fd;
	r.ru_capmax = cap;
	rc = sh_run(&r, e) == 0 ? 0 : -1;
	if (lenp != NULL)
		*lenp = r.ru_outlen;
	if (outp != NULL)
		*outp = r.ru_out;
	else
		free(r.ru_out);
	goto out;
nomem:
	sh_err_nomem(b->fb_err);
out:
	if (fd != -1)
		(void) close(fd);
	if (null != NULL)
		(void) fclose(null);
	for (i = 0; argv != NULL && i < n; i++)
		free(argv[i]);
	for (i = 0; envp != NULL && i < m; i++)
		free(envp[i]);
	free(argv);
	free(envp);
	return (rc);
}

/* DIR made, and its parents, as mkdir -p does. */
static int
fb_mkdirs(struct fb *b, const char *dir)
{
	char buf[SH_BUILD_PATH], c;
	size_t i;

	(void) snprintf(buf, sizeof (buf), "%s", dir);
	for (i = 1; ; i++) {
		if (buf[i] != '/' && buf[i] != '\0')
			continue;
		c = buf[i];
		buf[i] = '\0';
		if (mkdir(buf, 0755) != 0 && errno != EEXIST) {
			sh_errf(b->fb_err, "%s: %s", buf, strerror(errno));
			return (-1);
		}
		buf[i] = c;
		if (c == '\0')
			return (0);
	}
}

/* The small file FROM copied to TO. */
static int
fb_copy(struct fb *b, const char *from, const char *to)
{
	char buf[8192];
	FILE *in, *o;
	size_t n;
	int bad;

	if ((in = fopen(from, "r")) == NULL) {
		sh_errf(b->fb_err, "%s: %s", from, strerror(errno));
		return (-1);
	}
	if ((o = fopen(to, "w")) == NULL) {
		sh_errf(b->fb_err, "%s: %s", to, strerror(errno));
		(void) fclose(in);
		return (-1);
	}
	bad = 0;
	while ((n = fread(buf, 1, sizeof (buf), in)) > 0) {
		if (fwrite(buf, 1, n, o) != n)
			bad = 1;
	}
	if (ferror(in))
		bad = 1;
	(void) fclose(in);
	if (fclose(o) != 0 || bad) {
		sh_errf(b->fb_err, "%s: %s", to, strerror(errno));
		return (-1);
	}
	return (0);
}

/* TEXT written to the file PATH, made or emptied first. */
static int
fb_write(struct fb *b, const char *path, const char *text)
{
	FILE *f;

	if ((f = fopen(path, "w")) == NULL ||
	    fputs(text, f) == EOF || fclose(f) != 0) {
		sh_errf(b->fb_err, "%s: %s", path, strerror(errno));
		return (-1);
	}
	return (0);
}

/* The build root marked as building F; the config root snapshotted. */
static int
fb_begin(struct fb *b, time_t now)
{
	struct sh_fplan *p = b->fb_p;
	struct sh_zprop kind = { "shtetl:kind", "config" };
	char stamp[32], val[SH_ZFS_VALMAX];
	const char *one;
	struct tm tm;
	int fd, n;

	if (fb_mkdirs(b, p->fp_rundir) != 0)
		return (-1);
	fb_unrecord(b);
	(void) snprintf(b->fb_log, sizeof (b->fb_log), "%s/build.log",
	    p->fp_rundir);
	if ((fd = open(b->fb_log, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC,
	    0644)) == -1) {
		sh_errf(b->fb_err, "%s: %s", b->fb_log, strerror(errno));
		return (-1);
	}
	(void) close(fd);
	if (sh_zfs_set(b->fb_z, p->fp_src, SH_BUILD_PROP, p->fp_name,
	    b->fb_err) != 0)
		return (-1);
	fb_push(b, FU_UNLOCK, 0, p->fp_src);
	(void) gmtime_r(&now, &tm);
	(void) strftime(stamp, sizeof (stamp), "%Y%m%d-%H%M%S", &tm);
	(void) snprintf(b->fb_cfgsnap, sizeof (b->fb_cfgsnap),
	    "%s@build-%s-%s", p->fp_config, p->fp_name, stamp);
	/* a build in the same second as one before it: -2, -3, ... */
	for (n = 2; n < 100 && sh_zfs_get(b->fb_z, b->fb_cfgsnap, "type",
	    val, sizeof (val), NULL, b->fb_err) == 0; n++)
		(void) snprintf(b->fb_cfgsnap, sizeof (b->fb_cfgsnap),
		    "%s@build-%s-%s-%d", p->fp_config, p->fp_name, stamp, n);
	one = b->fb_cfgsnap;
	if (sh_zfs_snapshot(b->fb_z, &one, 1, &kind, 1, b->fb_err) != 0 ||
	    sh_zfs_get(b->fb_z, b->fb_cfgsnap, "guid", b->fb_cfgguid,
	    sizeof (b->fb_cfgguid), NULL, b->fb_err) != 0)
		return (-1);
	(void) fprintf(b->fb_out, "snapshot %s\n", b->fb_cfgsnap);
	return (0);
}

/* A dataset of the recipe's properties as ZFS takes them; N set. */
static size_t
fb_zprops(const struct sh_fds *d, struct sh_zprop *zp, size_t max)
{
	const struct sh_pair *pr;
	size_t i, n;

	if (d->fd_placeholder) {
		zp[0].zp_name = "canmount";
		zp[0].zp_value = "off";
		return (1);
	}
	for (i = n = 0; d->fd_props != NULL &&
	    i < d->fd_props->sn_obj.ob_n && n < max; i++) {
		pr = &d->fd_props->sn_obj.ob_v[i];
		zp[n].zp_name = pr->pr_key;
		zp[n++].zp_value = pr->pr_val->sn_type == SH_BOOL ?
		    (pr->pr_val->sn_bool ? "on" : "off") : pr->pr_val->sn_str;
	}
	return (n);
}

/*
 * F's datasets made, before anything is installed, or an update's
 * marked for going back to the last version on a failure; the obj
 * cache.
 */
static int
fb_create(struct fb *b)
{
	struct sh_fplan *p = b->fb_p;
	struct sh_zprop zp[32];
	size_t i, n;

	for (i = 0; p->fp_update && i < p->fp_nds; i++)
		fb_push(b, FU_ROLLBACK, 1, p->fp_dss[i].fd_ds);
	for (i = 0; !p->fp_update && i < p->fp_nds; i++) {
		n = fb_zprops(&p->fp_dss[i], zp, 32);
		if (sh_zfs_create(b->fb_z, p->fp_dss[i].fd_ds, zp, n,
		    b->fb_err) != 0)
			return (-1);
		fb_push(b, FU_DESTROY, 1, p->fp_dss[i].fd_ds);
		(void) fprintf(b->fb_out, "made %s\n", p->fp_dss[i].fd_ds);
	}
	if (!p->fp_obj_there) {
		if (sh_zfs_create(b->fb_z, p->fp_obj, NULL, 0, b->fb_err) != 0)
			return (-1);
		(void) fprintf(b->fb_out, "made %s\n", p->fp_obj);
	}
	return (0);
}

/*
 * The checkout of src_branch in the build root, which must have no
 * changes of its own, and its commit.
 */
static int
fb_checkout(struct fb *b)
{
	struct sh_fplan *p = b->fb_p;
	const char *w[8];
	char *out;
	size_t n;

	w[0] = b->fb_t->tl_git;
	w[1] = "-C";
	w[2] = p->fp_srcdir;
	w[3] = "diff";
	w[4] = "--quiet";
	w[5] = "HEAD";
	w[6] = NULL;
	if (fb_exec(b, w, NULL, 0, NULL, 1) != 0) {
		sh_errf(b->fb_err, "build root %s: changes of its own, or "
		    "no checkout", p->fp_src);
		return (-1);
	}
	w[3] = "fetch";
	w[4] = "origin";
	w[5] = p->fp_src_branch;
	if (fb_exec(b, w, NULL, 0, NULL, 0) != 0)
		return (-1);
	w[3] = "checkout";
	w[4] = "-q";
	w[5] = "--detach";
	w[6] = "FETCH_HEAD";
	w[7] = NULL;
	if (fb_exec(b, w, NULL, 0, NULL, 0) != 0)
		return (-1);
	w[3] = "rev-parse";
	w[4] = "HEAD";
	w[5] = NULL;
	if (fb_exec(b, w, NULL, FB_CAP, &out, 0) != 0)
		return (-1);
	n = out != NULL ? strcspn(out, "\n") : 0;
	(void) snprintf(b->fb_sha, sizeof (b->fb_sha), "%.*s", (int)n,
	    out != NULL ? out : "");
	free(out);
	(void) fprintf(b->fb_out, "checkout %s at %.12s\n", p->fp_src_branch,
	    b->fb_sha);
	return (0);
}

static int
fb_snapname(void *arg, const char *snap)
{
	char ***have = arg;
	size_t n;
	char **nv;

	for (n = 0; (*have)[n] != NULL; n++)
		;
	if ((nv = realloc(*have, (n + 2) * sizeof (*nv))) == NULL)
		return (-1);
	*have = nv;
	if ((nv[n] = strdup(snap)) == NULL)
		return (-1);
	nv[n + 1] = NULL;
	return (0);
}

/*
 * REVISION, asked of the checkout's newvers.sh, into *MAJOR, *MINOR
 * and the run's fb_rev, which the version records.
 */
static int
fb_revision(struct fb *b, long *major, long *minor)
{
	char path[SH_BUILD_PATH], *out;
	const char *w[5];
	int r;

	(void) snprintf(path, sizeof (path), "%s/sys/conf/newvers.sh",
	    b->fb_p->fp_srcdir);
	w[0] = b->fb_t->tl_sh;
	w[1] = path;
	w[2] = "-V";
	w[3] = "REVISION";
	w[4] = NULL;
	if (fb_exec(b, w, NULL, FB_CAP, &out, 0) != 0)
		return (-1);
	r = sh_version_revision(out != NULL ? out : "", major, minor,
	    b->fb_err);
	free(out);
	if (r == 0)
		(void) snprintf(b->fb_rev, sizeof (b->fb_rev), "%ld.%ld",
		    *major, *minor);
	return (r);
}

/*
 * REVISION, asked of the checkout's newvers.sh and checked against
 * ver, and the version's name.
 */
static int
fb_version(struct fb *b, time_t now)
{
	struct sh_fplan *p = b->fb_p;
	char **have;
	long major, minor;
	size_t n;
	int r;

	r = fb_revision(b, &major, &minor);
	if (r != 0 || sh_version_check(major, minor,
	    sh_obj_get(p->fp_recipe.sr_merged, "ver"), p->fp_recipe.sr_file,
	    b->fb_err) != 0)
		return (-1);
	if ((have = calloc(1, sizeof (*have))) == NULL) {
		sh_err_nomem(b->fb_err);
		return (-1);
	}
	r = sh_zfs_snapshots(b->fb_z, p->fp_ds, fb_snapname, &have,
	    b->fb_err);
	for (n = 0; have[n] != NULL; n++)
		;
	if (r == 0)
		r = sh_version_name(&p->fp_branch, major, now, b->fb_sha,
		    (const char *const *)(void *)have, n, b->fb_version,
		    sizeof (b->fb_version), b->fb_err);
	for (n = 0; have[n] != NULL; n++)
		free(have[n]);
	free(have);
	if (r != 0)
		return (-1);
	(void) fprintf(b->fb_out, "version %s (REVISION %ld.%ld)\n",
	    b->fb_version, major, minor);
	return (0);
}

/*
 * PATH, and everything below it, removed, its file flags cleared
 * first; gone already is done.
 */
static int
fb_rmtree(struct fb *b, const char *path)
{
	char sub[4096];
	struct dirent *de;
	struct stat st;
	DIR *d;
	int r;

	if (lstat(path, &st) != 0) {
		if (errno == ENOENT)
			return (0);
		sh_errf(b->fb_err, "%s: %s", path, strerror(errno));
		return (-1);
	}
	/* immutable or undeletable first, as installworld leaves some */
	if (st.st_flags != 0 && lchflags(path, 0) != 0) {
		sh_errf(b->fb_err, "%s: %s", path, strerror(errno));
		return (-1);
	}
	r = 0;
	if (S_ISDIR(st.st_mode)) {
		if ((d = opendir(path)) == NULL) {
			sh_errf(b->fb_err, "%s: %s", path, strerror(errno));
			return (-1);
		}
		while (r == 0 && (de = readdir(d)) != NULL) {
			if (strcmp(de->d_name, ".") == 0 ||
			    strcmp(de->d_name, "..") == 0)
				continue;
			if ((size_t)snprintf(sub, sizeof (sub), "%s/%s", path,
			    de->d_name) >= sizeof (sub)) {
				sh_errf(b->fb_err, "%s: path too long", path);
				r = -1;
			} else {
				r = fb_rmtree(b, sub);
			}
		}
		(void) closedir(d);
		if (r == 0 && rmdir(path) != 0) {
			sh_errf(b->fb_err, "%s: %s", path, strerror(errno));
			r = -1;
		}
	} else if (unlink(path) != 0) {
		sh_errf(b->fb_err, "%s: %s", path, strerror(errno));
		r = -1;
	}
	return (r);
}

/* The patches of the directory DIR, of source K, added to the list. */
static int
fb_listed(struct fb *b, size_t k, const char *dir)
{
	struct fb_pa *pa;
	char **names;
	size_t i;

	if ((names = sh_patch_list(dir, b->fb_err)) == NULL)
		return (-1);
	for (i = 0; names[i] != NULL; i++) {
		if (b->fb_npa == SH_PATCH_MAX) {
			sh_errf(b->fb_err, "more than %d patches",
			    SH_PATCH_MAX);
			sh_patch_list_free(names);
			return (-1);
		}
		pa = &b->fb_pa[b->fb_npa++];
		pa->pa_src = k;
		pa->pa_name = names[i];
		names[i] = NULL;
		(void) snprintf(pa->pa_path, sizeof (pa->pa_path), "%s/%s", dir,
		    pa->pa_name);
	}
	sh_patch_list_free(names);
	return (0);
}

/*
 * One git source into DEST: a shallow fetch of REF (none: the remote's
 * HEAD), checked out, its commit noted as the K'th's.
 */
static int
fb_gitsrc(struct fb *b, size_t k, const char *loc, const char *url,
    const char *ref, const char *dest)
{
	const char *w[10];
	char *out;
	size_t n;

	w[0] = b->fb_t->tl_git;
	w[1] = "init";
	w[2] = "-q";
	w[3] = dest;
	w[4] = NULL;
	if (fb_exec(b, w, NULL, 0, NULL, 0) != 0)
		return (-1);
	w[1] = "-C";
	w[2] = dest;
	w[3] = "fetch";
	w[4] = "-q";
	w[5] = "--depth";
	w[6] = "1";
	w[7] = url;
	w[8] = ref != NULL ? ref : "HEAD";
	w[9] = NULL;
	if (fb_exec(b, w, NULL, 0, NULL, 1) != 0) {
		sh_errf(b->fb_err, "patches %s: fetch of %s failed", loc, w[8]);
		return (-1);
	}
	w[3] = "checkout";
	w[4] = "-q";
	w[5] = "--detach";
	w[6] = "FETCH_HEAD";
	w[7] = NULL;
	if (fb_exec(b, w, NULL, 0, NULL, 0) != 0)
		return (-1);
	w[3] = "rev-parse";
	w[4] = "HEAD";
	w[5] = NULL;
	if (fb_exec(b, w, NULL, FB_CAP, &out, 0) != 0)
		return (-1);
	n = out != NULL ? strcspn(out, "\n") : 0;
	(void) snprintf(b->fb_pz.pz_commit[k], sizeof (b->fb_pz.pz_commit[k]),
	    "%.*s", (int)n, out != NULL ? out : "");
	free(out);
	(void) fprintf(b->fb_out, "fetched %s at %.12s\n", loc,
	    b->fb_pz.pz_commit[k]);
	return (0);
}

/* One directory of the configuration root's: its patches copied. */
static int
fb_dirsrc(struct fb *b, const char *loc, const char *dir, const char *dest)
{
	char from[SH_BUILD_PATH], a[SH_BUILD_PATH * 2], z[SH_BUILD_PATH * 2];
	char **names;
	size_t i;
	int r;

	(void) snprintf(from, sizeof (from), "%s/%s%s%s", b->fb_p->fp_root,
	    loc, dir != NULL ? "/" : "", dir != NULL ? dir : "");
	if (fb_mkdirs(b, dest) != 0 ||
	    (names = sh_patch_list(from, b->fb_err)) == NULL)
		return (-1);
	r = 0;
	for (i = 0; r == 0 && names[i] != NULL; i++) {
		(void) snprintf(a, sizeof (a), "%s/%s", from, names[i]);
		(void) snprintf(z, sizeof (z), "%s/%s", dest, names[i]);
		r = fb_copy(b, a, z);
	}
	sh_patch_list_free(names);
	if (r == 0)
		(void) fprintf(b->fb_out, "copied %s\n", loc);
	return (r);
}

/*
 * 7.1.1 step 1: each source into the run directory's patches/<n>, in
 * the recipe's order, and the list of what will be applied fixed from
 * there; or, FETCH 0, a paused build's list read back from there.
 */
static int
fb_fetch(struct fb *b, int fetch)
{
	struct sh_fplan *p = b->fb_p;
	const struct sh_node *srcs, *ref, *dir;
	char base[SH_BUILD_PATH], dest[SH_BUILD_PATH + 16];
	char list[SH_BUILD_PATH * 2], url[SH_BUILD_PATH];
	const char *loc;
	size_t k;

	srcs = sh_obj_get(p->fp_recipe.sr_merged, "patches");
	if (srcs == NULL || srcs->sn_obj.ob_n == 0)
		return (0);
	(void) snprintf(base, sizeof (base), "%s/patches", p->fp_rundir);
	if (fetch && (fb_rmtree(b, base) != 0 || fb_mkdirs(b, base) != 0))
		return (-1);
	if ((b->fb_pa = calloc(SH_PATCH_MAX, sizeof (*b->fb_pa))) == NULL) {
		sh_err_nomem(b->fb_err);
		return (-1);
	}
	b->fb_pz.pz_nsrc = srcs->sn_obj.ob_n;
	for (k = 0; k < srcs->sn_obj.ob_n; k++) {
		loc = srcs->sn_obj.ob_v[k].pr_key;
		ref = sh_obj_get(srcs->sn_obj.ob_v[k].pr_val, "ref");
		dir = sh_obj_get(srcs->sn_obj.ob_v[k].pr_val, "dir");
		(void) snprintf(dest, sizeof (dest), "%s/%zu", base, k + 1);
		if (sh_patch_url(loc, url, sizeof (url)) != NULL) {
			if (fetch && fb_gitsrc(b, k, loc, url, ref != NULL ?
			    ref->sn_str : NULL, dest) != 0)
				return (-1);
			(void) snprintf(list, sizeof (list), "%s%s%s", dest,
			    dir != NULL ? "/" : "", dir != NULL ?
			    dir->sn_str : "");
		} else {
			if (fetch && fb_dirsrc(b, loc, dir != NULL ?
			    dir->sn_str : NULL, dest) != 0)
				return (-1);
			(void) snprintf(list, sizeof (list), "%s", dest);
		}
		if (fb_listed(b, k, list) != 0)
			return (-1);
	}
	if (fetch)
		(void) fprintf(b->fb_out, "%zu patch%s from %zu source%s\n",
		    b->fb_npa, b->fb_npa == 1 ? "" : "es", srcs->sn_obj.ob_n,
		    srcs->sn_obj.ob_n == 1 ? "" : "s");
	return (0);
}

/* The build root's @shtetl-patch-N. */
static int
fb_psnap(struct fb *b, size_t n)
{
	char name[SH_BUILD_NAME + 32];
	const char *one = name;

	(void) snprintf(name, sizeof (name), "%s@" FB_PSNAP "%zu",
	    b->fb_p->fp_src, n);
	if (sh_zfs_snapshot(b->fb_z, &one, 1, NULL, 0, b->fb_err) != 0)
		return (-1);
	b->fb_psnaps = n;
	return (0);
}

/* git -C <the checkout> apply ARGS... PATCH: 0 when it applied. */
static int
fb_apply(struct fb *b, const char *a1, const char *a2, const char *patch)
{
	const char *w[8];
	size_t n;

	n = 0;
	w[n++] = b->fb_t->tl_git;
	w[n++] = "-C";
	w[n++] = b->fb_p->fp_srcdir;
	w[n++] = "apply";
	if (a1 != NULL)
		w[n++] = a1;
	if (a2 != NULL)
		w[n++] = a2;
	w[n++] = patch;
	w[n] = NULL;
	return (fb_exec(b, w, NULL, 0, NULL, 1));
}

/*
 * 7.1.1 steps 2 - 4, from the patch FROM on: @shtetl-patch-0 first,
 * then each patch after a snapshot of its own, applied, or skipped as
 * already in when its reverse applies.
 */
static int fb_pause(struct fb *b, size_t i);

static int
fb_patches(struct fb *b, size_t from)
{
	struct sh_fplan *p = b->fb_p;
	const struct sh_node *srcs;
	struct fb_pa *pa;
	const char *loc;
	size_t i;

	if (b->fb_npa == 0)
		return (0);
	srcs = sh_obj_get(p->fp_recipe.sr_merged, "patches");
	if (from == 0) {
		if (fb_psnap(b, 0) != 0)
			return (-1);
		fb_push(b, FU_PATCHES, 0, p->fp_src);
		(void) fprintf(b->fb_out, "snapshot %s@" FB_PSNAP "0\n",
		    p->fp_src);
	}
	for (i = from; i < b->fb_npa; i++) {
		pa = &b->fb_pa[i];
		loc = srcs->sn_obj.ob_v[pa->pa_src].pr_key;
		if (fb_psnap(b, i + 1) != 0)
			return (-1);
		if (fb_apply(b, NULL, NULL, pa->pa_path) == 0) {
			b->fb_pz.pz_out[i] = SH_PO_APPLIED;
			(void) fprintf(b->fb_out, "patch %s from %s: applied\n",
			    pa->pa_name, loc);
		} else if (fb_apply(b, "-R", "--check", pa->pa_path) == 0) {
			b->fb_pz.pz_out[i] = SH_PO_ALREADY;
			(void) fprintf(b->fb_out, "patch %s from %s: already "
			    "in, skipped\n", pa->pa_name, loc);
		} else {
			return (fb_pause(b, i));
		}
	}
	return (0);
}

/* git -C <the checkout> W1 W2 W3: what it prints, its first line. */
static int
fb_git1(struct fb *b, const char *w1, const char *w2, const char *w3,
    char *line, size_t len)
{
	const char *w[7];
	char *out;
	size_t n;

	w[0] = b->fb_t->tl_git;
	w[1] = "-C";
	w[2] = b->fb_p->fp_srcdir;
	w[3] = w1;
	w[4] = w2;
	w[5] = w3;
	w[6] = NULL;
	if (fb_exec(b, w, NULL, FB_CAP, &out, 0) != 0)
		return (-1);
	n = out != NULL ? strcspn(out, "\n") : 0;
	(void) snprintf(line, len, "%.*s", (int)n, out != NULL ? out : "");
	free(out);
	return (0);
}

/* The tree as it is, as a git tree: everything added, the tree written. */
static int
fb_tree(struct fb *b, char *id, size_t len)
{
	char line[8];

	if (fb_git1(b, "add", "-A", NULL, line, sizeof (line)) != 0 ||
	    fb_git1(b, "write-tree", NULL, NULL, id, len) != 0)
		return (-1);
	if (id[0] == '\0') {
		sh_errf(b->fb_err, "git write-tree: no tree");
		return (-1);
	}
	return (0);
}

/* The files the patch PATCH touches, as git apply --numstat -z says. */
static char **
fb_touched(struct fb *b, const char *patch)
{
	const char *w[8];
	char *out, **files;
	size_t len;

	w[0] = b->fb_t->tl_git;
	w[1] = "-C";
	w[2] = b->fb_p->fp_srcdir;
	w[3] = "apply";
	w[4] = "--numstat";
	w[5] = "-z";
	w[6] = patch;
	w[7] = NULL;
	if (fb_run(b, w, NULL, 1 << 20, &out, &len, 0) != 0)
		return (NULL);
	files = sh_patch_files(out != NULL ? out : "", out != NULL ? len : 0,
	    b->fb_err);
	free(out);
	return (files);
}

/*
 * The patch I does not apply (7.1.1, the pause): the tree as it is
 * noted as the base of the fix; the patch merged three ways, or,
 * where that leaves no conflict marker and did not merge, applied
 * with --reject; the record written; what to do said. 1, paused.
 */
static int
fb_pause(struct fb *b, size_t i)
{
	struct sh_fplan *p = b->fb_p;
	struct fb_pa *pa = &b->fb_pa[i];
	char path[SH_BUILD_PATH * 2], **files;
	const char *loc, *how;
	size_t j;

	loc = sh_obj_get(p->fp_recipe.sr_merged, "patches")->
	    sn_obj.ob_v[pa->pa_src].pr_key;
	if (fb_tree(b, b->fb_pz.pz_base, sizeof (b->fb_pz.pz_base)) != 0 ||
	    (files = fb_touched(b, pa->pa_path)) == NULL)
		return (-1);
	if (fb_apply(b, "--3way", NULL, pa->pa_path) == 0) {
		how = "merged three ways without conflict, to be looked over";
	} else if (sh_patch_left(p->fp_srcdir, files, NULL) == 1) {
		how = "merged three ways, conflict markers left";
	} else {
		(void) fb_apply(b, "--reject", NULL, pa->pa_path);
		how = "applied with --reject, .rej files left";
	}
	b->fb_pz.pz_at = i;
	(void) snprintf(b->fb_pz.pz_config, sizeof (b->fb_pz.pz_config), "%s",
	    b->fb_cfgsnap);
	(void) snprintf(b->fb_pz.pz_version, sizeof (b->fb_pz.pz_version),
	    "%s", b->fb_version);
	(void) snprintf(b->fb_pz.pz_sha, sizeof (b->fb_pz.pz_sha), "%s",
	    b->fb_sha);
	(void) snprintf(b->fb_pz.pz_msg, sizeof (b->fb_pz.pz_msg), "%s",
	    b->fb_msg != NULL ? b->fb_msg : "");
	(void) snprintf(path, sizeof (path), "%s/" SH_BUILD_PAUSED,
	    p->fp_rundir);
	if (sh_pause_write(path, &b->fb_pz, b->fb_err) != 0) {
		sh_patch_list_free(files);
		return (-1);
	}
	(void) fprintf(b->fb_out, "patch %s from %s: does not apply; %s\n"
	    "  the patch: %s\n  in %s:", pa->pa_name, loc, how, pa->pa_path,
	    p->fp_srcdir);
	for (j = 0; files[j] != NULL; j++)
		(void) fprintf(b->fb_out, " %s", files[j]);
	(void) fprintf(b->fb_out, "\nbuild of %s paused; then one of:\n"
	    "  shtetl build %s --continue   the fix made, go on\n"
	    "  shtetl build %s --skip       the patch left out\n"
	    "  shtetl build %s --abort      nothing built, all undone\n",
	    p->fp_name, p->fp_name, p->fp_name, p->fp_name);
	sh_patch_list_free(files);
	return (1);
}

/*
 * --continue: the fix checked, nothing left by the patch's merge; the
 * fix written out as the tree's difference from the base, a patch to
 * replace the source's (fixed/<source>/<name>). Not 0 when refused.
 */
static int
fb_fixed(struct fb *b)
{
	struct sh_fplan *p = b->fb_p;
	struct fb_pa *pa = &b->fb_pa[p->fp_pause.pz_at];
	char fixed[SH_PATCH_STR], dir[SH_BUILD_PATH + 32];
	char path[SH_BUILD_PATH * 2], **files, *out;
	const char *w[9], *loc;
	size_t len;
	FILE *f;
	int left, bad;

	if ((files = fb_touched(b, pa->pa_path)) == NULL)
		return (-1);
	left = sh_patch_left(p->fp_srcdir, files, b->fb_err);
	sh_patch_list_free(files);
	if (left != 0)
		return (2);
	if (fb_tree(b, fixed, sizeof (fixed)) != 0)
		return (-1);
	w[0] = b->fb_t->tl_git;
	w[1] = "-C";
	w[2] = p->fp_srcdir;
	w[3] = "diff";
	w[4] = "--binary";
	w[5] = p->fp_pause.pz_base;
	w[6] = fixed;
	w[7] = NULL;
	if (fb_run(b, w, NULL, 64 << 20, &out, &len, 0) != 0)
		return (-1);
	(void) snprintf(dir, sizeof (dir), "%s/fixed/%zu", p->fp_rundir,
	    pa->pa_src + 1);
	(void) snprintf(path, sizeof (path), "%s/%s", dir, pa->pa_name);
	if (fb_mkdirs(b, dir) != 0) {
		free(out);
		return (-1);
	}
	if ((f = fopen(path, "w")) == NULL) {
		sh_errf(b->fb_err, "%s: %s", path, strerror(errno));
		free(out);
		return (-1);
	}
	bad = len > 0 && fwrite(out, 1, len, f) != len;
	if (fclose(f) != 0 || bad) {
		sh_errf(b->fb_err, "%s: %s", path, strerror(errno));
		free(out);
		return (-1);
	}
	free(out);
	b->fb_pz.pz_out[p->fp_pause.pz_at] = SH_PO_FIXED;
	loc = sh_obj_get(p->fp_recipe.sr_merged, "patches")->
	    sn_obj.ob_v[pa->pa_src].pr_key;
	(void) fprintf(b->fb_out, "patch %s from %s: fixed by hand; the "
	    "fix: %s\n", pa->pa_name, loc, path);
	return (0);
}

/*
 * A paused build taken up again (--continue, --skip): what the run
 * had done read back from its record, and pushed for undoing as it
 * was; the list read back from the run directory; then the patch
 * fixed by hand, or left out, the tree back to before it.
 */
static int
fb_resume(struct fb *b)
{
	struct sh_fplan *p = b->fb_p;
	struct sh_pause *pz = &p->fp_pause;
	char snap[SH_BUILD_NAME + 32];
	long major, minor;
	size_t i, at;

	(void) snprintf(b->fb_log, sizeof (b->fb_log), "%s/build.log",
	    p->fp_rundir);
	b->fb_pz = *pz;
	at = pz->pz_at;
	(void) snprintf(b->fb_cfgsnap, sizeof (b->fb_cfgsnap), "%s",
	    pz->pz_config);
	(void) snprintf(b->fb_version, sizeof (b->fb_version), "%s",
	    pz->pz_version);
	(void) snprintf(b->fb_sha, sizeof (b->fb_sha), "%s", pz->pz_sha);
	if (fb_revision(b, &major, &minor) != 0)
		return (-1);
	if (b->fb_msg == NULL && pz->pz_msg[0] != '\0')
		b->fb_msg = pz->pz_msg;
	if (sh_zfs_get(b->fb_z, b->fb_cfgsnap, "guid", b->fb_cfgguid,
	    sizeof (b->fb_cfgguid), NULL, b->fb_err) != 0)
		return (-1);
	fb_push(b, FU_UNLOCK, 0, p->fp_src);
	for (i = 0; i < p->fp_nds; i++)
		fb_push(b, p->fp_update ? FU_ROLLBACK : FU_DESTROY, 1,
		    p->fp_dss[i].fd_ds);
	if (fb_fetch(b, 0) != 0)
		return (-1);
	if (at >= b->fb_npa || b->fb_pz.pz_nsrc != pz->pz_nsrc) {
		sh_errf(b->fb_err, "%s: the record does not match the run "
		    "directory", p->fp_rundir);
		return (-1);
	}
	b->fb_pz.pz_nsrc = pz->pz_nsrc;
	b->fb_psnaps = at + 1;
	fb_push(b, FU_PATCHES, 0, p->fp_src);
	(void) fprintf(b->fb_out, "build of %s, paused at patch %s\n",
	    p->fp_name, b->fb_pa[at].pa_name);
	if (p->fp_verb == SH_BV_CONTINUE)
		return (fb_fixed(b));
	(void) snprintf(snap, sizeof (snap), FB_PSNAP "%zu", at + 1);
	if (sh_zfs_rollback(b->fb_z, p->fp_src, snap, b->fb_err) != 0)
		return (-1);
	b->fb_pz.pz_out[at] = SH_PO_SKIPPED;
	(void) fprintf(b->fb_out, "patch %s from %s: left out\n",
	    b->fb_pa[at].pa_name, sh_obj_get(p->fp_recipe.sr_merged,
	    "patches")->sn_obj.ob_v[b->fb_pa[at].pa_src].pr_key);
	return (0);
}

/* A walk of children: each name kept. */
static int
fb_kid(void *arg, const char *name)
{
	char ***kids = arg;
	char **nk;
	size_t n;

	for (n = 0; (*kids)[n] != NULL; n++)
		;
	if ((nk = realloc(*kids, (n + 2) * sizeof (*nk))) == NULL)
		return (-1);
	*kids = nk;
	if ((nk[n] = strdup(name)) == NULL)
		return (-1);
	nk[n + 1] = NULL;
	return (0);
}

/* DS and every filesystem below it destroyed, children first. */
static int
fb_destroyf(struct fb *b, const char *ds)
{
	char **kids;
	size_t i;
	int r;

	if ((kids = calloc(1, sizeof (*kids))) == NULL) {
		sh_err_nomem(b->fb_err);
		return (-1);
	}
	r = sh_zfs_children(b->fb_z, ds, fb_kid, &kids, b->fb_err);
	for (i = 0; r == 0 && kids[i] != NULL; i++)
		r = fb_destroyf(b, kids[i]);
	for (i = 0; kids[i] != NULL; i++)
		free(kids[i]);
	free(kids);
	if (r != 0 || sh_zfs_destroy(b->fb_z, ds, b->fb_err) != 0)
		return (-1);
	(void) fprintf(b->fb_out, "destroyed %s\n", ds);
	return (0);
}

/* DS and every filesystem below it rolled back to SNAP. */
static int
fb_rollbackf(struct fb *b, const char *ds, const char *snap)
{
	char **all;
	size_t i;
	int r;

	if ((all = calloc(1, sizeof (*all))) == NULL || fb_kid(&all,
	    ds) != 0) {
		free(all);
		sh_err_nomem(b->fb_err);
		return (-1);
	}
	r = fb_walk(b->fb_z, ds, &all, b->fb_err);
	for (i = 0; r == 0 && all[i] != NULL; i++) {
		r = sh_zfs_rollback(b->fb_z, all[i], snap, b->fb_err);
		if (r == 0)
			(void) fprintf(b->fb_out, "rolled back %s to @%s\n",
			    all[i], snap);
	}
	for (i = 0; all[i] != NULL; i++)
		free(all[i]);
	free(all);
	return (r);
}

/*
 * --abort: the build root back to @shtetl-patch-0, a new F's datasets
 * destroyed or an update's rolled back to the last version, the mark
 * cleared, the record and the fetched patches gone; the log and the
 * config snapshot stay.
 */
static int
fb_abort(struct fb *b)
{
	struct sh_fplan *p = b->fb_p;
	char path[SH_BUILD_PATH * 2];

	(void) snprintf(b->fb_log, sizeof (b->fb_log), "%s/build.log",
	    p->fp_rundir);
	b->fb_psnaps = p->fp_pause.pz_at + 1;
	/* every patch snapshot gone, or the rollback failed and said so */
	fb_unpatch(b);
	if (b->fb_psnaps != 0 || (p->fp_update ?
	    fb_rollbackf(b, p->fp_ds, p->fp_last) :
	    fb_destroyf(b, p->fp_ds)) != 0 ||
	    sh_zfs_inherit(b->fb_z, p->fp_src, SH_BUILD_PROP,
	    b->fb_err) != 0)
		return (-1);
	(void) snprintf(path, sizeof (path), "%s/" SH_BUILD_PAUSED,
	    p->fp_rundir);
	if (unlink(path) != 0 && errno != ENOENT) {
		sh_errf(b->fb_err, "%s: %s", path, strerror(errno));
		return (-1);
	}
	(void) snprintf(path, sizeof (path), "%s/patches", p->fp_rundir);
	if (fb_rmtree(b, path) != 0)
		return (-1);
	(void) snprintf(path, sizeof (path), "%s/fixed", p->fp_rundir);
	if (fb_rmtree(b, path) != 0)
		return (-1);
	(void) fprintf(b->fb_out, "build of %s given up, nothing built\n",
	    p->fp_name);
	return (0);
}

/*
 * The fixes made by hand kept in the obj cache, mounted, as
 * patches/<version>/<source>/<name>: the run directory's go with the
 * next build.
 */
static int
fb_keepfixes(struct fb *b)
{
	struct fb_pa *pa;
	char from[SH_BUILD_PATH * 2], dir[SH_BUILD_PATH * 2];
	char to[SH_BUILD_PATH * 3];
	size_t i, n;

	for (i = n = 0; i < b->fb_npa; i++) {
		if (b->fb_pz.pz_out[i] != SH_PO_FIXED)
			continue;
		pa = &b->fb_pa[i];
		(void) snprintf(from, sizeof (from), "%s/fixed/%zu/%s",
		    b->fb_p->fp_rundir, pa->pa_src + 1, pa->pa_name);
		(void) snprintf(dir, sizeof (dir), "%s/patches/%s/%zu",
		    b->fb_objdir, b->fb_version, pa->pa_src + 1);
		(void) snprintf(to, sizeof (to), "%s/%s", dir, pa->pa_name);
		if (fb_mkdirs(b, dir) != 0 || fb_copy(b, from, to) != 0)
			return (-1);
		n++;
	}
	if (n > 0)
		(void) fprintf(b->fb_out, "kept %zu fix%s in %s/patches/%s\n",
		    n, n == 1 ? "" : "es", b->fb_objdir, b->fb_version);
	return (0);
}

/* The record of a pause gone: the build has ended. */
static void
fb_unrecord(struct fb *b)
{
	char path[SH_BUILD_PATH * 2];

	(void) snprintf(path, sizeof (path), "%s/" SH_BUILD_PAUSED,
	    b->fb_p->fp_rundir);
	if (unlink(path) != 0 && errno != ENOENT)
		sh_errf(b->fb_err, "%s: %s", path, strerror(errno));
}

/* The files the patch PATCH touches, each added to *ALL once. */
static int
fb_retouch1(struct fb *b, const char *patch, char ***all)
{
	char **files;
	size_t i, j;
	int r;

	if ((files = fb_touched(b, patch)) == NULL)
		return (-1);
	r = 0;
	for (i = 0; r == 0 && files[i] != NULL; i++) {
		for (j = 0; (*all)[j] != NULL &&
		    strcmp((*all)[j], files[i]) != 0; j++)
			;
		if ((*all)[j] == NULL && fb_kid(all, files[i]) != 0) {
			sh_err_nomem(b->fb_err);
			r = -1;
		}
	}
	sh_patch_list_free(files);
	return (r);
}

/*
 * The files the first TAKEN patches touched, and a fix by hand did,
 * given the time of now once the build root is rolled back: the
 * rollback gives them back their times from before the patches,
 * older than the objects the obj cache built from them, so the next
 * build's make, which goes by times, would take those as up to date,
 * a patch since dropped still in them.
 */
static void
fb_retouch(struct fb *b, size_t taken)
{
	char fix[SH_BUILD_PATH * 2], path[SH_BUILD_PATH * 2], **all;
	struct fb_pa *pa;
	size_t i, n;

	if (b->fb_pa == NULL || (all = calloc(1, sizeof (*all))) == NULL)
		return;
	for (i = 0; i < taken && i < b->fb_npa; i++) {
		pa = &b->fb_pa[i];
		if (fb_retouch1(b, pa->pa_path, &all) != 0)
			break;
		if (b->fb_pz.pz_out[i] != SH_PO_FIXED)
			continue;
		(void) snprintf(fix, sizeof (fix), "%s/fixed/%zu/%s",
		    b->fb_p->fp_rundir, pa->pa_src + 1, pa->pa_name);
		if (fb_retouch1(b, fix, &all) != 0)
			break;
	}
	for (i = n = 0; all[i] != NULL; i++) {
		(void) snprintf(path, sizeof (path), "%s/%s",
		    b->fb_p->fp_srcdir, all[i]);
		if (utimensat(AT_FDCWD, path, NULL, AT_SYMLINK_NOFOLLOW) == 0)
			n++;
		else if (errno != ENOENT)
			sh_errf(b->fb_err, "%s: %s", path, strerror(errno));
		free(all[i]);
	}
	free(all);
	if (n > 0)
		(void) fprintf(b->fb_out, "the time of now on %zu patched "
		    "file%s, for the next build's make\n", n, n == 1 ? "" :
		    "s");
}

/*
 * 7.1.1 step 5: the patch snapshots destroyed newest first, the build
 * root rolled back to @shtetl-patch-0, and that destroyed too; the
 * files the patches touched given the time of now.
 */
static void
fb_unpatch(struct fb *b)
{
	struct sh_fplan *p = b->fb_p;
	char name[SH_BUILD_NAME + 32];
	size_t n;

	for (n = b->fb_psnaps; n > 0; n--) {
		(void) snprintf(name, sizeof (name), "%s@" FB_PSNAP "%zu",
		    p->fp_src, n);
		if (sh_zfs_destroy(b->fb_z, name, b->fb_err) != 0)
			return;
	}
	if (sh_zfs_rollback(b->fb_z, p->fp_src, FB_PSNAP "0",
	    b->fb_err) != 0)
		return;
	(void) snprintf(name, sizeof (name), "%s@" FB_PSNAP "0", p->fp_src);
	(void) sh_zfs_destroy(b->fb_z, name, b->fb_err);
	(void) fprintf(b->fb_out, "rolled back %s to @" FB_PSNAP "0\n",
	    p->fp_src);
	fb_retouch(b, b->fb_psnaps);
	b->fb_psnaps = 0;
}

/*
 * shtetl:patches (PROPOSAL): each source, with the commit it was
 * fetched at, and the patches of it not applied as they are, each with
 * what became of it: "src@commit: 2-x.patch already; patches/local".
 */
static char *
fb_record(struct fb *b)
{
	const struct sh_node *srcs;
	struct fb_pa *pa;
	size_t k, i, len, at;
	char *rec;
	int first;

	srcs = sh_obj_get(b->fb_p->fp_recipe.sr_merged, "patches");
	len = 1;
	for (k = 0; k < b->fb_pz.pz_nsrc; k++)
		len += strlen(srcs->sn_obj.ob_v[k].pr_key) + 64;
	for (i = 0; i < b->fb_npa; i++)
		len += strlen(b->fb_pa[i].pa_name) + 16;
	if ((rec = malloc(len)) == NULL)
		return (NULL);
	at = 0;
	for (k = 0; k < b->fb_pz.pz_nsrc; k++) {
		at += (size_t)snprintf(rec + at, len - at, "%s%s%s%s",
		    k == 0 ? "" : "; ", srcs->sn_obj.ob_v[k].pr_key,
		    b->fb_pz.pz_commit[k][0] != '\0' ? "@" : "",
		    b->fb_pz.pz_commit[k]);
		first = 1;
		for (i = 0; i < b->fb_npa; i++) {
			pa = &b->fb_pa[i];
			if (pa->pa_src != k ||
			    b->fb_pz.pz_out[i] == SH_PO_APPLIED)
				continue;
			at += (size_t)snprintf(rec + at, len - at, "%s%s %s",
			    first ? ": " : ", ", pa->pa_name,
			    sh_pout_name[b->fb_pz.pz_out[i]]);
			first = 0;
		}
	}
	rec[at] = '\0';
	return (rec);
}

/* The pieces of the text KEY (an object of them) of OBJ, joined. */
static char *
fb_text(const struct sh_node *obj, const char *key)
{
	const struct sh_node *t;
	const char *v;
	size_t i, len;
	char *s;

	t = sh_obj_get(obj, key);
	len = 1;
	for (i = 0; t != NULL && i < t->sn_obj.ob_n; i++)
		len += strlen(t->sn_obj.ob_v[i].pr_val->sn_str) + 1;
	if ((s = calloc(1, len)) == NULL)
		return (NULL);
	for (i = 0; t != NULL && i < t->sn_obj.ob_n; i++) {
		v = t->sn_obj.ob_v[i].pr_val->sn_str;
		(void) strcat(s, v);
		if (v[0] != '\0' && v[strlen(v) - 1] != '\n')
			(void) strcat(s, "\n");
	}
	return (s);
}

/*
 * The kernel config shtetl writes (planning.md 5.5): an include of the
 * config named (a name in the checkout's conf directory for this
 * machine, or a path in the configuration root), its own ident, the
 * lines; and src.conf, the lines of buildopts.
 */
static int
fb_configs(struct fb *b)
{
	struct sh_fplan *p = b->fb_p;
	const struct sh_node *kc, *f;
	struct utsname u;
	char inc[SH_BUILD_PATH], *lines, *text, *opts;
	const char *name;
	size_t i, len;
	int r;

	if (uname(&u) != 0) {
		sh_errf(b->fb_err, "uname: %s", strerror(errno));
		return (-1);
	}
	for (i = 0; p->fp_name[i] != '\0' && i + 1 < sizeof (b->fb_ident);
	    i++)
		b->fb_ident[i] = (p->fp_name[i] >= 'a' &&
		    p->fp_name[i] <= 'z') ? p->fp_name[i] - 'a' + 'A' :
		    p->fp_name[i];
	b->fb_ident[i] = '\0';
	kc = sh_obj_get(p->fp_recipe.sr_merged, "kernconf");
	f = sh_obj_get(kc, "file");
	name = f != NULL ? f->sn_str : "GENERIC";
	if (strchr(name, '/') == NULL)
		(void) snprintf(inc, sizeof (inc), "%s/sys/%s/conf/%s",
		    p->fp_srcdir, u.machine, name);
	else
		(void) snprintf(inc, sizeof (inc), "%s/%s", p->fp_root, name);
	lines = fb_text(kc, "lines");
	opts = fb_text(p->fp_recipe.sr_merged, "buildopts");
	len = strlen(inc) + strlen(b->fb_ident) + 128 +
	    (lines != NULL ? strlen(lines) : 0);
	if (lines == NULL || opts == NULL || (text = malloc(len)) == NULL) {
		free(lines);
		free(opts);
		sh_err_nomem(b->fb_err);
		return (-1);
	}
	(void) snprintf(text, len, "# written by shtetl for foundation %s\n"
	    "include \"%s\"\nident %s\n%s", p->fp_name, inc, b->fb_ident,
	    lines);
	(void) snprintf(b->fb_kernconf, sizeof (b->fb_kernconf), "%s/%s",
	    p->fp_rundir, b->fb_ident);
	(void) snprintf(b->fb_srcconf, sizeof (b->fb_srcconf), "%s/src.conf",
	    p->fp_rundir);
	r = fb_write(b, b->fb_kernconf, text) == 0 &&
	    fb_write(b, b->fb_srcconf, opts) == 0 ? 0 : -1;
	free(text);
	free(lines);
	free(opts);
	if (r == 0)
		(void) fprintf(b->fb_out, "wrote %s, %s\n", b->fb_kernconf,
		    b->fb_srcconf);
	return (r);
}

/*
 * DS's mountpoint set to DIR, made, and DS mounted there; where it is
 * then into WHERE: below the altroot, on a pool imported with one.
 */
static int
fb_mountat(struct fb *b, const char *ds, const char *dir, char *where,
    size_t len)
{
	if (fb_mkdirs(b, dir) != 0 ||
	    sh_zfs_set(b->fb_z, ds, "mountpoint", dir, b->fb_err) != 0)
		return (-1);
	fb_push(b, FU_INHERIT, 0, ds);
	if (sh_zfs_mount(b->fb_z, ds, b->fb_err) != 0)
		return (-1);
	fb_push(b, FU_UNMOUNT, 0, ds);
	return (sh_zfs_get(b->fb_z, ds, "mountpoint", where, len, NULL,
	    b->fb_err));
}

/*
 * The obj cache mounted at <run dir>/obj, then F at <run dir>/root
 * and each of its datasets below it, placeholders left unmounted;
 * make and the install are given where they are, which on a pool
 * imported with an altroot is below it.
 */
static int
fb_mounts(struct fb *b, size_t *fmark)
{
	struct sh_fplan *p = b->fb_p;
	char dir[SH_BUILD_PATH + 8];
	size_t i;

	(void) snprintf(dir, sizeof (dir), "%s/obj", p->fp_rundir);
	if (fb_mountat(b, p->fp_obj, dir, b->fb_objdir,
	    sizeof (b->fb_objdir)) != 0)
		return (-1);
	*fmark = b->fb_nundo;
	(void) snprintf(dir, sizeof (dir), "%s/root", p->fp_rundir);
	if (fb_mountat(b, p->fp_ds, dir, b->fb_rootdir,
	    sizeof (b->fb_rootdir)) != 0)
		return (-1);
	for (i = 1; i < p->fp_nds; i++) {
		if (p->fp_dss[i].fd_placeholder)
			continue;
		if (sh_zfs_mount(b->fb_z, p->fp_dss[i].fd_ds,
		    b->fb_err) != 0)
			return (-1);
		fb_push(b, FU_UNMOUNT, 0, p->fp_dss[i].fd_ds);
	}
	(void) fprintf(b->fb_out, "mounted %s at %s, %s at %s\n", p->fp_obj,
	    b->fb_objdir, p->fp_ds, b->fb_rootdir);
	return (0);
}

/*
 * make in the checkout, with what every target takes: buildworld and
 * buildkernel (HOW 0); TARGET, an install into F (1); or delete-old
 * delete-old-libs in F (2).
 */
static int
fb_make(struct fb *b, const char *target, const char *jobs, int how)
{
	struct sh_fplan *p = b->fb_p;
	char srcconf[SH_BUILD_PATH + 16], kdir[SH_BUILD_PATH + 16];
	char kconf[SH_BUILD_NAME + 16], dest[SH_BUILD_PATH + 16];
	char obj[SH_BUILD_PATH + 32];
	const char *w[13], *env[3];
	size_t n;

	(void) snprintf(srcconf, sizeof (srcconf), "SRCCONF=%s",
	    b->fb_srcconf);
	(void) snprintf(kdir, sizeof (kdir), "KERNCONFDIR=%s", p->fp_rundir);
	(void) snprintf(kconf, sizeof (kconf), "KERNCONF=%s", b->fb_ident);
	(void) snprintf(dest, sizeof (dest), "DESTDIR=%s", b->fb_dest != NULL ?
	    b->fb_dest : b->fb_rootdir);
	(void) snprintf(obj, sizeof (obj), "MAKEOBJDIRPREFIX=%s",
	    b->fb_objdir);
	n = 0;
	w[n++] = b->fb_t->tl_make;
	w[n++] = "-C";
	w[n++] = p->fp_srcdir;
	if (jobs != NULL)
		w[n++] = jobs;
	if (how == 1) {
		w[n++] = target;
		w[n++] = dest;
		w[n++] = "INSTALL_CMD=install -C";
	} else if (how == 2) {
		target = "delete-old delete-old-libs";
		w[n++] = "delete-old";
		w[n++] = "delete-old-libs";
		w[n++] = dest;
		w[n++] = "BATCH_DELETE_OLD_FILES=yes";
	} else {
		target = "buildworld buildkernel";
		w[n++] = "buildworld";
		w[n++] = "buildkernel";
	}
	w[n++] = srcconf;
	w[n++] = kdir;
	w[n++] = kconf;
	w[n] = NULL;
	/* MAKEOBJDIRPREFIX is read from the environment only */
	env[0] = "PATH=" SH_RUN_PATH;
	env[1] = obj;
	env[2] = NULL;
	(void) fprintf(b->fb_out, "make %s (log %s)\n", target, b->fb_log);
	(void) fflush(b->fb_out);
	return (fb_exec(b, w, env, 0, NULL, 0));
}

static int
fb_strcmp(const void *a, const void *b)
{
	return (strcmp(*(char *const *)a, *(char *const *)b));
}

/* Whether NAME is PRE...SUF, or with EXACT, PRE alone. */
static int
fb_named(const char *name, const char *pre, const char *suf, int exact)
{
	size_t n = strlen(name), lp = strlen(pre), ls = strlen(suf);

	if (exact)
		return (strcmp(name, pre) == 0);
	/* ld-elf.so.1.old is PRE and SUF alone; .old alone is nothing */
	return (n >= lp + ls && n > ls && strncmp(name, pre, lp) == 0 &&
	    strcmp(name + n - ls, suf) == 0);
}

/*
 * installworld's own backups (7.2 step 3), their flags cleared first:
 * boot/kernel.old and boot's other *.old, libexec/ld-elf*.so.1.old,
 * sbin/init.bak.
 */
static int
fb_backups(struct fb *b)
{
	static const struct {
		const char	*bk_dir;
		const char	*bk_pre;
		const char	*bk_suf;
		int		bk_exact;
	} bk[] = {
		{ "boot", "", ".old", 0 },
		{ "libexec", "ld-elf", ".so.1.old", 0 },
		{ "sbin", "init.bak", "", 1 }
	};
	char dir[SH_BUILD_PATH + 16], path[SH_BUILD_PATH * 2], **gone;
	struct dirent *de;
	size_t i, j;
	DIR *d;
	int r;

	if ((gone = calloc(1, sizeof (*gone))) == NULL) {
		sh_err_nomem(b->fb_err);
		return (-1);
	}
	r = 0;
	for (i = 0; r == 0 && i < sizeof (bk) / sizeof (bk[0]); i++) {
		(void) snprintf(dir, sizeof (dir), "%s/%s", b->fb_rootdir,
		    bk[i].bk_dir);
		if ((d = opendir(dir)) == NULL)
			continue;
		while (r == 0 && (de = readdir(d)) != NULL) {
			if (!fb_named(de->d_name, bk[i].bk_pre, bk[i].bk_suf,
			    bk[i].bk_exact))
				continue;
			(void) snprintf(path, sizeof (path), "%s/%s",
			    bk[i].bk_dir, de->d_name);
			if (fb_kid(&gone, path) != 0) {
				sh_err_nomem(b->fb_err);
				r = -1;
			}
		}
		(void) closedir(d);
	}
	for (j = 0; gone[j] != NULL; j++)
		;
	qsort(gone, j, sizeof (*gone), fb_strcmp);
	for (j = 0; r == 0 && gone[j] != NULL; j++) {
		(void) snprintf(path, sizeof (path), "%s/%s", b->fb_rootdir,
		    gone[j]);
		r = fb_rmtree(b, path);
		(void) fprintf(b->fb_out, "%s%s", j == 0 ?
		    "removed installworld's backups: " : ", ", gone[j]);
	}
	if (j > 0)
		(void) fputc('\n', b->fb_out);
	for (j = 0; gone[j] != NULL; j++)
		free(gone[j]);
	free(gone);
	return (r);
}

/* What a kernel's sync into F did, file by file. */
struct fb_sync {
	size_t	sy_kept;
	size_t	sy_copied;
	size_t	sy_removed;
};

/* The directories installkernel makes whole, mirrored into F. */
static const char *const fb_kdirs[] = { "boot/kernel",
	"usr/lib/debug/boot/kernel", NULL };

/* Whether REL is one of fb_kdirs, or below one. */
static int
fb_mirrored(const char *rel)
{
	size_t i, n;

	for (i = 0; fb_kdirs[i] != NULL; i++) {
		n = strlen(fb_kdirs[i]);
		if (strncmp(rel, fb_kdirs[i], n) == 0 &&
		    (rel[n] == '\0' || rel[n] == '/'))
			return (1);
	}
	return (0);
}

/* Whether the regular files A and B hold the same SIZE bytes. */
static int
fb_same(const char *a, const char *b, off_t size)
{
	char x[16384], y[16384];
	ssize_t na, nb;
	int fa, fb, same;

	if ((fa = open(a, O_RDONLY | O_CLOEXEC)) == -1)
		return (0);
	if ((fb = open(b, O_RDONLY | O_CLOEXEC)) == -1) {
		(void) close(fa);
		return (0);
	}
	same = 1;
	while (same && size > 0) {
		na = read(fa, x, sizeof (x));
		nb = read(fb, y, sizeof (y));
		if (na <= 0 || na != nb || memcmp(x, y, (size_t)na) != 0)
			same = 0;
		else
			size -= na;
	}
	(void) close(fa);
	(void) close(fb);
	return (same);
}

/*
 * SRC (its lstat SS) put at DST as install -C would: a file alike left
 * as it is, its mode and owner set if they differ; otherwise what DST
 * was removed and SRC copied, mode, owner and times kept (install -p).
 */
static int
fb_put(struct fb *b, const char *src, const char *dst, const struct stat *ss,
    struct fb_sync *sy)
{
	char buf[16384], to[SH_BUILD_PATH], at[SH_BUILD_PATH];
	struct timespec tm[2];
	struct stat ds;
	ssize_t n, r;
	int in, out;

	if (S_ISLNK(ss->st_mode)) {
		if ((n = readlink(src, to, sizeof (to) - 1)) < 0)
			goto bad;
		to[n] = '\0';
		r = readlink(dst, at, sizeof (at) - 1);
		if (r == n && memcmp(to, at, (size_t)n) == 0) {
			sy->sy_kept++;
			return (0);
		}
		if (fb_rmtree(b, dst) != 0)
			return (-1);
		if (symlink(to, dst) != 0)
			goto bad;
		(void) lchown(dst, ss->st_uid, ss->st_gid);
		sy->sy_copied++;
		return (0);
	}
	if (!S_ISREG(ss->st_mode))
		return (0);
	if (lstat(dst, &ds) == 0 && S_ISREG(ds.st_mode) &&
	    ds.st_size == ss->st_size && fb_same(src, dst, ss->st_size)) {
		if ((ds.st_mode & 07777) != (ss->st_mode & 07777) &&
		    chmod(dst, ss->st_mode & 07777) != 0)
			goto bad;
		if ((ds.st_uid != ss->st_uid || ds.st_gid != ss->st_gid) &&
		    chown(dst, ss->st_uid, ss->st_gid) != 0)
			goto bad;
		sy->sy_kept++;
		return (0);
	}
	if (fb_rmtree(b, dst) != 0)
		return (-1);
	if ((in = open(src, O_RDONLY | O_CLOEXEC)) == -1) {
		dst = src;
		goto bad;
	}
	if ((out = open(dst, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC,
	    ss->st_mode & 0777)) == -1) {
		(void) close(in);
		goto bad;
	}
	r = 0;
	while (r == 0 && (n = read(in, buf, sizeof (buf))) > 0) {
		if (write(out, buf, (size_t)n) != n)
			r = -1;
	}
	if (n < 0)
		r = -1;
#ifdef __APPLE__
	tm[0] = ss->st_atimespec;
	tm[1] = ss->st_mtimespec;
#else
	tm[0] = ss->st_atim;
	tm[1] = ss->st_mtim;
#endif
	if (r != 0 || fchown(out, ss->st_uid, ss->st_gid) != 0 ||
	    fchmod(out, ss->st_mode & 07777) != 0 || futimens(out, tm) != 0) {
		(void) close(in);
		(void) close(out);
		goto bad;
	}
	(void) close(in);
	if (close(out) != 0)
		goto bad;
	sy->sy_copied++;
	return (0);
bad:
	sh_errf(b->fb_err, "%s: %s", dst, strerror(errno));
	return (-1);
}

/*
 * SROOT's REL put at DROOT's: each file by fb_put, each directory in
 * turn; in a directory of fb_kdirs, what the new kernel has not is
 * removed.
 */
static int
fb_sync(struct fb *b, const char *sroot, const char *droot,
    const char *rel, struct fb_sync *sy)
{
	char s[SH_BUILD_PATH * 2], d[SH_BUILD_PATH * 2];
	char sub[SH_BUILD_PATH], a[SH_BUILD_PATH * 3], z[SH_BUILD_PATH * 3];
	struct dirent *de;
	struct stat ss, ds;
	DIR *dir;
	int r;

	(void) snprintf(s, sizeof (s), "%s%s%s", sroot, rel[0] != '\0' ?
	    "/" : "", rel);
	(void) snprintf(d, sizeof (d), "%s%s%s", droot, rel[0] != '\0' ?
	    "/" : "", rel);
	if (lstat(s, &ss) != 0)
		goto bad;
	if (lstat(d, &ds) == 0 && !S_ISDIR(ds.st_mode) &&
	    fb_rmtree(b, d) != 0)
		return (-1);
	if (lstat(d, &ds) != 0 && (mkdir(d, ss.st_mode & 07777) != 0 ||
	    chown(d, ss.st_uid, ss.st_gid) != 0))
		goto bad;
	if ((dir = opendir(s)) == NULL)
		goto bad;
	r = 0;
	while (r == 0 && (de = readdir(dir)) != NULL) {
		if (strcmp(de->d_name, ".") == 0 ||
		    strcmp(de->d_name, "..") == 0)
			continue;
		(void) snprintf(sub, sizeof (sub), "%s%s%s", rel,
		    rel[0] != '\0' ? "/" : "", de->d_name);
		(void) snprintf(a, sizeof (a), "%s/%s", s, de->d_name);
		(void) snprintf(z, sizeof (z), "%s/%s", d, de->d_name);
		if (lstat(a, &ss) != 0) {
			(void) closedir(dir);
			(void) snprintf(d, sizeof (d), "%s", a);
			goto bad;
		}
		r = S_ISDIR(ss.st_mode) ? fb_sync(b, sroot, droot, sub, sy) :
		    fb_put(b, a, z, &ss, sy);
	}
	(void) closedir(dir);
	if (r != 0 || !fb_mirrored(rel))
		return (r);
	if ((dir = opendir(d)) == NULL)
		goto bad;
	while (r == 0 && (de = readdir(dir)) != NULL) {
		if (strcmp(de->d_name, ".") == 0 ||
		    strcmp(de->d_name, "..") == 0)
			continue;
		(void) snprintf(a, sizeof (a), "%s/%s", s, de->d_name);
		if (lstat(a, &ss) == 0)
			continue;
		(void) snprintf(z, sizeof (z), "%s/%s", d, de->d_name);
		if ((r = fb_rmtree(b, z)) == 0)
			sy->sy_removed++;
	}
	(void) closedir(dir);
	return (r);
bad:
	sh_errf(b->fb_err, "%s: %s", d, strerror(errno));
	return (-1);
}

/*
 * The kernel (7.1 step 5): installkernel into the obj cache's
 * kernel-install, then synced into F. installkernel into F itself
 * would remove its /boot/kernel first (sys/conf/kern.post.mk keeps a
 * .old only over the running kernel's directory), every file new on
 * every update. Synced, a file alike is left as it is, as install -C
 * leaves one; a changed or new one is copied in; in fb_kdirs, what the
 * new kernel has not is removed.
 */
static int
fb_kernel(struct fb *b)
{
	char tmp[SH_BUILD_PATH + 32], path[SH_BUILD_PATH * 2];
	struct fb_sync sy;
	struct stat st;
	size_t i;
	int r;

	(void) memset(&sy, 0, sizeof (sy));
	(void) snprintf(tmp, sizeof (tmp), "%s/kernel-install", b->fb_objdir);
	if (fb_rmtree(b, tmp) != 0 || fb_mkdirs(b, tmp) != 0)
		return (-1);
	b->fb_dest = tmp;
	r = fb_make(b, "installkernel", NULL, 1);
	b->fb_dest = NULL;
	if (r != 0)
		return (-1);
	(void) snprintf(path, sizeof (path), "%s/boot/kernel", tmp);
	if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) {
		sh_errf(b->fb_err, "installkernel: no boot/kernel in %s", tmp);
		return (-1);
	}
	if (fb_sync(b, tmp, b->fb_rootdir, "", &sy) != 0)
		return (-1);
	/* a kernel directory the new kernel has none of: gone from F */
	for (i = 0; fb_kdirs[i] != NULL; i++) {
		(void) snprintf(path, sizeof (path), "%s/%s", tmp,
		    fb_kdirs[i]);
		if (lstat(path, &st) == 0)
			continue;
		(void) snprintf(path, sizeof (path), "%s/%s", b->fb_rootdir,
		    fb_kdirs[i]);
		if (lstat(path, &st) != 0)
			continue;
		if (fb_rmtree(b, path) != 0)
			return (-1);
		sy.sy_removed++;
	}
	(void) fprintf(b->fb_out, "kernel into %s: %zu kept, %zu copied, "
	    "%zu removed\n", b->fb_p->fp_ds, sy.sy_kept, sy.sy_copied,
	    sy.sy_removed);
	return (fb_rmtree(b, tmp));
}

/*
 * The build, and the install into F (7.1 steps 4, 5), the kernel by
 * fb_kernel; for an update, delete-old before the install and
 * installworld's backups gone after (7.2).
 */
static int
fb_world(struct fb *b)
{
	static const char *const installs[] = { "installworld",
	    "installkernel", "distribution" };
	char jobs[32];
	long n;
	size_t i;

	n = b->fb_p->fp_jobs > 0 ? b->fb_p->fp_jobs :
	    sysconf(_SC_NPROCESSORS_ONLN);
	(void) snprintf(jobs, sizeof (jobs), "-j%ld", n > 0 ? n : 1);
	if (fb_make(b, NULL, jobs, 0) != 0)
		return (-1);
	if (b->fb_p->fp_update && fb_make(b, NULL, NULL, 2) != 0)
		return (-1);
	for (i = 0; i < sizeof (installs) / sizeof (installs[0]); i++) {
		if ((strcmp(installs[i], "installkernel") == 0 ? fb_kernel(b) :
		    fb_make(b, installs[i], NULL, 1)) != 0)
			return (-1);
	}
	return (b->fb_p->fp_update ? fb_backups(b) : 0);
}

static const char *const fb_env[] = { "PATH=" SH_RUN_PATH,
	"ASSUME_ALWAYS_YES=yes", "HOME=/root", NULL };

/*
 * A chroot of F made ready: devfs mounted in it and, for PKG, which
 * fetches, the host's resolv.conf where F has none, while it does;
 * each pushed for undoing.
 */
static int
fb_chroot(struct fb *b, int pkg)
{
	char dev[SH_BUILD_PATH + 8], resolv[SH_BUILD_PATH + 32];
	const char *w[6];
	struct stat st;

	(void) snprintf(dev, sizeof (dev), "%s/dev", b->fb_rootdir);
	if (fb_mkdirs(b, dev) != 0)
		return (-1);
	w[0] = b->fb_t->tl_mount;
	w[1] = "-t";
	w[2] = "devfs";
	w[3] = "devfs";
	w[4] = dev;
	w[5] = NULL;
	if (fb_exec(b, w, NULL, 0, NULL, 0) != 0)
		return (-1);
	fb_push(b, FU_UMOUNT, 0, dev);
	if (!pkg)
		return (0);
	(void) snprintf(resolv, sizeof (resolv), "%s/etc/resolv.conf",
	    b->fb_rootdir);
	if (stat(resolv, &st) != 0 && stat("/etc/resolv.conf", &st) == 0) {
		if (fb_copy(b, "/etc/resolv.conf", resolv) != 0)
			return (-1);
		fb_push(b, FU_UNLINK, 0, resolv);
	}
	return (0);
}

/* The recipe's packages and commands, in a chroot of F (7.1 step 6). */
static int
fb_inside(struct fb *b)
{
	struct sh_fplan *p = b->fb_p;
	const struct sh_node *pkg, *cmds, *block;
	const char **w;
	size_t i, j, mark;
	int rc;

	pkg = sh_obj_get(p->fp_recipe.sr_merged, "pkg");
	cmds = sh_obj_get(p->fp_recipe.sr_merged, "commands");
	if ((pkg == NULL || pkg->sn_obj.ob_n == 0) &&
	    (cmds == NULL || cmds->sn_obj.ob_n == 0))
		return (0);
	mark = b->fb_nundo;
	if ((w = calloc(8 + (pkg != NULL ? pkg->sn_obj.ob_n : 0),
	    sizeof (*w))) == NULL) {
		sh_err_nomem(b->fb_err);
		return (-1);
	}
	rc = -1;
	if (fb_chroot(b, pkg != NULL && pkg->sn_obj.ob_n > 0) != 0)
		goto out;
	if (pkg != NULL && pkg->sn_obj.ob_n > 0) {
		w[0] = b->fb_t->tl_pkg;
		w[1] = "-c";
		w[2] = b->fb_rootdir;
		w[3] = "install";
		w[4] = "-y";
		w[5] = "--";
		for (i = 0; i < pkg->sn_obj.ob_n; i++)
			w[6 + i] = pkg->sn_obj.ob_v[i].pr_key;
		w[6 + i] = NULL;
		(void) fprintf(b->fb_out, "pkg install in %s\n", b->fb_rootdir);
		if (fb_exec(b, w, fb_env, 0, NULL, 0) != 0)
			goto out;
	}
	for (i = 0; cmds != NULL && i < cmds->sn_obj.ob_n; i++) {
		block = cmds->sn_obj.ob_v[i].pr_val;
		for (j = 0; j < block->sn_obj.ob_n; j++) {
			w[0] = b->fb_t->tl_chroot;
			w[1] = b->fb_rootdir;
			w[2] = "/bin/sh";
			w[3] = "-e";
			w[4] = "-c";
			w[5] = block->sn_obj.ob_v[j].pr_val->sn_str;
			w[6] = NULL;
			(void) fprintf(b->fb_out, "commands %s, from %s\n",
			    cmds->sn_obj.ob_v[i].pr_key,
			    block->sn_obj.ob_v[j].pr_key);
			if (fb_exec(b, w, fb_env, 0, NULL, 0) != 0)
				goto out;
		}
	}
	rc = 0;
	fb_undo(b, mark, 0);
out:
	free(w);
	return (rc);
}

/*
 * F and every dataset of it, snapshotted as the version (7.1 step 7),
 * or KIND pkg, a pkg upgrade of one, which carries its record over.
 */
static int
fb_snapshot(struct fb *b, const char *msg, const char *kind)
{
	struct sh_fplan *p = b->fb_p;
	struct sh_zprop zp[6];
	char names[SH_BUILD_DS][SH_BUILD_NAME + SH_VERSION_MAX + 2];
	const char *np[SH_BUILD_DS];
	char deflt[256], *rec;
	size_t i, nzp;
	int r;

	if (msg == NULL) {
		(void) snprintf(deflt, sizeof (deflt), "build %s at %.12s",
		    p->fp_src_branch, b->fb_sha);
		msg = deflt;
	}
	zp[0].zp_name = "shtetl:kind";
	zp[0].zp_value = kind;
	zp[1].zp_name = "shtetl:commit";
	zp[1].zp_value = msg;
	zp[2].zp_name = "shtetl:config";
	zp[2].zp_value = b->fb_cfgsnap;
	zp[3].zp_name = "shtetl:config-guid";
	zp[3].zp_value = b->fb_cfgguid;
	nzp = 4;
	if (b->fb_rev[0] != '\0') {
		zp[nzp].zp_name = "shtetl:revision";
		zp[nzp++].zp_value = b->fb_rev;
	}
	rec = NULL;
	if (b->fb_pz.pz_nsrc > 0 || b->fb_prec[0] != '\0') {
		if ((rec = b->fb_prec[0] != '\0' ? strdup(b->fb_prec) :
		    fb_record(b)) == NULL) {
			sh_err_nomem(b->fb_err);
			return (-1);
		}
		zp[nzp].zp_name = "shtetl:patches";
		zp[nzp++].zp_value = rec;
	}
	for (i = 0; i < p->fp_nds; i++) {
		(void) snprintf(names[i], sizeof (names[i]), "%s@%s",
		    p->fp_dss[i].fd_ds, b->fb_version);
		np[i] = names[i];
	}
	r = sh_zfs_snapshot(b->fb_z, np, p->fp_nds, zp, nzp, b->fb_err);
	free(rec);
	if (r != 0)
		return (-1);
	(void) fprintf(b->fb_out, "snapshot -r %s@%s\n", p->fp_ds,
	    b->fb_version);
	return (0);
}

int
sh_build_run(struct sh_zfs *z, struct sh_fplan *p,
    const struct sh_tools *t, const char *msg, time_t now, FILE *out,
    struct sh_err *e)
{
	struct fb *b;
	size_t fmark, i;
	int rc, r;

	if ((b = calloc(1, sizeof (*b))) == NULL) {
		sh_err_nomem(e);
		return (-1);
	}
	b->fb_z = z;
	b->fb_p = p;
	b->fb_t = t;
	b->fb_out = out;
	b->fb_err = e;
	b->fb_msg = msg;
	rc = -1;
	if (p->fp_verb == SH_BV_ABORT) {
		if (fb_abort(b) == 0)
			rc = 0;
		goto out;
	}
	if (p->fp_verb == SH_BV_NEW) {
		r = fb_begin(b, now) != 0 || fb_create(b) != 0 ||
		    fb_checkout(b) != 0 || fb_version(b, now) != 0 ||
		    fb_fetch(b, 1) != 0 ? -1 : fb_patches(b, 0);
	} else {
		/* taking the pause up fails: it stays, for another try */
		r = fb_resume(b) != 0 ? 2 : fb_patches(b,
		    p->fp_pause.pz_at + 1);
	}
	if (r == 1) {
		/* paused: everything left as it is, for a person */
		rc = 1;
		goto out;
	}
	if (r == 2) {
		sh_errf(e, "build of %s still paused", p->fp_name);
		goto out;
	}
	if (r != 0 || fb_configs(b) != 0 || fb_mounts(b, &fmark) != 0 ||
	    fb_keepfixes(b) != 0 || fb_world(b) != 0 || fb_inside(b) != 0)
		goto fail;
	/* F unmounted, its mountpoint back, and then the version */
	fb_undo(b, fmark, 0);
	if (fb_snapshot(b, b->fb_msg, "version") != 0)
		goto fail;
	fb_commit(b);
	fb_undo(b, 0, 0);
	fb_unrecord(b);
	(void) fprintf(out, "built %s@%s\n", p->fp_ds, b->fb_version);
	rc = 0;
	goto out;
fail:
	fb_undo(b, 0, 1);
	fb_unrecord(b);
	sh_errf(e, "build of %s failed; log %s", p->fp_name, b->fb_log);
out:
	for (i = 0; b->fb_pa != NULL && i < b->fb_npa; i++)
		free(b->fb_pa[i].pa_name);
	free(b->fb_pa);
	free(b);
	return (rc);
}

/* ---- pkg-upgrade ------------------------------------------------- */

/* A walk of F's filesystems, as P's datasets: placeholders by canmount. */
static int
fb_pkgsets(struct sh_zfs *z, struct sh_fplan *p, struct sh_err *e)
{
	char **all, cm[SH_ZFS_VALMAX];
	size_t i, n;
	int r;

	if ((all = calloc(1, sizeof (*all))) == NULL) {
		sh_err_nomem(e);
		return (-1);
	}
	n = strlen(p->fp_ds);
	r = fb_add(p, "", 0, NULL, e) != 0 ? -1 : fb_walk(z, p->fp_ds, &all,
	    e);
	for (i = 0; r == 0 && all[i] != NULL; i++) {
		if ((r = sh_zfs_get(z, all[i], "canmount", cm, sizeof (cm),
		    NULL, e)) == 0)
			r = fb_add(p, all[i] + n + 1, strcmp(cm, "off") == 0,
			    NULL, e);
	}
	for (i = 0; all[i] != NULL; i++)
		free(all[i]);
	free(all);
	return (r);
}

int
sh_pkg_plan(struct sh_zfs *z, const struct sh_spaces *s,
    const char *target, const char *runbase, struct sh_fplan *p,
    struct sh_err *e)
{
	char val[SH_ZFS_VALMAX], name[SH_BUILD_NAME * 2];
	char snap[SH_BUILD_NAME * 3];
	enum sh_zsrc src;
	unsigned before;
	size_t len;
	int r, n;
	char *at;

	(void) memset(p, 0, sizeof (*p));
	before = e->se_count;
	if (sh_build_target(target, p->fp_name, sizeof (p->fp_name), e) != 0)
		return (-1);
	if (s->ss_root[SH_SP_LAKE] == NULL)
		sh_errf(e, "no lake root");
	if (s->ss_root[SH_SP_BUILD] == NULL)
		sh_errf(e, "no build root");
	if (e->se_count != before)
		return (-1);
	(void) snprintf(p->fp_ds, sizeof (p->fp_ds), "%s/foundations/%s",
	    s->ss_root[SH_SP_LAKE], p->fp_name);
	(void) snprintf(p->fp_src, sizeof (p->fp_src), "%s",
	    s->ss_root[SH_SP_BUILD]);
	(void) snprintf(p->fp_rundir, sizeof (p->fp_rundir), "%s/%s",
	    runbase, p->fp_name);
	p->fp_update = 1;
	if (sh_zfs_get(z, p->fp_src, SH_BUILD_PROP, val, sizeof (val), &src,
	    e) != 0)
		return (-1);
	if (src == SH_ZSRC_LOCAL)
		sh_errf(e, "build of %s %s", val, fb_paused(runbase, val) ?
		    "paused" : "in progress");
	if ((r = sh_zfs_get(z, p->fp_ds, "type", val, sizeof (val), NULL,
	    e)) != 0) {
		if (r == 1)
			sh_errf(e, "no foundation %s", p->fp_name);
		return (-1);
	}
	if (fb_last(z, p->fp_ds, p->fp_last, e) != 0)
		return (-1);
	if (p->fp_last[0] == '\0') {
		sh_errf(e, "foundation %s exists without a version",
		    p->fp_name);
		return (-1);
	}
	/* the version's -pkg, after another pkg upgrade of it too */
	(void) snprintf(p->fp_next, sizeof (p->fp_next), "%s", p->fp_last);
	if ((at = strstr(p->fp_next, "-pkg")) != NULL &&
	    (at[4] == '\0' || at[4] == '.'))
		*at = '\0';
	len = strlen(p->fp_next);
	(void) snprintf(p->fp_next + len, sizeof (p->fp_next) - len, "-pkg");
	(void) snprintf(name, sizeof (name), "%s", p->fp_next);
	for (n = 2; n < 100; n++) {
		(void) snprintf(snap, sizeof (snap), "%s@%s", p->fp_ds, name);
		if (sh_zfs_get(z, snap, "type", val, sizeof (val), NULL,
		    e) != 0)
			break;
		(void) snprintf(name, sizeof (name), "%s.%d", p->fp_next, n);
	}
	(void) snprintf(p->fp_next, sizeof (p->fp_next), "%s", name);
	(void) fb_pkgsets(z, p, e);
	return (e->se_count == before ? 0 : -1);
}

void
sh_pkg_show(FILE *out, const struct sh_fplan *p)
{
	(void) fprintf(out, "foundation %s, its packages upgraded, from @%s\n"
	    "  mounted below %s/root, with its datasets\n"
	    "  pkg -c <root> upgrade -y\n"
	    "  snapshot -r %s@%s\n", p->fp_name, p->fp_last, p->fp_rundir,
	    p->fp_ds, p->fp_next);
}

int
sh_pkg_run(struct sh_zfs *z, struct sh_fplan *p,
    const struct sh_tools *t, const char *msg, FILE *out,
    struct sh_err *e)
{
	struct fb *b;
	char dir[SH_BUILD_PATH + 8], snap[SH_BUILD_NAME * 2], deflt[512];
	const char *w[7];
	size_t i, fmark;
	int rc, fd;

	if ((b = calloc(1, sizeof (*b))) == NULL) {
		sh_err_nomem(e);
		return (-1);
	}
	b->fb_z = z;
	b->fb_p = p;
	b->fb_t = t;
	b->fb_out = out;
	b->fb_err = e;
	rc = -1;
	(void) snprintf(b->fb_log, sizeof (b->fb_log), "%s/pkg.log",
	    p->fp_rundir);
	(void) snprintf(snap, sizeof (snap), "%s@%s", p->fp_ds, p->fp_last);
	/* the version's record, carried over */
	if (fb_mkdirs(b, p->fp_rundir) != 0 ||
	    sh_zfs_get(z, snap, "shtetl:config", b->fb_cfgsnap,
	    sizeof (b->fb_cfgsnap), NULL, e) != 0 ||
	    sh_zfs_get(z, snap, "shtetl:config-guid", b->fb_cfgguid,
	    sizeof (b->fb_cfgguid), NULL, e) != 0 ||
	    sh_zfs_get(z, snap, "shtetl:patches", b->fb_prec,
	    sizeof (b->fb_prec), NULL, e) != 0 ||
	    sh_zfs_get(z, snap, "shtetl:revision", b->fb_rev,
	    sizeof (b->fb_rev), NULL, e) != 0)
		goto out;
	if (strcmp(b->fb_prec, "-") == 0)
		b->fb_prec[0] = '\0';
	if (strcmp(b->fb_rev, "-") == 0)
		b->fb_rev[0] = '\0';
	if ((fd = open(b->fb_log, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC,
	    0644)) == -1) {
		sh_errf(e, "%s: %s", b->fb_log, strerror(errno));
		goto out;
	}
	(void) close(fd);
	if (sh_zfs_set(z, p->fp_src, SH_BUILD_PROP, p->fp_name, e) != 0)
		goto out;
	fb_push(b, FU_UNLOCK, 0, p->fp_src);
	for (i = 0; i < p->fp_nds; i++)
		fb_push(b, FU_ROLLBACK, 1, p->fp_dss[i].fd_ds);
	fmark = b->fb_nundo;
	(void) snprintf(dir, sizeof (dir), "%s/root", p->fp_rundir);
	if (fb_mountat(b, p->fp_ds, dir, b->fb_rootdir,
	    sizeof (b->fb_rootdir)) != 0)
		goto fail;
	for (i = 1; i < p->fp_nds; i++) {
		if (p->fp_dss[i].fd_placeholder)
			continue;
		if (sh_zfs_mount(z, p->fp_dss[i].fd_ds, e) != 0)
			goto fail;
		fb_push(b, FU_UNMOUNT, 0, p->fp_dss[i].fd_ds);
	}
	if (fb_chroot(b, 1) != 0)
		goto fail;
	w[0] = t->tl_pkg;
	w[1] = "-c";
	w[2] = b->fb_rootdir;
	w[3] = "upgrade";
	w[4] = "-y";
	w[5] = NULL;
	(void) fprintf(out, "pkg upgrade in %s (log %s)\n", b->fb_rootdir,
	    b->fb_log);
	(void) fflush(out);
	if (fb_exec(b, w, fb_env, 0, NULL, 0) != 0)
		goto fail;
	fb_undo(b, fmark, 0);
	(void) snprintf(b->fb_version, sizeof (b->fb_version), "%s",
	    p->fp_next);
	(void) snprintf(deflt, sizeof (deflt), "pkg upgrade of %s",
	    p->fp_last);
	if (fb_snapshot(b, msg != NULL ? msg : deflt, "pkg") != 0)
		goto fail;
	fb_commit(b);
	fb_undo(b, 0, 0);
	(void) fprintf(out, "upgraded %s@%s\n", p->fp_ds, p->fp_next);
	rc = 0;
	goto out;
fail:
	fb_undo(b, 0, 1);
	sh_errf(e, "pkg upgrade of %s failed; log %s", p->fp_name, b->fb_log);
out:
	free(b);
	return (rc);
}

void
sh_build_free(struct sh_fplan *p)
{
	sh_recipe_free(&p->fp_recipe);
}
