/*
 * shtetl update X (planning.md 7.6, 7.6.2, 7.7, 9).
 *
 * Every dataset of X reaches X-staging, parents first: a dataset X
 * inherits from its foundation F is replayed onto F's target version,
 * one rebase a save (step 1 the clone form onto F/d@V, placed; each
 * later step the dataset form onto it, --allow-unrelated --base the
 * save before); F's placeholder is cloned from the target; X's own
 * datasets from X's from-save. Each dataset's replay ends before a
 * child of it is made (P4).
 *
 * Decided here, beyond the plan (ruled 2026-10-14): an update whose
 * rebase stops at its conflicts gate with no person at a terminal, or
 * whose person leaves the picker with names unanswered, pauses (exit
 * 3, as a build at a patch does); shtetl update --continue X takes it
 * up and --abort X gives it up. shtetl keeps no record of its own: the
 * tool's manifest and resolution are what a pause leaves, and where
 * the update stands is read from ZFS (sh_update_state; a dataset's step
 * is the count of its saves the staging dataset holds and whether the
 * tool's record is on it). X's from-save carries SH_UPDATE_MARK, the
 * target. X is stopped until the update is done or fails, and started
 * again by --abort or a failure; the staging tree's jail is X-staging,
 * X's hostname and settings. update.depth given as an interval
 * replays every save (intervals are M7's).
 */

#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "core/beam.h"
#include "core/build.h"
#include "core/cascade.h"
#include "core/err.h"
#include "core/jail.h"
#include "core/rebase.h"
#include "core/run.h"
#include "core/save.h"
#include "core/spaces.h"
#include "core/tree.h"
#include "core/update.h"
#include "core/version.h"
#include "core/zfs.h"
#include "core/zwalk.h"

#define	UQ_PHASE	"zfs_rebase:phase"	/* the tool's record */

/* ---- the plan ------------------------------------------------------ */

/* X's datasets, how each reaches X-staging (7.6 steps 5, 6). */
static int
uq_datasets(struct sh_zfs *z, struct sh_uplan *p, struct sh_err *e)
{
	const struct sh_node *dss, *v;
	char **all, val[SH_ZFS_VALMAX], fds[SH_UPDATE_NAME * 2];
	struct sh_uds *d;
	size_t i, n, rl;
	unsigned before;
	int r;

	before = e->se_count;
	if ((all = sh_zw_new(e)) == NULL)
		return (-1);
	r = sh_zw_add(&all, p->up_run) != 0 ? -1 :
	    sh_zw_walk(z, p->up_run, &all, e);
	n = strlen(p->up_run);
	rl = strlen(p->up_runf);
	dss = sh_obj_get(p->up_recipe.sr_merged, "datasets");
	for (i = 0; r == 0 && all[i] != NULL; i++) {
		if (p->up_nds == SH_UPDATE_DS) {
			sh_errf(e, "container %s: more than %d datasets",
			    p->up_name, SH_UPDATE_DS);
			break;
		}
		d = &p->up_dss[p->up_nds++];
		(void) memset(d, 0, sizeof (*d));
		(void) snprintf(d->ud_rel, sizeof (d->ud_rel), "%s",
		    i == 0 ? "" : all[i] + n + 1);
		v = d->ud_rel[0] != '\0' ? sh_obj_get(dss, d->ud_rel) : NULL;
		d->ud_props = v != NULL && v->sn_type == SH_OBJECT ? v : NULL;
		if (sh_zfs_get(z, all[i], "origin", val, sizeof (val), NULL,
		    e) != 0)
			break;
		if (strcmp(val, "-") == 0) {
			d->ud_how = SH_UH_OWN;
			if (sh_zfs_get(z, all[i], "canmount", val, sizeof (val),
			    NULL, e) != 0)
				break;
			d->ud_placeholder = strcmp(val, "off") == 0;
			continue;
		}
		/* F's own dataset at X's version */
		if (strncmp(val, p->up_runf, rl) != 0 ||
		    (val[rl] != '/' && val[rl] != '@')) {
			sh_errf(e, "%s: a clone of %s, not of %s", all[i], val,
			    p->up_runf);
			continue;
		}
		(void) snprintf(fds, sizeof (fds), "%.*s",
		    (int)(strchr(val, '@') - val), val);
		if (sh_zfs_get(z, fds, "canmount", val, sizeof (val), NULL,
		    e) != 0)
			break;
		d->ud_placeholder = strcmp(val, "off") == 0;
		d->ud_how = d->ud_placeholder ? SH_UH_TARGET : SH_UH_REPLAY;
	}
	sh_zw_free(all);
	return (r == 0 && e->se_count == before ? 0 : -1);
}

/*
 * The target: VERSION, or F's newest version or -pkg in zlake (LAKEF);
 * newer than X's own, by createtxg; its revision meeting X's ver.
 */
static int
uq_target(struct sh_zfs *z, struct sh_uplan *p, const char *lakef,
    const char *version, struct sh_err *e)
{
	char name[SH_UPDATE_NAME * 2], kind[SH_ZFS_VALMAX], **snaps, *end;
	long at, old, i, major, minor;

	if ((snaps = sh_zw_snaps(z, lakef, e)) == NULL)
		return (-1);
	at = -1;
	old = sh_zw_index(snaps, p->up_old);
	for (i = 0; snaps[i] != NULL; i++) {
		(void) snprintf(name, sizeof (name), "%s@%s", lakef, snaps[i]);
		if (sh_zfs_get(z, name, "shtetl:kind", kind, sizeof (kind),
		    NULL, e) != 0) {
			sh_zw_free(snaps);
			return (-1);
		}
		if (strcmp(kind, "version") != 0 && strcmp(kind, "pkg") != 0)
			continue;
		if (version == NULL || strcmp(version, snaps[i]) == 0)
			at = i;
	}
	if (at >= 0)
		(void) snprintf(p->up_version, sizeof (p->up_version), "%s",
		    snaps[at]);
	sh_zw_free(snaps);
	if (at < 0) {
		if (version != NULL)
			sh_errf(e, "foundation %s has no version %s",
			    p->up_found, version);
		else
			sh_errf(e, "foundation %s exists without a version",
			    p->up_found);
		return (-1);
	}
	if (at == old) {
		sh_errf(e, "container %s is on %s@%s already", p->up_name,
		    p->up_found, p->up_version);
		return (-1);
	}
	if (old >= 0 && at < old) {
		sh_errf(e, "%s@%s is older than %s, %s's", p->up_found,
		    p->up_version, p->up_old, p->up_name);
		return (-1);
	}
	(void) snprintf(name, sizeof (name), "%s@%s", lakef, p->up_version);
	if (sh_zfs_get(z, name, "shtetl:revision", kind, sizeof (kind), NULL,
	    e) != 0)
		return (-1);
	major = strtol(kind, &end, 10);
	minor = *end == '.' ? strtol(end + 1, &end, 10) : -1;
	if (minor < 0 || *end != '\0') {
		sh_errf(e, "%s@%s: no shtetl:revision", p->up_found,
		    p->up_version);
		return (-1);
	}
	return (sh_version_check(major, minor,
	    sh_obj_get(p->up_recipe.sr_merged, "ver"), p->up_recipe.sr_file,
	    e) == 0 ? 0 : -1);
}

