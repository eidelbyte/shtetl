/*
 * The src checkout, the patches and the recipe repositories: git
 * children, named for what they do it to ("src: fetch",
 * "github.com/x/patches: 0002-y.patch", "site: clone").
 */

#include <sys/types.h>

#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/cascade.h"
#include "core/job.h"
#include "core/mem.h"
#include "core/root.h"
#include "core/site.h"
#include "core/src.h"
#include "core/syntax.h"
#include "core/tree.h"

#define	SRC_WORDS	12	/* more than any git here is given */

/*
 * A git child added to J as the step "WHO: WHAT", its words after
 * WHAT, to a NULL.
 */
static void
src_git(struct sh_job *j, const char *who, const char *what, ...)
{
	const char *argv[SRC_WORDS];
	va_list ap;
	char *step;
	size_t n;

	argv[0] = "git";
	va_start(ap, what);
	for (n = 1; (argv[n] = va_arg(ap, const char *)) != NULL; n++)
		;
	va_end(ap);
	n = strlen(who) + strlen(what) + 3;
	step = sh_calloc(1, n);
	(void) snprintf(step, n, "%s: %s", who, what);
	sh_job_child(j, step, argv);
	free(step);
}

char *
sh_src_url(const char *remote)
{
	enum sh_loc kind = SH_LOC_URL;
	char *url;
	size_t n;

	/* checked when the configuration was read */
	(void) sh_syn_location(remote, &kind);
	if (kind != SH_LOC_HOST)
		return (sh_strdup(remote));
	n = sizeof ("https://") + strlen(remote);
	url = sh_calloc(1, n);
	(void) snprintf(url, n, "https://%s", remote);
	return (url);
}

static int
src_digit(char c)
{
	return (c >= '0' && c <= '9');
}

int
sh_natcmp(const char *a, const char *b)
{
	const char *a0 = a, *b0 = b, *x, *y;
	size_t lx, ly;
	int c;

	while (*a != '\0' && *b != '\0') {
		if (src_digit(*a) && src_digit(*b)) {
			/* leading zeros aside, the longer run is larger */
			for (x = a; *x == '0'; x++)
				;
			for (y = b; *y == '0'; y++)
				;
			for (lx = 0; src_digit(x[lx]); lx++)
				;
			for (ly = 0; src_digit(y[ly]); ly++)
				;
			if (lx != ly)
				return (lx < ly ? -1 : 1);
			if ((c = memcmp(x, y, lx)) != 0)
				return (c < 0 ? -1 : 1);
			a = x + lx;
			b = y + ly;
			continue;
		}
		if (*a != *b)
			return ((unsigned char)*a < (unsigned char)*b ? -1 : 1);
		a++;
		b++;
	}
	if (*a != *b)
		return (*a == '\0' ? -1 : 1);
	c = strcmp(a0, b0);
	return ((c > 0) - (c < 0));
}

/* The checkout SRC put back at REV, nothing else in it. */
static void
src_reset(struct sh_job *j, const char *src, const char *rev)
{
	src_git(j, "src", "reset", "-C", src, "reset", "-q", "--hard", rev,
	    NULL);
	src_git(j, "src", "clean", "-C", src, "clean", "-fdxq", NULL);
}

void
sh_src_reset(struct sh_job *j, const char *build)
{
	char *src = sh_root_path(build, "src");

	src_reset(j, src, "HEAD");
	free(src);
}

/* Where the K'th patch source, from a remote, is fetched: patches/K+1. */
static char *
src_clone(const struct sh_src *s, size_t k)
{
	char rel[32];

	(void) snprintf(rel, sizeof (rel), "patches/%zu", k + 1);
	return (sh_root_path(s->sc_build, rel));
}

/* Whether the patch source LOC is a directory of zconfig. */
static int
src_local(const char *loc)
{
	enum sh_loc kind = SH_LOC_URL;

	(void) sh_syn_location(loc, &kind);
	return (kind == SH_LOC_DIR);
}

/* Where the K'th patch source PR keeps its patches. */
static char *
src_where(const struct sh_src *s, size_t k, const struct sh_pair *pr)
{
	const struct sh_node *dir = sh_obj_get(pr->pr_val, "dir");
	char *top, *in;

	if (src_local(pr->pr_key)) {
		in = sh_recipe_rel(s->sc_recipe, pr->pr_from, pr->pr_key);
		top = sh_root_path(s->sc_config, in);
		free(in);
	} else {
		top = src_clone(s, k);
	}
	if (dir == NULL)
		return (top);
	in = sh_root_path(top, dir->sn_str);
	free(top);
	return (in);
}

/* A patch: a name ending .patch, not hidden, as an editor's lock is. */
static int
src_patch(const struct dirent *d)
{
	size_t n = strlen(d->d_name);

	return (d->d_name[0] != '.' && n > 6 &&
	    strcmp(d->d_name + n - 6, ".patch") == 0);
}

static int
src_order(const struct dirent **a, const struct dirent **b)
{
	return (sh_natcmp((*a)->d_name, (*b)->d_name));
}

