/*
 * A foundation's start. A crash part way through a roll is not a
 * failure (plan 6): whatever was written since the newest version is
 * undone, without a stall, and the next roll comes from the clock or a
 * person. One rule puts every foundation there, whatever the crash
 * left: a rollback to the newest snapshot changes nothing where
 * nothing was written, and a dataset with no snapshot holds no version.
 */

#include <stdlib.h>
#include <string.h>

#include "core/err.h"
#include "core/foundation.h"
#include "core/mem.h"
#include "core/zfs.h"

/* Names a walk found. */
struct names {
	char	**n_v;
	size_t	n_n;
};

static int
collect(void *arg, const char *name)
{
	struct names *n = arg;

	n->n_v = sh_realloc(n->n_v, (n->n_n + 1) * sizeof (char *));
	n->n_v[n->n_n++] = sh_strdup(name);
	return (0);
}

static void
names_free(struct names *n)
{
	while (n->n_n > 0)
		free(n->n_v[--n->n_n]);
	free(n->n_v);
}

/* A walk of snapshots: the newest's name into ARG. */
static int
last(void *arg, const char *snap)
{
	(void) strlcpy(arg, snap, 256);
	return (0);
}

/*
 * DS and everything below it put where an idle foundation is,
 * children first, since a filesystem with one mounted inside it will
 * not unmount and one with children will not be destroyed.
 */
static int
idle(struct sh_zfs *z, const char *ds, struct sh_err *e)
{
	struct names kids = { NULL, 0 };
	char newest[256] = "";
	size_t i;
	int r;

	r = sh_zfs_children(z, ds, collect, &kids, e);
	for (i = 0; r == 0 && i < kids.n_n; i++)
		r = idle(z, kids.n_v[i], e);
	names_free(&kids);
	if (r != 0 || sh_zfs_unmount(z, ds, e) != 0 ||
	    sh_zfs_snapshots(z, ds, last, newest, e) != 0)
		return (-1);
	if (newest[0] != '\0')
		return (sh_zfs_rollback(z, ds, newest, e));
	return (sh_zfs_destroy(z, ds, e));
}

/*
 * F started. What a step's refusal said is F's stall, and E hears it;
 * a head gone with its last version leaves F's name NULL.
 */
static void
start(struct sh_zfs *z, struct sh_foundation *f, struct sh_err *e)
{
	struct sh_err fe;
	char *buf = NULL, newest[256] = "", stall[1024];
	size_t len = 0;
	FILE *fp;
	int r;

	fp = sh_memstream(&buf, &len);
	sh_err_init(&fe, fp, "");
	if ((r = sh_zfs_snapshots(z, f->f_ds, last, newest, &fe)) == 0)
		r = idle(z, f->f_ds, &fe);
	if (r == 0 && newest[0] == '\0') {
		sh_errf(e, "foundation %s: a first build undone", f->f_name);
		free(f->f_name);
		f->f_name = NULL;
	} else if (r == 0 && (r = sh_zfs_user(z, f->f_ds, "shtetl:stall",
	    stall, sizeof (stall), &fe)) == 0) {
		f->f_state = SH_F_STALLED;
		f->f_stall = sh_strdup(stall);
	} else if (r < 0) {
		(void) fflush(fp);
		buf[strcspn(buf, "\n")] = '\0';
		sh_errf(e, "foundation %s: %s", f->f_name, buf);
		f->f_state = SH_F_STALLED;
		f->f_stall = sh_strdup(buf);
	}
	f->f_newest = newest[0] != '\0' ? sh_strdup(newest) : NULL;
	(void) fclose(fp);
	free(buf);
}

static int
by_name(const void *a, const void *b)
{
	return (strcmp(((const struct sh_foundation *)a)->f_name,
	    ((const struct sh_foundation *)b)->f_name));
}

int
sh_foundations_start(struct sh_zfs *z, const char *root,
    struct sh_foundation **fp, size_t *np, struct sh_err *e)
{
	struct names heads = { NULL, 0 };
	struct sh_foundation *f;
	size_t i, n = 0;

	*fp = NULL;
	*np = 0;
	if (sh_zfs_children(z, root, collect, &heads, e) != 0) {
		names_free(&heads);
		return (-1);
	}
	f = sh_calloc(heads.n_n + 1, sizeof (*f));
	for (i = 0; i < heads.n_n; i++) {
		f[n].f_ds = sh_strdup(heads.n_v[i]);
		f[n].f_name = sh_strdup(strrchr(heads.n_v[i], '/') + 1);
		start(z, &f[n], e);
		if (f[n].f_name != NULL)
			n++;
		else
			free(f[n].f_ds);
	}
	names_free(&heads);
	qsort(f, n, sizeof (*f), by_name);
	*fp = f;
	*np = n;
	return (0);
}

void
sh_foundations_free(struct sh_foundation *f, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++) {
		free(f[i].f_name);
		free(f[i].f_ds);
		free(f[i].f_newest);
		free(f[i].f_stall);
	}
	free(f);
}

void
sh_foundation_status(const struct sh_foundation *f, FILE *out)
{
	if (f->f_state == SH_F_STALLED)
		(void) fprintf(out, "foundation %s stalled %s\n", f->f_name,
		    f->f_stall);
	else
		(void) fprintf(out, "foundation %s idle %s\n", f->f_name,
		    f->f_newest);
}
