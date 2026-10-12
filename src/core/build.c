/*
 * shtetl build F (planning.md 7.1).
 *
 * The plan: every check made before anything is done, and what will
 * be done shown in full, as bootstrap's is (4.2).
 *
 * Decided here, beyond the plan (PROPOSAL): the target is the
 * foundation's name or its recipe's path; a build refuses one already
 * under way, the build root's shtetl:build naming it; the datasets of
 * F are F, each dataset of the recipe, and a placeholder
 * (canmount=off, holding nothing, 4.7) for every parent the recipe
 * does not name, parents first.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/build.h"
#include "core/cascade.h"
#include "core/err.h"
#include "core/spaces.h"
#include "core/syntax.h"
#include "core/tree.h"
#include "core/version.h"
#include "core/zfs.h"

/* TARGET, F or foundations/F[.conf], as F into NAME. */
static int
fb_target(const char *target, char *name, size_t len, struct sh_err *e)
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

int
sh_build_plan(struct sh_zfs *z, const struct sh_spaces *s,
    const struct sh_node *site, const char *root, const char *target,
    const char *runbase, struct sh_fplan *p, struct sh_err *e,
    struct sh_err *w)
{
	char rpath[SH_BUILD_NAME + 32], val[SH_ZFS_VALMAX];
	const struct sh_node *v;
	enum sh_zsrc src;
	unsigned before;
	int r, sp;

	(void) memset(p, 0, sizeof (*p));
	before = e->se_count;
	if (fb_target(target, p->fp_name, sizeof (p->fp_name), e) != 0)
		return (-1);
	for (sp = 0; sp < SH_SP_N; sp++) {
		if (sp != SH_SP_RUN && s->ss_root[sp] == NULL)
			sh_errf(e, "no %s root", sh_space_name(sp));
	}
	if (e->se_count != before)
		return (-1);
	(void) snprintf(rpath, sizeof (rpath), "foundations/%s", p->fp_name);
	if (sh_recipe_resolve(root, rpath, &p->fp_recipe, e, w) != 0)
		return (-1);
	p->fp_src_branch = sh_obj_get(p->fp_recipe.sr_merged,
	    "src_branch")->sn_str;
	(void) sh_syn_branch(p->fp_src_branch, &p->fp_branch);
	(void) sh_version_check_branch(&p->fp_branch,
	    sh_obj_get(p->fp_recipe.sr_merged, "ver"), p->fp_recipe.sr_file,
	    e);

	(void) snprintf(p->fp_ds, sizeof (p->fp_ds), "%s/foundations/%s",
	    s->ss_root[SH_SP_LAKE], p->fp_name);
	(void) snprintf(p->fp_obj, sizeof (p->fp_obj),
	    "%s/foundation-builds/%s", s->ss_root[SH_SP_LAKE], p->fp_name);
	(void) snprintf(p->fp_src, sizeof (p->fp_src), "%s",
	    s->ss_root[SH_SP_BUILD]);
	(void) snprintf(p->fp_config, sizeof (p->fp_config), "%s",
	    s->ss_root[SH_SP_CONFIG]);
	(void) snprintf(p->fp_rundir, sizeof (p->fp_rundir), "%s/%s",
	    runbase, p->fp_name);

	/* one build at a time: the build root says which */
	r = sh_zfs_get(z, p->fp_src, SH_BUILD_PROP, val, sizeof (val), &src,
	    e);
	if (r == 0 && src == SH_ZSRC_LOCAL)
		sh_errf(e, "build of %s in progress", val);
	else if (r == 0)
		(void) fb_srcdir(z, p, e);
	r = sh_zfs_get(z, p->fp_ds, "type", val, sizeof (val), NULL, e);
	if (r == 0)
		sh_errf(e, "foundation %s exists", p->fp_name);
	r = sh_zfs_get(z, p->fp_obj, "type", val, sizeof (val), NULL, e);
	p->fp_obj_there = r == 0;
	(void) fb_datasets(p, e);
	v = sh_obj_get(sh_obj_get(site, "build"), "make_jobs");
	p->fp_jobs = v != NULL && v->sn_type == SH_INT ? (long)v->sn_int : 0;
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

	(void) fprintf(out, "foundation %s, new, from %s\n", p->fp_name,
	    p->fp_src_branch);
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
		(void) fprintf(out, "  + %s", p->fp_dss[i].fd_ds);
		fb_props(out, &p->fp_dss[i]);
		(void) fputc('\n', out);
	}
	if (!p->fp_obj_there)
		(void) fprintf(out, "  + %s\n", p->fp_obj);
	(void) fprintf(out, "  checkout %s in %s (%s)\n", p->fp_src_branch,
	    p->fp_src, p->fp_srcdir);
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
	(void) fprintf(out, "  install world, kernel and distribution "
	    "(install -C)\n");
	(void) fprintf(out, "  pkg: ");
	fb_keys(out, p, "pkg");
	(void) fprintf(out, "\n  commands: ");
	fb_keys(out, p, "commands");
	(void) fprintf(out, "\n  snapshot -r %s@<version>\n", p->fp_ds);
}

void
sh_build_free(struct sh_fplan *p)
{
	sh_recipe_free(&p->fp_recipe);
}
