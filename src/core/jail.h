/*
 * The jail's block shtetl writes for a container (planning.md 7.4,
 * ruled 2026-10-14): create's, an update's staging tree's, cutover's.
 */

#ifndef	SH_JAIL_H
#define	SH_JAIL_H

#include "core/err.h"
#include "core/tree.h"

/*
 * The block of the jail NAME written whole to FILE: its path PATH (the
 * container's mount, as ZFS reports it), its hostname HOST, rc's start
 * and stop, exec.clean, devfs (ruleset 4), the host's network inherited,
 * then MERGED's jailconf (a recipe's, or NULL), each line a tab in.
 */
int sh_jail_write(const char *file, const char *name, const char *host,
    const char *path, const struct sh_node *merged, struct sh_err *e);

#endif	/* SH_JAIL_H */
