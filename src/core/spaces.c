/*
 * Finding the spaces, and their skeleton.
 *
 * A root may not sit inside another (main's ruling Q-Y): the datasets
 * below a root are that space's objects, and a root inside them would
 * be walked as one. A run root under <lake>/foundations would be taken
 * for a foundation and rolled back.
 */

#include <stdlib.h>
#include <string.h>

#include "core/err.h"
#include "core/mem.h"
#include "core/spaces.h"
#include "core/zfs.h"

static const char *const sp_names[SH_SP_N] = {
	"lake", "config", "build", "run"
};

/*
 * What each space keeps below its root, made when the daemon starts,
 * so that nothing later asks whether a parent is there.
 */
static const struct {
	enum sh_space	sk_space;
	const char	*sk_name;
	struct sh_zprop	sk_prop;
} sp_skeleton[] = {
	{ SH_SP_LAKE, "foundations", { "canmount", "off" } }
};

/* Every root of every space the walk found, before the rules. */
struct sp_found {
	char		**sf_roots[SH_SP_N];
	size_t		sf_n[SH_SP_N];
	struct sh_err	*sf_err;
	int		sf_bad;
};

static int
sp_found(void *arg, const char *ds, const char *value)
{
	struct sp_found *f = arg;
	int i;

	for (i = 0; i < SH_SP_N && strcmp(value, sp_names[i]) != 0; i++)
		;
	if (i == SH_SP_N) {
		sh_errf(f->sf_err, "%s: invalid shtetl:space \"%s\"", ds,
		    value);
		f->sf_bad = 1;
		return (0);
	}
	f->sf_roots[i] = sh_realloc(f->sf_roots[i], (f->sf_n[i] + 1) *
	    sizeof (char *));
	f->sf_roots[i][f->sf_n[i]++] = sh_strdup(ds);
	return (0);
}

static int
sp_cmp(const void *a, const void *b)
{
	return (strcmp(*(char *const *)a, *(char *const *)b));
}

/* Whether IN is below OUT. */
static int
sp_inside(const char *in, const char *out)
{
	size_t n = strlen(out);

	return (strncmp(in, out, n) == 0 && in[n] == '/');
}

int
sh_spaces_find(struct sh_zfs *z, struct sh_spaces *sp, struct sh_err *e)
{
	struct sp_found f;
	size_t i, j;
	int a, b, r;

	(void) memset(sp, 0, sizeof (*sp));
	(void) memset(&f, 0, sizeof (f));
	f.sf_err = e;
	r = sh_zfs_local(z, "shtetl:space", sp_found, &f, e);
	for (a = 0; r == 0 && a < SH_SP_N; a++) {
		qsort(f.sf_roots[a], f.sf_n[a], sizeof (char *), sp_cmp);
		if (f.sf_n[a] > 1) {
			sh_errf(e, "two %s roots: %s and %s", sp_names[a],
			    f.sf_roots[a][0], f.sf_roots[a][1]);
			f.sf_bad = 1;
		}
		for (b = 0; b < SH_SP_N; b++) {
			for (i = 0; i < f.sf_n[a]; i++) {
				for (j = 0; j < f.sf_n[b]; j++) {
					if (!sp_inside(f.sf_roots[a][i],
					    f.sf_roots[b][j]))
						continue;
					sh_errf(e, "%s: %s root inside %s root "
					    "%s", f.sf_roots[a][i], sp_names[a],
					    sp_names[b], f.sf_roots[b][j]);
					f.sf_bad = 1;
				}
			}
		}
	}
	for (a = 0; a < SH_SP_N; a++) {
		for (i = 0; i < f.sf_n[a]; i++) {
			if (i == 0 && r == 0 && !f.sf_bad)
				sp->ss_root[a] = f.sf_roots[a][i];
			else
				free(f.sf_roots[a][i]);
		}
		free(f.sf_roots[a]);
	}
	return (r != 0 || f.sf_bad ? -1 : 0);
}

const char *
sh_space_name(enum sh_space sp)
{
	return (sp_names[sp]);
}

void
sh_spaces_free(struct sh_spaces *sp)
{
	int a;

	for (a = 0; a < SH_SP_N; a++)
		free(sp->ss_root[a]);
}

/* A walk of children that stops at the one named ARG. */
static int
sp_is(void *arg, const char *child)
{
	return (strcmp(child, arg) == 0);
}

int
sh_spaces_skeleton(struct sh_zfs *z, const struct sh_spaces *sp,
    struct sh_err *e)
{
	const char *root;
	char name[512];
	size_t i;
	int r;

	for (i = 0; i < sizeof (sp_skeleton) / sizeof (sp_skeleton[0]);
	    i++) {
		if ((root = sp->ss_root[sp_skeleton[i].sk_space]) == NULL)
			continue;
		(void) snprintf(name, sizeof (name), "%s/%s", root,
		    sp_skeleton[i].sk_name);
		if ((r = sh_zfs_children(z, root, sp_is, name, e)) < 0 ||
		    (r == 0 && sh_zfs_create(z, name, &sp_skeleton[i].sk_prop,
		    1, e) != 0))
			return (-1);
	}
	return (0);
}
