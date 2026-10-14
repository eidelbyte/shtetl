/*
 * shtetl cutover X and shtetl abandon X (planning.md 7.9, 7.10).
 *
 * Decided here, beyond the plan (ruled 2026-10-14): either is refused
 * while the update is not done (paused at a gate, or cut short), which
 * shtetl update --continue or --abort settles; a cutover is refused
 * when the staging tree has no mirror in zlake yet, which a save of it
 * makes; the archive is archive/containers/X-<X's version before the
 * update>.
 *
 * A cutover is a dozen steps over two pools, mounts and jail(8), and
 * nothing makes them one; so each step asks ZFS first whether it is
 * done, and the same command run again carries on where a stopped one
 * stopped (ruled 2026-10-14). What can be seen beforehand the plan
 * refuses on before anything changes: a hold on a snapshot of the old
 * tree, or a clone of one outside it, that would keep it from being
 * destroyed; a snapshot name a promote would find on both sides; the
 * archive's name taken. Where a cutover stands is read from the trees:
 * the staging tree there, the old X with it or not; or X the staging
 * tree renamed, which its newest version snapshot says, while its
 * replayed from-save is still marked shtetl:update (the last step takes
 * the mark off). An abandon likewise: X's from-save still marked, the
 * staging tree gone from zrun. An abandon is refused once a cutover has
 * begun promoting, which only the cutover can finish.
 */

#include <sys/stat.h>

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "core/build.h"
#include "core/cascade.h"
#include "core/cutover.h"
#include "core/err.h"
#include "core/jail.h"
#include "core/save.h"
#include "core/spaces.h"
#include "core/tree.h"
#include "core/update.h"
#include "core/zfs.h"
#include "core/zwalk.h"

/* Whether NAME, the origin of a dataset, is below or of the tree DS. */
static int
kc_under(const char *name, const char *ds)
{
	size_t n = strlen(ds);

	return (strncmp(name, ds, n) == 0 && (name[n] == '/' ||
	    name[n] == '@'));
}

static int
kp_there(struct sh_zfs *z, const char *name, struct sh_err *e)
{
	char val[64];

	return (sh_zfs_get(z, name, "type", val, sizeof (val), NULL, e) == 0);
}

/*
 * The newest snapshot of DS whose PROP is VALUE, or set at all (VALUE
 * NULL), its name after the @ into BUF; "" for none. -1 refused.
 */
static int
kp_newest(struct sh_zfs *z, const char *ds, const char *prop,
    const char *value, char *buf, size_t len, struct sh_err *e)
{
	char name[SH_CUT_NAME * 2], val[SH_ZFS_VALMAX], **snaps;
	size_t i;
	int r;

	buf[0] = '\0';
	if ((snaps = sh_zw_snaps(z, ds, e)) == NULL)
		return (-1);
	for (i = 0, r = 0; r == 0 && snaps[i] != NULL; i++) {
		(void) snprintf(name, sizeof (name), "%s@%s", ds, snaps[i]);
		if (sh_zfs_get(z, name, prop, val, sizeof (val), NULL,
		    e) != 0)
			r = -1;
		else if (value != NULL ? strcmp(val, value) == 0 :
		    strcmp(val, "-") != 0)
			(void) snprintf(buf, len, "%s", snaps[i]);
	}
	sh_zw_free(snaps);
	return (r);
}

/* The tree DS, DS first, into *ALL; -1 refused. */
static int
kp_tree(struct sh_zfs *z, const char *ds, char ***all, struct sh_err *e)
{
	if ((*all = sh_zw_new(e)) == NULL)
		return (-1);
	if (sh_zw_add(all, ds) != 0 || sh_zw_walk(z, ds, all, e) != 0) {
		sh_zw_free(*all);
		*all = NULL;
		return (-1);
	}
	return (0);
}

/*
 * The datasets of the tree STAGE that are clones of the tree OLD's,
 * into OWN: what a cutover promotes. A promote takes the origin's
 * snapshots up to the origin along; one of the clone's own of the same
 * name refuses it (dsl_dataset_promote's EEXIST), said in E.
 */
