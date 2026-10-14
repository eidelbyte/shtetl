/*
 * shtetl create X --on F (planning.md 7.4).
 *
 * Decided here, beyond the plan (ruled 2026-10-14): the jail's block
 * is shtetl's, written whole to /etc/jail.conf.d/X.conf, the directory
 * FreeBSD's own rc.d/jail reads a jail's file from; it holds what
 * parasa's jails held (rc's start and stop, exec.clean, mount.devfs,
 * devfs_ruleset 4, the host's network inherited, which a jail lacks by
 * default), then the recipe's jailconf. The jail is started with
 * jail -f and -c, and rc.conf is left alone. Commands run by jexec X
 * /bin/sh -e -c, the packages by pkg -j X. The host's resolv.conf is
 * put in by the recipe, an external command of shtetl.conf's container
 * block (commands-external: on the host, before anything runs inside
 * X), not by create. requires names containers that must be there, not
 * that they run; one container mounting another's data is later work.
 * A name of digits only is refused, which jail(8) would take for a
 * jail's id.
 */

#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "core/beam.h"
#include "core/build.h"
#include "core/cascade.h"
#include "core/cfgsnap.h"
#include "core/create.h"
#include "core/err.h"
#include "core/jail.h"
#include "core/run.h"
#include "core/save.h"
#include "core/spaces.h"
#include "core/tree.h"
#include "core/version.h"
#include "core/zfs.h"
#include "core/zwalk.h"

/* The dataset REL in P's list, or NULL. */
static struct sh_cds *
cr_find(struct sh_cplan *p, const char *rel)
{
	size_t i;

	for (i = 0; i < p->cp_nds; i++) {
		if (strcmp(p->cp_dss[i].cd_rel, rel) == 0)
			return (&p->cp_dss[i]);
	}
	return (NULL);
}

static struct sh_cds *
cr_add(struct sh_cplan *p, const char *rel, int clone, int placeholder,
    struct sh_err *e)
{
	struct sh_cds *d;

	if (p->cp_nds == SH_CREATE_DS) {
		sh_errf(e, "container %s: more than %d datasets", p->cp_name,
		    SH_CREATE_DS);
		return (NULL);
	}
	d = &p->cp_dss[p->cp_nds++];
	(void) memset(d, 0, sizeof (*d));
	(void) snprintf(d->cd_rel, sizeof (d->cd_rel), "%s", rel);
	d->cd_clone = clone;
	d->cd_placeholder = placeholder;
	return (d);
}

/*
 * The version: VERSION, or F's newest snapshot that is a version or a
 * -pkg of one, in zlake (LAKEF).
 */
static int
cr_version(struct sh_zfs *z, struct sh_cplan *p, const char *lakef,
    const char *version, struct sh_err *e)
{
	char name[SH_CREATE_NAME * 2], kind[SH_ZFS_VALMAX], **snaps;
	size_t i;
	int found;

	if ((snaps = sh_zw_snaps(z, lakef, e)) == NULL)
		return (-1);
	found = 0;
	for (i = 0; snaps[i] != NULL; i++) {
		(void) snprintf(name, sizeof (name), "%s@%s", lakef, snaps[i]);
		if (sh_zfs_get(z, name, "shtetl:kind", kind, sizeof (kind),
		    NULL, e) != 0) {
			sh_zw_free(snaps);
			return (-1);
		}
		if (strcmp(kind, "version") != 0 && strcmp(kind, "pkg") != 0)
			continue;
		if (version == NULL || strcmp(version, snaps[i]) == 0) {
			(void) snprintf(p->cp_version, sizeof (p->cp_version),
			    "%s", snaps[i]);
			found = 1;
		}
	}
	sh_zw_free(snaps);
	if (!found && version != NULL)
		sh_errf(e, "foundation %s has no version %s", p->cp_found,
		    version);
	else if (!found)
		sh_errf(e, "foundation %s exists without a version",
		    p->cp_found);
	return (found ? 0 : -1);
}

