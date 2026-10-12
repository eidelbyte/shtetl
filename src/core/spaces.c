/*
 * Discovery and multi-rooting (planning.md 4.1).
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
#include "core/tree.h"
#include "core/zfs.h"

static const char *const sp_names[SH_SP_N] = {
	"lake", "config", "build", "run"
};

/* What the walk found, before the rules are applied. */
struct sp_found {
	struct sh_spaces	*sf_s;
	struct sh_err		*sf_err;
	int			sf_bad;
};

const char *
sh_space_name(enum sh_space sp)
{
	return ((unsigned)sp < SH_SP_N ? sp_names[sp] : "space");
}

static int
sp_add(struct sh_spaces *s, enum sh_space sp, const char *ds)
{
	char **nv, *d;

	if ((d = strdup(ds)) == NULL)
		return (-1);
	nv = realloc(s->ss_roots[sp], (s->ss_n[sp] + 1) * sizeof (*nv));
	if (nv == NULL) {
		free(d);
		return (-1);
	}
	s->ss_roots[sp] = nv;
	nv[s->ss_n[sp]++] = d;
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
	if (sp_add(f->sf_s, (enum sh_space)i, ds) != 0) {
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
	int a, b;

	(void) memset(s, 0, sizeof (*s));
	f.sf_s = s;
	f.sf_err = e;
	f.sf_bad = 0;
	if (sh_zfs_local(z, SH_SPACE_PROP, sp_found, &f, e) != 0)
		return (-1);
	for (a = 0; a < SH_SP_N; a++) {
		if (s->ss_n[a] > 1)
			qsort(s->ss_roots[a], s->ss_n[a], sizeof (char *),
			    sp_cmp);
	}
	for (a = SH_SP_CONFIG; a <= SH_SP_BUILD; a++) {
		if (s->ss_n[a] > 1) {
			sh_errf(e, "two %s roots: %s and %s", sp_names[a],
			    s->ss_roots[a][0], s->ss_roots[a][1]);
			f.sf_bad = 1;
		}
	}
	for (a = 0; a < SH_SP_N; a++) {
		for (i = 0; i < s->ss_n[a]; i++) {
			for (b = 0; b < SH_SP_N; b++) {
				for (j = 0; j < s->ss_n[b]; j++) {
					if (!sp_inside(s->ss_roots[a][i],
					    s->ss_roots[b][j]))
						continue;
					sh_errf(e, "%s: %s root inside %s "
					    "root %s", s->ss_roots[a][i],
					    sp_names[a], sp_names[b],
					    s->ss_roots[b][j]);
					f.sf_bad = 1;
				}
			}
		}
	}
	return (f.sf_bad ? -1 : 0);
}

void
sh_spaces_free(struct sh_spaces *s)
{
	size_t i;
	int a;

	for (a = 0; a < SH_SP_N; a++) {
		for (i = 0; i < s->ss_n[a]; i++)
			free(s->ss_roots[a][i]);
		free(s->ss_roots[a]);
	}
	(void) memset(s, 0, sizeof (*s));
}

int
sh_spaces_lookup(struct sh_zfs *z, const struct sh_spaces *s,
    enum sh_space sp, const char *rel, char **out, struct sh_err *e)
{
	char val[SH_ZFS_VALMAX], *name, *first;
	size_t i, len;
	int r;

	*out = NULL;
	first = NULL;
	for (i = 0; i < s->ss_n[sp]; i++) {
		len = strlen(s->ss_roots[sp][i]) + 1 + strlen(rel) + 1;
		if ((name = malloc(len)) == NULL) {
			sh_err_nomem(e);
			free(first);
			return (-1);
		}
		(void) snprintf(name, len, "%s/%s", s->ss_roots[sp][i], rel);
		r = sh_zfs_get(z, name, "type", val, sizeof (val), NULL, e);
		if (r == 1) {
			free(name);
			continue;
		}
		if (r != 0) {
			free(name);
			free(first);
			return (-1);
		}
		if (first != NULL) {
			sh_errf(e, "%s: under two %s roots: %s and %s", rel,
			    sp_names[sp], first, name);
			free(name);
			free(first);
			return (-1);
		}
		first = name;
	}
	if (first == NULL)
		return (1);
	*out = first;
	return (0);
}

/* Why there is no default root, for sp_default's callers. */
enum sp_why {
	SP_OK,
	SP_NONE,		/* no root */
	SP_UNNAMED,		/* several, and no default named */
	SP_NOTROOT		/* the default named is no root of the space */
};

static const char *
sp_default(const struct sh_spaces *s, const struct sh_node *site,
    enum sh_space sp, const struct sh_node **defp, enum sp_why *why)
{
	const struct sh_node *def;
	size_t i;

	def = sh_obj_get(sh_obj_get(sh_obj_get(site, "spaces"),
	    sp_names[sp]), "default");
	*defp = def;
	if (s->ss_n[sp] == 0) {
		*why = SP_NONE;
		return (NULL);
	}
	if (def == NULL || def->sn_type != SH_STR) {
		if (s->ss_n[sp] == 1) {
			*why = SP_OK;
			return (s->ss_roots[sp][0]);
		}
		*why = SP_UNNAMED;
		return (NULL);
	}
	for (i = 0; i < s->ss_n[sp]; i++) {
		if (strcmp(s->ss_roots[sp][i], def->sn_str) == 0) {
			*why = SP_OK;
			return (s->ss_roots[sp][i]);
		}
	}
	*why = SP_NOTROOT;
	return (NULL);
}

int
sh_spaces_default(const struct sh_spaces *s, const struct sh_node *site,
    enum sh_space sp, const char **root, struct sh_err *e)
{
	const struct sh_node *def;
	enum sp_why why;

	*root = sp_default(s, site, sp, &def, &why);
	switch (why) {
	case SP_OK:
		return (0);
	case SP_NONE:
		sh_errf(e, "no %s root", sp_names[sp]);
		break;
	case SP_UNNAMED:
		sh_errf(e, "%zu %s roots and no spaces.%s.default",
		    s->ss_n[sp], sp_names[sp], sp_names[sp]);
		break;
	case SP_NOTROOT:
		sh_errf(e, "%s: spaces.%s.default: %s is not a %s root",
		    def->sn_from, sp_names[sp], def->sn_str, sp_names[sp]);
		break;
	}
	return (-1);
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
	if (s->ss_n[SH_SP_CONFIG] == 0) {
		sh_errf(e, "no config root");
		return (-1);
	}
	root = s->ss_roots[SH_SP_CONFIG][0];
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
    const struct sh_node *site, struct sh_err *e)
{
	const struct sh_node *def;
	const char *dflt;
	char dir[SH_ZFS_VALMAX];
	enum sp_why w;
	size_t i;
	int sp, r;

	for (sp = 0; sp < SH_SP_N; sp++) {
		if (s->ss_n[sp] == 0) {
			(void) fprintf(out, "%-7s -  none: shtetl bootstrap "
			    "makes one\n", sp_names[sp]);
			continue;
		}
		dflt = NULL;
		if (sp == SH_SP_LAKE || sp == SH_SP_RUN)
			dflt = sp_default(s, site, (enum sh_space)sp, &def,
			    &w);
		for (i = 0; i < s->ss_n[sp]; i++) {
			(void) fprintf(out, "%-7s %s", sp_names[sp],
			    s->ss_roots[sp][i]);
			if (sp == SH_SP_CONFIG || sp == SH_SP_BUILD) {
				r = sp_where(z, s->ss_roots[sp][i], dir,
				    sizeof (dir), e);
				if (r < 0)
					return (-1);
				(void) fprintf(out, "  %s%s", r == 0 ?
				    "mounted at " : r == 1 ? "no mountpoint" :
				    "not mounted", r == 0 ? dir : "");
			} else if (dflt == s->ss_roots[sp][i] &&
			    s->ss_n[sp] > 1) {
				(void) fprintf(out, "  default");
			}
			(void) fputc('\n', out);
		}
		if ((sp == SH_SP_LAKE || sp == SH_SP_RUN) && dflt == NULL)
			(void) fprintf(out, "%-7s    no default: shtetl.conf "
			    "names none as spaces.%s.default\n", "",
			    sp_names[sp]);
	}
	return (0);
}
