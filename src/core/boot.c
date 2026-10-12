/*
 * shtetl bootstrap (planning.md 4.2, 4.3).
 *
 * The layout (4.6, 4.7), with the mount rules ruled 2026-10-11:
 *
 *	lake root	mountpoint=none, canmount=noauto; below it
 *			foundations, foundation-builds, containers,
 *			systems, archive/{containers,systems}, all of
 *			them inheriting mountpoint=none
 *	run root	mountpoint=none, canmount=noauto; below it
 *			foundations at /shtetl/foundations, never mounted
 *			itself (canmount=off: a foundation is mounted
 *			only while an update reads it); containers at
 *			/containers, mounted, the directory every
 *			container is in; systems, inheriting none
 *	config, build	their own mountpoint, which they must have; the
 *			config root is mounted, being read on every run
 *
 * With several run roots, one containers dataset is mounted at
 * /containers: the first run root's. Every later one inherits the
 * mountpoint with canmount=off, so its containers mount at
 * /containers/X inside the one directory and none hides another
 * (PROPOSAL).
 *
 * Decided here (PROPOSAL): bootstrap uses datasets the user has and
 * creates none of the roots; it refuses a lake or run root with any
 * child that is not shtetl's layout, so that the properties it sets
 * on a root reach nothing of the user's; and it clones the recipes
 * into the config root only when asked (--recipes), after a snapshot
 * of it (4.4: the config root is snapshotted before any git
 * operation).
 */

#include <sys/types.h>

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "core/boot.h"
#include "core/err.h"
#include "core/run.h"
#include "core/site.h"
#include "core/spaces.h"
#include "core/syntax.h"
#include "core/tree.h"
#include "core/zfs.h"

extern char **environ;

/* One dataset of a root's layout, and what shtetl sets on it. */
struct bl_spec {
	const char	*ls_rel;
	const char	*ls_mountpoint;
	const char	*ls_canmount;	/* "own": on for /containers' owner */
};

static const struct bl_spec bl_lake[] = {
	{ "foundations", NULL, NULL },
	{ "foundation-builds", NULL, NULL },
	{ "containers", NULL, NULL },
	{ "systems", NULL, NULL },
	{ "archive", NULL, NULL },
	{ "archive/containers", NULL, NULL },
	{ "archive/systems", NULL, NULL },
	{ NULL, NULL, NULL }
};

static const struct bl_spec bl_run[] = {
	{ "foundations", "/shtetl/foundations", "off" },
	{ "containers", "/containers", "own" },
	{ "systems", NULL, NULL },
	{ NULL, NULL, NULL }
};

/* One planning. */
struct bx {
	struct sh_zfs		*bx_z;
	const struct sh_spaces	*bx_s;
	const struct sh_node	*bx_site;
	const struct sh_node	*bx_built;
	struct sh_bplan		*bx_p;
	struct sh_err		*bx_err;
	const char		*bx_owner;	/* mounts /containers */
	int			bx_bad;
};

static int
bx_inside(const char *in, const char *out)
{
	size_t n = strlen(out);

	return (strncmp(in, out, n) == 0 && in[n] == '/');
}

/* A settings value as ZFS takes it: a string, or on and off. */
static const char *
bx_value(const struct sh_node *v)
{
	if (v == NULL)
		return (NULL);
	if (v->sn_type == SH_BOOL)
		return (v->sn_bool ? "on" : "off");
	return (v->sn_type == SH_STR ? v->sn_str : NULL);
}

static struct sh_brow *
bx_row(struct bx *x, struct sh_broot *rt, const char *prop)
{
	struct sh_brow *rw;
	size_t i;

	for (i = 0; i < rt->rt_nrows; i++) {
		if (strcmp(rt->rt_rows[i].rw_prop, prop) == 0)
			return (&rt->rt_rows[i]);
	}
	if (rt->rt_nrows == SH_BOOT_ROWS) {
		sh_errf(x->bx_err, "%s: more than %d properties", rt->rt_ds,
		    SH_BOOT_ROWS);
		x->bx_bad = 1;
		return (NULL);
	}
	rw = &rt->rt_rows[rt->rt_nrows++];
	(void) snprintf(rw->rw_prop, sizeof (rw->rw_prop), "%s", prop);
	return (rw);
}