/* X's ver against the version's shtetl:revision, N.M. */
static void
cr_ver(struct sh_zfs *z, struct sh_cplan *p, const char *lakef,
    struct sh_err *e)
{
	char name[SH_CREATE_NAME * 2], rev[SH_ZFS_VALMAX], *end;
	long major, minor;

	(void) snprintf(name, sizeof (name), "%s@%s", lakef, p->cp_version);
	if (sh_zfs_get(z, name, "shtetl:revision", rev, sizeof (rev), NULL,
	    e) != 0)
		return;
	major = strtol(rev, &end, 10);
	minor = *end == '.' ? strtol(end + 1, &end, 10) : -1;
	if (strcmp(rev, "-") == 0 || minor < 0 || *end != '\0') {
		sh_errf(e, "%s@%s: no shtetl:revision", p->cp_found,
		    p->cp_version);
		return;
	}
	(void) sh_version_check(major, minor,
	    sh_obj_get(p->cp_recipe.sr_merged, "ver"), p->cp_recipe.sr_file,
	    e);
}

/*
 * The datasets: F's, cloned, the recipe's properties on those it
 * names; then X's own, a placeholder before each parent nobody names.
 */
static int
cr_datasets(struct sh_zfs *z, struct sh_cplan *p, const char *lakef,
    struct sh_err *e)
{
	const struct sh_node *dss, *v;
	char rel[SH_CREATE_NAME], cm[SH_ZFS_VALMAX], **all;
	char snap[SH_CREATE_NAME * 3];
	const char *key, *slash;
	struct sh_cds *d;
	size_t i, n;
	int r;

	if ((all = sh_zw_new(e)) == NULL)
		return (-1);
	r = sh_zw_add(&all, lakef) != 0 ? -1 : sh_zw_walk(z, lakef, &all, e);
	n = strlen(lakef);
	for (i = 0; r == 0 && all[i] != NULL; i++) {
		/* one F split later than the version: not the version's */
		(void) snprintf(snap, sizeof (snap), "%s@%s", all[i],
		    p->cp_version);
		if ((r = sh_zfs_get(z, snap, "type", cm, sizeof (cm), NULL,
		    e)) == 1) {
			r = 0;
			continue;
		}
		if (r == 0 && (r = sh_zfs_get(z, all[i], "canmount", cm,
		    sizeof (cm), NULL, e)) == 0 && cr_add(p, i == 0 ? "" :
		    all[i] + n + 1, 1, strcmp(cm, "off") == 0, e) == NULL)
			r = -1;
	}
	sh_zw_free(all);
	if (r != 0)
		return (-1);
	dss = sh_obj_get(p->cp_recipe.sr_merged, "datasets");
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
			if ((d = cr_find(p, rel)) == NULL &&
			    (d = cr_add(p, rel, 0, slash != NULL, e)) == NULL)
				return (-1);
			if (slash == NULL) {
				if (!d->cd_clone)
					d->cd_placeholder = 0;
				d->cd_props = v->sn_type == SH_OBJECT ? v :
				    NULL;
			}
		}
	}
	return (0);
}

/*
 * The pieces of KEY, commands or commands-external, X's recipe gives,
 * into OUT (N of them); a piece the foundation's recipe (as of the
 * version's shtetl:config) gives too, the same label, file and text,
 * left out, counted in SKIPPED: the foundation ran it (7.4 step 6).
 */
