/*
 * The shaken recipe in what it built (plan 5): written into a tree,
 * read-only and immutable, and read back from a snapshot. What the
 * shaken recipe says is what show prints of the recipe, merged, with
 * its layers; a house's is stacked on its foundation version's
 * (sh_recipe_resolve's UNDER).
 *
 * The tree is not trusted: a replayed save can hold a link anywhere in
 * it, so each component of a path is opened beneath the one before it,
 * never through a link.
 */

#ifndef	SH_SHAKE_H
#define	SH_SHAKE_H

#include <stddef.h>

#include "core/err.h"
#include "core/tree.h"

/* Where the shaken recipes are in what they built. */
#define	SH_SHAKEN_FOUNDATION	"var/db/shtetl/foundation.conf"
#define	SH_SHAKEN_HOUSE		"var/db/shtetl/house.conf"

/*
 * TEXT, LEN bytes, written whole as REL in the tree at DIR, replacing
 * what is there (its flags cleared first), mode 0444 and FLAGS set:
 * SF_IMMUTABLE, schg, as shtetl writes it; in a test, a flag its user
 * may set. A directory missing on the way is made, 0755. Refused: a
 * link on the way, "PATH: a link"; what the system says.
 */
int sh_shaken_write(const char *dir, const char *rel, const char *text,
    size_t len, unsigned long flags, struct sh_err *e);

/*
 * REL of the snapshot SNAP of the filesystem mounted at DIR, read
 * through DIR/.zfs/snapshot/SNAP and parsed with NAME as its layer:
 * the tree, or NULL with the reason in E. A link on the way is
 * refused.
 */
struct sh_node *sh_shaken_read(const char *dir, const char *snap,
    const char *rel, const char *name, struct sh_err *e);

#endif	/* SH_SHAKE_H */