/* The rows of the settings for one root (4.3; 5.2's properties). */
static void
bx_settings(struct bx *x, struct sh_broot *rt)
{
	const struct sh_node *props, *sections[2], *v, *b;
	const struct sh_pair *pr;
	struct sh_brow *rw;
	size_t i;
	int k;

	props = sh_obj_get(x->bx_site, "properties");
	sections[0] = sh_obj_get(props, "default");
	sections[1] = sh_obj_get(props, sh_space_name(rt->rt_sp));
	b = sh_obj_get(sh_obj_get(x->bx_built, "properties"), "default");
	for (k = 0; k < 2; k++) {
		for (i = 0; sections[k] != NULL &&
		    i < sections[k]->sn_obj.ob_n; i++) {
			pr = &sections[k]->sn_obj.ob_v[i];
			if ((rw = bx_row(x, rt, pr->pr_key)) == NULL)
				return;
			v = sh_obj_get(sections[1], pr->pr_key);
			if (v == NULL)
				v = pr->pr_val;
			(void) snprintf(rw->rw_use, sizeof (rw->rw_use), "%s",
			    bx_value(v) != NULL ? bx_value(v) : "-");
			(void) snprintf(rw->rw_dflt, sizeof (rw->rw_dflt),
			    "%s", bx_value(sh_obj_get(b, pr->pr_key)) != NULL ?
			    bx_value(sh_obj_get(b, pr->pr_key)) : "-");
			(void) snprintf(rw->rw_conf, sizeof (rw->rw_conf),
			    "%s", strcmp(v->sn_from, sh_site_conf) == 0 ?
			    rw->rw_use : "-");
		}
	}
}

/* A row shtetl decides itself: the root's mountpoint and canmount. */
static void
bx_own(struct bx *x, struct sh_broot *rt, const char *prop,
    const char *value)
{
	struct sh_brow *rw;

	if ((rw = bx_row(x, rt, prop)) == NULL)
		return;
	(void) snprintf(rw->rw_dflt, sizeof (rw->rw_dflt), "%s", value);
	(void) snprintf(rw->rw_conf, sizeof (rw->rw_conf), "shtetl's");
	(void) snprintf(rw->rw_use, sizeof (rw->rw_use), "%s", value);
}

/* Each row's value now, and whether bootstrap sets it. */
static void
bx_now(struct bx *x, struct sh_broot *rt)
{
	struct sh_brow *rw;
	size_t i;

	for (i = 0; i < rt->rt_nrows; i++) {
		rw = &rt->rt_rows[i];
		if (sh_zfs_get(x->bx_z, rt->rt_ds, rw->rw_prop, rw->rw_now,
		    sizeof (rw->rw_now), &rw->rw_src, x->bx_err) != 0) {
			x->bx_bad = 1;
			continue;
		}
		if (rw->rw_set != -1)
			rw->rw_set = strcmp(rw->rw_now, rw->rw_use) != 0;
		else
			rw->rw_set = 0;
	}
}

/* Children of a root, collected: is each one shtetl's? */
struct bx_kids {
	struct bx		*bk_x;
	const char		*bk_root;
	const char *const	*bk_names;
};

static int
bx_kid(void *arg, const char *child)
{
	struct bx_kids *k = arg;
	const char *rel = child + strlen(k->bk_root) + 1;
	size_t i;

	for (i = 0; k->bk_names[i] != NULL; i++) {
		if (strcmp(rel, k->bk_names[i]) == 0)
			return (0);
	}
	sh_errf(k->bk_x->bx_err, "%s: unexpected child %s", k->bk_root,
	    child);
	k->bk_x->bx_bad = 1;
	return (0);
}

