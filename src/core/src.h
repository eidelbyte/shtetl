/*
 * What a foundation's roll fetches (plan 6 steps 1 and 2, plan 4): the
 * FreeBSD src into zbuild, the recipe's patches onto it, and a recipe
 * repository missing from zconfig. Each git is a child of a job.
 *
 * zbuild holds src, the checkout, and patches/N, the recipe's N'th
 * patch source as the roll under way fetched it; a directory of
 * zconfig is read where it is. A patch applies with git apply --3way,
 * which merges three ways where the checkout has the patch's base, and
 * applies it as it is where not: a clean merge goes on, one already in
 * included, and a conflict fails. The reset that starts every roll
 * writes each file a patch changed anew, so its time is now's and make
 * builds it again over the obj cache (main 8b27e68).
 */

#ifndef	SH_SRC_H
#define	SH_SRC_H

#include "core/cascade.h"
#include "core/job.h"
#include "core/site.h"

struct sh_src {
	const char		*sc_build;	/* zbuild's mountpoint */
	const char		*sc_config;	/* zconfig's */
	const struct sh_site	*sc_site;	/* remotes.src */
	const struct sh_recipe	*sc_recipe;	/* the foundation's */
};

/*
 * Roll steps 1 and 2 added to J: the checkout fetched from the src
 * remote and reset hard and cleaned to the head of the recipe's
 * src_branch; each patch source from a remote fetched at its ref; then
 * the patches, the sources in the recipe's order and each one's
 * *.patch in natural order. S outlives J's steps.
 */
void sh_src_steps(struct sh_job *j, struct sh_src *s);

/* Steps added to J: the checkout of zbuild BUILD put back at its commit. */
void sh_src_reset(struct sh_job *j, const char *build);

/*
 * Each recipe repository SITE names that is missing from zconfig
 * CONFIG cloned into its directory, as steps of J. One that is there
 * is the person's, and never touched (plan 4).
 */
void sh_src_recipes(struct sh_job *j, const char *config,
    const struct sh_site *site);

/*
 * A remote as git takes it, allocated: a URL as written, a host and a
 * path over https.
 */
char *sh_src_url(const char *remote);

/*
 * A and B in natural order: runs of digits compare as numbers, the
 * rest byte by byte, and names equal so far (2, 02) by their bytes.
 */
int sh_natcmp(const char *a, const char *b);

#endif	/* SH_SRC_H */