/* X's saves to replay, oldest first, depth's newest but the from-save. */
static int
uq_saves(struct sh_zfs *z, struct sh_uplan *p, struct sh_err *e)
{
	char name[SH_UPDATE_NAME * 2], kind[SH_ZFS_VALMAX], **snaps;
	size_t i, n, first;
	char *keep[SH_UPDATE_SAVES];

	if ((snaps = sh_zw_snaps(z, p->up_run, e)) == NULL)
		return (-1);
	n = 0;
	for (i = 0; snaps[i] != NULL; i++) {
		(void) snprintf(name, sizeof (name), "%s@%s", p->up_run,
		    snaps[i]);
		if (sh_zfs_get(z, name, "shtetl:kind", kind, sizeof (kind),
		    NULL, e) != 0) {
			sh_zw_free(snaps);
			return (-1);
		}
		if (strcmp(kind, "save") != 0)
			continue;
		/* the newest SH_UPDATE_SAVES - 1, room for the from-save */
		if (n == SH_UPDATE_SAVES - 1) {
			(void) memmove(keep, keep + 1,
			    (n - 1) * sizeof (*keep));
			n--;
		}
		keep[n++] = snaps[i];
	}
	first = 0;
	if (p->up_depth > 0)
		first = n + 1 > p->up_depth ? n + 1 - p->up_depth : 0;
	for (i = first; i < n; i++)
		(void) snprintf(p->up_saves[p->up_nsaves++], SH_UPDATE_NAME,
		    "%s", keep[i]);
	sh_zw_free(snaps);
	return (0);
}

/*
 * The saves of an update under way: X's, oldest first, up to the
 * newest marked SH_UPDATE_MARK, update.depth of them; its target the
 * mark's.
 */
static int
uq_marked(struct sh_zfs *z, struct sh_uplan *p, struct sh_err *e)
{
	char name[SH_UPDATE_NAME * 2], val[SH_ZFS_VALMAX], **snaps;
	char *keep[SH_UPDATE_SAVES];
	size_t i, n, first, last;

	if ((snaps = sh_zw_snaps(z, p->up_run, e)) == NULL)
		return (-1);
	n = 0;
	last = 0;
	for (i = 0; snaps[i] != NULL; i++) {
		(void) snprintf(name, sizeof (name), "%s@%s", p->up_run,
		    snaps[i]);
		if (sh_zfs_get(z, name, "shtetl:kind", val, sizeof (val), NULL,
		    e) != 0) {
			sh_zw_free(snaps);
			return (-1);
		}
		if (strcmp(val, "save") != 0)
			continue;
		if (n == SH_UPDATE_SAVES) {
			(void) memmove(keep, keep + 1,
			    (n - 1) * sizeof (*keep));
			n--;
		}
		keep[n++] = snaps[i];
		if (sh_zfs_get(z, name, SH_UPDATE_MARK, val, sizeof (val),
		    NULL, e) != 0) {
			sh_zw_free(snaps);
			return (-1);
		}
		if (strcmp(val, "-") != 0) {
			last = n;
			(void) snprintf(p->up_version, sizeof (p->up_version),
			    "%s", val);
		}
	}
	if (last == 0) {
		sh_zw_free(snaps);
		sh_errf(e, "container %s has no save an update started from",
		    p->up_name);
		return (-1);
	}
	first = 0;
	if (p->up_depth > 0)
		first = last > p->up_depth ? last - p->up_depth : 0;
	for (i = first; i < last; i++)
		(void) snprintf(p->up_saves[p->up_nsaves++], SH_UPDATE_NAME,
		    "%s", keep[i]);
	sh_zw_free(snaps);
	return (0);
}

int
sh_update_state(struct sh_zfs *z, const char *run, const char *stage,
    char *version, size_t vlen, char *from, size_t flen, struct sh_err *e)
{
	char name[SH_UPDATE_NAME * 2], val[SH_ZFS_VALMAX], **all, **snaps;
	size_t i;
	int r, open;

	version[0] = from[0] = '\0';
	if ((r = sh_zfs_get(z, stage, "type", val, sizeof (val), NULL,
	    e)) != 0)
		return (r == 1 ? SH_US_NONE : -1);
	if ((all = sh_zw_new(e)) == NULL)
		return (-1);
	r = sh_zw_add(&all, stage) != 0 ? -1 : sh_zw_walk(z, stage, &all, e);
	open = 0;
	for (i = 0; r == 0 && all[i] != NULL; i++) {
		if (sh_zfs_get(z, all[i], UQ_PHASE, val, sizeof (val), NULL,
		    e) != 0)
			r = -1;
		else if (strcmp(val, "-") != 0)
			open = 1;
	}
	sh_zw_free(all);
	if (r != 0)
		return (-1);
	if (open)
		return (SH_US_UNDERWAY);
	if ((snaps = sh_zw_snaps(z, stage, e)) == NULL)
		return (-1);
	for (i = 0; snaps[i] != NULL; i++) {
		(void) snprintf(name, sizeof (name), "%s@%s", stage, snaps[i]);
		if (sh_zfs_get(z, name, "shtetl:kind", val, sizeof (val), NULL,
		    e) != 0) {
			sh_zw_free(snaps);
			return (-1);
		}
		if (strcmp(val, "version") == 0)
			(void) snprintf(version, vlen, "%s", snaps[i]);
	}
	sh_zw_free(snaps);
	if (version[0] == '\0')
		return (SH_US_UNDERWAY);
	/* X's from-save: its newest save marked with the target */
	if ((snaps = sh_zw_snaps(z, run, e)) == NULL)
		return (-1);
	for (i = 0; snaps[i] != NULL; i++) {
		(void) snprintf(name, sizeof (name), "%s@%s", run, snaps[i]);
		if (sh_zfs_get(z, name, SH_UPDATE_MARK, val, sizeof (val),
		    NULL, e) != 0) {
			sh_zw_free(snaps);
			return (-1);
		}
		if (strcmp(val, version) == 0)
			(void) snprintf(from, flen, "%s", snaps[i]);
	}
	sh_zw_free(snaps);
	return (SH_US_DONE);
}

int
sh_update_plan(struct sh_zfs *z, const struct sh_spaces *s,
    const struct sh_node *site, const char *root, const char *target,
    const char *version, enum sh_uverb verb, const char *jaildir,
    const char *rundir, struct sh_uplan *p, struct sh_err *e,
    struct sh_err *w)
{
	const struct sh_node *upd, *v;
	char val[SH_ZFS_VALMAX], lakef[SH_UPDATE_NAME * 2];
	char rec[1100], rpath[SH_UPDATE_NAME + 16], from[SH_UPDATE_NAME];
	const char *suffix, *at, *slash;
	unsigned before;
	size_t i, rl;
	int r, sp;

