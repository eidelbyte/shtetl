/*
 * Discovery (planning.md 4.1): one root to a space.
 *
 * Decided here, beyond the plan (PROPOSAL): a root may not sit inside
 * another root. Each root's layout is shtetl's own (4.4 - 4.7), and a
 * root inside it would be both a space and a part of another's.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/err.h"
#include "core/spaces.h"
#include "core/zfs.h"

static const char *const sp_names[SH_SP_N] = {
	"lake", "config", "build", "run"
};

/* What the walk found, every root of every space, before the rules. */
struct sp_found {
	char		**sf_roots[SH_SP_N];
	size_t		sf_n[SH_SP_N];
	struct sh_err	*sf_err;
	int		sf_bad;
};

const char *
sh_space_name(enum sh_space sp)
{
	return ((unsigned)sp < SH_SP_N ? sp_names[sp] : "space");
}

static int
sp_add(struct sp_found *f, enum sh_space sp, const char *ds)
{
	char **nv, *d;

	if ((d = strdup(ds)) == NULL)
		return (-1);
	nv = realloc(f->sf_roots[sp], (f->sf_n[sp] + 1) * sizeof (*nv));
	if (nv == NULL) {
		free(d);
		return (-1);
	}
	f->sf_roots[sp] = nv;
	nv[f->sf_n[sp]++] = d;
	return (0);
}

static int
sp_found(void *arg, const char *ds, const char *value)
{
	struct sp_found *f = arg;
	int i;

	for (i = 0; i < SH_SP_N; i++) {
		if (strcmp(value, sp_names[i]) == 0)
			break;
	}
	if (i == SH_SP_N) {
		sh_errf(f->sf_err, "%s: invalid %s \"%s\"", ds, SH_SPACE_PROP,
		    value);
		f->sf_bad = 1;
		return (0);
	}
	if (sp_add(f, (enum sh_space)i, ds) != 0) {
		sh_err_nomem(f->sf_err);
		return (-1);
	}
	return (0);
}

static int
sp_cmp(const void *a, const void *b)
{
	return (strcmp(*(char *const *)a, *(char *const *)b));
}

/* Whether IN is a dataset below OUT. */
static int
sp_inside(const char *in, const char *out)
{
	size_t n = strlen(out);

	return (strncmp(in, out, n) == 0 && in[n] == '/');
}

int
sh_spaces_discover(struct sh_zfs *z, struct sh_spaces *s, struct sh_err *e)
{
	struct sp_found f;
	size_t i, j;
	int a, b, r;

	(void) memset(s, 0, sizeof (*s));
	(void) memset(&f, 0, sizeof (f));
	f.sf_err = e;
	r = sh_zfs_local(z, SH_SPACE_PROP, sp_found, &f, e);
	for (a = 0; r == 0 && a < SH_SP_N; a++) {
		qsort(f.sf_roots[a], f.sf_n[a], sizeof (char *), sp_cmp);
		if (f.sf_n[a] > 1) {
			sh_errf(e, "two %s roots: %s and %s", sp_names[a],
			    f.sf_roots[a][0], f.sf_roots[a][1]);
			f.sf_bad = 1;
		}
		for (i = 0; i < f.sf_n[a]; i++) {
			for (b = 0; b < SH_SP_N; b++) {
				for (j = 0; j < f.sf_n[b]; j++) {
					if (!sp_inside(f.sf_roots[a][i],
					    f.sf_roots[b][j]))
						continue;
					sh_errf(e, "%s: %s root inside %s "
					    "root %s", f.sf_roots[a][i],
					    sp_names[a], sp_names[b],
					    f.sf_roots[b][j]);
					f.sf_bad = 1;
				}
			}
		}
	}
	for (a = 0; a < SH_SP_N; a++) {
		for (i = 0; i < f.sf_n[a]; i++) {
			if (i == 0 && r == 0 && !f.sf_bad)
				s->ss_root[a] = f.sf_roots[a][i];
			else
				free(f.sf_roots[a][i]);
		}
		free(f.sf_roots[a]);
	}
	return (r != 0 || f.sf_bad ? -1 : 0);
}

