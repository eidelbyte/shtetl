/*
 * A container's jail (plan 7.3): made and removed from a block shtetl
 * writes whole at each start and never reads back, so that jail -r
 * undoes mount.devfs, which it does only for a jail its configuration
 * names (box run 1). The block holds what main's did (ruled 2026-10-14
 * on main): the path, the house's name as the hostname, rc's start and
 * stop, exec.clean, devfs with ruleset 4, the host's network
 * inherited, then the house's jailconf. The jail's name is the
 * house's, so jexec H and pkg -j H reach it.
 */

#ifndef	SH_JAIL_H
#define	SH_JAIL_H

#include "core/err.h"
#include "core/job.h"
#include "core/tree.h"

/*
 * The block of the jail NAME, its root at PATH, written whole to FILE,
 * replacing what is there: CONF's pieces after the fixed lines, each
 * line a tab in, blank ones left out; CONF is the shaken recipe's
 * jailconf, or NULL. Refused: "FILE: why".
 */
int sh_jail_write(const char *file, const char *name, const char *path,
    const struct sh_node *conf, struct sh_err *e);

/*
 * A step of J, a child named jail: jail -f FILE -cm NAME, which makes
 * the jail, and leaves one that runs already as it is, so a start
 * after a crash is a first start; or, where jls -j NAME finds it
 * running, jail -f FILE -r NAME, so that stopping one that is not, its
 * start having failed or a hand having removed it, is done already
 * rather than refused (m5-roll).
 */
void sh_jail_start(struct sh_job *j, const char *file, const char *name);
void sh_jail_stop(struct sh_job *j, const char *file, const char *name);

#endif	/* SH_JAIL_H */
