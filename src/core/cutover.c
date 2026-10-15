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
 * nothing makes them one (ruled 2026-10-15, 12.1 m6-cutover). So each
 * step that changes something notes how to put it back, and a step
 * failing puts every step before it back, last first: a failed cutover
 * leaves X as it found it, and is run again from the start. Nothing is
 * destroyed until the end. The old X is renamed aside as X-<its
 * version>, its previous root, and pruned last, the new X's clones of
 * its datasets promoted off it first; a prune that cannot go through
 * leaves the cutover done and the previous root as it is, said as a
 * warning. An abandon likewise destroys the staging tree last.
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

/* The zlake half of a cutover's plan; its reasons said in E. */
static int
kp_lake(struct sh_zfs *z, const struct sh_spaces *s, struct sh_kplan *p,
    struct sh_err *e)
{
	if (!p->kp_haslakestage) {
		sh_errf(e, "%s has no mirror in zlake: save it first",
		    p->kp_stagename);
		return (0);
	}
	if (!p->kp_haslake)
		return (0);
	/* the old mirror archived as X-<its version> */
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
	char val[SH_ZFS_VALMAX], prop[SH_CUT_NAME + 16], pool[SH_CUT_NAME];
	char **all;
	size_t i, n;
	int r;

	if (kp_newest(z, p->kp_run, "shtetl:kind", "save", p->kp_last,
	    sizeof (p->kp_last), e) != 0)
		return (-1);
	if (p->kp_last[0] == '\0')
		return (0);
	/* what X wrote a moment ago committed, so that written@ counts it */
	(void) snprintf(pool, sizeof (pool), "%.*s",
	    (int)strcspn(p->kp_run, "/"), p->kp_run);
	if (sh_zfs_sync(z, pool, e) != 0 || kp_tree(z, p->kp_run, &all,
	    e) != 0)
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
	char rpath[SH_CUT_NAME + 16], val[SH_ZFS_VALMAX];
	const char *suffix, *at;
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
	if (!p->kp_hasx) {
		sh_errf(e, "no container %s", p->kp_name);
		return (-1);
	}
	if (!p->kp_hasstage) {
		sh_errf(e, "no %s: no update of %s to %s", p->kp_stagename,
		    p->kp_name, abandon ? "abandon" : "cut over");
		return (-1);
	}
	if ((r = sh_update_state(z, p->kp_run, p->kp_stage, p->kp_version,
	    sizeof (p->kp_version), p->kp_from, sizeof (p->kp_from), e)) < 0)
		return (-1);
	if (r != SH_US_DONE) {
		sh_errf(e, "the update of %s is not done: shtetl update "
		    "--continue %s, or --abort %s", p->kp_name, p->kp_name,
		    p->kp_name);
		return (-1);
	}
	if (p->kp_from[0] == '\0') {
		sh_errf(e, "container %s has no save the update started from",
		    p->kp_name);
		return (-1);
	}
	(void) snprintf(rpath, sizeof (rpath), "containers/%s", p->kp_name);
	if (sh_recipe_resolve(root, rpath, &p->kp_recipe, e, w) != 0)
		return (-1);
	if (abandon) {
		/* the staging tree and its mirror destroyable */
		if (kp_destroyable(z, s->ss_root[SH_SP_RUN], p->kp_stage,
		    NULL, 0, e) != 0 || (p->kp_haslakestage &&
		    kp_destroyable(z, s->ss_root[SH_SP_LAKE], p->kp_lakestage,
		    NULL, 0, e) != 0))
			return (-1);
		return (e->se_count == before ? 0 : -1);
	}
	/* X's version, from its origin, names its previous root */
	if (sh_zfs_get(z, p->kp_run, "origin", val, sizeof (val), NULL,
	    e) != 0)
		return (-1);
	if ((at = strchr(val, '@')) == NULL) {
		sh_errf(e, "container %s is not a clone of a foundation's "
		    "version", p->kp_name);
		return (-1);
	}
	(void) snprintf(p->kp_old, sizeof (p->kp_old), "%s", at + 1);
	(void) snprintf(p->kp_aside, sizeof (p->kp_aside), "%s-%s",
	    p->kp_run, p->kp_old);
	if (kp_there(z, p->kp_aside, e))
		sh_errf(e, "%s: the name of %s's previous root is taken",
		    p->kp_aside, p->kp_name);
	if (kp_lake(z, s, p, e) != 0 || kp_owns(z, p->kp_stage, p->kp_run,
	    p->kp_own, &p->kp_nown, e) != 0 || kp_written(z, p, e) != 0)
		return (-1);
	return (e->se_count == before ? 0 : -1);
}