static int
kp_owns(struct sh_zfs *z, const char *stage, const char *old,
    char (*own)[SH_CUT_NAME], size_t *nown, struct sh_err *e)
{
	char val[SH_ZFS_VALMAX], ods[SH_CUT_NAME * 2];
	char **all, **mine, **theirs;
	const char *at;
	size_t i, j;
	long upto, k;
	int r;

	*nown = 0;
	if (kp_tree(z, stage, &all, e) != 0)
		return (-1);
	for (i = 0, r = 0; r == 0 && all[i] != NULL; i++) {
		if (sh_zfs_get(z, all[i], "origin", val, sizeof (val), NULL,
		    e) != 0) {
			r = -1;
			break;
		}
		if (!kc_under(val, old) || (at = strchr(val, '@')) == NULL ||
		    *nown == SH_CUT_DS)
			continue;
		(void) snprintf(own[(*nown)++], SH_CUT_NAME, "%s", all[i]);
		(void) snprintf(ods, sizeof (ods), "%.*s", (int)(at - val),
		    val);
		if ((mine = sh_zw_snaps(z, all[i], e)) == NULL) {
			r = -1;
			break;
		}
		if ((theirs = sh_zw_snaps(z, ods, e)) == NULL) {
			sh_zw_free(mine);
			r = -1;
			break;
		}
		upto = sh_zw_index(theirs, at + 1);
		for (j = 0; mine[j] != NULL; j++) {
			k = sh_zw_index(theirs, mine[j]);
			if (k >= 0 && k <= upto)
				sh_errf(e, "%s@%s: %s has a snapshot of that "
				    "name, which a promote of it would meet",
				    ods, mine[j], all[i]);
		}
		sh_zw_free(mine);
		sh_zw_free(theirs);
	}
	sh_zw_free(all);
	return (r);
}

/*
 * Whether the snapshot SNAP (ds@name) goes along with a promote of one
 * of OWN (N): one of them a clone of SNAP's dataset at or after it.
 */
static int
kp_moves(struct sh_zfs *z, char (*own)[SH_CUT_NAME], size_t n,
    const char *snap, struct sh_err *e)
{
	char val[SH_ZFS_VALMAX], ds[SH_CUT_NAME * 2], **snaps;
	const char *at = strchr(snap, '@');
	size_t k;
	long mine;
	int r;

	if (at == NULL || n == 0)
		return (0);
	(void) snprintf(ds, sizeof (ds), "%.*s", (int)(at - snap), snap);
	if ((snaps = sh_zw_snaps(z, ds, e)) == NULL)
		return (-1);
	mine = sh_zw_index(snaps, at + 1);
	for (k = 0, r = 0; r == 0 && k < n; k++) {
		if (sh_zfs_get(z, own[k], "origin", val, sizeof (val), NULL,
		    e) != 0)
			r = -1;
		else if (strncmp(val, snap, (size_t)(at - snap) + 1) == 0 &&
		    sh_zw_index(snaps, strchr(val, '@') + 1) >= mine)
			r = 1;
	}
	sh_zw_free(snaps);
	return (r);
}

/*
 * What would keep the tree OLD from being destroyed, each said in E: a
 * hold on one of its snapshots; a clone of one outside it, found by
 * walking ROOT. Those that OWN's promotes (N) take along are not OLD's
 * by then.
 */
static int
kp_destroyable(struct sh_zfs *z, const char *root, const char *old,
    char (*own)[SH_CUT_NAME], size_t n, struct sh_err *e)
{
	char name[SH_CUT_NAME * 2], val[SH_ZFS_VALMAX], **all, **snaps;
	size_t i, j;
	int r, m;

	if (kp_tree(z, old, &all, e) != 0)
		return (-1);
	for (i = 0, r = 0; r == 0 && all[i] != NULL; i++) {
		if ((snaps = sh_zw_snaps(z, all[i], e)) == NULL) {
			r = -1;
			break;
		}
		for (j = 0; r == 0 && snaps[j] != NULL; j++) {
			(void) snprintf(name, sizeof (name), "%s@%s", all[i],
			    snaps[j]);
			if (sh_zfs_get(z, name, "userrefs", val, sizeof (val),
			    NULL, e) != 0) {
				r = -1;
				break;
			}
			if (strtoul(val, NULL, 10) == 0)
				continue;
			if ((m = kp_moves(z, own, n, name, e)) < 0)
				r = -1;
			else if (m == 0)
				sh_errf(e, "%s is held (zfs holds), which "
				    "keeps it from being destroyed", name);
		}
		sh_zw_free(snaps);
	}
	sh_zw_free(all);
	if (r != 0 || kp_tree(z, root, &all, e) != 0)
		return (-1);
	for (i = 0; r == 0 && all[i] != NULL; i++) {
		if (strcmp(all[i], old) == 0 || kc_under(all[i], old))
			continue;
		if (sh_zfs_get(z, all[i], "origin", val, sizeof (val), NULL,
		    e) != 0) {
			r = -1;
			break;
		}
		if (!kc_under(val, old))
			continue;
		if ((m = kp_moves(z, own, n, val, e)) < 0)
			r = -1;
		else if (m == 0)
			sh_errf(e, "%s is a clone of %s, which keeps it from "
			    "being destroyed", all[i], val);
	}
	sh_zw_free(all);
	return (r);
}

