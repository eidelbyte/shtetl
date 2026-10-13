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

#include <sys/stat.h>
#include <sys/utsname.h>

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
#include "core/run.h"
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
	(void) snprintf(p->fp_root, sizeof (p->fp_root), "%s", root);
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

/* ---- the run ---------------------------------------------------- */

const struct sh_tools sh_build_tools = {
	"git", "make", "pkg", "chroot", "mount", "umount", "sh"
};

/* What a run has done, to be undone, newest last. */
enum fu_kind {
	FU_UNLOCK,	/* the build root's shtetl:build inherited */
	FU_DESTROY,	/* a dataset made, destroyed */
	FU_UNMOUNT,	/* a dataset mounted, unmounted */
	FU_INHERIT,	/* a dataset's mountpoint set, inherited */
	FU_UMOUNT,	/* a path mounted (devfs), unmounted */
	FU_UNLINK	/* a file put in F, removed */
};

struct fu {
	enum fu_kind	fu_kind;
	int		fu_fail;	/* undone only on a failure */
	char		fu_what[SH_BUILD_PATH];
};

#define	FB_UNDO		(SH_BUILD_DS * 3 + 16)
#define	FB_CAP		4096	/* what a captured program may print */

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

static int fb_exec(struct fb *b, const char *const *words,
    const char *const *env, size_t cap, char **outp, int quiet);

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
 * *OUTP (the caller frees it). ENV, or the environment shtetl has.
 * QUIET: a failure is not reported (the caller reads the status).
 * The exit status is 0, or -1.
 */
static int
fb_exec(struct fb *b, const char *const *words, const char *const *env,
    size_t cap, char **outp, int quiet)
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
	const char *one;
	char stamp[32];
	struct tm tm;
	int fd;

	if (fb_mkdirs(b, p->fp_rundir) != 0)
		return (-1);
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

