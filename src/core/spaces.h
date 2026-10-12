/*
 * The four spaces and their roots (planning.md 4.1): every filesystem
 * of every imported pool with a local shtetl:space is a root of the
 * space it names. lake and run may have several, config and build
 * one each.
 */

#ifndef	SH_SPACES_H
#define	SH_SPACES_H

#include <stddef.h>
#include <stdio.h>

#include "core/err.h"
#include "core/tree.h"
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
	char	**ss_roots[SH_SP_N];	/* each space's roots, by name */
	size_t	ss_n[SH_SP_N];
};

/*
 * Finds the roots into S. Refused: a value that names no space; two
 * config or two build roots; a root inside another root. Each space's
 * roots come sorted by name. sh_spaces_free cleans up either way.
 */
int sh_spaces_discover(struct sh_zfs *z, struct sh_spaces *s,
    struct sh_err *e);
void sh_spaces_free(struct sh_spaces *s);

/*
 * REL, a path below a root (foundations/F), under the roots of SP:
 * 0 and the one match's full name in *OUT (the caller frees it), 1
 * and nothing reported when no root has it, -1 when two do (one
 * name, one place: 4.1) or ZFS refuses.
 */
int sh_spaces_lookup(struct sh_zfs *z, const struct sh_spaces *s,
    enum sh_space sp, const char *rel, char **out, struct sh_err *e);

/*
 * Where something new goes in SP, lake or run: its one root, or with
 * several, the one spaces.<sp>.default names in the site's settings
 * SITE (the merged tree of src/core/site.c, or NULL). Refused: no
 * root; several and no default; a default that is no root of SP.
 */
int sh_spaces_default(const struct sh_spaces *s, const struct sh_node *site,
    enum sh_space sp, const char **root, struct sh_err *e);

/*
 * The directory the configuration is read from: the config root's
 * mountpoint, where it is mounted (the caller frees it). Refused: no
 * config root; no mountpoint (none, legacy); not mounted.
 */
int sh_spaces_config_dir(struct sh_zfs *z, const struct sh_spaces *s,
    char **dir, struct sh_err *e);

/*
 * shtetl spaces: each space's roots, a line each. With several lake or
 * run roots, the default is marked (SITE, the settings, may be NULL);
 * config and build say where they are mounted.
 */
int sh_spaces_show(FILE *out, struct sh_zfs *z, const struct sh_spaces *s,
    const struct sh_node *site, struct sh_err *e);

#endif	/* SH_SPACES_H */
