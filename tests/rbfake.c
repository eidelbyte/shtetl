/*
 * A rebaser for the tests over the fake ZFS: see tests/rbfake.h.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/err.h"
#include "core/rebase.h"
#include "core/zfs.h"
#include "rbfake.h"

#define	RF_MAX		64	/* rebases, and settings */
#define	RF_NAME		512
#define	RF_PHASE	"zfs_rebase:phase"	/* the record's word */

/* One rebase the fake knows: a result, its starts, its documents. */
struct rf_run {
	char	rr_result[RF_NAME];	/* IDENT: the dataset carrying it */
	int	rr_starts;
	int	rr_clone;		/* the clone form */
	char	rr_pre[RF_NAME];	/* the dataset form's pre-apply */
	char	rr_res[RF_NAME];	/* the resolution */
};

/* A setting: the STEPth start of RESULT, its conflicts or its exit. */
struct rf_set {
	char		rs_result[RF_NAME];
	int		rs_step;
	const char	*const *rs_paths;
	int		rs_rc;
};

static struct rf_run rf_runs[RF_MAX];
static int rf_nruns;
static struct rf_set rf_sets[RF_MAX];
static int rf_nsets;
static int (*rf_pick)(const char *, void *);
static void *rf_pickarg;
static char *rf_log;
static size_t rf_loglen;

static void
rf_say(const char *fmt, const char *a, const char *b, const char *c,
    const char *d)
{
	char line[2048];
	size_t n;
	char *nl;

	n = (size_t)snprintf(line, sizeof (line), fmt, a, b, c, d);
	if ((nl = realloc(rf_log, rf_loglen + n + 2)) == NULL)
		return;
	rf_log = nl;
	(void) memcpy(rf_log + rf_loglen, line, n);
	rf_loglen += n;
	rf_log[rf_loglen++] = '\n';
	rf_log[rf_loglen] = '\0';
}

void
rbfake_reset(void)
{
	(void) memset(rf_runs, 0, sizeof (rf_runs));
	rf_nruns = 0;
	(void) memset(rf_sets, 0, sizeof (rf_sets));
	rf_nsets = 0;
	rf_pick = NULL;
	rf_pickarg = NULL;
	free(rf_log);
	rf_log = NULL;
	rf_loglen = 0;
}

void
rbfake_conflicts(const char *result, int step, const char *const *paths)
{
	struct rf_set *s = &rf_sets[rf_nsets++];

	(void) snprintf(s->rs_result, sizeof (s->rs_result), "%s", result);
	s->rs_step = step;
	s->rs_paths = paths;
	s->rs_rc = SH_RB_GATE;
}

void
rbfake_exit(const char *result, int step, int rc)
{
	struct rf_set *s = &rf_sets[rf_nsets++];

	(void) snprintf(s->rs_result, sizeof (s->rs_result), "%s", result);
	s->rs_step = step;
	s->rs_paths = NULL;
	s->rs_rc = rc;
}

void
rbfake_picker(int (*fn)(const char *path, void *arg), void *arg)
{
	rf_pick = fn;
	rf_pickarg = arg;
}

const char *
rbfake_log(void)
{
	return (rf_log != NULL ? rf_log : "");
}

static struct rf_run *
rf_find(const char *result)
{
	int i;

	for (i = 0; i < rf_nruns; i++) {
		if (strcmp(rf_runs[i].rr_result, result) == 0)
			return (&rf_runs[i]);
	}
	return (NULL);
}

const char *
rbfake_resolution(const char *ident)
{
	struct rf_run *r = rf_find(ident);

	return (r != NULL && r->rr_res[0] != '\0' ? r->rr_res : NULL);
}

static const struct rf_set *
rf_setting(const char *result, int step)
{
	int i;

	for (i = 0; i < rf_nsets; i++) {
		if (strcmp(rf_sets[i].rs_result, result) == 0 &&
		    rf_sets[i].rs_step == step)
			return (&rf_sets[i]);
	}
	return (NULL);
}

static struct sh_zfs *
rf_zfs(void)
{
	static struct sh_err e;
	struct sh_zfs *z;

	sh_err_init(&e, stderr, "rbfake: ");
	return (sh_zfs_open(&z, &e) == 0 ? z : NULL);
}