	(void) memset(p, 0, sizeof (*p));
	p->up_verb = verb;
	before = e->se_count;
	if (sh_save_target(target, p->up_name, sizeof (p->up_name), e) != 0)
		return (-1);
	for (sp = 0; sp < SH_SP_N; sp++) {
		if (sp != SH_SP_BUILD && s->ss_root[sp] == NULL)
			sh_errf(e, "no %s root", sh_space_name(sp));
	}
	if (e->se_count != before)
		return (-1);
	upd = site != NULL ? sh_obj_get(site, "update") : NULL;
	v = sh_obj_get(upd, "suffix");
	suffix = v != NULL && v->sn_type == SH_STR ? v->sn_str : "staging";
	v = sh_obj_get(upd, "depth");
	if (v != NULL && v->sn_type == SH_INT && v->sn_int > 0)
		p->up_depth = (unsigned long)v->sn_int;
	else if (v != NULL && v->sn_type == SH_STR && w != NULL)
		sh_errf(w, "update.depth: an interval is not read yet; every "
		    "save is replayed");
	(void) snprintf(p->up_stagename, sizeof (p->up_stagename), "%s-%s",
	    p->up_name, suffix);
	(void) snprintf(p->up_run, sizeof (p->up_run), "%s/containers/%s",
	    s->ss_root[SH_SP_RUN], p->up_name);
	(void) snprintf(p->up_stage, sizeof (p->up_stage), "%s/containers/%s",
	    s->ss_root[SH_SP_RUN], p->up_stagename);
	(void) snprintf(p->up_config, sizeof (p->up_config), "%s",
	    s->ss_root[SH_SP_CONFIG]);
	(void) snprintf(p->up_jailfile, sizeof (p->up_jailfile), "%s/%s.conf",
	    jaildir, p->up_name);
	(void) snprintf(p->up_stagejail, sizeof (p->up_stagejail),
	    "%s/%s.conf", jaildir, p->up_stagename);
	(void) snprintf(p->up_rundir, sizeof (p->up_rundir), "%s/%s", rundir,
	    p->up_name);
	if ((r = sh_zfs_get(z, p->up_run, "origin", val, sizeof (val), NULL,
	    e)) != 0) {
		if (r == 1)
			sh_errf(e, "no container %s", p->up_name);
		return (-1);
	}
	(void) snprintf(rpath, sizeof (rpath), "containers/%s", p->up_name);
	if (sh_recipe_resolve(root, rpath, &p->up_recipe, e, w) != 0)
		return (-1);
	/* X's foundation and version, from its origin */
	(void) snprintf(lakef, sizeof (lakef), "%s/foundations/",
	    s->ss_root[SH_SP_RUN]);
	rl = strlen(lakef);
	if (strncmp(val, lakef, rl) != 0 || (at = strchr(val, '@')) == NULL ||
	    ((slash = strchr(val + rl, '/')) != NULL && slash < at)) {
		sh_errf(e, "container %s is no clone of a foundation's version",
		    p->up_name);
		return (-1);
	}
	(void) snprintf(p->up_found, sizeof (p->up_found), "%.*s",
	    (int)(at - val - rl), val + rl);
	(void) snprintf(p->up_old, sizeof (p->up_old), "%s", at + 1);
	(void) snprintf(p->up_runf, sizeof (p->up_runf), "%s/foundations/%s",
	    s->ss_root[SH_SP_RUN], p->up_found);
	(void) snprintf(lakef, sizeof (lakef), "%s/foundations/%s",
	    s->ss_root[SH_SP_LAKE], p->up_found);
	if ((r = sh_update_state(z, p->up_run, p->up_stage, p->up_version,
	    sizeof (p->up_version), from, sizeof (from), e)) < 0)
		return (-1);
	if (r == SH_US_DONE)
		sh_errf(e, "the update of %s is done: shtetl cutover %s, or "
		    "abandon %s", p->up_name, p->up_name, p->up_name);
	else if (verb == SH_UV_NEW && r == SH_US_UNDERWAY)
		sh_errf(e, "an update of %s is under way: shtetl update "
		    "--continue %s, or --abort %s", p->up_name, p->up_name,
		    p->up_name);
	else if (verb != SH_UV_NEW && r == SH_US_NONE)
		sh_errf(e, "no update of %s to %s", p->up_name,
		    verb == SH_UV_CONTINUE ? "take up" : "give up");
	if (e->se_count != before)
		return (-1);
	if (uq_datasets(z, p, e) != 0)
		return (-1);
	if (verb != SH_UV_NEW)
		return (uq_marked(z, p, e));
	for (i = 0; i < p->up_nds; i++) {
		(void) snprintf(rec, sizeof (rec), "%s%s%s", p->up_run,
		    i > 0 ? "/" : "", p->up_dss[i].ud_rel);
		if (sh_zfs_get(z, rec, UQ_PHASE, val, sizeof (val), NULL,
		    e) == 0 && strcmp(val, "-") != 0)
			sh_errf(e, "%s: an open rebase", rec);
	}
	if (e->se_count != before || uq_target(z, p, lakef, version, e) != 0)
		return (-1);
	(void) snprintf(rec, sizeof (rec), "%s@%s", p->up_runf,
	    p->up_version);
	if ((r = sh_zfs_get(z, rec, "type", val, sizeof (val), NULL, e)) < 0)
		return (-1);
	p->up_beam = r == 1;
	if (p->up_beam && sh_beam_plan(z, s, p->up_found, p->up_version,
	    &p->up_beamplan, e) != 0)
		return (-1);
	if (uq_saves(z, p, e) != 0)
		return (-1);
	return (e->se_count == before ? 0 : -1);
}

static const char *const uq_hows[] = { "replayed", "cloned from the target",
	"cloned from the from-save" };

void
sh_update_show(FILE *out, const struct sh_uplan *p)
{
	const struct sh_uds *d;
	size_t i;

	(void) fprintf(out, "container %s, from %s@%s to @%s%s\n", p->up_name,
	    p->up_found, p->up_old, p->up_version, p->up_beam ?
	    ", beamed to zrun first" : "");
	(void) fprintf(out, "  %s stopped; a save of it, \"update to "
	    "%s@%s\"\n", p->up_name, p->up_found, p->up_version);
	for (i = 0; i < p->up_nds; i++) {
		d = &p->up_dss[i];
		(void) fprintf(out, "  + %s%s%s  %s%s\n", p->up_stage,
		    i > 0 ? "/" : "", d->ud_rel, uq_hows[d->ud_how],
		    d->ud_placeholder ? ", canmount=off" : "");
	}
	(void) fprintf(out, "  saves replayed:");
	for (i = 0; i < p->up_nsaves; i++)
		(void) fprintf(out, " %s", p->up_saves[i]);
	(void) fprintf(out, "%sthe from-save\n", p->up_nsaves > 0 ? ", then " :
	    " ");
	(void) fprintf(out, "  %s started as %s, saved\n", p->up_stagename,
	    p->up_name);
}

/* ---- the run ------------------------------------------------------- */

struct uq {
	struct sh_zfs		*c_z;
	struct sh_uplan		*c_p;
	const struct sh_spaces	*c_s;
	const struct sh_tools	*c_t;
	const struct sh_rebaser	*c_rb;
	FILE			*c_out;
	struct sh_err		*c_err;
	time_t			c_now;
	int			c_tty;
	char			c_log[1100];
	int			c_logfd;
	char			c_pause[2048];	/* what a pause says */
	int			c_marked;	/* the from-save is marked */
};