static void
cr_pieces(struct sh_cplan *p, const char *key, struct sh_cpiece *out,
    size_t *n, size_t *skipped)
{
	const struct sh_node *xc, *fc, *xb, *fb, *ft;
	const char *label, *file, *text;
	size_t i, j;

	xc = sh_obj_get(p->cp_recipe.sr_merged, key);
	fc = sh_obj_get(p->cp_frecipe.sr_merged, key);
	for (i = 0; xc != NULL && i < xc->sn_obj.ob_n; i++) {
		label = xc->sn_obj.ob_v[i].pr_key;
		xb = xc->sn_obj.ob_v[i].pr_val;
		fb = sh_obj_get(fc, label);
		for (j = 0; j < xb->sn_obj.ob_n; j++) {
			file = xb->sn_obj.ob_v[j].pr_key;
			text = xb->sn_obj.ob_v[j].pr_val->sn_str;
			ft = sh_obj_get(fb, file);
			if (ft != NULL && ft->sn_type == SH_STR &&
			    strcmp(ft->sn_str, text) == 0) {
				(*skipped)++;
				continue;
			}
			if (*n == SH_CREATE_DS)
				continue;
			out[*n].cp_label = label;
			out[*n].cp_file = file;
			out[(*n)++].cp_text = text;
		}
	}
}

int
sh_create_plan(struct sh_zfs *z, const struct sh_spaces *s,
    const char *root, const char *target, const char *found,
    const char *version, const char *jaildir, const char *logbase,
    struct sh_cplan *p, struct sh_err *e, struct sh_err *w)
{
	char lakef[SH_CREATE_NAME * 2], val[SH_ZFS_VALMAX];
	char name[SH_CREATE_NAME * 3], snapdir[2048], rpath[SH_CREATE_NAME];
	const struct sh_node *req;
	const char *at;
	unsigned before;
	size_t i;
	int r, sp;

	(void) memset(p, 0, sizeof (*p));
	before = e->se_count;
	if (sh_save_target(target, p->cp_name, sizeof (p->cp_name), e) != 0 ||
	    sh_build_target(found, p->cp_found, sizeof (p->cp_found), e) != 0)
		return (-1);
	for (sp = 0; sp < SH_SP_N; sp++) {
		if (sp != SH_SP_BUILD && s->ss_root[sp] == NULL)
			sh_errf(e, "no %s root", sh_space_name(sp));
	}
	if (e->se_count != before)
		return (-1);
	(void) snprintf(p->cp_run, sizeof (p->cp_run), "%s/containers/%s",
	    s->ss_root[SH_SP_RUN], p->cp_name);
	(void) snprintf(p->cp_runf, sizeof (p->cp_runf), "%s/foundations/%s",
	    s->ss_root[SH_SP_RUN], p->cp_found);
	(void) snprintf(lakef, sizeof (lakef), "%s/foundations/%s",
	    s->ss_root[SH_SP_LAKE], p->cp_found);
	(void) snprintf(p->cp_config, sizeof (p->cp_config), "%s",
	    s->ss_root[SH_SP_CONFIG]);
	(void) snprintf(p->cp_root, sizeof (p->cp_root), "%s", root);
	(void) snprintf(p->cp_jailfile, sizeof (p->cp_jailfile), "%s/%s.conf",
	    jaildir, p->cp_name);
	(void) snprintf(p->cp_logdir, sizeof (p->cp_logdir), "%s/%s", logbase,
	    p->cp_name);
	(void) snprintf(rpath, sizeof (rpath), "containers/%s", p->cp_name);
	if (sh_recipe_resolve(root, rpath, &p->cp_recipe, e, w) != 0)
		return (-1);
	if ((r = sh_zfs_get(z, p->cp_run, "type", val, sizeof (val), NULL,
	    e)) == 0)
		sh_errf(e, "container %s exists", p->cp_name);
	if ((r = sh_zfs_get(z, lakef, "type", val, sizeof (val), NULL,
	    e)) != 0) {
		if (r == 1)
			sh_errf(e, "no foundation %s", p->cp_found);
		return (-1);
	}
	if (cr_version(z, p, lakef, version, e) != 0)
		return (-1);
	cr_ver(z, p, lakef, e);
	req = sh_obj_get(p->cp_recipe.sr_merged, "requires");
	for (i = 0; req != NULL && i < req->sn_obj.ob_n; i++) {
		(void) snprintf(name, sizeof (name), "%s/containers/%s",
		    s->ss_root[SH_SP_RUN], req->sn_obj.ob_v[i].pr_key);
		if (sh_zfs_get(z, name, "type", val, sizeof (val), NULL,
		    e) == 1)
			sh_errf(e, "requires: no container %s",
			    req->sn_obj.ob_v[i].pr_key);
	}
	/* on zrun, or beamed first */
	(void) snprintf(name, sizeof (name), "%s@%s", p->cp_runf,
	    p->cp_version);
	if ((r = sh_zfs_get(z, name, "type", val, sizeof (val), NULL,
	    e)) < 0)
		return (-1);
	p->cp_beam = r == 1;
	if (p->cp_beam && sh_beam_plan(z, s, p->cp_found, p->cp_version,
	    &p->cp_beamplan, e) != 0)
		return (-1);
	if (cr_datasets(z, p, lakef, e) != 0)
		return (-1);
	/* F's recipe as the version had it, for the pieces it ran */
	(void) snprintf(name, sizeof (name), "%s@%s", lakef, p->cp_version);
	if (sh_zfs_get(z, name, "shtetl:config", val, sizeof (val), NULL,
	    e) != 0)
		return (-1);
	if ((at = strchr(val, '@')) == NULL) {
		sh_errf(e, "%s says no config snapshot (shtetl:config)", name);
		return (-1);
	}
	(void) snprintf(snapdir, sizeof (snapdir), "%s/.zfs/snapshot/%s",
	    root, at + 1);
	(void) snprintf(rpath, sizeof (rpath), "foundations/%s",
	    p->cp_found);
	if (sh_recipe_resolve(snapdir, rpath, &p->cp_frecipe, e, NULL) != 0)
		return (-1);
	cr_pieces(p, "commands-external", p->cp_ext, &p->cp_next,
	    &p->cp_extskipped);
	cr_pieces(p, "commands", p->cp_cmds, &p->cp_ncmds, &p->cp_skipped);
	return (e->se_count == before ? 0 : -1);
}