void
sh_cutover_show(FILE *out, const struct sh_kplan *p)
{
	size_t i;

	if (p->kp_abandon) {
		(void) fprintf(out, "abandon %s, the update of %s to @%s\n"
		    "  %s stopped, its block removed\n"
		    "  %s started\n"
		    "  last, %s destroyed on zrun and in zlake\n",
		    p->kp_stagename, p->kp_name, p->kp_version,
		    p->kp_stagename, p->kp_name, p->kp_stagename);
		return;
	}
	(void) fprintf(out, "cutover of %s to %s, @%s\n", p->kp_name,
	    p->kp_stagename, p->kp_version);
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
	(void) fprintf(out, "  %s stopped, renamed %s, its previous root\n"
	    "  %s stopped, renamed %s, its block %s's\n", p->kp_name,
	    p->kp_aside, p->kp_stagename, p->kp_run, p->kp_name);
	if (p->kp_haslake)
		(void) fprintf(out, "  in zlake: %s archived as %s, %s renamed "
		    "%s\n", p->kp_lake, p->kp_archive, p->kp_lakestage,
		    p->kp_lake);
	else
		(void) fprintf(out, "  in zlake: %s renamed %s\n",
		    p->kp_lakestage, p->kp_lake);
	(void) fprintf(out, "  %s started and saved\n  last, the prune: "
	    "promoted", p->kp_name);
	/* the staging tree's own clones, by the names the rename gives */
	for (i = 0; i < p->kp_nown; i++)
		(void) fprintf(out, " %s%s", p->kp_run, p->kp_own[i] +
		    strlen(p->kp_stage));
	(void) fprintf(out, "%s; %s destroyed\n", p->kp_nown == 0 ? " none" :
	    "", p->kp_aside);
	if (p->kp_haslake && p->kp_nlown > 0) {
		(void) fprintf(out, "  in zlake: promoted");
		for (i = 0; i < p->kp_nlown; i++)
			(void) fprintf(out, " %s%s", p->kp_lake,
			    p->kp_lown[i] + strlen(p->kp_lakestage));
		(void) fprintf(out, " off the archive\n");
	}
}

/* How a step is put back. */
enum {
	KU_RENAME,	/* KU_A renamed from KU_B: renamed back */
	KU_START,	/* the jail KU_A stopped: started, by its block KU_B */
	KU_STOP,	/* the jail KU_A started: stopped, by its block KU_B */
	KU_FILE,	/* the file KU_A changed: its bytes put back, or gone */
	KU_MARK		/* SH_UPDATE_MARK, KU_B, off the snapshot KU_A */
};

#define	KC_UNDO	24

struct kc_undo {
	int		ku_kind;
	char		ku_a[1024];
	char		ku_b[1024];
	char		*ku_bytes;	/* KU_FILE's: NULL, it was not there */
	size_t		ku_len;
};

/* One run. */
struct kc {
	struct sh_zfs		*k_z;
	struct sh_kplan		*k_p;
	const struct sh_spaces	*k_s;
	const struct sh_tools	*k_t;
	FILE			*k_out;
	struct sh_err		*k_err;
	struct sh_err		*k_warn;
	time_t			k_now;
	char			k_log[1100];
	struct kc_undo		k_undo[KC_UNDO];
	size_t			k_nundo;
};

/*
 * A step's note, KIND of A and B, taken before the step: a step that
 * fails, or finds nothing to do, takes it back (k_nundo--). NULL, no
 * room, said in E.
 */
static struct kc_undo *
kc_note(struct kc *c, int kind, const char *a, const char *b)
{
	struct kc_undo *u;

	if (c->k_nundo == KC_UNDO) {
		sh_errf(c->k_err, "more than %d steps to put back", KC_UNDO);
		return (NULL);
	}
	u = &c->k_undo[c->k_nundo++];
	(void) memset(u, 0, sizeof (*u));
	u->ku_kind = kind;
	(void) snprintf(u->ku_a, sizeof (u->ku_a), "%s", a);
	(void) snprintf(u->ku_b, sizeof (u->ku_b), "%s", b != NULL ? b : "");
	return (u);
}