/*
 * Whether a cutover of X has begun promoting: a dataset of the tree RUN
 * a clone of the staging tree STAGE's.
 */
static int
kp_promoted(struct sh_zfs *z, const char *run, const char *stage,
    struct sh_err *e)
{
	char val[SH_ZFS_VALMAX], **all;
	size_t i;
	int r;

	if (kp_tree(z, run, &all, e) != 0)
		return (-1);
	for (i = 0, r = 0; r == 0 && all[i] != NULL; i++) {
		if (sh_zfs_get(z, all[i], "origin", val, sizeof (val), NULL,
		    e) != 0)
			r = -1;
		else if (kc_under(val, stage))
			r = 1;
	}
	sh_zw_free(all);
	return (r);
}

/*
 * The staging tree gone from zrun (no update to take up, or one taken
 * as far as the rename): whether X's marks say a stopped cutover (X
 * the staging tree renamed, its newest version snapshot the mark's) or
 * a stopped abandon (X not), as P's verb asks.
 */
static int
kp_stopped(struct sh_zfs *z, struct sh_kplan *p, struct sh_err *e)
{
	char name[SH_CUT_NAME * 2], from[SH_CUT_NAME], ver[SH_CUT_NAME];

	if (kp_newest(z, p->kp_run, SH_UPDATE_MARK, NULL, from,
	    sizeof (from), e) != 0)
		return (-1);
	if (from[0] == '\0')
		return (0);
	(void) snprintf(name, sizeof (name), "%s@%s", p->kp_run, from);
	if (sh_zfs_get(z, name, SH_UPDATE_MARK, p->kp_version,
	    sizeof (p->kp_version), NULL, e) != 0 ||
	    kp_newest(z, p->kp_run, "shtetl:kind", "version", ver,
	    sizeof (ver), e) != 0)
		return (-1);
	(void) snprintf(p->kp_from, sizeof (p->kp_from), "%s", from);
	return ((strcmp(ver, p->kp_version) == 0) != p->kp_abandon);
}

/* The zlake half of a cutover's plan; its reasons said in E. */
static int
kp_lake(struct sh_zfs *z, const struct sh_spaces *s, struct sh_kplan *p,
    struct sh_err *e)
{
	char val[SH_ZFS_VALMAX];
	const char *at;

	if (!p->kp_haslakestage) {
		if (p->kp_hasstage)
			sh_errf(e, "%s has no mirror in zlake: save it first",
			    p->kp_stagename);
		return (0);
	}
	if (!p->kp_haslake)
		return (0);
	/* the old mirror archived as X-<its version>, from its origin */
	if (sh_zfs_get(z, p->kp_lake, "origin", val, sizeof (val), NULL,
	    e) != 0)
		return (-1);
	at = strchr(val, '@');
	(void) snprintf(p->kp_old, sizeof (p->kp_old), "%s",
	    at != NULL ? at + 1 : "");
	(void) snprintf(p->kp_archive, sizeof (p->kp_archive),
	    "%s/archive/containers/%s-%s", s->ss_root[SH_SP_LAKE],
	    p->kp_name, p->kp_old);
	if (kp_there(z, p->kp_archive, e))
		sh_errf(e, "%s: an archive of %s on that version exists",
		    p->kp_archive, p->kp_name);
	return (kp_owns(z, p->kp_lakestage, p->kp_lake, p->kp_lown,
	    &p->kp_nlown, e));
}