/* WORDS run, ENV their environment (NULL: shtetl's), to the log. */
static int
uq_exec(struct uq *c, const char *const *words, const char *const *env,
    int quiet)
{
	extern char **environ;
	struct sh_err q, *e;
	struct sh_run r;
	char **argv, **envp;
	size_t i, n, m;
	FILE *null;
	int rc;

	for (n = 0; words[n] != NULL; n++)
		;
	for (m = 0; env != NULL && env[m] != NULL; m++)
		;
	argv = calloc(n + 1, sizeof (*argv));
	envp = calloc(m + 1, sizeof (*envp));
	rc = -1;
	null = NULL;
	if (argv == NULL || envp == NULL)
		goto nomem;
	for (i = 0; i < n; i++) {
		if ((argv[i] = strdup(words[i])) == NULL)
			goto nomem;
	}
	for (i = 0; i < m; i++) {
		if ((envp[i] = strdup(env[i])) == NULL)
			goto nomem;
	}
	e = c->c_err;
	if (quiet) {
		if ((null = fopen("/dev/null", "w")) == NULL)
			goto nomem;
		sh_err_init(&q, null, "");
		e = &q;
	}
	(void) memset(&r, 0, sizeof (r));
	r.ru_argv = argv;
	r.ru_env = env != NULL ? envp : environ;
	r.ru_log = c->c_logfd;
	rc = sh_run(&r, e) == 0 ? 0 : -1;
	free(r.ru_out);
	goto out;
nomem:
	sh_err_nomem(c->c_err);
out:
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

/* DIR made, and its parents. */
static int
uq_mkdirs(struct uq *c, const char *dir)
{
	char buf[1100], ch;
	size_t i;

	(void) snprintf(buf, sizeof (buf), "%s", dir);
	for (i = 1; ; i++) {
		if (buf[i] != '/' && buf[i] != '\0')
			continue;
		ch = buf[i];
		buf[i] = '\0';
		if (mkdir(buf, 0755) != 0 && errno != EEXIST) {
			sh_errf(c->c_err, "%s: %s", buf, strerror(errno));
			return (-1);
		}
		buf[i] = ch;
		if (ch == '\0')
			return (0);
	}
}

/* Whether the jail NAME runs (jls -j NAME). */
static int
uq_running(struct uq *c, const char *name)
{
	const char *w[5];

	w[0] = c->c_t->tl_jls;
	w[1] = "-j";
	w[2] = name;
	w[3] = "jid";
	w[4] = NULL;
	return (uq_exec(c, w, NULL, 1) == 0);
}

/* The jail NAME, of the block FILE, started (VERB -c) or stopped (-r). */
static int
uq_jail(struct uq *c, const char *file, const char *name, const char *verb)
{
	const char *w[6];

	w[0] = c->c_t->tl_jail;
	w[1] = "-f";
	w[2] = file;
	w[3] = verb;
	w[4] = name;
	w[5] = NULL;
	if (uq_exec(c, w, NULL, 0) != 0)
		return (-1);
	(void) fprintf(c->c_out, "%s the jail %s\n", verb[1] == 'c' ?
	    "started" : "stopped", name);
	return (0);
}

/* The full name of P's dataset I below BASE. */
static void
uq_ds(const struct sh_uplan *p, const char *base, size_t i, char *buf,
    size_t len)
{
	(void) snprintf(buf, len, "%s%s%s", base, i > 0 ? "/" : "",
	    p->up_dss[i].ud_rel);
}

/* Whether NAME, a dataset or a snapshot, is there. */
static int
uq_there(struct uq *c, const char *name)
{
	char val[64];

	return (sh_zfs_get(c->c_z, name, "type", val, sizeof (val), NULL,
	    c->c_err) == 0);
}

/* Whether DS carries the tool's record: an open rebase. */
static int
uq_open(struct uq *c, const char *ds)
{
	char val[SH_ZFS_VALMAX];

	return (sh_zfs_get(c->c_z, ds, UQ_PHASE, val, sizeof (val), NULL,
	    c->c_err) == 0 && strcmp(val, "-") != 0);
}

/* The rebases' documents gone, the log kept: the update has ended. */
static void
uq_tidy(struct uq *c)
{
	struct sh_uplan *p = c->c_p;
	char path[1100];
	size_t i, k;

	for (i = 0; i < p->up_nds; i++) {
		for (k = 1; k <= p->up_nsaves; k++) {
			(void) snprintf(path, sizeof (path),
			    "%s/rebase/d%zu.%zu.manifest", p->up_rundir, i, k);
			(void) unlink(path);
			(void) strncat(path, ".resolution",
			    sizeof (path) - strlen(path) - 1);
			(void) unlink(path);
			(void) snprintf(path, sizeof (path),
			    "%s/rebase/d%zu.%zu.derive", p->up_rundir, i, k);
			(void) unlink(path);
		}
	}
	(void) snprintf(path, sizeof (path), "%s/rebase", p->up_rundir);
	(void) rmdir(path);
}

/* The config root snapshotted, as a build does: update-X-<time>. */
static int
uq_config(struct uq *c)
{
	struct sh_uplan *p = c->c_p;
	struct sh_zprop kind = { "shtetl:kind", "config" };
	char stamp[32];
	const char *one;
	struct tm tm;
	int n;

	(void) gmtime_r(&c->c_now, &tm);
	(void) strftime(stamp, sizeof (stamp), "%Y%m%d-%H%M%S", &tm);
	(void) snprintf(p->up_cfgsnap, sizeof (p->up_cfgsnap),
	    "%s@update-%s-%s", p->up_config, p->up_name, stamp);
	for (n = 2; n < 100 && uq_there(c, p->up_cfgsnap); n++)
		(void) snprintf(p->up_cfgsnap, sizeof (p->up_cfgsnap),
		    "%s@update-%s-%s-%d", p->up_config, p->up_name, stamp, n);
	one = p->up_cfgsnap;
	if (sh_zfs_snapshot(c->c_z, &one, 1, &kind, 1, c->c_err) != 0)
		return (-1);
	(void) fprintf(c->c_out, "snapshot %s\n", p->up_cfgsnap);
	return (0);
}

/* A save of NAME (X or X-staging), its config CONFIG; its name to SNAP. */
static int
uq_save(struct uq *c, const char *name, const char *config, char *snap,
    size_t len)
{
	struct sh_uplan *p = c->c_p;
	struct sh_save *sp;
	char msg[SH_UPDATE_NAME * 3];
	int r;

	if ((sp = calloc(1, sizeof (*sp))) == NULL) {
		sh_err_nomem(c->c_err);
		return (-1);
	}
	(void) snprintf(msg, sizeof (msg), "update to %s@%s", p->up_found,
	    p->up_version);
	snap[0] = '\0';
	r = sh_save_plan(c->c_z, c->c_s, name, config, sp, c->c_err) != 0 ||
	    sh_save_run(c->c_z, sp, msg, c->c_now, snap, len, c->c_out,
	    c->c_err) != 0 ? -1 : 0;
	free(sp);
	return (r);
}

/* F's datasets the replays read, mounted (UP) or unmounted. */
static int
uq_mountf(struct uq *c, int up)
{
	struct sh_uplan *p = c->c_p;
	char ds[SH_UPDATE_NAME * 2];
	size_t i, k;

	for (k = 0; k < p->up_nds; k++) {
		i = up ? k : p->up_nds - 1 - k;
		if (p->up_dss[i].ud_how != SH_UH_REPLAY)
			continue;
		uq_ds(p, p->up_runf, i, ds, sizeof (ds));
		if (up && sh_zfs_mount(c->c_z, ds, c->c_err) != 0)
			return (-1);
		if (!up)
			(void) sh_zfs_unmount(c->c_z, ds, c->c_err);
	}
	return (0);
}

/* X/d@SAVE's shtetl:* on STAGE/d@SAVE, and shtetl:replayed-from. */
static int
uq_snapprops(struct uq *c, const char *xsnap, const char *ssnap)
{
	char guid[64];

	if (sh_shtetl_copy(c->c_z, xsnap, ssnap, c->c_err) != 0 ||
	    sh_zfs_get(c->c_z, xsnap, "guid", guid, sizeof (guid), NULL,
	    c->c_err) != 0 ||
	    sh_zfs_set(c->c_z, ssnap, "shtetl:replayed-from", guid,
	    c->c_err) != 0)
		return (-1);
	return (0);
}

/* The clone a first step made placed: X-staging's mountpoint, writable. */
static int
uq_place(struct uq *c, const char *ds)
{
	char mp[SH_ZFS_VALMAX];
	enum sh_zsrc src;

	if (sh_zfs_get(c->c_z, ds, "mountpoint", mp, sizeof (mp), &src,
	    c->c_err) != 0)
		return (-1);
	if (src == SH_ZSRC_LOCAL && sh_zfs_inherit(c->c_z, ds, "mountpoint",
	    c->c_err) != 0)
		return (-1);
	if (sh_zfs_set(c->c_z, ds, "readonly", "off", c->c_err) != 0 ||
	    sh_zfs_mount(c->c_z, ds, c->c_err) != 0)
		return (-1);
	return (0);
}

/* The recipe's properties for dataset I, set locally on DS. */
static int
uq_props(struct uq *c, size_t i, const char *ds)
{
	const struct sh_node *props = c->c_p->up_dss[i].ud_props;
	const struct sh_pair *pr;
	size_t j;

	for (j = 0; props != NULL && j < props->sn_obj.ob_n; j++) {
		pr = &props->sn_obj.ob_v[j];
		if (sh_zfs_set(c->c_z, ds, pr->pr_key,
		    pr->pr_val->sn_type == SH_BOOL ? (pr->pr_val->sn_bool ?
		    "on" : "off") : pr->pr_val->sn_str, c->c_err) != 0)
			return (-1);
	}
	return (0);
}

/* The manifest of dataset I's step K, beside the record. */
static void
uq_manifest(struct uq *c, size_t i, size_t k, char *buf, size_t len)
{
	(void) snprintf(buf, len, "%s/rebase/d%zu.%zu.manifest",
	    c->c_p->up_rundir, i, k);
}

/* One derivation of X's recipe, as one dataset's tree spells it. */
struct uq_deriv {
	char		dv_path[SH_UPDATE_NAME * 2];	/* /etc/pwd.db */
	char		dv_source[SH_UPDATE_NAME * 2];	/* /etc/master.passwd */
	const char	*dv_command;
};

/* The dataset of P that holds PATH, X-relative: the longest prefix. */
static size_t
uq_owner(const struct sh_uplan *p, const char *path)
{
	size_t i, best, len, blen;

	best = 0;
	blen = 0;
	for (i = 1; i < p->up_nds; i++) {
		len = strlen(p->up_dss[i].ud_rel);
		if (len > blen &&
		    strncmp(path, p->up_dss[i].ud_rel, len) == 0 &&
		    path[len] == '/') {
			best = i;
			blen = len;
		}
	}
	return (best);
}

/* PATH, X-relative, as dataset I's tree spells it, into BUF. */
static void
uq_treepath(const struct sh_uplan *p, size_t i, const char *path,
    char *buf, size_t len)
{
	size_t rl = strlen(p->up_dss[i].ud_rel);

	(void) snprintf(buf, len, "/%s", rl > 0 ? path + rl + 1 : path);
}

/*
 * The derivations of X's recipe whose derived path is in dataset I,
 * into D: how many. One whose source is in another dataset spans two
 * rebases (7.7's open detail), and is left out, with a warning.
 */
static size_t
uq_derivs(struct uq *c, size_t i, struct uq_deriv *d, size_t max)
{
	const struct sh_uplan *p = c->c_p;
	const struct sh_node *all, *src, *cmd;
	const char *path;
	size_t j, n;

	all = sh_obj_get(p->up_recipe.sr_merged, "derivations");
	for (j = n = 0; all != NULL && j < all->sn_obj.ob_n && n < max; j++) {
		path = all->sn_obj.ob_v[j].pr_key;
		src = sh_obj_get(all->sn_obj.ob_v[j].pr_val, "source");
		cmd = sh_obj_get(all->sn_obj.ob_v[j].pr_val, "command");
		if (src == NULL || cmd == NULL || uq_owner(p, path) != i)
			continue;
		if (uq_owner(p, src->sn_str) != i) {
			(void) fprintf(c->c_out, "warning: the derivation of "
			    "%s has its source in another dataset: not run\n",
			    path);
			continue;
		}
		uq_treepath(p, i, path, d[n].dv_path, sizeof (d[n].dv_path));
		uq_treepath(p, i, src->sn_str, d[n].dv_source,
		    sizeof (d[n].dv_source));
		d[n++].dv_command = cmd->sn_str;
	}
	return (n);
}

static const char *const uq_env[] = { "PATH=" SH_RUN_PATH, "HOME=/root",
	NULL };

/*
 * The derivations D (N) that RES notes, the derived path a line of it,
 * whose source is FROM's (WANT 1: answered from) or not (WANT 0), run
 * in a chroot of DIR, each distinct command once. 0, or -1 said.
 */
static int
uq_derive(struct uq *c, const struct sh_resolution *res,
    const struct uq_deriv *d, size_t n, int want, const char *dir)
{
	const struct sh_resline *l;
	const char *w[7], *done[32];
	size_t j, k, ndone;
	int from;

	ndone = 0;
	for (j = 0; j < n; j++) {
		if (sh_res_find(res, d[j].dv_path) == NULL)
			continue;
		l = sh_res_find(res, d[j].dv_source);
		from = l != NULL && strcmp(l->rl_choice, "from") == 0;
		if (from != want)
			continue;
		for (k = 0; k < ndone; k++) {
			if (strcmp(done[k], d[j].dv_command) == 0)
				break;
		}
		if (k < ndone)
			continue;
		w[0] = c->c_t->tl_chroot;
		w[1] = dir;
		w[2] = "/bin/sh";
		w[3] = "-e";
		w[4] = "-c";
		w[5] = d[j].dv_command;
		w[6] = NULL;
		(void) fprintf(c->c_out, "derived %s, in %s\n", d[j].dv_path,
		    dir);
		if (uq_exec(c, w, uq_env, 0) != 0) {
			(void) snprintf(c->c_pause, sizeof (c->c_pause),
			    "the derivation of %s failed (log %s): shtetl "
			    "update --continue %s runs it again",
			    d[j].dv_path, c->c_log, c->c_p->up_name);
			return (-1);
		}
		if (ndone < 32)
			done[ndone++] = d[j].dv_command;
	}
	return (0);
}

/* The marker of dataset I's step K: derivations owed after its done. */
static void
uq_owed(struct uq *c, size_t i, size_t k, char *buf, size_t len)
{
	(void) snprintf(buf, len, "%s/rebase/d%zu.%zu.derive",
	    c->c_p->up_rundir, i, k);
}

/*
 * The derivations of a source answered from (7.7 step 5), owed by
 * dataset I's step K once done, run in a chroot of DS where it is
 * placed; the marker removed. 0, or 1 paused.
 */
static int
uq_owing(struct uq *c, size_t i, size_t k, const char *ds)
{
	struct uq_deriv d[32];
	struct sh_resolution res;
	char man[1100], path[1200], mark[1200], mp[SH_ZFS_VALMAX];
	size_t n;
	int r;

	uq_owed(c, i, k, mark, sizeof (mark));
	if (access(mark, F_OK) != 0)
		return (0);
	uq_manifest(c, i, k, man, sizeof (man));
	(void) snprintf(path, sizeof (path), "%s.resolution", man);
	(void) memset(&res, 0, sizeof (res));
	if (uq_place(c, ds) != 0 || sh_zfs_get(c->c_z, ds, "mountpoint", mp,
	    sizeof (mp), NULL, c->c_err) != 0 ||
	    sh_res_read(path, &res, c->c_err) != 0) {
		sh_res_free(&res);
		return (-1);
	}
	n = uq_derivs(c, i, d, 32);
	r = uq_derive(c, &res, d, n, 1, mp);
	sh_res_free(&res);
	if (r != 0)
		return (1);
	(void) unlink(mark);
	return (0);
}

/*
 * At a gate of dataset I, step K, its rebase IDENT (7.7): each
 * conflicted derived path answered keep; the picker on a terminal while
 * names are unanswered; the document complete, the derivations whose
 * source is in the tree run in the private mount, those of a source
 * answered from owed; then --continue. 0 the step done; 1 paused; -1.
 */
static int
uq_gate(struct uq *c, size_t i, size_t k, const char *ident)
{
	struct sh_uplan *p = c->c_p;
	struct sh_resolution res;
	struct uq_deriv d[32];
	const char *paths[32];
	char man[1100], path[1200], mark[1200], priv[1100];
	unsigned left;
	size_t n, j;
	int rc, owed;
	FILE *f;

	uq_manifest(c, i, k, man, sizeof (man));
	(void) snprintf(path, sizeof (path), "%s.resolution", man);
	(void) memset(&res, 0, sizeof (res));
	if (sh_res_read(path, &res, c->c_err) != 0) {
		sh_res_free(&res);
		return (-1);
	}
	/* the text first: a derived file kept, made again from it */
	n = uq_derivs(c, i, d, 32);
	for (j = 0; j < n; j++)
		paths[j] = d[j].dv_path;
	if (sh_res_keep(&res, paths, n) > 0 &&
	    sh_res_write(path, &res, c->c_err) != 0) {
		sh_res_free(&res);
		return (-1);
	}
	left = sh_res_unanswered(&res);
	sh_res_free(&res);
	if (left > 0 && c->c_tty) {
		(void) fflush(c->c_out);
		if (c->c_rb->rb_continue(c->c_rb, ident, SH_RB_NOMERGE |
		    SH_RB_PICK, c->c_logfd, c->c_err) < 0)
			return (-1);
		if (sh_res_read(path, &res, c->c_err) != 0) {
			sh_res_free(&res);
			return (-1);
		}
		left = sh_res_unanswered(&res);
		sh_res_free(&res);
	}
	if (left > 0) {
		(void) snprintf(c->c_pause, sizeof (c->c_pause), "%s stopped "
		    "at conflicts, step %zu of %zu, %u name%s unanswered: "
		    "answer %s, then shtetl update --continue %s (on a "
		    "terminal it opens the picker)", ident, k, p->up_nsaves,
		    left, left == 1 ? "" : "s", path, p->up_name);
		return (1);
	}
	if (sh_res_read(path, &res, c->c_err) != 0) {
		sh_res_free(&res);
		return (-1);
	}
	sh_rb_privmnt(ident, priv, sizeof (priv));
	rc = uq_derive(c, &res, d, n, 0, priv);
	owed = 0;
	for (j = 0; rc == 0 && j < n; j++) {
		const struct sh_resline *l = sh_res_find(&res,
		    d[j].dv_source);

		if (sh_res_find(&res, d[j].dv_path) != NULL && l != NULL &&
		    strcmp(l->rl_choice, "from") == 0)
			owed = 1;
	}
	sh_res_free(&res);
	if (rc != 0)
		return (1);
	if (owed) {
		uq_owed(c, i, k, mark, sizeof (mark));
		if ((f = fopen(mark, "w")) == NULL) {
			sh_errf(c->c_err, "%s: %s", mark, strerror(errno));
			return (-1);
		}
		(void) fclose(f);
	}
	rc = c->c_rb->rb_continue(c->c_rb, ident, 0, c->c_logfd, c->c_err);
	if (rc == SH_RB_DONE)
		return (uq_owing(c, i, k, ident));
	if (rc == SH_RB_GATE) {
		(void) snprintf(c->c_pause, sizeof (c->c_pause), "%s at its "
		    "conflicts gate again, step %zu of %zu: see %s, then "
		    "shtetl update --continue %s", ident, k, p->up_nsaves, path,
		    p->up_name);
		return (1);
	}
	sh_errf(c->c_err, "%s: zfs_rebase --continue %s", ident,
	    sh_rb_said(rc));
	return (-1);
}

/*
 * Dataset I replayed, as far as it goes: the step it stands at read
 * back from ZFS, each step started or taken up. 0 done; 1 paused; -1.
 */
static int
uq_replay(struct uq *c, size_t i)
{
	struct sh_uplan *p = c->c_p;
	char stage[SH_UPDATE_NAME * 2], xds[SH_UPDATE_NAME * 2];
	char fds[SH_UPDATE_NAME * 2], from[SH_UPDATE_NAME * 3];
	char onto[SH_UPDATE_NAME * 3], base[SH_UPDATE_NAME * 3];
	char snap[SH_UPDATE_NAME * 3], man[1100], res[1200];
	struct sh_rbstart st;
	const char *one;
	size_t n, m, k;
	int rc;

	n = p->up_nsaves;
	uq_ds(p, p->up_stage, i, stage, sizeof (stage));
	uq_ds(p, p->up_run, i, xds, sizeof (xds));
	uq_ds(p, p->up_runf, i, fds, sizeof (fds));
	for (;;) {
		/* derivations a step done still owes, first */
		for (k = 1; k <= n && uq_there(c, stage) && !uq_open(c, stage);
		    k++) {
			if ((rc = uq_owing(c, i, k, stage)) != 0)
				return (rc);
		}
		/* the saves the staging dataset holds, from the first */
		for (m = 0; m < n; m++) {
			(void) snprintf(snap, sizeof (snap), "%s@%s", stage,
			    p->up_saves[m]);
			if (!uq_there(c, snap))
				break;
		}
		if (uq_there(c, stage) && uq_open(c, stage)) {
			/* a step open at its gate: m + 1 */
			if ((rc = uq_gate(c, i, m + 1, stage)) != 0)
				return (rc);
			continue;
		}
		if (m == n) {
			if (uq_props(c, i, stage) != 0)
				return (-1);
			return (0);
		}
		if (uq_there(c, stage)) {
			/* step m + 1 done: its snapshot, or the next step */
			if (m == 0 && uq_place(c, stage) != 0)
				return (-1);
			if (m > 0) {
				(void) snprintf(from, sizeof (from), "%s@%s",
				    xds, p->up_saves[m - 1]);
				(void) snprintf(snap, sizeof (snap), "%s@%s",
				    stage, p->up_saves[m - 1]);
				if (uq_snapprops(c, from, snap) != 0)
					return (-1);
			}
			k = m + 1;
			if (k == n) {
				(void) snprintf(snap, sizeof (snap), "%s@%s",
				    stage, p->up_saves[n - 1]);
				one = snap;
				(void) snprintf(from, sizeof (from), "%s@%s",
				    xds, p->up_saves[n - 1]);
				if (sh_zfs_snapshot(c->c_z, &one, 1, NULL, 0,
				    c->c_err) != 0 ||
				    uq_snapprops(c, from, snap) != 0)
					return (-1);
				(void) fprintf(c->c_out, "replayed %s, %zu "
				    "save%s\n", stage, n, n == 1 ? "" : "s");
				continue;
			}
			k = m + 2;
		} else {
			k = 1;
		}
		/* step k: the clone form first, the dataset form after */
		(void) snprintf(from, sizeof (from), "%s@%s", xds,
		    p->up_saves[k - 1]);
		(void) memset(&st, 0, sizeof (st));
		st.rs_from = from;
		if (k == 1) {
			(void) snprintf(onto, sizeof (onto), "%s@%s", fds,
			    p->up_version);
			st.rs_onto = onto;
			st.rs_result = stage;
		} else {
			(void) snprintf(base, sizeof (base), "%s@%s", xds,
			    p->up_saves[k - 2]);
			st.rs_onto = stage;
			st.rs_result = p->up_saves[k - 2];
			st.rs_base = base;
		}
		uq_manifest(c, i, k, man, sizeof (man));
		(void) snprintf(res, sizeof (res), "%s.resolution", man);
		(void) unlink(man);
		(void) unlink(res);
		st.rs_manifest = man;
		(void) fprintf(c->c_out, "rebase %s, step %zu of %zu: %s onto "
		    "%s\n", stage, k, n, from, st.rs_onto);
		rc = c->c_rb->rb_start(c->c_rb, &st, c->c_logfd, c->c_err);
		if (rc == SH_RB_DONE)
			continue;
		if (rc == SH_RB_GATE) {
			if ((rc = uq_gate(c, i, k, stage)) != 0)
				return (rc);
			continue;
		}
		sh_errf(c->c_err, "%s: zfs_rebase %s (log %s)", stage,
		    sh_rb_said(rc), c->c_log);
		return (-1);
	}
}

/* Dataset I cloned: F's placeholder from the target, X's own from-save. */
static int
uq_clone(struct uq *c, size_t i)
{
	struct sh_uplan *p = c->c_p;
	const struct sh_uds *d = &p->up_dss[i];
	const struct sh_node *props = d->ud_props;
	struct sh_zprop zp[33];
	char stage[SH_UPDATE_NAME * 2], origin[SH_UPDATE_NAME * 3];
	size_t n, j;

	uq_ds(p, p->up_stage, i, stage, sizeof (stage));
	if (uq_there(c, stage))
		return (0);
	if (d->ud_how == SH_UH_TARGET) {
		uq_ds(p, p->up_runf, i, origin, sizeof (origin));
		(void) snprintf(origin + strlen(origin),
		    sizeof (origin) - strlen(origin), "@%s", p->up_version);
	} else {
		uq_ds(p, p->up_run, i, origin, sizeof (origin));
		(void) snprintf(origin + strlen(origin),
		    sizeof (origin) - strlen(origin), "@%s",
		    p->up_saves[p->up_nsaves - 1]);
	}
	n = 0;
	if (d->ud_placeholder) {
		zp[n].zp_name = "canmount";
		zp[n++].zp_value = "off";
	}
	for (j = 0; props != NULL && j < props->sn_obj.ob_n && n < 33; j++) {
		zp[n].zp_name = props->sn_obj.ob_v[j].pr_key;
		zp[n++].zp_value = props->sn_obj.ob_v[j].pr_val->sn_type ==
		    SH_BOOL ? (props->sn_obj.ob_v[j].pr_val->sn_bool ? "on" :
		    "off") : props->sn_obj.ob_v[j].pr_val->sn_str;
	}
	if (sh_zfs_clone(c->c_z, origin, stage, zp, n, c->c_err) != 0)
		return (-1);
	(void) fprintf(c->c_out, "cloned %s from %s\n", stage, origin);
	if (!d->ud_placeholder && sh_zfs_mount(c->c_z, stage, c->c_err) != 0)
		return (-1);
	return (0);
}

/*
 * Given up: every open rebase aborted, the staging tree's jail stopped
 * and its block removed, the staging tree destroyed, F unmounted, X
 * started again, the record gone. X's from-save and the config
 * snapshot stay.
 */
static void
uq_undo(struct uq *c)
{
	struct sh_uplan *p = c->c_p;
	char ds[SH_UPDATE_NAME * 2];
	size_t i;

	for (i = p->up_nds; i > 0; i--) {
		uq_ds(p, p->up_stage, i - 1, ds, sizeof (ds));
		if (uq_there(c, ds) && uq_open(c, ds) &&
		    c->c_rb->rb_abort(c->c_rb, ds, c->c_logfd, c->c_err) !=
		    SH_RB_DONE)
			sh_errf(c->c_err, "%s: zfs_rebase --abort failed", ds);
	}
	if (uq_running(c, p->up_stagename))
		(void) uq_jail(c, p->up_stagejail, p->up_stagename, "-r");
	if (unlink(p->up_stagejail) != 0 && errno != ENOENT)
		sh_errf(c->c_err, "%s: %s", p->up_stagejail, strerror(errno));
	for (i = p->up_nds; i > 0; i--) {
		uq_ds(p, p->up_stage, i - 1, ds, sizeof (ds));
		if (uq_there(c, ds))
			(void) sh_zfs_unmount(c->c_z, ds, c->c_err);
	}
	if (uq_there(c, p->up_stage) && sh_zw_destroy(c->c_z, p->up_stage,
	    c->c_err) == 0)
		(void) fprintf(c->c_out, "destroyed %s\n", p->up_stage);
	(void) uq_mountf(c, 0);
	/* the from-save a save like any, no longer an update's */
	if (c->c_marked && p->up_nsaves > 0) {
		(void) snprintf(ds, sizeof (ds), "%s@%s", p->up_run,
		    p->up_saves[p->up_nsaves - 1]);
		if (uq_there(c, ds))
			(void) sh_zfs_inherit(c->c_z, ds, SH_UPDATE_MARK,
			    c->c_err);
	}
	if (!uq_running(c, p->up_name) && access(p->up_jailfile, F_OK) == 0)
		(void) uq_jail(c, p->up_jailfile, p->up_name, "-c");
	uq_tidy(c);
}

/* After the replays: F away, X-staging as the version, started, saved. */
static int
uq_finish(struct uq *c)
{
	struct sh_uplan *p = c->c_p;
	struct sh_zprop zp[4];
	char names[SH_UPDATE_DS][SH_UPDATE_NAME * 3], ds[SH_UPDATE_NAME * 2];
	char mp[SH_ZFS_VALMAX], msg[SH_UPDATE_NAME * 3], guid[64], snap[256];
	const char *np[SH_UPDATE_DS];
	size_t i;

	(void) uq_mountf(c, 0);
	(void) snprintf(names[0], sizeof (names[0]), "%s@%s", p->up_stage,
	    p->up_version);
	if (!uq_there(c, names[0])) {
		if (uq_config(c) != 0)
			return (-1);
		for (i = 0; i < p->up_nds; i++) {
			uq_ds(p, p->up_stage, i, ds, sizeof (ds));
			(void) snprintf(names[i], sizeof (names[i]), "%s@%s",
			    ds, p->up_version);
			np[i] = names[i];
		}
		(void) snprintf(msg, sizeof (msg), "update to %s@%s",
		    p->up_found, p->up_version);
		if (sh_zfs_get(c->c_z, p->up_cfgsnap, "guid", guid,
		    sizeof (guid), NULL, c->c_err) != 0)
			return (-1);
		zp[0].zp_name = "shtetl:kind";
		zp[0].zp_value = "version";
		zp[1].zp_name = "shtetl:commit";
		zp[1].zp_value = msg;
		zp[2].zp_name = "shtetl:config";
		zp[2].zp_value = p->up_cfgsnap;
		zp[3].zp_name = "shtetl:config-guid";
		zp[3].zp_value = guid;
		if (sh_zfs_snapshot(c->c_z, np, p->up_nds, zp, 4,
		    c->c_err) != 0)
			return (-1);
		(void) fprintf(c->c_out, "snapshot %s@%s\n", p->up_stage,
		    p->up_version);
	}
	if (sh_zfs_get(c->c_z, p->up_stage, "mountpoint", mp, sizeof (mp),
	    NULL, c->c_err) != 0)
		return (-1);
	if (sh_jail_write(p->up_stagejail, p->up_stagename, p->up_name, mp,
	    p->up_recipe.sr_merged, c->c_err) != 0)
		return (-1);
	(void) fprintf(c->c_out, "wrote %s\n", p->up_stagejail);
	if (!uq_running(c, p->up_stagename) && uq_jail(c, p->up_stagejail,
	    p->up_stagename, "-c") != 0)
		return (-1);
	if (uq_save(c, p->up_stagename, p->up_cfgsnap, snap, sizeof (snap)) !=
	    0)
		return (-1);
	return (0);
}

int
sh_update_run(struct sh_zfs *z, struct sh_uplan *p,
    const struct sh_spaces *s, const struct sh_tools *t,
    const struct sh_rebaser *rb, int tty, time_t now, FILE *out,
    struct sh_err *e)
{
	struct uq c;
	char dir[1100], snap[256];
	size_t i;
	int r;

	(void) memset(&c, 0, sizeof (c));
	c.c_z = z;
	c.c_p = p;
	c.c_s = s;
	c.c_t = t;
	c.c_rb = rb;
	c.c_out = out;
	c.c_err = e;
	c.c_now = now;
	c.c_tty = tty;
	c.c_marked = p->up_verb != SH_UV_NEW;
	(void) snprintf(dir, sizeof (dir), "%s/rebase", p->up_rundir);
	if (uq_mkdirs(&c, dir) != 0)
		return (-1);
	(void) snprintf(c.c_log, sizeof (c.c_log), "%s/update.log",
	    p->up_rundir);
	if ((c.c_logfd = open(c.c_log, O_WRONLY | O_CREAT | O_APPEND |
	    O_CLOEXEC, 0644)) == -1) {
		sh_errf(e, "%s: %s", c.c_log, strerror(errno));
		return (-1);
	}
	if (p->up_verb == SH_UV_ABORT) {
		uq_undo(&c);
		(void) fprintf(out, "gave up the update of %s\n", p->up_name);
		(void) close(c.c_logfd);
		return (e->se_count == 0 ? 0 : -1);
	}
	if (p->up_verb == SH_UV_NEW) {
		if ((p->up_beam && sh_beam_run(z, &p->up_beamplan, out,
		    e) != 0) ||
		    (uq_running(&c, p->up_name) && uq_jail(&c, p->up_jailfile,
		    p->up_name, "-r") != 0) ||
		    uq_save(&c, p->up_name, NULL, snap, sizeof (snap)) != 0)
			goto fail;
		(void) snprintf(p->up_saves[p->up_nsaves++], SH_UPDATE_NAME,
		    "%s", snap);
		/* the from-save marked: an update's, and its target */
		(void) snprintf(dir, sizeof (dir), "%s@%s", p->up_run, snap);
		if (sh_zfs_set(z, dir, SH_UPDATE_MARK, p->up_version, e) != 0)
			goto fail;
		c.c_marked = 1;
	}
	if (uq_mountf(&c, 1) != 0)
		goto fail;
	for (i = 0; i < p->up_nds; i++) {
		r = p->up_dss[i].ud_how == SH_UH_REPLAY ? uq_replay(&c, i) :
		    uq_clone(&c, i);
		if (r == 1) {
			(void) uq_mountf(&c, 0);
			(void) fprintf(out, "paused: %s\n", c.c_pause);
			(void) close(c.c_logfd);
			return (1);
		}
		if (r != 0)
			goto fail;
	}
	if (uq_finish(&c) != 0)
		goto fail;
	uq_tidy(&c);
	(void) fprintf(out, "staged %s on %s@%s: shtetl cutover %s, or "
	    "abandon %s\n", p->up_stagename, p->up_found, p->up_version,
	    p->up_name, p->up_name);
	(void) close(c.c_logfd);
	return (0);
fail:
	uq_undo(&c);
	sh_errf(e, "update of %s failed and was given up; log %s",
	    p->up_name, c.c_log);
	(void) close(c.c_logfd);
	return (-1);
}

void
sh_update_free(struct sh_uplan *p)
{
	sh_recipe_free(&p->up_recipe);
}