/* N bytes of S as the tree escapes a name. */
static void
rf_escape(FILE *f, const char *s, size_t n)
{
	size_t i;
	unsigned char c;

	for (i = 0; i < n; i++) {
		c = (unsigned char)s[i];
		if (c >= 0x21 && c <= 0x7e && c != '\\' && c != '#')
			(void) fputc(c, f);
		else
			(void) fprintf(f, "\\%03o", c);
	}
}

/*
 * The resolution at PATH: PATHS, given grouped by directory, each a
 * conflict of its own group, unanswered.
 */
static void
rf_resolution(const char *path, const char *const *paths)
{
	const char *open[32], *p, *comp[32];
	size_t olen[32], clen[32], depth, n, k, common, i, j;
	FILE *f;

	for (n = 0; paths[n] != NULL; n++)
		;
	if ((f = fopen(path, "w")) == NULL)
		return;
	(void) fprintf(f, "#rebase-resolution 5\n#base b 1\n#from f 2\n"
	    "#onto o 3\n#mode strict\n#names %zu\n#unanswered %zu\n/\n", n, n);
	depth = 0;
	for (i = 0; i < n; i++) {
		for (k = 0, p = paths[i] + 1; *p != '\0' && k < 32; k++) {
			comp[k] = p;
			clen[k] = strcspn(p, "/");
			p += clen[k];
			if (*p == '/')
				p++;
		}
		for (common = 0; common < depth && common + 1 < k &&
		    olen[common] == clen[common] &&
		    strncmp(open[common], comp[common], clen[common]) == 0;
		    common++)
			;
		while (depth > common) {
			depth--;
			for (j = 0; j <= depth; j++)
				(void) fputs("    ", f);
			(void) fputs("..\n", f);
		}
		for (; depth + 1 < k; depth++) {
			for (j = 0; j <= depth; j++)
				(void) fputs("    ", f);
			rf_escape(f, comp[depth], clen[depth]);
			(void) fputs("/\n", f);
			open[depth] = comp[depth];
			olen[depth] = clen[depth];
		}
		for (j = 0; j <= depth; j++)
			(void) fputs("    ", f);
		rf_escape(f, comp[k - 1], clen[k - 1]);
		(void) fprintf(f, " conflict %zu -\n", i + 1);
	}
	while (depth > 0) {
		depth--;
		for (j = 0; j <= depth; j++)
			(void) fputs("    ", f);
		(void) fputs("..\n", f);
	}
	(void) fputs("..\n", f);
	(void) fclose(f);
}

static int
rf_start(const struct sh_rebaser *self, const struct sh_rbstart *s, int log,
    struct sh_err *e)
{
	static const struct sh_zprop born[] = {
		{ "mountpoint", "none" }, { "readonly", "on" }
	};
	const struct rf_set *set;
	struct sh_zfs *z;
	struct rf_run *r;
	char ds[RF_NAME], snap[RF_NAME], val[64];
	const char *one;
	FILE *f;
	int clone;