/* FROM renamed TO, noted. */
static int
kc_rename(struct kc *c, const char *from, const char *to)
{
	if (kc_note(c, KU_RENAME, to, from) == NULL)
		return (-1);
	if (sh_zfs_rename(c->k_z, from, to, c->k_err) != 0) {
		c->k_nundo--;
		return (-1);
	}
	(void) fprintf(c->k_out, "renamed %s %s\n", from, to);
	return (0);
}

/*
 * The jail NAME started (START) or stopped by its block FILE, unless it
 * already is: 1 done, 0 nothing to do.
 */
static int
kc_jail(struct kc *c, const char *file, const char *name, int start)
{
	if (sh_jail_running(c->k_t, name) == start)
		return (0);
	if (sh_jail_do(c->k_t, file, name, start ? "-c" : "-r", c->k_log,
	    c->k_err) != 0)
		return (-1);
	(void) fprintf(c->k_out, "%s the jail %s\n", start ? "started" :
	    "stopped", name);
	return (1);
}

/* kc_jail, noted when it does something. */
static int
kc_jailn(struct kc *c, const char *file, const char *name, int start)
{
	int r;

	if (kc_note(c, start ? KU_STOP : KU_START, name, file) == NULL)
		return (-1);
	if ((r = kc_jail(c, file, name, start)) != 1)
		c->k_nundo--;
	return (r < 0 ? -1 : 0);
}

/* The file PATH's bytes noted before it changes; not there, so too. */
static int
kc_keep(struct kc *c, const char *path)
{
	struct kc_undo *u;
	struct stat st;
	FILE *f;
	int r;

	if ((u = kc_note(c, KU_FILE, path, NULL)) == NULL)
		return (-1);
	if ((f = fopen(path, "r")) == NULL) {
		if (errno == ENOENT)
			return (0);
		sh_errf(c->k_err, "%s: %s", path, strerror(errno));
		c->k_nundo--;
		return (-1);
	}
	r = -1;
	if (fstat(fileno(f), &st) == 0 &&
	    (u->ku_bytes = malloc((size_t)st.st_size + 1)) != NULL &&
	    (u->ku_len = fread(u->ku_bytes, 1, (size_t)st.st_size, f)) ==
	    (size_t)st.st_size)
		r = 0;
	else
		sh_errf(c->k_err, "%s: %s", path, strerror(errno));
	(void) fclose(f);
	if (r != 0) {
		free(u->ku_bytes);
		u->ku_bytes = NULL;
		c->k_nundo--;
	}
	return (r);
}

/* The file U's bytes put back, or the file removed. */
static int
kc_put(struct kc *c, const struct kc_undo *u)
{
	FILE *f;
	int r;

	if (u->ku_bytes == NULL) {
		if (unlink(u->ku_a) != 0 && errno != ENOENT) {
			sh_errf(c->k_err, "%s: %s", u->ku_a, strerror(errno));
			return (-1);
		}
		(void) fprintf(c->k_out, "removed %s\n", u->ku_a);
		return (0);
	}
	if ((f = fopen(u->ku_a, "w")) == NULL) {
		sh_errf(c->k_err, "%s: %s", u->ku_a, strerror(errno));
		return (-1);
	}
	r = fwrite(u->ku_bytes, 1, u->ku_len, f) == u->ku_len ? 0 : -1;
	if (fclose(f) != 0 || r != 0) {
		sh_errf(c->k_err, "%s: %s", u->ku_a, strerror(errno));
		return (-1);
	}
	(void) fprintf(c->k_out, "put back %s\n", u->ku_a);
	return (0);
}

/* The file PATH removed, if it is there; noted. */
static int
kc_remove(struct kc *c, const char *path)
{
	if (kc_keep(c, path) != 0)
		return (-1);
	if (unlink(path) != 0 && errno != ENOENT) {
		sh_errf(c->k_err, "%s: %s", path, strerror(errno));
		return (-1);
	}
	(void) fprintf(c->k_out, "removed %s\n", path);
	return (0);
}

/*
 * SH_UPDATE_MARK off every snapshot of DS that carries it, each noted.
 * zlake's never do: a save sends only the newest snapshot, and X's
 * from-save is marked after its save.
 */
