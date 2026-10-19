/*
 * The recipe cascade (docs/configuration.md 5, 6): a recipe's layers, nearest
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
	/* common/web.conf, or a kind's block: container in shtetl.conf */
	char		*sl_file;
	struct sh_node	*sl_tree;	/* as parsed */
	int		sl_default;	/* shtetl.conf, or what it imports */
};

/*
 * A resolved recipe. The layers come in merge order: the default
 * (shtetl.conf, after what it imports), then the recipe's imports,
 * each after its own, and the recipe's file. What a recipe's files
 * give again of the default's overrides it; the rest stacks
 * (docs/configuration.md 5).
 * In sr_merged a key's pr_from is the layer that added it and a
 * value's sn_from the layer that last set it, both pointers to a
 * layer's sl_file.
 */
struct sh_recipe {
	enum sh_kind	sr_kind;
	char		*sr_name;	/* nginx */
	char		*sr_file;	/* site/containers/nginx.conf */
	char		*sr_root;	/* site, or "" for zconfig's top */
	struct sh_layer	*sr_layers;
	size_t		sr_nlayers;
	struct sh_node	*sr_merged;
};

/*
 * Resolves TARGET, a recipe named by its kind's directory and name,
 * with or without .conf (containers/nginx), into R. ROOT is zconfig:
 * the recipe is looked for at its top and in each recipe repository
 * of REPOS (NULL-ended), a directory of it, and found in one; its
 * imports are read in the place it was found, the default's at the
 * top (plan 4). 0, or -1 with every reason reported to E;
 * sh_recipe_free cleans up R either way. A dataset property one file
 * overrides another's on is reported to W, when there is one, and
 * refuses nothing.
 */
int sh_recipe_resolve(const char *root, const char *const *repos,
    const char *target, struct sh_recipe *r, struct sh_err *e,
    struct sh_err *w);
void sh_recipe_free(struct sh_recipe *r);

/* The layer FROM (a pointer out of sr_merged) names, or NULL. */
const struct sh_layer *sh_recipe_layer(const struct sh_recipe *r,
    const char *from);

#endif	/* SH_CASCADE_H */
