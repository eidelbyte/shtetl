/*
 * The tests' zfs_rebase over the fake ZFS: see tests/rbfake.h.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "core/err.h"
#include "core/job.h"
#include "core/mem.h"
#include "core/rebase.h"
#include "core/zfs.h"
#include "rbfake.h"

#define	RF_ENDS	32
#define	RF_PRE	"zfs_rebase:fakepre"	/* the fake's: a dataset form's */

static struct {
	char	re_ident[512];
	int	re_rc;
} rf_ends[RF_ENDS];
static int rf_nends;

void
rbfake_end(const char *ident, int rc)
{
	if (rf_nends == RF_ENDS)
		return;
	(void) snprintf(rf_ends[rf_nends].re_ident,
	    sizeof (rf_ends[rf_nends].re_ident), "%s", ident);
	rf_ends[rf_nends++].re_rc = rc;
}

void
rbfake_reset(void)
{
	rf_nends = 0;
}

static int
rf_rc(const char *ident)
{
	int i;

	for (i = 0; i < rf_nends; i++) {
		if (strcmp(rf_ends[i].re_ident, ident) == 0)
			return (rf_ends[i].re_rc);
	}
	return (0);
}

/* The start's words, a line of $FAKE_LOG. */
static void
rf_log(char *const *v)
{
	const char *path = getenv("FAKE_LOG");
	FILE *f;

	if (path == NULL || (f = fopen(path, "a")) == NULL)
		return;
	(void) fputs("zfs_rebase", f);
	for (; *v != NULL; v++)
		(void) fprintf(f, " %s", *v);
	(void) fputc('\n', f);
	(void) fclose(f);
}

/* The run ended RC, the job told so as it is of the program. */
static void
rf_exit(int rc)
{
	if (rc != 0)
		(void) printf("exited %d\n", rc);
	(void) fflush(stdout);
	_exit(rc);
}

/* Whether the dataset of NAME, a snapshot or a filesystem, is mounted. */
static int
rf_mounted(struct sh_zfs *z, const char *name, struct sh_err *e)
{
	char ds[512], at[1024];

	(void) snprintf(ds, sizeof (ds), "%.*s", (int)strcspn(name, "@"),
	    name);
	return (sh_zfs_mounted(z, ds, at, sizeof (at), e) == 1);
}

struct rf_local {
	const char	*rl_prop;
	char		rl_val[512];
	int		rl_got;
};

static int
rf_local_one(void *arg, const char *prop, const char *value)
{
	struct rf_local *l = arg;

	if (strcmp(prop, l->rl_prop) == 0) {
		(void) snprintf(l->rl_val, sizeof (l->rl_val), "%s", value);
		l->rl_got = 1;
	}
	return (0);
}

/* PROP as set on DS itself into L. */
static void
rf_local(struct sh_zfs *z, const char *ds, const char *prop,
    struct rf_local *l, struct sh_err *e)
{
	l->rl_prop = prop;
	l->rl_got = 0;
	(void) sh_zfs_props(z, ds, rf_local_one, l, e);
}

static void
rf_unrecord(struct sh_zfs *z, const char *ds, struct sh_err *e)
{
	(void) sh_zfs_inherit(z, ds, SH_REBASE_TAG, e);
	(void) sh_zfs_inherit(z, ds, SH_REBASE_PHASE, e);
	(void) sh_zfs_inherit(z, ds, RF_PRE, e);
}

/*
 * --abort DS: the run DS carries undone, as if it never happened; from
 * applying2, only stepped back to the conflicts gate.
 */
static void
rf_abort(struct sh_zfs *z, const char *ds, struct sh_err *e)
{
	struct rf_local tag, pre, phase;
	char snap[1024];

	rf_local(z, ds, SH_REBASE_TAG, &tag, e);
	if (!tag.rl_got) {
		(void) printf("zfs_rebase: %s: no rebase\n", ds);
		rf_exit(2);
	}
	rf_local(z, ds, SH_REBASE_PHASE, &phase, e);
	if (phase.rl_got && strcmp(phase.rl_val, "applying2") == 0)
		rf_exit(sh_zfs_set(z, ds, SH_REBASE_PHASE, "conflicts",
		    e) == 0 ? 0 : 3);
	rf_local(z, ds, RF_PRE, &pre, e);
	if (!pre.rl_got) {
		/* the clone form's result: made by the run, so taken away */
		rf_exit(sh_zfs_destroy(z, ds, e) == 0 ? 0 : 3);
	}
	(void) snprintf(snap, sizeof (snap), "%s@%s", ds, pre.rl_val);
	if (sh_zfs_rollback(z, ds, pre.rl_val, e) != 0 ||
	    sh_zfs_destroy(z, snap, e) != 0)
		rf_exit(3);
	rf_unrecord(z, ds, e);
	rf_exit(sh_zfs_mount(z, ds, e) == 0 ? 0 : 3);
}

/*
 * -c [-i] IDENT: the run stopped at its conflicts taken on from the
 * gate; with -i, a person at the picker, the fake's a line on standard
 * input: "ok" answers every name, and the run goes to done; anything
 * else, or none, and the gate stands, exit 1, as headless.
 */