static int
kc_unmark(struct kc *c, const char *ds)
{
	char from[SH_CUT_NAME], name[SH_CUT_NAME * 2], val[SH_ZFS_VALMAX];

	for (;;) {
		if (kp_newest(c->k_z, ds, SH_UPDATE_MARK, NULL, from,
		    sizeof (from), c->k_err) != 0)
			return (-1);
		if (from[0] == '\0')
			return (0);
		(void) snprintf(name, sizeof (name), "%s@%s", ds, from);
		if (sh_zfs_get(c->k_z, name, SH_UPDATE_MARK, val, sizeof (val),
		    NULL, c->k_err) != 0 ||
		    kc_note(c, KU_MARK, name, val) == NULL)
			return (-1);
		if (sh_zfs_inherit(c->k_z, name, SH_UPDATE_MARK,
		    c->k_err) != 0) {
			c->k_nundo--;
			return (-1);
		}
	}
}

/* What putting U back is, for a person, into BUF. */
static void
kc_say(const struct kc_undo *u, char *buf, size_t len)
{
	switch (u->ku_kind) {
	case KU_RENAME:
		(void) snprintf(buf, len, "renaming %s back to %s", u->ku_a,
		    u->ku_b);
		break;
	case KU_START:
		(void) snprintf(buf, len, "starting the jail %s", u->ku_a);
		break;
	case KU_STOP:
		(void) snprintf(buf, len, "stopping the jail %s", u->ku_a);
		break;
	case KU_FILE:
		(void) snprintf(buf, len, "putting back %s", u->ku_a);
		break;
	default:
		(void) snprintf(buf, len, "marking %s again", u->ku_a);
		break;
	}
}

/*
 * Every noted step put back, last first. -1 at the first that cannot
 * be, it and those before it said in E as not put back.
 */
static int
kc_back(struct kc *c)
{
	struct kc_undo *u;
	char say[2200];
	int r;

	while (c->k_nundo > 0) {
		u = &c->k_undo[c->k_nundo - 1];
		switch (u->ku_kind) {
		case KU_RENAME:
			r = sh_zfs_rename(c->k_z, u->ku_a, u->ku_b, c->k_err);
			if (r == 0)
				(void) fprintf(c->k_out, "renamed %s back to "
				    "%s\n", u->ku_a, u->ku_b);
			break;
		case KU_START:
		case KU_STOP:
			r = kc_jail(c, u->ku_b, u->ku_a, u->ku_kind ==
			    KU_START);
			break;
		case KU_FILE:
			r = kc_put(c, u);
			break;
		default:
			r = sh_zfs_set(c->k_z, u->ku_a, SH_UPDATE_MARK,
			    u->ku_b, c->k_err);
			if (r == 0)
				(void) fprintf(c->k_out, "marked %s again\n",
				    u->ku_a);
			break;
		}
		if (r < 0)
			break;
		c->k_nundo--;
	}
	if (c->k_nundo == 0)
		return (0);
	while (c->k_nundo > 0) {
		kc_say(&c->k_undo[--c->k_nundo], say, sizeof (say));
		sh_errf(c->k_err, "not put back: %s", say);
	}
	return (-1);
}

static void
kc_free(struct kc *c)
{
	size_t i;

	for (i = 0; i < KC_UNDO; i++)
		free(c->k_undo[i].ku_bytes);
	free(c);
}

/* A save of NAME, its commit MSG, its snapshot's name into SNAP. */
static int
kc_save(struct kc *c, const char *name, const char *msg, char *snap,
    size_t len, struct sh_err *e)
{
	struct sh_save *sp;
	int r;

	if ((sp = calloc(1, sizeof (*sp))) == NULL) {
		sh_err_nomem(e);
		return (-1);
	}
	r = sh_save_plan(c->k_z, c->k_s, name, NULL, sp, e) != 0 ||
	    sh_save_run(c->k_z, sp, msg, c->k_now, snap, len, c->k_out,
	    e) != 0 ? -1 : 0;
	free(sp);
	return (r);
}

/*
 * X's closing save, its commit MSG: 0 made; 1 made on zrun and its
 * mirror into zlake failed, which the next save brings up to date (a
 * save's own rule, 7), said in W; -1 not made, said in E.
 */
