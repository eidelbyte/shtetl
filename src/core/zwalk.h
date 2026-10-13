/*
 * Lists of datasets and snapshots, as beam, save and create walk them.
 */

#ifndef	SH_ZWALK_H
#define	SH_ZWALK_H

#include "core/err.h"
#include "core/zfs.h"

/* A new list, NULL-ended and empty; NULL, said in E, out of memory. */
char **sh_zw_new(struct sh_err *e);

/* NAME added to the list ARG (char ***), as a walk's FN: -1 no memory. */
int sh_zw_add(void *arg, const char *name);

void sh_zw_free(char **list);

/* The index of NAME in LIST, or -1. */
long sh_zw_index(char *const *list, const char *name);

/* DS's snapshots, the names after the @, oldest first; NULL: refused. */
char **sh_zw_snaps(struct sh_zfs *z, const char *ds, struct sh_err *e);

/* Every filesystem below DS, parents first, by full name, added to *ALL. */
int sh_zw_walk(struct sh_zfs *z, const char *ds, char ***all,
    struct sh_err *e);

/*
 * DS, every filesystem below it and their snapshots destroyed, deepest
 * first.
 */
int sh_zw_destroy(struct sh_zfs *z, const char *ds, struct sh_err *e);

#endif	/* SH_ZWALK_H */