/* What X wrote since its newest save, dataset by dataset. */
static int
kp_written(struct sh_zfs *z, struct sh_kplan *p, struct sh_err *e)
{
	char val[SH_ZFS_VALMAX], prop[SH_CUT_NAME + 16], **all;
	size_t i, n;
	int r;

	if (kp_newest(z, p->kp_run, "shtetl:kind", "save", p->kp_last,
	    sizeof (p->kp_last), e) != 0)
		return (-1);
	if (p->kp_last[0] == '\0')
		return (0);
	if (kp_tree(z, p->kp_run, &all, e) != 0)
		return (-1);
	n = strlen(p->kp_run);
	(void) snprintf(prop, sizeof (prop), "written@%s", p->kp_last);
	for (i = 0, r = 0; r == 0 && all[i] != NULL && i < SH_CUT_DS; i++) {
		(void) snprintf(p->kp_xrel[i], SH_CUT_NAME, "%s", i == 0 ? "" :
		    all[i] + n + 1);
		if (sh_zfs_get(z, all[i], prop, val, sizeof (val), NULL,
		    e) != 0) {
			r = -1;
			break;
		}
		p->kp_written[i] = strtoul(val, NULL, 10);
		p->kp_wrote += p->kp_written[i];
		p->kp_nx++;
	}
	sh_zw_free(all);
	return (r);
}

int
sh_cutover_plan(struct sh_zfs *z, const struct sh_spaces *s,
    const struct sh_node *site, const char *root, const char *target,
    int abandon, const char *jaildir, const char *rundir, struct sh_kplan *p,
    struct sh_err *e, struct sh_err *w)
{
	const struct sh_node *v;
	char rpath[SH_CUT_NAME + 16];
	const char *suffix;
	unsigned before;
	int r, sp;

