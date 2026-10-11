/*
 * The keys of a recipe (planning.md 5.5), one table, and the check of
 * one layer against the kind of recipe it is merged into. The cascade
 * reads the same table to know how each key merges.
 */

#ifndef	SH_SCHEMA_H
#define	SH_SCHEMA_H

#include "core/err.h"
#include "core/tree.h"

enum sh_kind {
	SH_FOUNDATION = 0x1,
	SH_CONTAINER = 0x2,
	SH_SYSTEM = 0x4
};

#define	SH_ALLKINDS	(SH_FOUNDATION | SH_CONTAINER | SH_SYSTEM)

/* "foundation", "container", "system". */
const char *sh_kind_name(enum sh_kind kind);

/* What a key's value is. */
enum sh_vt {
	SH_V_IMPORT,		/* recipes merged in before this file */
	SH_V_RESET,		/* collections emptied before this file */
	SH_V_VER,		/* a version constraint */
	SH_V_STRING,		/* text, a heredoc */
	SH_V_FILE,		/* a path in the configuration root */
	SH_V_COLL		/* a keyed collection (5.4) */
};

/* The collections, each keyed by its element's identity (5.4). */
enum sh_coll {
	SH_C_NONE,
	SH_C_PKG,		/* package name: true */
	SH_C_DATASETS,		/* dataset path: true, or properties */
	SH_C_COMMANDS,		/* label: sh */
	SH_C_DERIVATIONS,	/* derived path: {source, command} */
	SH_C_REQUIRES,		/* container name: true */
	SH_C_PATCHES		/* location: true, or {ref, dir} */
};

struct sh_rkey {
	const char	*rk_name;
	unsigned	rk_kinds;	/* the kinds that take it */
	enum sh_vt	rk_vt;
	enum sh_coll	rk_coll;
	const char	*rk_twin;	/* kernconf and kernconf_file */
};

/* KEY's row, or NULL when no recipe takes it. */
const struct sh_rkey *sh_rkey(const char *key);

/*
 * Whether a container or system must keep every element its
 * foundation's layers gave this collection: pkg and datasets, the
 * superset of 7.4 (ruled 2026-10-09, H).
 */
int sh_coll_superset(enum sh_coll coll);

/*
 * Checks LAYER, the tree of FILE, against the keys a KIND recipe
 * takes: every problem reported to E, and their number returned. A
 * layer that passes may still be refused by the merge (5.4's drops)
 * or by the checks on the merged recipe.
 */
unsigned sh_schema_recipe(const struct sh_node *layer, enum sh_kind kind,
    const char *file, struct sh_err *e);

/*
 * Checks PROPS, an object of ZFS properties at PATH in FILE: a
 * dataset entry's, or a space's in shtetl.conf. The number of
 * problems reported.
 */
unsigned sh_schema_props(const struct sh_node *props, const char *file,
    const struct sh_path *path, struct sh_err *e);

#endif	/* SH_SCHEMA_H */