/* F's datasets made, before anything is installed; the obj cache. */
static int
fb_create(struct fb *b)
{
	struct sh_fplan *p = b->fb_p;
	struct sh_zprop zp[32];
	size_t i, n;

	for (i = 0; i < p->fp_nds; i++) {
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
 * REVISION, asked of the checkout's newvers.sh and checked against
 * ver, and the version's name.
 */
static int
fb_version(struct fb *b, time_t now)
{
	struct sh_fplan *p = b->fb_p;
	char path[SH_BUILD_PATH], **have, *out;
	const char *w[5];
	long major, minor;
	size_t n;
	int r;

	(void) snprintf(path, sizeof (path), "%s/sys/conf/newvers.sh",
	    p->fp_srcdir);
	w[0] = b->fb_t->tl_sh;
	w[1] = path;
	w[2] = "-V";
	w[3] = "REVISION";
	w[4] = NULL;
	if (fb_exec(b, w, NULL, FB_CAP, &out, 0) != 0)
		return (-1);
	r = sh_version_revision(out != NULL ? out : "", &major, &minor,
	    b->fb_err);
	free(out);
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

/* make in the checkout: TARGET, with what every one of them takes. */
static int
fb_make(struct fb *b, const char *target, const char *jobs, int install)
{
	struct sh_fplan *p = b->fb_p;
	char srcconf[SH_BUILD_PATH + 16], kdir[SH_BUILD_PATH + 16];
	char kconf[SH_BUILD_NAME + 16], dest[SH_BUILD_PATH + 16];
	char obj[SH_BUILD_PATH + 32];
	const char *w[12], *env[3];
	size_t n;

	(void) snprintf(srcconf, sizeof (srcconf), "SRCCONF=%s",
	    b->fb_srcconf);
	(void) snprintf(kdir, sizeof (kdir), "KERNCONFDIR=%s", p->fp_rundir);
	(void) snprintf(kconf, sizeof (kconf), "KERNCONF=%s", b->fb_ident);
	(void) snprintf(dest, sizeof (dest), "DESTDIR=%s", b->fb_rootdir);
	(void) snprintf(obj, sizeof (obj), "MAKEOBJDIRPREFIX=%s",
	    b->fb_objdir);
	n = 0;
	w[n++] = b->fb_t->tl_make;
	w[n++] = "-C";
	w[n++] = p->fp_srcdir;
	if (jobs != NULL)
		w[n++] = jobs;
	if (install) {
		w[n++] = target;
		w[n++] = dest;
		w[n++] = "INSTALL_CMD=install -C";
	} else {
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
	(void) fprintf(b->fb_out, "make %s (log %s)\n", install ? target :
	    "buildworld buildkernel", b->fb_log);
	(void) fflush(b->fb_out);
	return (fb_exec(b, w, env, 0, NULL, 0));
}

/* The build, and the install into F (7.1 steps 4, 5). */
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
	for (i = 0; i < sizeof (installs) / sizeof (installs[0]); i++) {
		if (fb_make(b, installs[i], NULL, 1) != 0)
			return (-1);
	}
	return (0);
}

/* The recipe's packages and commands, in a chroot of F (7.1 step 6). */
static int
fb_inside(struct fb *b)
{
	struct sh_fplan *p = b->fb_p;
	static const char *const env[] = { "PATH=" SH_RUN_PATH,
	    "ASSUME_ALWAYS_YES=yes", "HOME=/root", NULL };
	const struct sh_node *pkg, *cmds, *block;
	char dev[SH_BUILD_PATH + 8], resolv[SH_BUILD_PATH + 32];
	const char **w;
	struct stat st;
	size_t i, j, mark;
	int rc;

	pkg = sh_obj_get(p->fp_recipe.sr_merged, "pkg");
	cmds = sh_obj_get(p->fp_recipe.sr_merged, "commands");
	if ((pkg == NULL || pkg->sn_obj.ob_n == 0) &&
	    (cmds == NULL || cmds->sn_obj.ob_n == 0))
		return (0);
	mark = b->fb_nundo;
	(void) snprintf(dev, sizeof (dev), "%s/dev", b->fb_rootdir);
	if ((w = calloc(8 + (pkg != NULL ? pkg->sn_obj.ob_n : 0),
	    sizeof (*w))) == NULL) {
		sh_err_nomem(b->fb_err);
		return (-1);
	}
	rc = -1;
	if (fb_mkdirs(b, dev) != 0)
		goto out;
	w[0] = b->fb_t->tl_mount;
	w[1] = "-t";
	w[2] = "devfs";
	w[3] = "devfs";
	w[4] = dev;
	w[5] = NULL;
	if (fb_exec(b, w, NULL, 0, NULL, 0) != 0)
		goto out;
	fb_push(b, FU_UMOUNT, 0, dev);
	if (pkg != NULL && pkg->sn_obj.ob_n > 0) {
		(void) snprintf(resolv, sizeof (resolv), "%s/etc/resolv.conf",
		    b->fb_rootdir);
		/* pkg fetches: the host's resolver, while it does */
		if (stat(resolv, &st) != 0 && stat("/etc/resolv.conf",
		    &st) == 0) {
			if (fb_copy(b, "/etc/resolv.conf", resolv) != 0)
				goto out;
			fb_push(b, FU_UNLINK, 0, resolv);
		}
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
		if (fb_exec(b, w, env, 0, NULL, 0) != 0)
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
			if (fb_exec(b, w, env, 0, NULL, 0) != 0)
				goto out;
		}
	}
	rc = 0;
	fb_undo(b, mark, 0);
out:
	free(w);
	return (rc);
}

/* F and every dataset of it, snapshotted as the version (7.1 step 7). */
static int
fb_snapshot(struct fb *b, const char *msg)
{
	struct sh_fplan *p = b->fb_p;
	struct sh_zprop zp[4];
	char names[SH_BUILD_DS][SH_BUILD_NAME + SH_VERSION_MAX + 2];
	const char *np[SH_BUILD_DS];
	char deflt[256];
	size_t i;

	if (msg == NULL) {
		(void) snprintf(deflt, sizeof (deflt), "build %s at %.12s",
		    p->fp_src_branch, b->fb_sha);
		msg = deflt;
	}
	zp[0].zp_name = "shtetl:kind";
	zp[0].zp_value = "version";
	zp[1].zp_name = "shtetl:commit";
	zp[1].zp_value = msg;
	zp[2].zp_name = "shtetl:config";
	zp[2].zp_value = b->fb_cfgsnap;
	zp[3].zp_name = "shtetl:config-guid";
	zp[3].zp_value = b->fb_cfgguid;
	for (i = 0; i < p->fp_nds; i++) {
		(void) snprintf(names[i], sizeof (names[i]), "%s@%s",
		    p->fp_dss[i].fd_ds, b->fb_version);
		np[i] = names[i];
	}
	if (sh_zfs_snapshot(b->fb_z, np, p->fp_nds, zp, 4, b->fb_err) != 0)
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
	size_t fmark;
	int rc;

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
	if (fb_begin(b, now) != 0 || fb_create(b) != 0 ||
	    fb_checkout(b) != 0 || fb_version(b, now) != 0 ||
	    fb_configs(b) != 0 || fb_mounts(b, &fmark) != 0 ||
	    fb_world(b) != 0 || fb_inside(b) != 0)
		goto fail;
	/* F unmounted, its mountpoint back, and then the version */
	fb_undo(b, fmark, 0);
	if (fb_snapshot(b, msg) != 0)
		goto fail;
	fb_commit(b);
	fb_undo(b, 0, 0);
	(void) fprintf(out, "built %s@%s\n", p->fp_ds, b->fb_version);
	rc = 0;
	goto out;
fail:
	fb_undo(b, 0, 1);
	sh_errf(e, "build of %s failed; log %s", p->fp_name, b->fb_log);
out:
	free(b);
	return (rc);
}

void
sh_build_free(struct sh_fplan *p)
{
	sh_recipe_free(&p->fp_recipe);
}