	(void) memset(p, 0, sizeof (*p));
	p->kp_abandon = abandon;
	before = e->se_count;
	if (sh_save_target(target, p->kp_name, sizeof (p->kp_name), e) != 0)
		return (-1);
	for (sp = 0; sp < SH_SP_N; sp++) {
		if (sp != SH_SP_BUILD && s->ss_root[sp] == NULL)
			sh_errf(e, "no %s root", sh_space_name(sp));
	}
	if (e->se_count != before)
		return (-1);
	v = sh_obj_get(site != NULL ? sh_obj_get(site, "update") : NULL,
	    "suffix");
	suffix = v != NULL && v->sn_type == SH_STR ? v->sn_str : "staging";
	(void) snprintf(p->kp_stagename, sizeof (p->kp_stagename), "%s-%s",
	    p->kp_name, suffix);
	(void) snprintf(p->kp_run, sizeof (p->kp_run), "%s/containers/%s",
	    s->ss_root[SH_SP_RUN], p->kp_name);
	(void) snprintf(p->kp_stage, sizeof (p->kp_stage), "%s/containers/%s",
	    s->ss_root[SH_SP_RUN], p->kp_stagename);
	(void) snprintf(p->kp_lake, sizeof (p->kp_lake), "%s/containers/%s",
	    s->ss_root[SH_SP_LAKE], p->kp_name);
	(void) snprintf(p->kp_lakestage, sizeof (p->kp_lakestage),
	    "%s/containers/%s", s->ss_root[SH_SP_LAKE], p->kp_stagename);
	(void) snprintf(p->kp_jailfile, sizeof (p->kp_jailfile), "%s/%s.conf",
	    jaildir, p->kp_name);
	(void) snprintf(p->kp_stagejail, sizeof (p->kp_stagejail),
	    "%s/%s.conf", jaildir, p->kp_stagename);
	(void) snprintf(p->kp_rundir, sizeof (p->kp_rundir), "%s/%s", rundir,
	    p->kp_name);
	p->kp_hasx = kp_there(z, p->kp_run, e);
	p->kp_hasstage = kp_there(z, p->kp_stage, e);
	p->kp_haslake = kp_there(z, p->kp_lake, e);
	p->kp_haslakestage = kp_there(z, p->kp_lakestage, e);
	if (e->se_count != before)
		return (-1);
	if (!p->kp_hasx && !p->kp_hasstage) {
		sh_errf(e, "no container %s", p->kp_name);
		return (-1);
	}
	if (!p->kp_hasstage) {
		if ((r = kp_stopped(z, p, e)) < 0)
			return (-1);
		if (r == 0) {
			sh_errf(e, "no %s: no update of %s to %s",
			    p->kp_stagename, p->kp_name, abandon ? "abandon" :
			    "cut over");
			return (-1);
		}
		p->kp_resumed = 1;
	} else {
		if ((r = sh_update_state(z, p->kp_run, p->kp_stage,
		    p->kp_version, sizeof (p->kp_version), p->kp_from,
		    sizeof (p->kp_from), e)) < 0)
			return (-1);
		if (r != SH_US_DONE) {
			sh_errf(e, "the update of %s is not done: shtetl "
			    "update --continue %s, or --abort %s", p->kp_name,
			    p->kp_name, p->kp_name);
			return (-1);
		}
		if (abandon && (!p->kp_hasx || (r = kp_promoted(z,
		    p->kp_run, p->kp_stage, e)) != 0)) {
			if (r >= 0)
				sh_errf(e, "a cutover of %s is under way: "
				    "shtetl cutover %s carries it on",
				    p->kp_name, p->kp_name);
			return (-1);
		}
		if (p->kp_hasx && p->kp_from[0] == '\0') {
			sh_errf(e, "container %s has no save the update "
			    "started from", p->kp_name);
			return (-1);
		}
	}
	(void) snprintf(rpath, sizeof (rpath), "containers/%s", p->kp_name);
	if (sh_recipe_resolve(root, rpath, &p->kp_recipe, e, w) != 0)
		return (-1);
	if (abandon) {
		/* the staging tree and its mirror destroyable */
		if ((p->kp_hasstage && kp_destroyable(z, s->ss_root[SH_SP_RUN],
		    p->kp_stage, NULL, 0, e) != 0) ||
		    (p->kp_haslakestage && kp_destroyable(z,
		    s->ss_root[SH_SP_LAKE], p->kp_lakestage, NULL, 0, e) != 0))
			return (-1);
		return (e->se_count == before ? 0 : -1);
	}
	if (kp_lake(z, s, p, e) != 0 || (p->kp_hasstage && kp_owns(z,
	    p->kp_stage, p->kp_run, p->kp_own, &p->kp_nown, e) != 0))
		return (-1);
	if (!p->kp_hasstage || !p->kp_hasx)
		return (e->se_count == before ? 0 : -1);
	if (kp_destroyable(z, s->ss_root[SH_SP_RUN], p->kp_run, p->kp_own,
	    p->kp_nown, e) != 0 || (r = kp_promoted(z, p->kp_run, p->kp_stage,
	    e)) < 0)
		return (-1);
	/* the final save comes before the promotes, which take its base */
	if (r == 0 && kp_written(z, p, e) != 0)
		return (-1);
	return (e->se_count == before ? 0 : -1);
}

