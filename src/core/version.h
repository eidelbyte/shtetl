/*
 * A foundation version (planning.md 6.1): its name, and the recipe's
 * ver checked against what is built (7.1 step 1).
 */

#ifndef	SH_VERSION_H
#define	SH_VERSION_H

#include <stddef.h>
#include <time.h>

#include "core/err.h"
#include "core/syntax.h"
#include "core/tree.h"

/* @{ver}-{branch-type}-{YYYY-MM-DD}-{12 hex}.NNN, and room to spare. */
#define	SH_VERSION_MAX	64

/*
 * REVISION as newvers.sh -V REVISION prints it, the one line
 * REVISION="N.M", into *MAJOR and *MINOR. Refused: anything else.
 */
int sh_version_revision(const char *out, long *major, long *minor,
    struct sh_err *e);

/*
 * The name of a version of a foundation built from branch B on the
 * day WHEN (UTC) from the commit SHA (40 hex digits): 15.1-releng,
 * 15-stable or, MAJOR being REVISION's, 16-current; the day; the
 * commit's first 12 digits. When a snapshot already has that name
 * (among the NHAVE names in HAVE), .2, .3 and on.
 */
int sh_version_name(const struct sh_branch *b, long major, time_t when,
    const char *sha, const char *const *have, size_t nhave, char *out,
    size_t len, struct sh_err *e);

/*
 * The recipe FILE's merged ver (a list, or NULL) against branch B
 * before the checkout: releng/N.M must satisfy every constraint,
 * stable/N leave some N.x they all allow; main is not known yet.
 */
int sh_version_check_branch(const struct sh_branch *b,
    const struct sh_node *ver, const char *file, struct sh_err *e);

/* The same after the checkout: N.M, REVISION, satisfies them all. */
int sh_version_check(long major, long minor, const struct sh_node *ver,
    const char *file, struct sh_err *e);

#endif	/* SH_VERSION_H */
