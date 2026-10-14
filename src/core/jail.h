/*
 * The jail's block shtetl writes for a container (planning.md 7.4,
 * ruled 2026-10-14): create's, an update's staging tree's, cutover's.
 */

#ifndef	SH_JAIL_H
#define	SH_JAIL_H

#include "core/build.h"
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

/* Whether the jail NAME runs: jls -j NAME, by the tools T. */
int sh_jail_running(const struct sh_tools *t, const char *name);

/*
 * jail -f FILE VERB NAME, VERB -c (start) or -r (stop), its output
 * appended to the file LOG. 0, or -1 said in E.
 */
int sh_jail_do(const struct sh_tools *t, const char *file, const char *name,
    const char *verb, const char *log, struct sh_err *e);

#endif	/* SH_JAIL_H */
