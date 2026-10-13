/*
 * Lists of datasets and snapshots (src/core/zwalk.h).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/err.h"
#include "core/zfs.h"
#include "core/zwalk.h"

char **
sh_zw_new(struct sh_err *e)
{
	char **list;

	if ((list = calloc(1, sizeof (*list))) == NULL)
		sh_err_nomem(e);
	return (list);
}

int
sh_zw_add(void *arg, const char *name)
{
	char ***list = arg;
	char **nl;
	size_t n;

	for (n = 0; (*list)[n] != NULL; n++)
		;
	if ((nl = realloc(*list, (n + 2) * sizeof (*nl))) == NULL)
		return (-1);
	*list = nl;
	if ((nl[n] = strdup(name)) == NULL)
		return (-1);
	nl[n + 1] = NULL;
	return (0);
}

void
sh_zw_free(char **list)
{
	size_t i;

	for (i = 0; list != NULL && list[i] != NULL; i++)
		free(list[i]);
	free(list);
}

long
sh_zw_index(char *const *list, const char *name)
{
	long i;

	for (i = 0; list[i] != NULL; i++) {
		if (strcmp(list[i], name) == 0)
			return (i);
	}
	return (-1);
}

char **
sh_zw_snaps(struct sh_zfs *z, const char *ds, struct sh_err *e)
{
	char **list;

	if ((list = sh_zw_new(e)) == NULL)
		return (NULL);
	if (sh_zfs_snapshots(z, ds, sh_zw_add, &list, e) != 0) {
		sh_zw_free(list);
		return (NULL);
	}
	return (list);
}

int
sh_zw_walk(struct sh_zfs *z, const char *ds, char ***all, struct sh_err *e)
{
	char **kids;
	size_t i;
	int r;

	if ((kids = sh_zw_new(e)) == NULL)
		return (-1);
	r = sh_zfs_children(z, ds, sh_zw_add, &kids, e);
	for (i = 0; r == 0 && kids[i] != NULL; i++) {
		if (sh_zw_add(all, kids[i]) != 0) {
			sh_err_nomem(e);
			r = -1;
		} else {
			r = sh_zw_walk(z, kids[i], all, e);
		}
	}
	sh_zw_free(kids);
	return (r);
}

int
sh_zw_destroy(struct sh_zfs *z, const char *ds, struct sh_err *e)
{
	char **kids, **snaps, name[1024];
	size_t i;
	int r;

	if ((kids = sh_zw_new(e)) == NULL)
		return (-1);
	r = sh_zfs_children(z, ds, sh_zw_add, &kids, e);
	for (i = 0; r == 0 && kids[i] != NULL; i++)
		r = sh_zw_destroy(z, kids[i], e);
	sh_zw_free(kids);
	if (r != 0 || (snaps = sh_zw_snaps(z, ds, e)) == NULL)
		return (-1);
	for (i = 0; r == 0 && snaps[i] != NULL; i++) {
		(void) snprintf(name, sizeof (name), "%s@%s", ds, snaps[i]);
		r = sh_zfs_destroy(z, name, e);
	}
	sh_zw_free(snaps);
	return (r != 0 ? -1 : sh_zfs_destroy(z, ds, e));
}