void
sh_spaces_free(struct sh_spaces *s)
{
	int a;

	for (a = 0; a < SH_SP_N; a++)
		free(s->ss_root[a]);
	(void) memset(s, 0, sizeof (*s));
}

int
sh_spaces_lookup(struct sh_zfs *z, const struct sh_spaces *s,
    enum sh_space sp, const char *rel, char **out, struct sh_err *e)
{
	char val[SH_ZFS_VALMAX], *name;
	size_t len;
	int r;

	*out = NULL;
	if (s->ss_root[sp] == NULL)
		return (1);
	len = strlen(s->ss_root[sp]) + 1 + strlen(rel) + 1;
	if ((name = malloc(len)) == NULL) {
		sh_err_nomem(e);
		return (-1);
	}
	(void) snprintf(name, len, "%s/%s", s->ss_root[sp], rel);
	r = sh_zfs_get(z, name, "type", val, sizeof (val), NULL, e);
	if (r != 0) {
		free(name);
		return (r == 1 ? 1 : -1);
	}
	*out = name;
	return (0);
}

/*
 * Where ROOT is mounted: 0 and the path in DIR; 1 when it has no
 * mountpoint (none, legacy); 2 when it is not mounted; -1 when ZFS
 * refuses.
 */
static int
sp_where(struct sh_zfs *z, const char *root, char *dir, size_t len,
    struct sh_err *e)
{
	char mounted[SH_ZFS_VALMAX];

	if (sh_zfs_get(z, root, "mountpoint", dir, len, NULL, e) != 0 ||
	    sh_zfs_get(z, root, "mounted", mounted, sizeof (mounted), NULL,
	    e) != 0)
		return (-1);
	if (dir[0] != '/')
		return (1);
	if (strcmp(mounted, "yes") != 0)
		return (2);
	return (0);
}

int
sh_spaces_config_dir(struct sh_zfs *z, const struct sh_spaces *s,
    char **dir, struct sh_err *e)
{
	char path[SH_ZFS_VALMAX];
	const char *root;
	int r;

	*dir = NULL;
	if ((root = s->ss_root[SH_SP_CONFIG]) == NULL) {
		sh_errf(e, "no config root");
		return (-1);
	}
	if ((r = sp_where(z, root, path, sizeof (path), e)) < 0)
		return (-1);
	if (r > 0) {
		sh_errf(e, "config root %s: %s", root, r == 1 ?
		    "no mountpoint" : "not mounted");
		return (-1);
	}
	if ((*dir = strdup(path)) == NULL) {
		sh_err_nomem(e);
		return (-1);
	}
	return (0);
}

int
sh_spaces_show(FILE *out, struct sh_zfs *z, const struct sh_spaces *s,
    struct sh_err *e)
{
	char dir[SH_ZFS_VALMAX];
	int sp, r;

	for (sp = 0; sp < SH_SP_N; sp++) {
		if (s->ss_root[sp] == NULL) {
			(void) fprintf(out, "%-7s -  none: shtetl bootstrap "
			    "makes one\n", sp_names[sp]);
			continue;
		}
		(void) fprintf(out, "%-7s %s", sp_names[sp], s->ss_root[sp]);
		if (sp == SH_SP_CONFIG || sp == SH_SP_BUILD) {
			r = sp_where(z, s->ss_root[sp], dir, sizeof (dir), e);
			if (r < 0)
				return (-1);
			(void) fprintf(out, "  %s%s", r == 0 ? "mounted at " :
			    r == 1 ? "no mountpoint" : "not mounted",
			    r == 0 ? dir : "");
		}
		(void) fputc('\n', out);
	}
	return (0);
}
