/*
 * The four spaces and their roots (planning.md 4.1): the filesystem of
 * an imported pool with a local shtetl:space is the root of the space
 * it names, one root to a space.
 */

#ifndef	SH_SPACES_H
#define	SH_SPACES_H

#include <stdio.h>

#include "core/err.h"
#include "core/zfs.h"

#define	SH_SPACE_PROP	"shtetl:space"

enum sh_space {
	SH_SP_LAKE,
	SH_SP_CONFIG,
	SH_SP_BUILD,
	SH_SP_RUN
};

#define	SH_SP_N	4

/* "lake", "config", "build", "run". */
const char *sh_space_name(enum sh_space sp);

struct sh_spaces {
	char	*ss_root[SH_SP_N];	/* each space's root, or NULL */
};

/*
 * Finds the roots into S. Refused: a value that names no space; two
 * roots of one space; a root inside another root. sh_spaces_free
 * cleans up either way.
 */
int sh_spaces_discover(struct sh_zfs *z, struct sh_spaces *s,
    struct sh_err *e);
void sh_spaces_free(struct sh_spaces *s);

/*
 * REL, a path below SP's root (foundations/F): 0 and its full name in
 * *OUT (the caller frees it); 1 and nothing reported when it is not
 * there, or SP has no root; -1 when ZFS refuses.
 */
int sh_spaces_lookup(struct sh_zfs *z, const struct sh_spaces *s,
    enum sh_space sp, const char *rel, char **out, struct sh_err *e);

/*
 * The directory the configuration is read from: the config root's
 * mountpoint, where it is mounted (the caller frees it). Refused: no
 * config root; no mountpoint (none, legacy); not mounted.
 */
int sh_spaces_config_dir(struct sh_zfs *z, const struct sh_spaces *s,
    char **dir, struct sh_err *e);

/*
 * shtetl spaces: each space's root, a line each; config and build say
 * where they are mounted.
 */
int sh_spaces_show(FILE *out, struct sh_zfs *z, const struct sh_spaces *s,
    struct sh_err *e);

#endif	/* SH_SPACES_H */