	(void) self;
	(void) log;
	rf_say("start --from %s --onto %s --result %s%s", s->rs_from,
	    s->rs_onto, s->rs_result, s->rs_base != NULL ? " --base" : "");
	if (s->rs_base != NULL)
		rf_say("  base %s%s%s%s", s->rs_base, "", "", "");
	if ((z = rf_zfs()) == NULL)
		return (-1);
	clone = strchr(s->rs_onto, '@') != NULL;
	/* the dataset carrying the record: the clone, or onto */
	(void) snprintf(ds, sizeof (ds), "%s", clone ? s->rs_result :
	    s->rs_onto);
	if ((r = rf_find(ds)) == NULL) {
		if (rf_nruns == RF_MAX)
			return (-1);
		r = &rf_runs[rf_nruns++];
		(void) snprintf(r->rr_result, sizeof (r->rr_result), "%s", ds);
	}
	r->rr_starts++;
	set = rf_setting(ds, r->rr_starts);
	if (set != NULL && set->rs_rc == SH_RB_REFUSED)
		return (SH_RB_REFUSED);
	if (sh_zfs_get(z, ds, RF_PHASE, val, sizeof (val), NULL, e) == 0 &&
	    strcmp(val, "-") != 0) {
		sh_errf(e, "rbfake: %s is an open rebase", ds);
		return (SH_RB_REFUSED);
	}
	r->rr_clone = clone;
	if (clone) {
		if (sh_zfs_clone(z, s->rs_onto, s->rs_result, born, 2, e) != 0)
			return (SH_RB_REFUSED);
	} else {
		(void) snprintf(snap, sizeof (snap), "%s@%s", s->rs_onto,
		    s->rs_result);
		one = snap;
		if (sh_zfs_snapshot(z, &one, 1, NULL, 0, e) != 0)
			return (SH_RB_REFUSED);
		(void) snprintf(r->rr_pre, sizeof (r->rr_pre), "%s", snap);
	}
	if ((f = fopen(s->rs_manifest, "w")) != NULL) {
		(void) fprintf(f, "#rebase-manifest 5\n");
		(void) fclose(f);
	}
	(void) snprintf(r->rr_res, sizeof (r->rr_res), "%s.resolution",
	    s->rs_manifest);
	if (set != NULL && set->rs_rc == SH_RB_FAILED) {
		(void) sh_zfs_set(z, ds, RF_PHASE, "applying1", e);
		return (SH_RB_FAILED);
	}
	if (set == NULL || set->rs_paths == NULL) {
		r->rr_res[0] = '\0';
		r->rr_pre[0] = clone ? '\0' : r->rr_pre[0];
		return (SH_RB_DONE);
	}
	rf_resolution(r->rr_res, set->rs_paths);
	(void) sh_zfs_set(z, ds, RF_PHASE, "conflicts", e);
	return (SH_RB_GATE);
}

static int
rf_continue(const struct sh_rebaser *self, const char *ident,
    unsigned flags, int log, struct sh_err *e)
{
	struct sh_resolution res;
	struct sh_zfs *z;
	struct rf_run *r;
	char val[64];
	unsigned left;

	(void) self;
	(void) log;
	rf_say("continue %s%s%s%s", ident, flags & SH_RB_NOMERGE ?
	    " --no-merge" : "", flags & SH_RB_PICK ? " -i" : "", "");
	if ((z = rf_zfs()) == NULL)
		return (-1);
	if ((r = rf_find(ident)) == NULL || sh_zfs_get(z, ident, RF_PHASE,
	    val, sizeof (val), NULL, e) != 0 || strcmp(val, "-") == 0) {
		sh_errf(e, "rbfake: %s: no rebase to move", ident);
		return (SH_RB_REFUSED);
	}
	if ((flags & SH_RB_PICK) && rf_pick != NULL)
		(void) rf_pick(r->rr_res, rf_pickarg);
	if (sh_res_read(r->rr_res, &res, e) != 0) {
		sh_res_free(&res);
		return (SH_RB_GATE);
	}
	left = sh_res_unanswered(&res);
	sh_res_free(&res);
	if (left > 0 || (flags & SH_RB_NOMERGE))
		return (SH_RB_GATE);
	/* done: the record taken off */
	(void) sh_zfs_inherit(z, ident, RF_PHASE, e);
	return (SH_RB_DONE);
}

static int
rf_abort(const struct sh_rebaser *self, const char *ident, int log,
    struct sh_err *e)
{
	struct sh_zfs *z;
	struct rf_run *r;
	char val[64];
	const char *at;

	(void) self;
	(void) log;
	rf_say("abort %s%s%s%s", ident, "", "", "");
	if ((z = rf_zfs()) == NULL)
		return (-1);
	if ((r = rf_find(ident)) == NULL || sh_zfs_get(z, ident, RF_PHASE,
	    val, sizeof (val), NULL, e) != 0 || strcmp(val, "-") == 0) {
		sh_errf(e, "rbfake: %s: no rebase to move", ident);
		return (SH_RB_REFUSED);
	}
	if (r->rr_clone) {
		if (sh_zfs_destroy(z, ident, e) != 0)
			return (SH_RB_FAILED);
	} else {
		at = strchr(r->rr_pre, '@');
		if (sh_zfs_rollback(z, ident, at + 1, e) != 0 ||
		    sh_zfs_destroy(z, r->rr_pre, e) != 0)
			return (SH_RB_FAILED);
		(void) sh_zfs_inherit(z, ident, RF_PHASE, e);
	}
	r->rr_res[0] = '\0';
	return (SH_RB_DONE);
}

const struct sh_rebaser rbfake = {
	rf_start, rf_continue, rf_abort, "rbfake", NULL
};