static void
bx_children(struct bx *x, const char *root, const char *const *names)
{
	struct bx_kids k;

	k.bk_x = x;
	k.bk_root = root;
	k.bk_names = names;
	if (sh_zfs_children(x->bx_z, root, bx_kid, &k, x->bx_err) != 0)
		x->bx_bad = 1;
}

/*
 * One layout dataset already there: is it as shtetl makes it? A pool
 * imported with an altroot (zpool import -R) shows every mountpoint
 * below it, so the mountpoint expected is read below ALTROOT too.
 */
static void
bx_check_kid(struct bx *x, const struct sh_bkid *kid, const char *altroot)
{
	char val[SH_ZFS_VALMAX], want[SH_ZFS_VALMAX];
	enum sh_zsrc src;
	size_t i;

	for (i = 0; i < kid->bk_n; i++) {
		if (sh_zfs_get(x->bx_z, kid->bk_ds, kid->bk_props[i].zp_name,
		    val, sizeof (val), &src, x->bx_err) != 0) {
			x->bx_bad = 1;
			continue;
		}
		(void) snprintf(want, sizeof (want), "%s%s",
		    strcmp(kid->bk_props[i].zp_name, "mountpoint") == 0 &&
		    altroot[0] == '/' ? altroot : "",
		    kid->bk_props[i].zp_value);
		if (strcmp(val, want) == 0 && src == SH_ZSRC_LOCAL)
			continue;
		sh_errf(x->bx_err, "%s: %s is %s%s, must be %s", kid->bk_ds,
		    kid->bk_props[i].zp_name, val,
		    src == SH_ZSRC_LOCAL ? "" : " (not local)", want);
		x->bx_bad = 1;
	}
}

static void
bx_layout(struct bx *x, struct sh_broot *rt)
{
	static const char *const lake_top[] = { "foundations",
	    "foundation-builds", "containers", "systems", "archive", NULL };
	static const char *const lake_archive[] = { "containers", "systems",
	    NULL };
	static const char *const run_top[] = { "foundations", "containers",
	    "systems", NULL };
	const struct bl_spec *spec;
	struct sh_bkid *kid;
	char val[SH_ZFS_VALMAX], archive[SH_BOOT_NAME + 16];
	char pool[SH_BOOT_NAME], altroot[SH_ZFS_VALMAX];
	int r;

	(void) snprintf(pool, sizeof (pool), "%.*s",
	    (int)strcspn(rt->rt_ds, "/"), rt->rt_ds);
	if (sh_zpool_get(x->bx_z, pool, "altroot", altroot, sizeof (altroot),
	    x->bx_err) != 0) {
		x->bx_bad = 1;
		return;
	}
	spec = rt->rt_sp == SH_SP_LAKE ? bl_lake : bl_run;
	bx_children(x, rt->rt_ds, rt->rt_sp == SH_SP_LAKE ? lake_top :
	    run_top);
	(void) snprintf(archive, sizeof (archive), "%s/archive", rt->rt_ds);
	if (rt->rt_sp == SH_SP_LAKE && sh_zfs_get(x->bx_z, archive, "type",
	    val, sizeof (val), NULL, x->bx_err) == 0)
		bx_children(x, archive, lake_archive);
	for (; spec->ls_rel != NULL; spec++) {
		kid = &rt->rt_kids[rt->rt_nkids++];
		(void) snprintf(kid->bk_ds, sizeof (kid->bk_ds), "%s/%s",
		    rt->rt_ds, spec->ls_rel);
		if (spec->ls_mountpoint != NULL) {
			kid->bk_props[kid->bk_n].zp_name = "mountpoint";
			kid->bk_props[kid->bk_n++].zp_value =
			    spec->ls_mountpoint;
		}
		if (spec->ls_canmount != NULL) {
			kid->bk_props[kid->bk_n].zp_name = "canmount";
			if (strcmp(spec->ls_canmount, "own") == 0) {
				kid->bk_mount = x->bx_owner != NULL &&
				    strcmp(x->bx_owner, rt->rt_ds) == 0;
				kid->bk_props[kid->bk_n++].zp_value =
				    kid->bk_mount ? "on" : "off";
			} else {
				kid->bk_props[kid->bk_n++].zp_value =
				    spec->ls_canmount;
			}
		}
		r = sh_zfs_get(x->bx_z, kid->bk_ds, "type", val, sizeof (val),
		    NULL, x->bx_err);
		if (r < 0) {
			x->bx_bad = 1;
		} else if (r == 0) {
			kid->bk_there = 1;
			bx_check_kid(x, kid, altroot);
		}
	}
}