void
sh_cutover_show(FILE *out, const struct sh_kplan *p)
{
	const char *taken;
	size_t i;

	taken = p->kp_resumed || !p->kp_hasx ? ", taken up where it stopped" :
	    "";
	if (p->kp_abandon) {
		(void) fprintf(out, "abandon %s, the update of %s to @%s%s\n"
		    "  %s stopped, destroyed on zrun and in zlake\n"
		    "  %s started again\n", p->kp_stagename, p->kp_name,
		    p->kp_version, taken, p->kp_stagename, p->kp_name);
		return;
	}
	(void) fprintf(out, "cutover of %s to %s, @%s%s\n", p->kp_name,
	    p->kp_stagename, p->kp_version, taken);
	if (p->kp_wrote > 0) {
		(void) fprintf(out, "  %s was written to since its last save "
		    "%s, %lu bytes:", p->kp_name, p->kp_last, p->kp_wrote);
		for (i = 0; i < p->kp_nx; i++) {
			if (p->kp_written[i] > 0)
				(void) fprintf(out, " %s %lu",
				    p->kp_xrel[i][0] != '\0' ?
				    p->kp_xrel[i] : "/", p->kp_written[i]);
		}
		(void) fprintf(out, "\n  a final save of %s, for its archive\n",
		    p->kp_name);
	}
	if (p->kp_hasstage) {
		(void) fprintf(out, "  %s stopped; promoted:", p->kp_stagename);
		for (i = 0; i < p->kp_nown; i++)
			(void) fprintf(out, " %s", p->kp_own[i]);
		if (p->kp_nown == 0)
			(void) fprintf(out, " none");
		(void) fprintf(out, "\n");
		if (p->kp_hasx)
			(void) fprintf(out, "  %s destroyed on zrun\n",
			    p->kp_run);
		(void) fprintf(out, "  %s renamed %s\n", p->kp_stage,
		    p->kp_run);
	}
	if (p->kp_haslakestage && p->kp_haslake)
		(void) fprintf(out, "  in zlake: %s archived as %s, %s renamed "
		    "%s\n", p->kp_lake, p->kp_archive, p->kp_lakestage,
		    p->kp_lake);
	else if (p->kp_haslakestage)
		(void) fprintf(out, "  in zlake: %s renamed %s\n",
		    p->kp_lakestage, p->kp_lake);
	(void) fprintf(out, "  %s started and saved\n", p->kp_name);
}

/* One run. */
struct kc {
	struct sh_zfs		*k_z;
	struct sh_kplan		*k_p;
	const struct sh_spaces	*k_s;
	const struct sh_tools	*k_t;
	FILE			*k_out;
	struct sh_err		*k_err;
	time_t			k_now;
	char			k_log[1100];
};

static int
kc_there(struct kc *c, const char *name)
{
	return (kp_there(c->k_z, name, c->k_err));
}

/* A save of NAME, its message MSG. */
static int
kc_save(struct kc *c, const char *name, const char *msg)
{
	struct sh_save *sp;
	char snap[256];
	int r;

	if ((sp = calloc(1, sizeof (*sp))) == NULL) {
		sh_err_nomem(c->k_err);
		return (-1);
	}
	r = sh_save_plan(c->k_z, c->k_s, name, NULL, sp, c->k_err) != 0 ||
	    sh_save_run(c->k_z, sp, msg, c->k_now, snap, sizeof (snap),
	    c->k_out, c->k_err) != 0 ? -1 : 0;
	free(sp);
	return (r);
}

/* Whether X's newest save carries the commit MSG: a run's, made. */
static int
kc_saved(struct kc *c, const char *msg)
{
	char last[SH_CUT_NAME], name[SH_CUT_NAME * 2], val[SH_ZFS_VALMAX];

	if (kp_newest(c->k_z, c->k_p->kp_run, "shtetl:kind", "save", last,
	    sizeof (last), c->k_err) != 0)
		return (-1);
	if (last[0] == '\0')
		return (0);
	(void) snprintf(name, sizeof (name), "%s@%s", c->k_p->kp_run, last);
	if (sh_zfs_get(c->k_z, name, "shtetl:commit", val, sizeof (val),
	    NULL, c->k_err) != 0)
		return (-1);
	return (strcmp(val, msg) == 0);
}

/* The tree DS unmounted, deepest first, and destroyed. */
static int
kc_destroy(struct kc *c, const char *ds)
{
	char **all;
	size_t n;

	if (kp_tree(c->k_z, ds, &all, c->k_err) != 0)
		return (-1);
	for (n = 0; all[n] != NULL; n++)
		;
	while (n > 0)
		(void) sh_zfs_unmount(c->k_z, all[--n], c->k_err);
	sh_zw_free(all);
	if (sh_zw_destroy(c->k_z, ds, c->k_err) != 0)
		return (-1);
	(void) fprintf(c->k_out, "destroyed %s\n", ds);
	return (0);
}

/* The jail NAME stopped, by its block FILE, if it runs. */
static int
kc_stop(struct kc *c, const char *file, const char *name)
{
	if (!sh_jail_running(c->k_t, name))
		return (0);
	if (sh_jail_do(c->k_t, file, name, "-r", c->k_log, c->k_err) != 0)
		return (-1);
	(void) fprintf(c->k_out, "stopped the jail %s\n", name);
	return (0);
}

