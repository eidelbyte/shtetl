/*
 * shtetl cutover X and shtetl abandon X (planning.md 7.9, 7.10).
 *
 * Decided here, beyond the plan (PROPOSAL): either is refused while
 * the update is not done (paused at a gate, or cut short), which
 * shtetl update --continue or --abort settles; a cutover is refused
 * when the staging tree has no mirror in zlake yet, which a save of it
 * makes; the archive is archive/containers/X-<YYYY-MM-DD> (-2 and on
 * for another that day); a cutover that fails part way stops there and
 * says what is done, nothing being undone (the old X may be gone).
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

int
sh_cutover_plan(struct sh_zfs *z, const struct sh_spaces *s,
    const struct sh_node *site, const char *root, const char *target,
    int abandon, const char *jaildir, const char *rundir, struct sh_kplan *p,
    struct sh_err *e, struct sh_err *w)
{
	const struct sh_node *v;
	char val[SH_ZFS_VALMAX], state[32], rpath[SH_CUT_NAME + 16];
	char prop[SH_CUT_NAME + 16], **all;
	const char *suffix;
	unsigned before;
	size_t i, n;
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
	(void) snprintf(p->kp_archive, sizeof (p->kp_archive),
	    "%s/archive/containers", s->ss_root[SH_SP_LAKE]);
	(void) snprintf(p->kp_jailfile, sizeof (p->kp_jailfile), "%s/%s.conf",
	    jaildir, p->kp_name);
	(void) snprintf(p->kp_stagejail, sizeof (p->kp_stagejail),
	    "%s/%s.conf", jaildir, p->kp_stagename);
	(void) snprintf(p->kp_rundir, sizeof (p->kp_rundir), "%s/%s", rundir,
	    p->kp_name);
	if ((r = sh_zfs_get(z, p->kp_run, "type", val, sizeof (val), NULL,
	    e)) != 0) {
		if (r == 1)
			sh_errf(e, "no container %s", p->kp_name);
		return (-1);
	}
	if (sh_zfs_get(z, p->kp_stage, "type", val, sizeof (val), NULL,
	    e) != 0) {
		sh_errf(e, "no %s: no update of %s to %s", p->kp_stagename,
		    p->kp_name, abandon ? "abandon" : "cut over");
		return (-1);
	}
	if ((r = sh_update_record(rundir, p->kp_name, p->kp_version,
	    sizeof (p->kp_version), p->kp_from, sizeof (p->kp_from), state,
	    sizeof (state), e)) < 0)
		return (-1);
	if (r == 1) {
		sh_errf(e, "%s is no update's: its record is gone",
		    p->kp_stagename);
		return (-1);
	}
	if (strcmp(state, "staged") != 0) {
		sh_errf(e, "the update of %s is not done: shtetl update "
		    "--continue %s, or --abort %s", p->kp_name, p->kp_name,
		    p->kp_name);
		return (-1);
	}
	(void) snprintf(rpath, sizeof (rpath), "containers/%s", p->kp_name);
	if (sh_recipe_resolve(root, rpath, &p->kp_recipe, e, w) != 0)
		return (-1);
	if (abandon)
		return (0);
	if (sh_zfs_get(z, p->kp_lakestage, "type", val, sizeof (val), NULL,
	    e) != 0) {
		sh_errf(e, "%s has no mirror in zlake: save it first",
		    p->kp_stagename);
		return (-1);
	}
	/* what X wrote since its from-save, dataset by dataset */
	if ((all = sh_zw_new(e)) == NULL)
		return (-1);
	r = sh_zw_add(&all, p->kp_run) != 0 ? -1 :
	    sh_zw_walk(z, p->kp_run, &all, e);
	n = strlen(p->kp_run);
	(void) snprintf(prop, sizeof (prop), "written@%s", p->kp_from);
	for (i = 0; r == 0 && all[i] != NULL && i < SH_CUT_DS; i++) {
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
	if (r != 0)
		return (-1);
	/* X-staging's clones of X's own datasets */
	if ((all = sh_zw_new(e)) == NULL)
		return (-1);
	r = sh_zw_add(&all, p->kp_stage) != 0 ? -1 :
	    sh_zw_walk(z, p->kp_stage, &all, e);
	for (i = 0; r == 0 && all[i] != NULL; i++) {
		if (sh_zfs_get(z, all[i], "origin", val, sizeof (val), NULL,
		    e) != 0) {
			r = -1;
			break;
		}
		if (kc_under(val, p->kp_run) && p->kp_nown < SH_CUT_DS)
			(void) snprintf(p->kp_own[p->kp_nown++], SH_CUT_NAME,
			    "%s", all[i]);
	}
	sh_zw_free(all);
	return (r == 0 && e->se_count == before ? 0 : -1);
}