/* A config or build root: its mountpoint, which it must have. */
static void
bx_mounted(struct bx *x, struct sh_broot *rt)
{
	char mounted[SH_ZFS_VALMAX];
	struct sh_brow *rw;

	if (sh_zfs_get(x->bx_z, rt->rt_ds, "mountpoint", rt->rt_dir,
	    sizeof (rt->rt_dir), NULL, x->bx_err) != 0 ||
	    sh_zfs_get(x->bx_z, rt->rt_ds, "mounted", mounted,
	    sizeof (mounted), NULL, x->bx_err) != 0) {
		x->bx_bad = 1;
		return;
	}
	if (rt->rt_dir[0] != '/') {
		sh_errf(x->bx_err, "%s: no mountpoint", rt->rt_ds);
		x->bx_bad = 1;
		return;
	}
	rt->rt_mount = rt->rt_sp == SH_SP_CONFIG &&
	    strcmp(mounted, "yes") != 0;
	if ((rw = bx_row(x, rt, "mountpoint")) == NULL)
		return;
	(void) snprintf(rw->rw_dflt, sizeof (rw->rw_dflt), "-");
	(void) snprintf(rw->rw_conf, sizeof (rw->rw_conf), "yours");
	(void) snprintf(rw->rw_use, sizeof (rw->rw_use), "%s", rt->rt_dir);
	rw->rw_set = -1;
}

/* One named root: the checks, then its rows and layout. */
static void
bx_root(struct bx *x, const struct sh_bootreq *req, size_t i)
{
	struct sh_broot *rt = &x->bx_p->bt_roots[x->bx_p->bt_n++];
	char val[SH_ZFS_VALMAX];
	enum sh_zsrc src;
	const char *why;
	size_t j;
	int r;

	(void) snprintf(rt->rt_ds, sizeof (rt->rt_ds), "%s", req->br_ds[i]);
	rt->rt_sp = req->br_sp[i];
	if ((why = sh_syn_dsname(rt->rt_ds)) != NULL) {
		sh_errf(x->bx_err, "%s: invalid dataset name: %s", rt->rt_ds,
		    why);
		x->bx_bad = 1;
		return;
	}
	for (j = 0; j < i; j++) {
		if (strcmp(req->br_ds[j], rt->rt_ds) == 0) {
			sh_errf(x->bx_err, "%s: named twice", rt->rt_ds);
			x->bx_bad = 1;
			return;
		}
	}
	r = sh_zfs_get(x->bx_z, rt->rt_ds, SH_SPACE_PROP, val, sizeof (val),
	    &src, x->bx_err);
	if (r != 0) {
		if (r == 1)
			sh_errf(x->bx_err, "%s: no such filesystem",
			    rt->rt_ds);
		x->bx_bad = 1;
		return;
	}
	if (src == SH_ZSRC_LOCAL) {
		sh_errf(x->bx_err, "%s: already a %s root", rt->rt_ds, val);
		x->bx_bad = 1;
		return;
	}
	bx_settings(x, rt);
	if (rt->rt_sp == SH_SP_LAKE || rt->rt_sp == SH_SP_RUN) {
		bx_own(x, rt, "mountpoint", "none");
		bx_own(x, rt, "canmount", "noauto");
		bx_layout(x, rt);
	} else {
		bx_mounted(x, rt);
	}
	bx_now(x, rt);
}