static int
kc_close(struct kc *c, const char *msg)
{
	char snap[256], name[SH_CUT_NAME + 260], *said, *line, *nl;
	struct sh_err se;
	size_t len;
	FILE *f;
	int r;

	said = NULL;
	if ((f = open_memstream(&said, &len)) == NULL) {
		sh_err_nomem(c->k_err);
		return (-1);
	}
	sh_err_init(&se, f, "");
	snap[0] = '\0';
	r = kc_save(c, c->k_p->kp_name, msg, snap, sizeof (snap), &se);
	(void) fclose(f);
	if (r != 0 && snap[0] != '\0') {
		(void) snprintf(name, sizeof (name), "%s@%s", c->k_p->kp_run,
		    snap);
		if (kp_there(c->k_z, name, c->k_err))
			r = 1;
	}
	for (line = said; r != 0 && line != NULL && *line != '\0';
	    line = nl) {
		if ((nl = strchr(line, '\n')) != NULL)
			*nl++ = '\0';
		else
			nl = line + strlen(line);
		sh_errf(r == 1 ? c->k_warn : c->k_err, "%s", line);
	}
	free(said);
	return (r);
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

/*
 * X stopped, and its final save if it wrote since its newest save:
 * read again with X's jail stopped and the pool synced, whatever the
 * plan saw, so that nothing written after the plan goes with the old
 * tree.
 */
static int
kc_final(struct kc *c)
{
	struct sh_kplan *p = c->k_p;
	char msg[SH_CUT_NAME * 2], snap[256];

	if (kc_jailn(c, p->kp_jailfile, p->kp_name, 0) != 0)
		return (-1);
	p->kp_wrote = 0;
	p->kp_nx = 0;
	if (kp_written(c->k_z, p, c->k_err) != 0)
		return (-1);
	if (p->kp_wrote == 0)
		return (0);
	(void) snprintf(msg, sizeof (msg), "written during the soak of %s",
	    p->kp_stagename);
	return (kc_save(c, p->kp_name, msg, snap, sizeof (snap), c->k_err));
}

/*
 * The cutover (7.9), every step noted: X stopped and its final save;
 * X-staging stopped; X renamed aside, its previous root, and X-staging
 * renamed X; X-staging's block removed and X's written for the new X;
 * in zlake, the old X archived and the staging mirror renamed X; the
 * mark off the replayed from-save; X started and its closing save,
 * after which nothing is put back. 1, made with its mirror behind.
 */
static int
kc_cutover(struct kc *c)
{
	struct sh_kplan *p = c->k_p;
	char mp[SH_ZFS_VALMAX], msg[SH_CUT_NAME * 2];

	if (kc_final(c) != 0 ||
	    kc_jailn(c, p->kp_stagejail, p->kp_stagename, 0) != 0 ||
	    kc_rename(c, p->kp_run, p->kp_aside) != 0 ||
	    kc_rename(c, p->kp_stage, p->kp_run) != 0 ||
	    kc_remove(c, p->kp_stagejail) != 0 ||
	    kc_keep(c, p->kp_jailfile) != 0)
		return (-1);
	if (sh_zfs_get(c->k_z, p->kp_run, "mountpoint", mp, sizeof (mp),
	    NULL, c->k_err) != 0 ||
	    sh_jail_write(p->kp_jailfile, p->kp_name, p->kp_name, mp,
	    p->kp_recipe.sr_merged, c->k_err) != 0)
		return (-1);
	(void) fprintf(c->k_out, "wrote %s\n", p->kp_jailfile);
	if ((p->kp_haslake && kc_rename(c, p->kp_lake, p->kp_archive) != 0) ||
	    kc_rename(c, p->kp_lakestage, p->kp_lake) != 0 ||
	    kc_unmark(c, p->kp_run) != 0 ||
	    kc_jailn(c, p->kp_jailfile, p->kp_name, 1) != 0)
		return (-1);
	(void) snprintf(msg, sizeof (msg), "cutover to %s", p->kp_version);
	return (kc_close(c, msg));
}

/*
 * The prune (7.9), last, what keeps it from going through said in W:
 * the new X's clones of the previous root's datasets promoted off it,
 * then the previous root destroyed, unless something would keep it, in
 * which case it is left whole; in zlake, the renamed mirror's clones of
 * the archive's promoted off it, so that the archive stands alone.
 */
static void
kc_prune(struct kc *c)
{
	struct sh_kplan *p = c->k_p;
	struct sh_err *e = c->k_err;
	unsigned before;

	c->k_err = c->k_warn;
	before = c->k_err->se_count;
	if (kp_owns(c->k_z, p->kp_run, p->kp_aside, p->kp_own, &p->kp_nown,
	    c->k_err) != 0 || c->k_err->se_count != before ||
	    kc_promote(c, p->kp_own, p->kp_nown, p->kp_aside) != 0 ||
	    kp_destroyable(c->k_z, c->k_s->ss_root[SH_SP_RUN], p->kp_aside,
	    NULL, 0, c->k_err) != 0 || c->k_err->se_count != before ||
	    kc_destroy(c, p->kp_aside) != 0)
		sh_errf(c->k_err, "%s not pruned: %s is cut over, its previous "
		    "root left as it is", p->kp_aside, p->kp_name);
	before = c->k_err->se_count;
	if (p->kp_haslake && (kp_owns(c->k_z, p->kp_lake, p->kp_archive,
	    p->kp_lown, &p->kp_nlown, c->k_err) != 0 ||
	    c->k_err->se_count != before ||
	    kc_promote(c, p->kp_lown, p->kp_nlown, p->kp_archive) != 0))
		sh_errf(c->k_err, "%s not promoted off the archive %s",
		    p->kp_lake, p->kp_archive);
	c->k_err = e;
}

/*
 * The abandon (7.10), every step noted: X-staging stopped, its block
 * removed; the mark off X's from-save; X started.
 */
static int
kc_abandon(struct kc *c)
{
	struct sh_kplan *p = c->k_p;

	if (kc_jailn(c, p->kp_stagejail, p->kp_stagename, 0) != 0 ||
	    kc_remove(c, p->kp_stagejail) != 0 ||
	    kc_unmark(c, p->kp_run) != 0 ||
	    kc_jailn(c, p->kp_jailfile, p->kp_name, 1) != 0)
		return (-1);
	return (0);
}

/*
 * The abandon's last step, what keeps it from going through said in W:
 * X-staging destroyed on zrun and its mirror in zlake.
 */
static void
kc_drop(struct kc *c)
{
	struct sh_kplan *p = c->k_p;
	struct sh_err *e = c->k_err;

	c->k_err = c->k_warn;
	if (kc_destroy(c, p->kp_stage) != 0)
		sh_errf(c->k_err, "%s not destroyed: the update of %s is "
		    "abandoned, the staging tree left as it is", p->kp_stage,
		    p->kp_name);
	if (p->kp_haslakestage && kc_destroy(c, p->kp_lakestage) != 0)
		sh_errf(c->k_err, "%s not destroyed: the update of %s is "
		    "abandoned, the staging mirror left as it is",
		    p->kp_lakestage, p->kp_name);
	c->k_err = e;
}

int
sh_cutover_run(struct sh_zfs *z, struct sh_kplan *p,
    const struct sh_spaces *s, const struct sh_tools *t, time_t now,
    FILE *out, struct sh_err *e, struct sh_err *w)
{
	const char *verb = p->kp_abandon ? "abandon" : "cutover";
	struct kc *c;
	int r;

	if ((c = calloc(1, sizeof (*c))) == NULL) {
		sh_err_nomem(e);
		return (-1);
	}
	c->k_z = z;
	c->k_p = p;
	c->k_s = s;
	c->k_t = t;
	c->k_out = out;
	c->k_err = e;
	c->k_warn = w;
	c->k_now = now;
	(void) snprintf(c->k_log, sizeof (c->k_log), "%s/update.log",
	    p->kp_rundir);
	r = p->kp_abandon ? kc_abandon(c) : kc_cutover(c);
	if (r < 0) {
		if (kc_back(c) == 0)
			sh_errf(e, "%s of %s failed and was put back", verb,
			    p->kp_name);
		else
			sh_errf(e, "%s of %s failed, and putting it back "
			    "stopped part way", verb, p->kp_name);
		kc_free(c);
		return (-1);
	}
	if (p->kp_abandon)
		kc_drop(c);
	else
		kc_prune(c);
	kc_tidy(c);
	(void) fprintf(out, "%s %s\n", p->kp_abandon ? "abandoned" :
	    "cut over", p->kp_abandon ? p->kp_stagename : p->kp_name);
	kc_free(c);
	return (0);
}

void
sh_cutover_free(struct sh_kplan *p)
{
	sh_recipe_free(&p->kp_recipe);
}
