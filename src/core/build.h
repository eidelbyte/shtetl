/*
 * shtetl build F (planning.md 7.1): a foundation built from its
 * recipe into zlake. Planned in full first, every check made, shown,
 * then run.
 */

#ifndef	SH_BUILD_H
#define	SH_BUILD_H

#include <stddef.h>
#include <stdio.h>
#include <time.h>

#include "core/cascade.h"
#include "core/err.h"
#include "core/patch.h"
#include "core/spaces.h"
#include "core/syntax.h"
#include "core/tree.h"
#include "core/zfs.h"

#define	SH_BUILD_DIR	"/var/db/shtetl/build"	/* the run directories */
#define	SH_BUILD_PROP	"shtetl:build"		/* on the src root */
#define	SH_BUILD_PAUSED	"paused.conf"	/* in the run directory */
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

/*
 * What shtetl build F does: a new build, or one paused at a patch
 * (planning.md 7.1.1) carried on, the patch fixed by hand or left out,
 * or given up.
 */
enum sh_bverb {
	SH_BV_NEW,
	SH_BV_CONTINUE,
	SH_BV_SKIP,
	SH_BV_ABORT
};

struct sh_fplan {
	enum sh_bverb	fp_verb;
	struct sh_pause	fp_pause;		/* a paused build's record */
	char		fp_name[SH_BUILD_NAME];	/* F */
	struct sh_recipe fp_recipe;		/* foundations/F, resolved */
	struct sh_branch fp_branch;
	const char	*fp_src_branch;
	char		fp_ds[SH_BUILD_NAME];	/* lake/foundations/F */
	int		fp_update;	/* F there: its next version (7.2) */
	char		fp_last[SH_BUILD_NAME];	/* an update's last, @... */
	char		fp_next[SH_BUILD_NAME];	/* pkg-upgrade's, @... */
	char		fp_obj[SH_BUILD_NAME];	/* lake/foundation-builds/F */
	int		fp_obj_there;
	char		fp_src[SH_BUILD_NAME];	/* the build root */
	char		fp_srcdir[SH_BUILD_PATH];	/* its mountpoint */
	char		fp_config[SH_BUILD_NAME];	/* the config root */
	char		fp_root[SH_BUILD_PATH];	/* the recipe read there */
	char		fp_rundir[SH_BUILD_PATH];
	struct sh_fds	fp_dss[SH_BUILD_DS];
	size_t		fp_nds;
	long		fp_jobs;	/* build.make_jobs; 0: hw.ncpu */
};

/*
 * What a build runs, each by name (looked for in SH_RUN_PATH) or by
 * path: sh_build_tools, or the tests' fakes; and where it reads the
 * securelevel, which an update needs at 0 or below.
 */
struct sh_tools {
	const char	*tl_git;
	const char	*tl_make;
	const char	*tl_pkg;
	const char	*tl_chroot;
	const char	*tl_mount;
	const char	*tl_umount;
	const char	*tl_sh;
	const char	*tl_jail;
	const char	*tl_jexec;
	const char	*tl_jls;
	int		(*tl_securelevel)(void);
};

extern const struct sh_tools sh_build_tools;

/*
 * TARGET, F or foundations/F[.conf], as the foundation's name F into
 * NAME. Refused: another kind's path, a bad name.
 */
int sh_build_target(const char *target, char *name, size_t len,
    struct sh_err *e);

/*
 * The plan for building the foundation TARGET (F, or foundations/F)
 * into P, its recipe read from the configuration root ROOT, with the
 * roots S found and the site's settings SITE. RUNBASE is where run
 * directories go (SH_BUILD_DIR; the tests' scratch). Warnings to W.
 * Refused, with every reason: no lake, build or config root; the
 * recipe refused; ver not met by src_branch; a build already under
 * way or paused.
 *
 * F there already is updated (7.2): its next version, into the same
 * datasets, which must be the recipe's, at a securelevel (T's) of 0
 * or below. Refused: F there with no version; the datasets differing.
 *
 * VERB other than SH_BV_NEW carries on a build of F paused at a patch:
 * its record read from its run directory, and the recipe from the
 * config snapshot the build began with, in ROOT's .zfs/snapshot (not
 * for SH_BV_ABORT, which needs none). Refused: no build of F paused.
 */
int sh_build_plan(struct sh_zfs *z, const struct sh_spaces *s,
    const struct sh_node *site, const char *root, const char *target,
    const char *runbase, const struct sh_tools *t, enum sh_bverb verb,
    struct sh_fplan *p, struct sh_err *e, struct sh_err *w);

/* The plan, as the person confirming it reads it. */
void sh_build_show(FILE *out, const struct sh_fplan *p);


/*
 * The plan P carried out (7.1 steps 2 - 8), each step said on OUT:
 * the build root marked; the config root snapshotted; F's datasets
 * made; the checkout, and its REVISION checked against ver; F and the
 * obj cache mounted below the run directory; the kernel config and
 * src.conf written there; make buildworld buildkernel, then
 * installworld, installkernel and distribution with install -C; pkg
 * and the commands in a chroot of F; everything unmounted; F and its
 * datasets snapshotted as the version, NOW's day, carrying
 * shtetl:kind=version, shtetl:commit (MSG, or a message of the
 * build's own) and shtetl:config. On a failure, everything done is
 * undone, a new F's datasets destroyed, and the logs left in the run
 * directory. 0, or -1 with the reasons in E.
 *
 * With the recipe's patches (7.1.1): fetched, applied after the
 * checkout, the build root rolled back at the end, shtetl:patches on
 * the version. A patch that does not apply pauses the build: 1, with
 * what to do said on OUT, the record left in the run directory and
 * the build root as the patch left it. P's verb carries a paused
 * build on, or gives it up (0: undone). A --continue whose fix has
 * left a conflict marker or a .rej is refused and stays paused.
 *
 * An update makes no dataset, runs make delete-old delete-old-libs
 * before the install and removes installworld's backups after it; on
 * a failure, or --abort, F goes back to its last version.
 */
int sh_build_run(struct sh_zfs *z, struct sh_fplan *p,
    const struct sh_tools *t, const char *msg, time_t now, FILE *out,
    struct sh_err *e);

/*
 * shtetl pkg-upgrade F (planning.md 7.2, 6.1): F's last snapshot, a
 * version or the pkg upgrade of one, its packages upgraded in a chroot
 * of F, and F and its datasets snapshotted as that version's -pkg
 * (-pkg.2 and on after the first), shtetl:kind=pkg, carrying the
 * version's shtetl:config and shtetl:patches.
 * Planned into P: refused with no lake or build root, no such
 * foundation, F with no version, a build under way or paused (the
 * upgrade takes the build root's mark too). Run like a build, undone
 * like an update: F back to its last snapshot on a failure.
 */
int sh_pkg_plan(struct sh_zfs *z, const struct sh_spaces *s,
    const char *target, const char *runbase, struct sh_fplan *p,
    struct sh_err *e);
void sh_pkg_show(FILE *out, const struct sh_fplan *p);
int sh_pkg_run(struct sh_zfs *z, struct sh_fplan *p,
    const struct sh_tools *t, const char *msg, FILE *out,
    struct sh_err *e);

void sh_build_free(struct sh_fplan *p);

#endif	/* SH_BUILD_H */