/* A config or build root more than one: named twice, or one there. */
static void
bx_single(struct bx *x, const struct sh_bootreq *req, enum sh_space sp)
{
	const char *first;
	size_t i;

	first = x->bx_s->ss_n[sp] > 0 ? x->bx_s->ss_roots[sp][0] : NULL;
	for (i = 0; i < req->br_n; i++) {
		if (req->br_sp[i] != sp)
			continue;
		if (first != NULL) {
			sh_errf(x->bx_err, "two %s roots: %s and %s",
			    sh_space_name(sp), first, req->br_ds[i]);
			x->bx_bad = 1;
			return;
		}
		first = req->br_ds[i];
	}
}

/* A named root inside or around another root, named or found. */
static void
bx_nesting(struct bx *x, const struct sh_bootreq *req)
{
	const char *a, *b;
	size_t i, j;
	int sp;

	for (i = 0; i < req->br_n; i++) {
		a = req->br_ds[i];
		for (j = 0; j < req->br_n; j++) {
			b = req->br_ds[j];
			if (j != i && bx_inside(a, b)) {
				sh_errf(x->bx_err, "%s: inside %s", a, b);
				x->bx_bad = 1;
			}
		}
		for (sp = 0; sp < SH_SP_N; sp++) {
			for (j = 0; j < x->bx_s->ss_n[sp]; j++) {
				b = x->bx_s->ss_roots[sp][j];
				if (bx_inside(a, b) || bx_inside(b, a)) {
					sh_errf(x->bx_err, "%s: overlaps %s "
					    "root %s", a, sh_space_name(sp), b);
					x->bx_bad = 1;
				}
			}
		}
	}
}

/*
 * The run root whose containers is mounted at /containers: a run root
 * found already whose containers has canmount=on, or else the first
 * run root named.
 */
static const char *
bx_owner(struct bx *x, const struct sh_bootreq *req)
{
	char ds[SH_BOOT_NAME + 16], val[SH_ZFS_VALMAX];
	size_t i;

	for (i = 0; i < x->bx_s->ss_n[SH_SP_RUN]; i++) {
		(void) snprintf(ds, sizeof (ds), "%s/containers",
		    x->bx_s->ss_roots[SH_SP_RUN][i]);
		if (sh_zfs_get(x->bx_z, ds, "canmount", val, sizeof (val), NULL,
		    x->bx_err) == 0 && strcmp(val, "on") == 0)
			return (x->bx_s->ss_roots[SH_SP_RUN][i]);
	}
	for (i = 0; i < req->br_n; i++) {
		if (req->br_sp[i] == SH_SP_RUN)
			return (req->br_ds[i]);
	}
	return (NULL);
}

static void
bx_pools(struct bx *x)
{
	struct sh_bplan *p = x->bx_p;
	struct sh_bpool *pl;
	char pool[SH_BOOT_NAME];
	size_t i, j;

	for (i = 0; i < p->bt_n; i++) {
		(void) snprintf(pool, sizeof (pool), "%.*s",
		    (int)strcspn(p->bt_roots[i].rt_ds, "/"),
		    p->bt_roots[i].rt_ds);
		for (j = 0; j < p->bt_npools; j++) {
			if (strcmp(p->bt_pools[j].pl_pool, pool) == 0)
				break;
		}
		if (j < p->bt_npools)
			continue;
		pl = &p->bt_pools[p->bt_npools++];
		(void) snprintf(pl->pl_pool, sizeof (pl->pl_pool), "%s", pool);
		if (sh_zpool_get(x->bx_z, pool, "ashift", pl->pl_ashift,
		    sizeof (pl->pl_ashift), x->bx_err) != 0 ||
		    sh_zpool_get(x->bx_z, pool, "autotrim", pl->pl_autotrim,
		    sizeof (pl->pl_autotrim), x->bx_err) != 0) {
			x->bx_bad = 1;
			continue;
		}
		pl->pl_set = strcmp(pl->pl_autotrim, "on") != 0;
	}
}

