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
		sh_errf(f->sf_err, "%s: %s is \"%s\", which names no space: "
		    "lake, config, build or run", ds, SH_SPACE_PROP, value);
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
			sh_errf(e, "two %s roots, %s and %s: the %s space has "
			    "one", sp_names[a], s->ss_roots[a][0],
			    s->ss_roots[a][1], sp_names[a]);
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
					sh_errf(e, "%s: a %s root inside "
					    "the %s root %s", s->ss_roots[a][i],
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
			sh_errf(e, "%s is under two %s roots, as %s and %s: "
			    "one name, one place", rel, sp_names[sp], first,
			    name);
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

int
sh_spaces_default(const struct sh_spaces *s, const struct sh_node *site,
    enum sh_space sp, const char **root, struct sh_err *e)
{
	const struct sh_node *def;
	size_t i;

	*root = NULL;
	def = sh_obj_get(sh_obj_get(sh_obj_get(site, "spaces"),
	    sp_names[sp]), "default");
	if (s->ss_n[sp] == 0) {
		sh_errf(e, "no %s root: shtetl bootstrap makes one",
		    sp_names[sp]);
		return (-1);
	}
	if (def == NULL || def->sn_type != SH_STR) {
		if (s->ss_n[sp] == 1) {
			*root = s->ss_roots[sp][0];
			return (0);
		}
		sh_errf(e, "%zu %s roots, and shtetl.conf names none of them "
		    "as spaces.%s.default", s->ss_n[sp], sp_names[sp],
		    sp_names[sp]);
		return (-1);
	}
	for (i = 0; i < s->ss_n[sp]; i++) {
		if (strcmp(s->ss_roots[sp][i], def->sn_str) == 0) {
			*root = s->ss_roots[sp][i];
			return (0);
		}
	}
	sh_errf(e, "%s: spaces.%s.default names %s, which is no %s root",
	    def->sn_from, sp_names[sp], def->sn_str, sp_names[sp]);
	return (-1);
}