static void
rf_continue(struct sh_zfs *z, const char *ident, int inter, struct sh_err *e)
{
	struct rf_local tag, pre;
	char ds[512], line[64];

	(void) snprintf(ds, sizeof (ds), "%.*s", (int)strcspn(ident, "@"),
	    ident);
	rf_local(z, ds, SH_REBASE_TAG, &tag, e);
	if (!tag.rl_got) {
		(void) printf("zfs_rebase: %s: no rebase\n", ident);
		rf_exit(2);
	}
	if (!inter || fgets(line, sizeof (line), stdin) == NULL ||
	    strcmp(line, "ok\n") != 0)
		rf_exit(1);
	rf_local(z, ds, RF_PRE, &pre, e);
	rf_unrecord(z, ds, e);
	/* the dataset form's dataset, unmounted for the run, back */
	if (pre.rl_got && sh_zfs_mount(z, ds, e) != 0)
		rf_exit(3);
	rf_exit(0);
}

/* One start, in the job's forked child: ARG its words. */
static int
rf_run(struct sh_job *j, void *arg, struct sh_err *e)
{
	static const struct sh_zprop none[] = {
		{ "readonly", "on" }, { "mountpoint", "none" }
	};
	const char *from = NULL, *onto = NULL, *result = NULL;
	const char *base = NULL, *named = NULL;
	char **v = arg, ident[1024], snap[1024];
	const char *names[1];
	struct sh_zfs *z;
	int i, u = 0, rc, clone, cont = 0, inter = 0;

	(void) j;
	rf_log(v);
	if (sh_zfs_open(&z, e) != 0)
		rf_exit(3);
	for (i = 0; v[i] != NULL; i++) {
		if (strcmp(v[i], "-m") == 0)
			continue;
		if (strcmp(v[i], "-u") == 0)
			u = 1;
		else if (strcmp(v[i], "-c") == 0)
			cont = 1;
		else if (strcmp(v[i], "-i") == 0)
			inter = 1;
		else if (cont && v[i][0] != '-')
			named = v[i];
		else if (strcmp(v[i], "--abort") == 0 && v[i + 1] != NULL)
			rf_abort(z, v[i + 1], e);
		else if (strcmp(v[i], "-b") == 0 && v[i + 1] != NULL)
			base = v[++i];
		else if (strcmp(v[i], "--from") == 0 && v[i + 1] != NULL)
			from = v[++i];
		else if (strcmp(v[i], "--onto") == 0 && v[i + 1] != NULL)
			onto = v[++i];
		else if (strcmp(v[i], "--result") == 0 && v[i + 1] != NULL)
			result = v[++i];
		else
			rf_exit(2);
	}
	if (cont && named != NULL)
		rf_continue(z, named, inter, e);
	if (from == NULL || onto == NULL || result == NULL)
		rf_exit(2);
	clone = strchr(onto, '@') != NULL;
	if (clone)
		(void) snprintf(ident, sizeof (ident), "%s", result);
	else
		(void) snprintf(ident, sizeof (ident), "%s@%s", onto, result);
	/* refused, as the program refuses, before anything is touched */
	if (!rf_mounted(z, from, e) || !rf_mounted(z, onto, e) ||
	    (base != NULL && !rf_mounted(z, base, e))) {
		(void) printf("zfs_rebase: a side is not mounted\n");
		rf_exit(2);
	}
	if (!clone && (!u || base == NULL))
		rf_exit(2);
	if ((rc = rf_rc(ident)) == 2)
		rf_exit(2);
	if (clone) {
		if (sh_zfs_clone(z, onto, result, none, 2, e) != 0)
			rf_exit(3);
		(void) sh_zfs_set(z, result, SH_REBASE_TAG, "zr-fake", e);
		(void) sh_zfs_set(z, result, SH_REBASE_PHASE, "applying1", e);
	} else {
		(void) snprintf(snap, sizeof (snap), "%s@%s", onto, result);
		names[0] = snap;
		if (sh_zfs_snapshot(z, names, 1, NULL, 0, e) != 0)
			rf_exit(3);
		(void) sh_zfs_set(z, onto, SH_REBASE_TAG, "zr-fake", e);
		(void) sh_zfs_set(z, onto, SH_REBASE_PHASE, "applying1", e);
		(void) sh_zfs_set(z, onto, RF_PRE, result, e);
		/* the take: one somebody is using will not unmount */
		if (sh_zfs_unmount(z, onto, e) != 0) {
			rf_unrecord(z, onto, e);
			(void) sh_zfs_destroy(z, snap, e);
			rf_exit(2);
		}
	}
	if (rc == -1) {
		for (;;)
			(void) pause();
	}
	if (rc == -2) {
		(void) sleep(1);
		rc = 0;
	}
	/* stopped at the conflicts gate; failed part way past it */
	if (rc == 1 || rc == 3)
		(void) sh_zfs_set(z, clone ? result : onto, SH_REBASE_PHASE,
		    rc == 1 ? "conflicts" : "applying2", e);
	if (rc != 0)
		rf_exit(rc);
	rf_unrecord(z, clone ? result : onto, e);
	if (!clone && sh_zfs_mount(z, onto, e) != 0)
		rf_exit(3);
	rf_exit(0);
	return (0);
}

void
sh_rebase(struct sh_job *j, const char *step, const char *const *argv)
{
	char **v;
	size_t i, n;

	for (n = 0; argv[n] != NULL; n++)
		;
	/* kept for the forked call, a test's starts being few */
	v = sh_calloc(n + 1, sizeof (*v));
	for (i = 0; i < n; i++)
		v[i] = sh_strdup(argv[i]);
	sh_job_fork(j, step, rf_run, v);
}