int
sh_boot_plan(struct sh_zfs *z, const struct sh_spaces *s,
    const struct sh_node *site, const struct sh_bootreq *req,
    struct sh_bplan *p, struct sh_err *e)
{
	struct sh_node *built;
	struct bx x;
	size_t i;

	(void) memset(p, 0, sizeof (*p));
	p->bt_recipes = req->br_recipes;
	if (req->br_n == 0 || req->br_n > SH_BOOT_MAX) {
		sh_errf(e, "bootstrap takes 1 to %d datasets", SH_BOOT_MAX);
		return (-1);
	}
	if ((built = sh_site_builtins(e)) == NULL)
		return (-1);
	(void) memset(&x, 0, sizeof (x));
	x.bx_z = z;
	x.bx_s = s;
	x.bx_site = site != NULL ? site : built;
	x.bx_built = built;
	x.bx_p = p;
	x.bx_err = e;
	bx_single(&x, req, SH_SP_CONFIG);
	bx_single(&x, req, SH_SP_BUILD);
	bx_nesting(&x, req);
	if (!x.bx_bad) {
		x.bx_owner = bx_owner(&x, req);
		for (i = 0; i < req->br_n; i++)
			bx_root(&x, req, i);
		bx_pools(&x);
	}
	sh_node_free(built);
	return (x.bx_bad ? -1 : 0);
}

static const char *
bx_src(enum sh_zsrc src)
{
	switch (src) {
	case SH_ZSRC_DEFAULT:
		return ("default");
	case SH_ZSRC_LOCAL:
		return ("local");
	case SH_ZSRC_INHERITED:
		return ("inherited");
	case SH_ZSRC_RECEIVED:
		return ("received");
	case SH_ZSRC_TEMPORARY:
		return ("temporary");
	default:
		return ("unset");
	}
}

void
sh_boot_show(FILE *out, const struct sh_bplan *p)
{
	static const char *const what[SH_SP_N] = { "a lake root",
	    "the config root", "the build root", "a run root" };
	const struct sh_broot *rt;
	const struct sh_brow *rw;
	const struct sh_bkid *k;
	size_t i, j, m;

	for (i = 0; i < p->bt_n; i++) {
		rt = &p->bt_roots[i];
		(void) fprintf(out, "%s: %s\n", rt->rt_ds, what[rt->rt_sp]);
		(void) fprintf(out, "    %-13s %-12s %-12s %-12s %s\n",
		    "property", "default", "shtetl.conf", "used", "now");
		for (j = 0; j < rt->rt_nrows; j++) {
			rw = &rt->rt_rows[j];
			(void) fprintf(out, "  %c %-13s %-12s %-12s %-12s %s "
			    "(%s)\n", rw->rw_set ? '*' : ' ', rw->rw_prop,
			    rw->rw_dflt, rw->rw_conf, rw->rw_use, rw->rw_now,
			    bx_src(rw->rw_src));
		}
		for (j = 0; j < rt->rt_nkids; j++) {
			k = &rt->rt_kids[j];
			(void) fprintf(out, "  %c %s", k->bk_there ? ' ' : '+',
			    k->bk_ds);
			for (m = 0; m < k->bk_n; m++)
				(void) fprintf(out, "%s%s=%s", m == 0 ? "  " :
				    ", ", k->bk_props[m].zp_name,
				    k->bk_props[m].zp_value);
			(void) fprintf(out, "%s%s\n", k->bk_mount ?
			    ", mounted" : "", k->bk_there ? " (there)" : "");
		}
		if (rt->rt_sp == SH_SP_CONFIG && rt->rt_mount)
			(void) fprintf(out, "  + mounted at %s\n", rt->rt_dir);
		if (rt->rt_sp == SH_SP_CONFIG && p->bt_recipes != NULL)
			(void) fprintf(out, "  + git clone %s into %s, if it "
			    "is empty, after a snapshot\n", p->bt_recipes,
			    rt->rt_dir);
	}
	for (i = 0; i < p->bt_npools; i++) {
		(void) fprintf(out, "pool %s: ashift %s%s; autotrim %s%s\n",
		    p->bt_pools[i].pl_pool, p->bt_pools[i].pl_ashift,
		    strcmp(p->bt_pools[i].pl_ashift, "12") == 0 ? "" :
		    " (shtetl recommends 12, which only a new pool can have)",
		    p->bt_pools[i].pl_autotrim,
		    p->bt_pools[i].pl_set ? ", set to on" : "");
	}
	(void) fprintf(out, "then shtetl:space on");
	for (i = 0; i < p->bt_n; i++)
		(void) fprintf(out, "%s %s (%s)", i > 0 ? "," : "",
		    p->bt_roots[i].rt_ds,
		    sh_space_name(p->bt_roots[i].rt_sp));
	(void) fprintf(out, "\n* is set, + is made\n");
}

