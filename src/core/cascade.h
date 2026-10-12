/*
 * The recipe cascade (planning.md 5.4): a recipe's layers, nearest
 * last, merged into the one recipe shtetl acts on.
 */

#ifndef	SH_CASCADE_H
#define	SH_CASCADE_H

#include <stddef.h>

#include "core/err.h"
#include "core/schema.h"
#include "core/tree.h"

/* How deep imports may nest. */
#define	SH_IMPORT_DEPTH	16

struct sh_layer {
	char		*sl_file;	/* common/web.conf, or (built-in) */
	struct sh_node	*sl_tree;	/* as parsed */
};

/*
 * A resolved recipe. The layers come in merge order: the built-in
 * layer when it has anything, default.conf after what it imports,
 * then the target's imports, each after its own, and the target.
 * In sr_merged a key's pr_from is the layer that added it and a
 * value's sn_from the layer that last set it, both pointers to a
 * layer's sl_file.
 */
struct sh_recipe {
	enum sh_kind	sr_kind;
	char		*sr_name;	/* nginx */
	char		*sr_file;	/* containers/nginx.conf */
	struct sh_layer	*sr_layers;
	size_t		sr_nlayers;
	struct sh_node	*sr_merged;
};

/*
 * Resolves TARGET, a recipe named by its path in the configuration
 * root ROOT with or without .conf (containers/nginx), into R. 0, or
 * -1 with every reason reported to E; sh_recipe_free cleans up R
 * either way.
 */
int sh_recipe_resolve(const char *root, const char *target,
    struct sh_recipe *r, struct sh_err *e);
void sh_recipe_free(struct sh_recipe *r);

/* The layer FROM (a pointer out of sr_merged) names, or NULL. */
const struct sh_layer *sh_recipe_layer(const struct sh_recipe *r,
    const char *from);

#endif	/* SH_CASCADE_H */