/* D's properties, as the recipe gives them: a=b, c=d. */
static void
cr_props(FILE *out, const struct sh_cds *d)
{
	const struct sh_pair *pr;
	size_t i;

	for (i = 0; d->cd_props != NULL && i < d->cd_props->sn_obj.ob_n;
	    i++) {
		pr = &d->cd_props->sn_obj.ob_v[i];
		(void) fprintf(out, ", %s=%s", pr->pr_key,
		    pr->pr_val->sn_type == SH_BOOL ? (pr->pr_val->sn_bool ?
		    "on" : "off") : pr->pr_val->sn_str);
	}
}

/* LABEL, then the pieces P, N of them, and SKIPPED the foundation ran. */
static void
cr_showpieces(FILE *out, const char *label, const struct sh_cpiece *p,
    size_t n, size_t skipped)
{
	size_t i;

	(void) fprintf(out, "  %s:", label);
	for (i = 0; i < n; i++)
		(void) fprintf(out, "%s %s (%s)", i == 0 ? "" : ",",
		    p[i].cp_label, p[i].cp_file);
	if (n == 0)
		(void) fprintf(out, " none");
	if (skipped > 0)
		(void) fprintf(out, "; %zu the foundation ran", skipped);
	(void) fprintf(out, "\n");
}

/* The keys of the merged recipe's KEY, a collection: a, b, c. */
static void
cr_keys(FILE *out, const struct sh_node *c)
{
	size_t i;

	for (i = 0; c != NULL && i < c->sn_obj.ob_n; i++)
		(void) fprintf(out, "%s%s", i == 0 ? "" : ", ",
		    c->sn_obj.ob_v[i].pr_key);
	if (c == NULL || c->sn_obj.ob_n == 0)
		(void) fprintf(out, "none");
}