int
sh_boot_confirm(FILE *in, FILE *out, int yes, int tty)
{
	char line[64];
	size_t n;

	if (yes)
		return (1);
	if (!tty)
		return (0);
	(void) fprintf(out, "Bootstrap as above? [y/N] ");
	(void) fflush(out);
	if (fgets(line, sizeof (line), in) == NULL)
		return (0);
	n = strcspn(line, "\r\n");
	line[n] = '\0';
	return (strcmp(line, "y") == 0 || strcmp(line, "yes") == 0);
}

/* Whether the directory DIR holds nothing; -1 if it cannot be read. */
static int
bx_empty(const char *dir)
{
	struct dirent *d;
	DIR *dp;
	int empty;

	if ((dp = opendir(dir)) == NULL)
		return (-1);
	empty = 1;
	while ((d = readdir(dp)) != NULL) {
		if (strcmp(d->d_name, ".") != 0 && strcmp(d->d_name, "..") != 0)
			empty = 0;
	}
	(void) closedir(dp);
	return (empty);
}

/*
 * git clone URL into the config root's directory, after a snapshot of
 * it. git asks nothing of the terminal (GIT_TERMINAL_PROMPT=0): a
 * clone that needs credentials fails rather than waits.
 */
static int
bx_clone(struct sh_zfs *z, const struct sh_broot *rt, const char *url,
    FILE *out, struct sh_err *e)
{
	char snap[SH_BOOT_NAME + 64], stamp[32], **env, *u, *d;
	char *argv[] = { "git", "clone", "--", NULL, NULL, NULL };
	struct sh_run r;
	struct tm tm;
	time_t now;
	size_t n;
	int empty, rc;

	if ((empty = bx_empty(rt->rt_dir)) < 0) {
		sh_errf(e, "%s: unreadable", rt->rt_dir);
		return (-1);
	}
	if (!empty) {
		(void) fprintf(out, "%s is not empty: not cloned into\n",
		    rt->rt_dir);
		return (0);
	}
	now = time(NULL);
	(void) gmtime_r(&now, &tm);
	(void) strftime(stamp, sizeof (stamp), "%Y%m%d-%H%M%S", &tm);
	(void) snprintf(snap, sizeof (snap), "%s@bootstrap-%s", rt->rt_ds,
	    stamp);
	if (sh_zfs_snapshot(z, snap, NULL, 0, e) != 0)
		return (-1);
	(void) fprintf(out, "snapshot %s\n", snap);
	for (n = 0; environ[n] != NULL; n++)
		;
	env = calloc(n + 2, sizeof (*env));
	u = strdup(url);
	d = strdup(rt->rt_dir);
	if (env == NULL || u == NULL || d == NULL) {
		free(env);
		free(u);
		free(d);
		sh_err_nomem(e);
		return (-1);
	}
	(void) memcpy(env, environ, n * sizeof (*env));
	env[n] = "GIT_TERMINAL_PROMPT=0";
	argv[3] = u;
	argv[4] = d;
	(void) memset(&r, 0, sizeof (r));
	r.ru_argv = argv;
	r.ru_env = env;
	r.ru_log = -1;
	r.ru_echo = 1;
	rc = sh_run(&r, e);
	free(env);
	free(u);
	free(d);
	if (rc != 0) {
		sh_errf(e, "%s: cloning %s failed; no root marked", rt->rt_ds,
		    url);
		return (-1);
	}
	(void) fprintf(out, "cloned %s into %s\n", url, rt->rt_dir);
	return (0);
}