/* The call: each source's patches added as steps, in order. */
static int
src_patches(struct sh_job *j, void *arg, struct sh_err *e)
{
	const struct sh_src *s = arg;
	const struct sh_node *srcs;
	const struct sh_pair *pr;
	struct dirent **names;
	char *src, *top, *path;
	size_t k;
	int i, n;

	srcs = sh_obj_get(s->sc_recipe->sr_merged, "patches");
	src = sh_root_path(s->sc_build, "src");
	for (k = 0; srcs != NULL && k < srcs->sn_obj.ob_n; k++) {
		pr = &srcs->sn_obj.ob_v[k];
		top = src_where(s, k, pr);
		if ((n = scandir(top, &names, src_patch, src_order)) < 0) {
			sh_errf(e, "%s: %s", top, strerror(errno));
			free(top);
			free(src);
			return (-1);
		}
		for (i = 0; i < n; i++) {
			path = sh_root_path(top, names[i]->d_name);
			src_git(j, pr->pr_key, names[i]->d_name, "-C", src,
			    "apply", "--3way", path, NULL);
			free(path);
			free(names[i]);
		}
		free(names);
		free(top);
	}
	free(src);
	return (0);
}

void
sh_src_steps(struct sh_job *j, struct sh_src *s)
{
	const struct sh_node *srcs, *ref;
	const struct sh_pair *pr;
	char *src, *url, *dir;
	size_t k;

	src = sh_root_path(s->sc_build, "src");
	url = sh_src_url(sh_obj_get(sh_obj_get(s->sc_site->ss_merged,
	    "remotes"), "src")->sn_str);
	src_git(j, "src", "init", "init", "-q", src, NULL);
	src_git(j, "src", "fetch", "-C", src, "fetch", "-q", url,
	    sh_obj_get(s->sc_recipe->sr_merged, "src_branch")->sn_str, NULL);
	src_reset(j, src, "FETCH_HEAD");
	free(url);
	free(src);
	srcs = sh_obj_get(s->sc_recipe->sr_merged, "patches");
	for (k = 0; srcs != NULL && k < srcs->sn_obj.ob_n; k++) {
		pr = &srcs->sn_obj.ob_v[k];
		if (src_local(pr->pr_key))
			continue;
		ref = sh_obj_get(pr->pr_val, "ref");
		dir = src_clone(s, k);
		url = sh_src_url(pr->pr_key);
		src_git(j, pr->pr_key, "init", "init", "-q", dir, NULL);
		src_git(j, pr->pr_key, "fetch", "-C", dir, "fetch", "-q",
		    "--depth", "1", url, ref != NULL ? ref->sn_str : "HEAD",
		    NULL);
		src_git(j, pr->pr_key, "reset", "-C", dir, "reset", "-q",
		    "--hard", "FETCH_HEAD", NULL);
		free(url);
		free(dir);
	}
	sh_job_call(j, "patches", src_patches, s);
}

int
sh_src_commit(const char *repo, char *oid, size_t len, struct sh_err *e)
{
	char buf[128], *path;
	size_t n = 0, i;
	FILE *f;

	path = sh_root_path(repo, ".git/FETCH_HEAD");
	if ((f = fopen(path, "r")) != NULL) {
		n = fread(buf, 1, sizeof (buf), f);
		(void) fclose(f);
	}
	free(path);
	for (i = 0; i < n && i + 1 < len && (src_digit(buf[i]) ||
	    (buf[i] >= 'a' && buf[i] <= 'f')); i++)
		oid[i] = buf[i];
	oid[i] = '\0';
	/* sha1's 40 digits, sha256's 64 */
	if (i < 40) {
		sh_errf(e, "%s: no commit fetched", repo);
		return (-1);
	}
	return (0);
}

int
sh_src_record(FILE *out, const struct sh_src *s, struct sh_err *e)
{
	const struct sh_node *srcs;
	const struct sh_pair *pr;
	char oid[72], *repo, *url;
	size_t k;
	int r;

	repo = sh_root_path(s->sc_build, "src");
	url = sh_src_url(sh_obj_get(sh_obj_get(s->sc_site->ss_merged,
	    "remotes"), "src")->sn_str);
	if ((r = sh_src_commit(repo, oid, sizeof (oid), e)) == 0)
		(void) fprintf(out, "# src %s at %s, from %s\n",
		    sh_obj_get(s->sc_recipe->sr_merged, "src_branch")->sn_str,
		    oid, url);
	free(url);
	free(repo);
	srcs = sh_obj_get(s->sc_recipe->sr_merged, "patches");
	for (k = 0; r == 0 && srcs != NULL && k < srcs->sn_obj.ob_n; k++) {
		pr = &srcs->sn_obj.ob_v[k];
		if (src_local(pr->pr_key)) {
			(void) fprintf(out, "# patches %s\n", pr->pr_key);
			continue;
		}
		repo = src_clone(s, k);
		if ((r = sh_src_commit(repo, oid, sizeof (oid), e)) == 0)
			(void) fprintf(out, "# patches %s at %s\n", pr->pr_key,
			    oid);
		free(repo);
	}
	return (r);
}

void
sh_src_recipes(struct sh_job *j, const char *config,
    const struct sh_site *site)
{
	const struct sh_node *repos;
	const struct sh_pair *pr;
	char *dir, *url;
	size_t i;

	repos = sh_obj_get(sh_obj_get(site->ss_merged, "remotes"), "recipes");
	for (i = 0; repos != NULL && i < repos->sn_obj.ob_n; i++) {
		pr = &repos->sn_obj.ob_v[i];
		if (sh_root_kind(config, pr->pr_key) != 0)
			continue;
		dir = sh_root_path(config, pr->pr_key);
		url = sh_src_url(pr->pr_val->sn_str);
		src_git(j, pr->pr_key, "clone", "clone", "-q", url, dir, NULL);
		free(url);
		free(dir);
	}
}
