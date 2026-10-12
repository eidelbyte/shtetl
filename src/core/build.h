/*
 * shtetl build F (planning.md 7.1): a foundation built from its
 * recipe into zlake. Planned in full first, every check made, shown,
 * then run.
 */

#ifndef	SH_BUILD_H
#define	SH_BUILD_H

#include <stddef.h>
#include <stdio.h>

#include "core/cascade.h"
#include "core/err.h"
#include "core/spaces.h"
#include "core/syntax.h"
#include "core/tree.h"
#include "core/zfs.h"

#define	SH_BUILD_DIR	"/var/db/shtetl/build"	/* the run directories */
#define	SH_BUILD_PROP	"shtetl:build"		/* on the src root */
#define	SH_BUILD_DS	32	/* datasets of one foundation */
#define	SH_BUILD_NAME	256	/* a dataset's name */
#define	SH_BUILD_PATH	1024

/* One dataset of the foundation, made before anything is installed. */
struct sh_fds {
	char		fd_ds[SH_BUILD_NAME];	/* its full name */
	char		fd_rel[SH_BUILD_NAME];	/* below F; "" for F */
	int		fd_placeholder;	/* canmount=off, holds nothing */
	const struct sh_node *fd_props;		/* the recipe's, or NULL */
};

struct sh_fplan {
	char		fp_name[SH_BUILD_NAME];	/* F */
	struct sh_recipe fp_recipe;		/* foundations/F, resolved */
	struct sh_branch fp_branch;
	const char	*fp_src_branch;
	char		fp_ds[SH_BUILD_NAME];	/* lake/foundations/F */
	char		fp_obj[SH_BUILD_NAME];	/* lake/foundation-builds/F */
	int		fp_obj_there;
	char		fp_src[SH_BUILD_NAME];	/* the build root */
	char		fp_srcdir[SH_BUILD_PATH];	/* its mountpoint */
	char		fp_config[SH_BUILD_NAME];	/* the config root */
	char		fp_rundir[SH_BUILD_PATH];
	struct sh_fds	fp_dss[SH_BUILD_DS];
	size_t		fp_nds;
	long		fp_jobs;	/* build.make_jobs; 0: hw.ncpu */
};

/*
 * The plan for building the foundation TARGET (F, or foundations/F)
 * into P, its recipe read from the configuration root ROOT, with the
 * roots S found and the site's settings SITE. RUNBASE is where run
 * directories go (SH_BUILD_DIR; the tests' scratch). Warnings to W.
 * Refused, with every reason: no lake, build or config root; the
 * recipe refused; ver not met by src_branch; a build already under
 * way; a foundation of that name already built.
 */
int sh_build_plan(struct sh_zfs *z, const struct sh_spaces *s,
    const struct sh_node *site, const char *root, const char *target,
    const char *runbase, struct sh_fplan *p, struct sh_err *e,
    struct sh_err *w);

/* The plan, as the person confirming it reads it. */
void sh_build_show(FILE *out, const struct sh_fplan *p);

void sh_build_free(struct sh_fplan *p);

#endif	/* SH_BUILD_H */