void
sh_cutover_show(FILE *out, const struct sh_kplan *p)
{
	size_t i;

	if (p->kp_abandon) {
		(void) fprintf(out, "abandon %s, the update of %s to @%s\n"
		    "  %s stopped, destroyed on zrun and in zlake\n"
		    "  %s started again\n", p->kp_stagename, p->kp_name,
		    p->kp_version, p->kp_stagename, p->kp_name);
		return;
	}
	(void) fprintf(out, "cutover of %s to %s, @%s\n", p->kp_name,
	    p->kp_stagename, p->kp_version);
	if (p->kp_wrote > 0) {
		(void) fprintf(out, "  %s was written to since its from-save "
		    "%s, %lu bytes:", p->kp_name, p->kp_from, p->kp_wrote);
		for (i = 0; i < p->kp_nx; i++) {
			if (p->kp_written[i] > 0)
				(void) fprintf(out, " %s %lu",
				    p->kp_xrel[i][0] != '\0' ?
				    p->kp_xrel[i] : "/", p->kp_written[i]);
		}
		(void) fprintf(out, "\n  a final save of %s, for its archive\n",
		    p->kp_name);
	}
	(void) fprintf(out, "  %s stopped; promoted:", p->kp_stagename);
	for (i = 0; i < p->kp_nown; i++)
		(void) fprintf(out, " %s", p->kp_own[i]);
	if (p->kp_nown == 0)
		(void) fprintf(out, " none");
	(void) fprintf(out, "\n  %s destroyed on zrun, %s renamed %s\n"
	    "  in zlake: %s to %s, %s renamed %s\n  %s started and saved\n",
	    p->kp_run, p->kp_stage, p->kp_run, p->kp_lake, p->kp_archive,
	    p->kp_lakestage, p->kp_lake, p->kp_name);
}

/* One run: what it has done so far, for a failure to say. */
struct kc {
	struct sh_zfs		*k_z;
	struct sh_kplan		*k_p;
	const struct sh_spaces	*k_s;
	const struct sh_tools	*k_t;
	FILE			*k_out;
	struct sh_err		*k_err;
	time_t			k_now;
	char			k_log[1100];
	char			k_done[2048];
};

static void
kc_did(struct kc *c, const char *what)
{
	size_t n = strlen(c->k_done);

	(void) snprintf(c->k_done + n, sizeof (c->k_done) - n, "%s%s",
	    n > 0 ? "; " : "", what);
}