int
sh_boot_apply(struct sh_zfs *z, const struct sh_bplan *p, FILE *out,
    struct sh_err *e)
{
	const struct sh_broot *rt;
	const struct sh_brow *rw;
	const struct sh_bkid *k;
	char mounted[SH_ZFS_VALMAX];
	size_t i, j;

	for (i = 0; i < p->bt_npools; i++) {
		if (!p->bt_pools[i].pl_set)
			continue;
		if (sh_zpool_set(z, p->bt_pools[i].pl_pool, "autotrim", "on",
		    e) != 0)
			return (-1);
		(void) fprintf(out, "set autotrim=on on pool %s\n",
		    p->bt_pools[i].pl_pool);
	}
	for (i = 0; i < p->bt_n; i++) {
		rt = &p->bt_roots[i];
		for (j = 0; j < rt->rt_nrows; j++) {
			rw = &rt->rt_rows[j];
			if (!rw->rw_set)
				continue;
			if (sh_zfs_set(z, rt->rt_ds, rw->rw_prop, rw->rw_use,
			    e) != 0)
				return (-1);
			(void) fprintf(out, "set %s=%s on %s\n", rw->rw_prop,
			    rw->rw_use, rt->rt_ds);
		}
		for (j = 0; j < rt->rt_nkids; j++) {
			k = &rt->rt_kids[j];
			if (k->bk_there)
				continue;
			if (sh_zfs_create(z, k->bk_ds, k->bk_props, k->bk_n,
			    e) != 0)
				return (-1);
			(void) fprintf(out, "made %s\n", k->bk_ds);
		}
		for (j = 0; j < rt->rt_nkids; j++) {
			k = &rt->rt_kids[j];
			if (!k->bk_mount)
				continue;
			if (sh_zfs_get(z, k->bk_ds, "mounted", mounted,
			    sizeof (mounted), NULL, e) != 0)
				return (-1);
			if (strcmp(mounted, "yes") == 0)
				continue;
			if (sh_zfs_mount(z, k->bk_ds, e) != 0)
				return (-1);
			(void) fprintf(out, "mounted %s\n", k->bk_ds);
		}
		if (rt->rt_sp != SH_SP_CONFIG)
			continue;
		if (rt->rt_mount) {
			if (sh_zfs_mount(z, rt->rt_ds, e) != 0)
				return (-1);
			(void) fprintf(out, "mounted %s\n", rt->rt_ds);
		}
		if (p->bt_recipes != NULL &&
		    bx_clone(z, rt, p->bt_recipes, out, e) != 0)
			return (-1);
	}
	for (i = 0; i < p->bt_n; i++) {
		rt = &p->bt_roots[i];
		if (sh_zfs_set(z, rt->rt_ds, SH_SPACE_PROP,
		    sh_space_name(rt->rt_sp), e) != 0)
			return (-1);
		(void) fprintf(out, "%s is a %s root\n", rt->rt_ds,
		    sh_space_name(rt->rt_sp));
	}
	return (0);
}