void
sh_create_show(FILE *out, const struct sh_cplan *p)
{
	const struct sh_cds *d;
	size_t i;

	(void) fprintf(out, "container %s, on %s@%s%s\n", p->cp_name,
	    p->cp_found, p->cp_version, p->cp_beam ? ", beamed to zrun first" :
	    "");
	for (i = 0; i < p->cp_nds; i++) {
		d = &p->cp_dss[i];
		(void) fprintf(out, "  + %s%s%s  %s%s", p->cp_run,
		    i > 0 ? "/" : "", d->cd_rel,
		    d->cd_clone ? "clone" : "its own",
		    d->cd_placeholder ? ", canmount=off" : "");
		cr_props(out, d);
		(void) fprintf(out, "\n");
	}
	cr_showpieces(out, "external", p->cp_ext, p->cp_next,
	    p->cp_extskipped);
	(void) fprintf(out, "  jail: %s\n  pkg: ", p->cp_jailfile);
	cr_keys(out, sh_obj_get(p->cp_recipe.sr_merged, "pkg"));
	(void) fprintf(out, "\n");
	cr_showpieces(out, "commands", p->cp_cmds, p->cp_ncmds,
	    p->cp_skipped);
	(void) fprintf(out, "  save, mirrored into zlake\n");
}

/* ---- the run ---------------------------------------------------- */

static const char *const cr_env[] = { "PATH=" SH_RUN_PATH,
	"ASSUME_ALWAYS_YES=yes", "HOME=/root", NULL };

/* One run of create. */
struct cr {
	struct sh_zfs		*c_z;
	struct sh_cplan		*c_p;
	const struct sh_tools	*c_t;
	FILE			*c_out;
	struct sh_err		*c_err;
	char			c_log[1100];
	char			c_cfgsnap[SH_CREATE_NAME * 2];
	char			c_path[1024];	/* X, mounted */
	int			c_made;		/* X's datasets */
	int			c_mnt[SH_CREATE_DS];	/* each, mounted */
	int			c_written;	/* the jail's block */
	int			c_jailed;
};

