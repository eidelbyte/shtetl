/*
 * shtetl cutover X and shtetl abandon X (planning.md 7.9, 7.10): an
 * update's staging tree made X, the old X archived in zlake; or the
 * staging tree given up, X carrying on.
 */

#ifndef	SH_CUTOVER_H
#define	SH_CUTOVER_H

#include <stddef.h>
#include <stdio.h>
#include <time.h>

#include "core/build.h"
#include "core/cascade.h"
#include "core/err.h"
#include "core/spaces.h"
#include "core/zfs.h"

#define	SH_CUT_DS	64
#define	SH_CUT_NAME	256

struct sh_kplan {
	int		kp_abandon;
	char		kp_name[SH_CUT_NAME];		/* X */
	char		kp_stagename[SH_CUT_NAME];	/* X-staging */
	char		kp_run[SH_CUT_NAME];		/* run/containers/X */
	char		kp_stage[SH_CUT_NAME];		/* .../X-staging */
	char		kp_lake[SH_CUT_NAME];		/* lake/containers/X */
	char		kp_lakestage[SH_CUT_NAME];	/* .../X-staging */
	char		kp_archive[SH_CUT_NAME * 2];	/* lake/archive/... */
	char		kp_old[SH_CUT_NAME];		/* X's version */
	char		kp_jailfile[1024];		/* X's block */
	char		kp_stagejail[1024];		/* X-staging's */
	char		kp_rundir[1024];		/* the update's */
	char		kp_version[SH_CUT_NAME];	/* the target */
	char		kp_from[SH_CUT_NAME];		/* X's from-save */
	struct sh_recipe kp_recipe;			/* containers/X */
	/* X's datasets, and what each wrote since the from-save */
	char		kp_xrel[SH_CUT_DS][SH_CUT_NAME];
	unsigned long	kp_written[SH_CUT_DS];
	size_t		kp_nx;
	unsigned long	kp_wrote;			/* their sum */
	/* X-staging's datasets that are clones of X's, to promote */
	char		kp_own[SH_CUT_DS][SH_CUT_NAME];
	size_t		kp_nown;
};

/*
 * The plan for cutting TARGET (X, or containers/X) over to its staging
 * tree, or for abandoning it (ABANDON), into P; X's recipe read from
 * the configuration root ROOT. JAILDIR, RUNDIR as an update's. Refused:
 * no lake, run or config root; a bad name; no container X; no staging
 * tree; an update of X not done (paused, or running).
 */
int sh_cutover_plan(struct sh_zfs *z, const struct sh_spaces *s,
    const struct sh_node *site, const char *root, const char *target,
    int abandon, const char *jaildir, const char *rundir, struct sh_kplan *p,
    struct sh_err *e, struct sh_err *w);

/* The plan, as the person confirming it reads it: X's writes said. */
void sh_cutover_show(FILE *out, const struct sh_kplan *p);

/*
 * The plan carried out at NOW, each step said on OUT (7.9). A cutover:
 * a final save of X if it wrote since its from-save; X-staging's jail
 * stopped; its clones of X's datasets promoted; X's jail stopped if it
 * runs, X destroyed on zrun; X-staging renamed X, its jail's block X's;
 * in zlake, the staging mirror's clones promoted, the old X moved to
 * the archive as X-<ISO date>, the staging mirror renamed X; X started
 * and saved; the update's record gone. An abandon: X-staging's jail
 * stopped, its block removed, X-staging destroyed on zrun and in
 * zlake, X started, the record gone. A failure stops where it is, said
 * with what is done.
 */
int sh_cutover_run(struct sh_zfs *z, struct sh_kplan *p,
    const struct sh_spaces *s, const struct sh_tools *t, time_t now,
    FILE *out, struct sh_err *e);

void sh_cutover_free(struct sh_kplan *p);

#endif	/* SH_CUTOVER_H */