static int
kc_there(struct kc *c, const char *name)
{
	char val[64];

	return (sh_zfs_get(c->k_z, name, "type", val, sizeof (val), NULL,
	    c->k_err) == 0);
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

/* The tree DS unmounted, deepest first, and destroyed. */
static int
kc_destroy(struct kc *c, const char *ds)
{
	char **all;
	size_t n;

	if ((all = sh_zw_new(c->k_err)) == NULL)
		return (-1);
	if (sh_zw_add(&all, ds) != 0 || sh_zw_walk(c->k_z, ds, &all,
	    c->k_err) != 0) {
		sh_zw_free(all);
		return (-1);
	}
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

/* The jail NAME stopped, if it runs. */
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

/* The update's record and its documents gone; its log kept. */
static void
kc_unrecord(struct kc *c)
{
	char dir[1100], path[1400];
	struct dirent *de;
	DIR *d;

	(void) snprintf(path, sizeof (path), "%s/" SH_UPDATE_RECORD,
	    c->k_p->kp_rundir);
	if (unlink(path) != 0 && errno != ENOENT)
		sh_errf(c->k_err, "%s: %s", path, strerror(errno));
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

/* The staging mirror's clones of the lake's X promoted. */
static int
kc_lakepromote(struct kc *c)
{
	struct sh_kplan *p = c->k_p;
	char **all, val[SH_ZFS_VALMAX];
	size_t i;
	int r;

	if ((all = sh_zw_new(c->k_err)) == NULL)
		return (-1);
	r = sh_zw_add(&all, p->kp_lakestage) != 0 ? -1 :
	    sh_zw_walk(c->k_z, p->kp_lakestage, &all, c->k_err);
	for (i = 0; r == 0 && all[i] != NULL; i++) {
		if (sh_zfs_get(c->k_z, all[i], "origin", val, sizeof (val),
		    NULL, c->k_err) != 0) {
			r = -1;
			break;
		}
		if (!kc_under(val, p->kp_lake))
			continue;
		if (sh_zfs_promote(c->k_z, all[i], c->k_err) != 0) {
			r = -1;
			break;
		}
		(void) fprintf(c->k_out, "promoted %s\n", all[i]);
	}
	sh_zw_free(all);
	return (r);
}

static int
kc_cutover(struct kc *c)
{
	struct sh_kplan *p = c->k_p;
	char arch[SH_CUT_NAME * 2], mp[SH_ZFS_VALMAX], msg[SH_CUT_NAME * 2];
	char day[16];
	struct tm tm;
	size_t i;
	int n;

	if (p->kp_wrote > 0) {
		(void) snprintf(msg, sizeof (msg), "written during the soak "
		    "of %s", p->kp_stagename);
		if (kc_save(c, p->kp_name, msg) != 0)
			return (-1);
		kc_did(c, "a final save of X");
	}
	if (kc_stop(c, p->kp_stagejail, p->kp_stagename) != 0)
		return (-1);
	for (i = 0; i < p->kp_nown; i++) {
		if (sh_zfs_promote(c->k_z, p->kp_own[i], c->k_err) != 0)
			return (-1);
		(void) fprintf(c->k_out, "promoted %s\n", p->kp_own[i]);
	}
	kc_did(c, "the staging tree stopped, its clones promoted");
	if (kc_stop(c, p->kp_jailfile, p->kp_name) != 0 ||
	    kc_destroy(c, p->kp_run) != 0)
		return (-1);
	kc_did(c, "the old tree destroyed on zrun");
	if (sh_zfs_rename(c->k_z, p->kp_stage, p->kp_run, c->k_err) != 0)
		return (-1);
	(void) fprintf(c->k_out, "renamed %s %s\n", p->kp_stage, p->kp_run);
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
	kc_did(c, "the staging tree renamed, its jail's block the container's");
	/* zlake: the same promotes, the old tree archived, the rename */
	if (kc_lakepromote(c) != 0)
		return (-1);
	(void) gmtime_r(&c->k_now, &tm);
	(void) strftime(day, sizeof (day), "%Y-%m-%d", &tm);
	(void) snprintf(arch, sizeof (arch), "%s/%s-%s", p->kp_archive,
	    p->kp_name, day);
	for (n = 2; n < 100 && kc_there(c, arch); n++)
		(void) snprintf(arch, sizeof (arch), "%s/%s-%s-%d",
		    p->kp_archive, p->kp_name, day, n);
	if (sh_zfs_rename(c->k_z, p->kp_lake, arch, c->k_err) != 0)
		return (-1);
	(void) fprintf(c->k_out, "archived %s as %s\n", p->kp_lake, arch);
	if (sh_zfs_rename(c->k_z, p->kp_lakestage, p->kp_lake,
	    c->k_err) != 0)
		return (-1);
	(void) fprintf(c->k_out, "renamed %s %s\n", p->kp_lakestage,
	    p->kp_lake);
	kc_did(c, "zlake's mirror the same, the old one archived");
	if (sh_jail_do(c->k_t, p->kp_jailfile, p->kp_name, "-c", c->k_log,
	    c->k_err) != 0)
		return (-1);
	(void) fprintf(c->k_out, "started the jail %s\n", p->kp_name);
	kc_did(c, "the container started");
	(void) snprintf(msg, sizeof (msg), "cutover to %s", p->kp_version);
	if (kc_save(c, p->kp_name, msg) != 0)
		return (-1);
	return (0);
}

static int
kc_abandon(struct kc *c)
{
	struct sh_kplan *p = c->k_p;

	if (kc_stop(c, p->kp_stagejail, p->kp_stagename) != 0)
		return (-1);
	if (unlink(p->kp_stagejail) != 0 && errno != ENOENT) {
		sh_errf(c->k_err, "%s: %s", p->kp_stagejail, strerror(errno));
		return (-1);
	}
	if (kc_destroy(c, p->kp_stage) != 0)
		return (-1);
	kc_did(c, "the staging tree destroyed on zrun");
	if (kc_there(c, p->kp_lakestage) && kc_destroy(c,
	    p->kp_lakestage) != 0)
		return (-1);
	kc_did(c, "its mirror destroyed in zlake");
	if (!sh_jail_running(c->k_t, p->kp_name)) {
		if (sh_jail_do(c->k_t, p->kp_jailfile, p->kp_name, "-c",
		    c->k_log, c->k_err) != 0)
			return (-1);
		(void) fprintf(c->k_out, "started the jail %s\n", p->kp_name);
	}
	return (0);
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
		sh_errf(e, "%s of %s stopped; done: %s", p->kp_abandon ?
		    "abandon" : "cutover", p->kp_name, c.k_done[0] != '\0' ?
		    c.k_done : "nothing");
		return (-1);
	}
	kc_unrecord(&c);
	(void) fprintf(out, "%s %s\n", p->kp_abandon ? "abandoned" :
	    "cut over", p->kp_abandon ? p->kp_stagename : p->kp_name);
	return (0);
}

void
sh_cutover_free(struct sh_kplan *p)
{
	sh_recipe_free(&p->kp_recipe);
}