/* WORDS run, ENV their environment (NULL: shtetl's), to the log. */
static int
cr_exec(struct cr *c, const char *const *words, const char *const *env)
{
	extern char **environ;
	struct sh_run r;
	char **argv, **envp;
	size_t i, n, m;
	int fd, rc;

	for (n = 0; words[n] != NULL; n++)
		;
	for (m = 0; env != NULL && env[m] != NULL; m++)
		;
	argv = calloc(n + 1, sizeof (*argv));
	envp = calloc(m + 1, sizeof (*envp));
	rc = -1;
	fd = -1;
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
	if ((fd = open(c->c_log, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC,
	    0644)) == -1) {
		sh_errf(c->c_err, "%s: %s", c->c_log, strerror(errno));
		goto out;
	}
	(void) memset(&r, 0, sizeof (r));
	r.ru_argv = argv;
	r.ru_env = env != NULL ? envp : environ;
	r.ru_log = fd;
	rc = sh_run(&r, c->c_err) == 0 ? 0 : -1;
	free(r.ru_out);
	goto out;
nomem:
	sh_err_nomem(c->c_err);
out:
	if (fd != -1)
		(void) close(fd);
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
cr_mkdirs(struct cr *c, const char *dir)
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

/* The config snapshot X records: the newest, or one taken (cfgsnap.c). */
static int
cr_config(struct cr *c, time_t now)
{
	struct sh_cplan *p = c->c_p;
	char msg[SH_CREATE_NAME * 3];

	(void) snprintf(msg, sizeof (msg), "create %s on %s@%s", p->cp_name,
	    p->cp_found, p->cp_version);
	return (sh_cfgsnap(c->c_z, p->cp_config, msg, now, c->c_cfgsnap,
	    sizeof (c->c_cfgsnap), c->c_out, c->c_err));
}

/* A dataset of the recipe's properties as ZFS takes them; N set. */
static size_t
cr_zprops(const struct sh_cds *d, struct sh_zprop *zp, size_t max)
{
	const struct sh_pair *pr;
	size_t i, n;

	n = 0;
	if (d->cd_placeholder) {
		zp[n].zp_name = "canmount";
		zp[n++].zp_value = "off";
	}
	for (i = 0; d->cd_props != NULL && i < d->cd_props->sn_obj.ob_n &&
	    n < max; i++) {
		pr = &d->cd_props->sn_obj.ob_v[i];
		zp[n].zp_name = pr->pr_key;
		zp[n++].zp_value = pr->pr_val->sn_type == SH_BOOL ?
		    (pr->pr_val->sn_bool ? "on" : "off") : pr->pr_val->sn_str;
	}
	return (n);
}

/* The full name of P's dataset I below BASE. */
static void
cr_ds(const struct sh_cplan *p, const char *base, size_t i, char *buf,
    size_t len)
{
	(void) snprintf(buf, len, "%s%s%s", base, i > 0 ? "/" : "",
	    p->cp_dss[i].cd_rel);
}

/* F's datasets cloned, X's own made, X mounted (7.4 steps 3, 4). */
static int
cr_datasets_run(struct cr *c)
{
	struct sh_cplan *p = c->c_p;
	struct sh_zprop zp[33];
	char ds[SH_CREATE_NAME * 2], origin[SH_CREATE_NAME * 3];
	size_t i, n;
	int r;

	for (i = 0; i < p->cp_nds; i++) {
		cr_ds(p, p->cp_run, i, ds, sizeof (ds));
		n = cr_zprops(&p->cp_dss[i], zp, 33);
		if (p->cp_dss[i].cd_clone) {
			cr_ds(p, p->cp_runf, i, origin, sizeof (origin));
			(void) snprintf(origin + strlen(origin),
			    sizeof (origin) - strlen(origin), "@%s",
			    p->cp_version);
			r = sh_zfs_clone(c->c_z, origin, ds, zp, n, c->c_err);
		} else {
			r = sh_zfs_create(c->c_z, ds, zp, n, c->c_err);
		}
		if (r != 0)
			return (-1);
		c->c_made = 1;
		(void) fprintf(c->c_out, "%s %s\n", p->cp_dss[i].cd_clone ?
		    "cloned" : "made", ds);
	}
	/* X itself, then each that mounts: not canmount off, nor legacy */
	for (i = 0; i < p->cp_nds; i++) {
		cr_ds(p, p->cp_run, i, ds, sizeof (ds));
		if (i > 0 && (sh_zfs_get(c->c_z, ds, "canmount", origin,
		    sizeof (origin), NULL, c->c_err) != 0 ||
		    strcmp(origin, "off") == 0 || sh_zfs_get(c->c_z, ds,
		    "mountpoint", origin, sizeof (origin), NULL,
		    c->c_err) != 0 || origin[0] != '/'))
			continue;
		if (sh_zfs_mount(c->c_z, ds, c->c_err) != 0)
			return (-1);
		c->c_mnt[i] = 1;
	}
	if (sh_zfs_get(c->c_z, p->cp_run, "mountpoint", c->c_path,
	    sizeof (c->c_path), NULL, c->c_err) != 0)
		return (-1);
	(void) fprintf(c->c_out, "mounted %s at %s\n", p->cp_run, c->c_path);
	return (0);
}

/* The jail's block written, the jail started (7.4 step 5). */
static int
cr_jail(struct cr *c)
{
	struct sh_cplan *p = c->c_p;
	char dir[1024], *slash;
	const char *w[6];

	(void) snprintf(dir, sizeof (dir), "%s", p->cp_jailfile);
	if ((slash = strrchr(dir, '/')) != NULL)
		*slash = '\0';
	if (cr_mkdirs(c, dir) != 0)
		return (-1);
	c->c_written = 1;
	if (sh_jail_write(p->cp_jailfile, p->cp_name, p->cp_name, c->c_path,
	    p->cp_recipe.sr_merged, c->c_err) != 0)
		return (-1);
	w[0] = c->c_t->tl_jail;
	w[1] = "-f";
	w[2] = p->cp_jailfile;
	w[3] = "-c";
	w[4] = p->cp_name;
	w[5] = NULL;
	if (cr_exec(c, w, NULL) != 0)
		return (-1);
	c->c_jailed = 1;
	(void) fprintf(c->c_out, "wrote %s; started the jail %s\n",
	    p->cp_jailfile, p->cp_name);
	return (0);
}

/*
 * The external command pieces, each by sh -e -c on the host, X's root
 * in SHTETL_ROOT, before anything runs inside X (7.4).
 */
static int
cr_external(struct cr *c)
{
	struct sh_cplan *p = c->c_p;
	char root[1100], name[SH_CREATE_NAME + 16];
	const char *env[6], *w[5];
	size_t i;

	(void) snprintf(root, sizeof (root), "SHTETL_ROOT=%s", c->c_path);
	(void) snprintf(name, sizeof (name), "SHTETL_NAME=%s", p->cp_name);
	env[0] = "PATH=" SH_RUN_PATH;
	env[1] = "HOME=/root";
	env[2] = root;
	env[3] = name;
	env[4] = "SHTETL_KIND=container";
	env[5] = NULL;
	for (i = 0; i < p->cp_next; i++) {
		w[0] = c->c_t->tl_sh;
		w[1] = "-e";
		w[2] = "-c";
		w[3] = p->cp_ext[i].cp_text;
		w[4] = NULL;
		(void) fprintf(c->c_out, "external %s, from %s\n",
		    p->cp_ext[i].cp_label, p->cp_ext[i].cp_file);
		if (cr_exec(c, w, env) != 0)
			return (-1);
	}
	return (0);
}

/* pkg -j X install, then the commands by jexec (7.4 step 6). */
static int
cr_inside(struct cr *c)
{
	struct sh_cplan *p = c->c_p;
	const struct sh_node *pkg;
	const char **w;
	size_t i, n;
	int rc;

	pkg = sh_obj_get(p->cp_recipe.sr_merged, "pkg");
	n = pkg != NULL ? pkg->sn_obj.ob_n : 0;
	if ((w = calloc(7 + n, sizeof (*w))) == NULL) {
		sh_err_nomem(c->c_err);
		return (-1);
	}
	rc = -1;
	if (n > 0) {
		w[0] = c->c_t->tl_pkg;
		w[1] = "-j";
		w[2] = p->cp_name;
		w[3] = "install";
		w[4] = "-y";
		w[5] = "--";
		for (i = 0; i < n; i++)
			w[6 + i] = pkg->sn_obj.ob_v[i].pr_key;
		w[6 + i] = NULL;
		(void) fprintf(c->c_out, "pkg install in %s\n", p->cp_name);
		if (cr_exec(c, w, cr_env) != 0)
			goto out;
	}
	for (i = 0; i < p->cp_ncmds; i++) {
		w[0] = c->c_t->tl_jexec;
		w[1] = p->cp_name;
		w[2] = "/bin/sh";
		w[3] = "-e";
		w[4] = "-c";
		w[5] = p->cp_cmds[i].cp_text;
		w[6] = NULL;
		(void) fprintf(c->c_out, "commands %s, from %s\n",
		    p->cp_cmds[i].cp_label, p->cp_cmds[i].cp_file);
		if (cr_exec(c, w, cr_env) != 0)
			goto out;
	}
	rc = 0;
out:
	free(w);
	return (rc);
}

/* What the run did, undone: the jail, its block, X's datasets. */
static void
cr_undo(struct cr *c)
{
	struct sh_cplan *p = c->c_p;
	const char *w[6];
	char ds[SH_CREATE_NAME * 2];
	size_t i;

	if (c->c_jailed) {
		w[0] = c->c_t->tl_jail;
		w[1] = "-f";
		w[2] = p->cp_jailfile;
		w[3] = "-r";
		w[4] = p->cp_name;
		w[5] = NULL;
		(void) cr_exec(c, w, NULL);
	}
	if (c->c_written && unlink(p->cp_jailfile) != 0 && errno != ENOENT)
		sh_errf(c->c_err, "%s: %s", p->cp_jailfile, strerror(errno));
	for (i = p->cp_nds; i > 0; i--) {
		cr_ds(p, p->cp_run, i - 1, ds, sizeof (ds));
		if (c->c_mnt[i - 1])
			(void) sh_zfs_unmount(c->c_z, ds, c->c_err);
	}
	if (c->c_made && sh_zw_destroy(c->c_z, p->cp_run, c->c_err) == 0)
		(void) fprintf(c->c_out, "destroyed %s\n", p->cp_run);
}

int
sh_create_run(struct sh_zfs *z, struct sh_cplan *p,
    const struct sh_spaces *s, const struct sh_tools *t, const char *msg,
    time_t now, FILE *out, struct sh_err *e)
{
	struct sh_save *sp;
	struct cr c;
	char snap[256], name[SH_CREATE_NAME * 3], val[64], deflt[512];
	int fd;

	(void) memset(&c, 0, sizeof (c));
	c.c_z = z;
	c.c_p = p;
	c.c_t = t;
	c.c_out = out;
	c.c_err = e;
	(void) snprintf(c.c_log, sizeof (c.c_log), "%s/create.log",
	    p->cp_logdir);
	if (cr_mkdirs(&c, p->cp_logdir) != 0)
		return (-1);
	if ((fd = open(c.c_log, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC,
	    0644)) == -1) {
		sh_errf(e, "%s: %s", c.c_log, strerror(errno));
		return (-1);
	}
	(void) close(fd);
	if (cr_config(&c, now) != 0)
		return (-1);
	if (p->cp_beam && sh_beam_run(z, &p->cp_beamplan, out, e) != 0)
		goto fail;
	if (cr_datasets_run(&c) != 0 || cr_external(&c) != 0 ||
	    cr_jail(&c) != 0 || cr_inside(&c) != 0)
		goto fail;
	if ((sp = calloc(1, sizeof (*sp))) == NULL) {
		sh_err_nomem(e);
		goto fail;
	}
	(void) snprintf(deflt, sizeof (deflt), "create %s on %s@%s",
	    p->cp_name, p->cp_found, p->cp_version);
	snap[0] = '\0';
	if (sh_save_plan(z, s, p->cp_name, c.c_cfgsnap, sp, e) != 0 ||
	    sh_save_run(z, sp, msg != NULL ? msg : deflt, now, snap,
	    sizeof (snap), out, e) != 0) {
		free(sp);
		/* the save taken, its mirror failing: X stands */
		(void) snprintf(name, sizeof (name), "%s@%s", p->cp_run, snap);
		if (snap[0] != '\0' && sh_zfs_get(z, name, "type", val,
		    sizeof (val), NULL, e) == 0)
			return (-1);
		goto fail;
	}
	free(sp);
	(void) fprintf(out, "created %s on %s@%s\n", p->cp_name, p->cp_found,
	    p->cp_version);
	return (0);
fail:
	cr_undo(&c);
	sh_errf(e, "create of %s failed; log %s", p->cp_name, c.c_log);
	return (-1);
}

void
sh_create_free(struct sh_cplan *p)
{
	sh_recipe_free(&p->cp_recipe);
	sh_recipe_free(&p->cp_frecipe);
}