/* The jail NAME started, by its block FILE, if it does not run. */
static int
kc_start(struct kc *c, const char *file, const char *name)
{
	if (sh_jail_running(c->k_t, name))
		return (0);
	if (sh_jail_do(c->k_t, file, name, "-c", c->k_log, c->k_err) != 0)
		return (-1);
	(void) fprintf(c->k_out, "started the jail %s\n", name);
	return (0);
}

/* Those of OWN (N) still clones of the tree OLD's promoted. */
static int
kc_promote(struct kc *c, char (*own)[SH_CUT_NAME], size_t n,
    const char *old)
{
	char val[SH_ZFS_VALMAX];
	size_t i;

	for (i = 0; i < n; i++) {
		if (sh_zfs_get(c->k_z, own[i], "origin", val, sizeof (val),
		    NULL, c->k_err) != 0)
			return (-1);
		if (!kc_under(val, old))
			continue;
		if (sh_zfs_promote(c->k_z, own[i], c->k_err) != 0)
			return (-1);
		(void) fprintf(c->k_out, "promoted %s\n", own[i]);
	}
	return (0);
}

/*
 * SH_UPDATE_MARK off every snapshot of DS that carries it. zlake's
 * never do: a save sends only the newest snapshot, and X's from-save
 * is marked after its save.
 */
static int
kc_unmark(struct kc *c, const char *ds)
{
	char from[SH_CUT_NAME], name[SH_CUT_NAME * 2];

	for (;;) {
		if (kp_newest(c->k_z, ds, SH_UPDATE_MARK, NULL, from,
		    sizeof (from), c->k_err) != 0)
			return (-1);
		if (from[0] == '\0')
			return (0);
		(void) snprintf(name, sizeof (name), "%s@%s", ds, from);
		if (sh_zfs_inherit(c->k_z, name, SH_UPDATE_MARK,
		    c->k_err) != 0)
			return (-1);
	}
}

/* The update's documents gone, if any are left; its log kept. */
static void
kc_tidy(struct kc *c)
{
	char dir[1100], path[1400];
	struct dirent *de;
	DIR *d;

	(void) snprintf(dir, sizeof (dir), "%s/rebase", c->k_p->kp_rundir);
	if ((d = opendir(dir)) == NULL)
		return;
	while ((de = readdir(d)) != NULL) {
		if (de->d_name[0] == '.')
			continue;
		(void) snprintf(path, sizeof (path), "%s/%s", dir, de->d_name);
		(void) unlink(path);
	}
	(void) closedir(d);
	(void) rmdir(dir);
}

/* On zrun, while the staging tree is there: it made X. */
static int
kc_zrun(struct kc *c)
{
	struct sh_kplan *p = c->k_p;
	char msg[SH_CUT_NAME * 2];

	if (!kc_there(c, p->kp_stage))
		return (0);
	if (p->kp_wrote > 0 && kc_there(c, p->kp_run)) {
		(void) snprintf(msg, sizeof (msg), "written during the soak "
		    "of %s", p->kp_stagename);
		if (kc_save(c, p->kp_name, msg) != 0)
			return (-1);
	}
	if (kc_stop(c, p->kp_stagejail, p->kp_stagename) != 0 ||
	    kc_promote(c, p->kp_own, p->kp_nown, p->kp_run) != 0)
		return (-1);
	if (kc_there(c, p->kp_run) && (kc_stop(c, p->kp_jailfile,
	    p->kp_name) != 0 || kc_destroy(c, p->kp_run) != 0))
		return (-1);
	if (sh_zfs_rename(c->k_z, p->kp_stage, p->kp_run, c->k_err) != 0)
		return (-1);
	(void) fprintf(c->k_out, "renamed %s %s\n", p->kp_stage, p->kp_run);
	return (0);
}

/* In zlake, while the staging mirror is there: the same, archived. */
static int
kc_zlake(struct kc *c)
{
	struct sh_kplan *p = c->k_p;

	if (!kc_there(c, p->kp_lakestage))
		return (0);
	if (p->kp_archive[0] != '\0') {
		if (kc_promote(c, p->kp_lown, p->kp_nlown, p->kp_lake) != 0 ||
		    sh_zfs_rename(c->k_z, p->kp_lake, p->kp_archive,
		    c->k_err) != 0)
			return (-1);
		(void) fprintf(c->k_out, "archived %s as %s\n", p->kp_lake,
		    p->kp_archive);
	}
	if (sh_zfs_rename(c->k_z, p->kp_lakestage, p->kp_lake,
	    c->k_err) != 0)
		return (-1);
	(void) fprintf(c->k_out, "renamed %s %s\n", p->kp_lakestage,
	    p->kp_lake);
	return (0);
}

/*
 * The cutover (7.9), each step done unless it is: zrun's, the jails'
 * blocks, zlake's; then X started and saved, the mark off its replayed
 * from-save, which ends it.
 */
static int
kc_cutover(struct kc *c)
{
	struct sh_kplan *p = c->k_p;
	char mp[SH_ZFS_VALMAX], msg[SH_CUT_NAME * 2];
	int r;

	if (kc_zrun(c) != 0)
		return (-1);
	if (unlink(p->kp_stagejail) != 0 && errno != ENOENT) {
		sh_errf(c->k_err, "%s: %s", p->kp_stagejail, strerror(errno));
		return (-1);
	}
	if (sh_zfs_get(c->k_z, p->kp_run, "mountpoint", mp, sizeof (mp),
	    NULL, c->k_err) != 0 ||
	    sh_jail_write(p->kp_jailfile, p->kp_name, p->kp_name, mp,
	    p->kp_recipe.sr_merged, c->k_err) != 0)
		return (-1);
	(void) fprintf(c->k_out, "wrote %s\n", p->kp_jailfile);
	if (kc_zlake(c) != 0 || kc_start(c, p->kp_jailfile, p->kp_name) != 0)
		return (-1);
	(void) snprintf(msg, sizeof (msg), "cutover to %s", p->kp_version);
	if ((r = kc_saved(c, msg)) < 0 ||
	    (r == 0 && kc_save(c, p->kp_name, msg) != 0))
		return (-1);
	return (kc_unmark(c, p->kp_run));
}

/* The abandon (7.10): the staging tree gone, X started, unmarked. */
static int
kc_abandon(struct kc *c)
{
	struct sh_kplan *p = c->k_p;

	if (kc_there(c, p->kp_stage)) {
		if (kc_stop(c, p->kp_stagejail, p->kp_stagename) != 0 ||
		    kc_destroy(c, p->kp_stage) != 0)
			return (-1);
	}
	if (unlink(p->kp_stagejail) != 0 && errno != ENOENT) {
		sh_errf(c->k_err, "%s: %s", p->kp_stagejail, strerror(errno));
		return (-1);
	}
	if (kc_there(c, p->kp_lakestage) && kc_destroy(c,
	    p->kp_lakestage) != 0)
		return (-1);
	if (kc_start(c, p->kp_jailfile, p->kp_name) != 0)
		return (-1);
	return (kc_unmark(c, p->kp_run));
}

int
sh_cutover_run(struct sh_zfs *z, struct sh_kplan *p,
    const struct sh_spaces *s, const struct sh_tools *t, time_t now,
    FILE *out, struct sh_err *e)
{
	struct kc c;
	int r;

	(void) memset(&c, 0, sizeof (c));
	c.k_z = z;
	c.k_p = p;
	c.k_s = s;
	c.k_t = t;
	c.k_out = out;
	c.k_err = e;
	c.k_now = now;
	(void) snprintf(c.k_log, sizeof (c.k_log), "%s/update.log",
	    p->kp_rundir);
	r = p->kp_abandon ? kc_abandon(&c) : kc_cutover(&c);
	if (r != 0) {
		sh_errf(e, "%s of %s stopped part way: shtetl %s %s carries it "
		    "on", p->kp_abandon ? "abandon" : "cutover", p->kp_name,
		    p->kp_abandon ? "abandon" : "cutover", p->kp_name);
		return (-1);
	}
	kc_tidy(&c);
	(void) fprintf(out, "%s %s\n", p->kp_abandon ? "abandoned" :
	    "cut over", p->kp_abandon ? p->kp_stagename : p->kp_name);
	return (0);
}

void
sh_cutover_free(struct sh_kplan *p)
{
	sh_recipe_free(&p->kp_recipe);
}
