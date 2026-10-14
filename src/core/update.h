/*
 * shtetl update X (planning.md 7.6, 7.6.2, 7.7): X's datasets replayed
 * onto a newer version of its foundation into X-staging, its history
 * kept, paused at a conflicts gate and taken up again.
 */

#ifndef	SH_UPDATE_H
#define	SH_UPDATE_H

#include <stddef.h>
#include <stdio.h>
#include <time.h>

#include "core/beam.h"
#include "core/build.h"
#include "core/cascade.h"
#include "core/err.h"
#include "core/rebase.h"
#include "core/spaces.h"
#include "core/zfs.h"

#define	SH_UPDATE_DIR	"/var/db/shtetl/update"	/* manifests, logs */
#define	SH_UPDATE_MARK	"shtetl:update"	/* X's from-save: the target */
#define	SH_UPDATE_DS	64
#define	SH_UPDATE_SAVES	64
#define	SH_UPDATE_NAME	256

/* What an update run does: start one, take a paused one up, give up. */
enum sh_uverb {
	SH_UV_NEW,
	SH_UV_CONTINUE,
	SH_UV_ABORT
};

/* How a dataset of X reaches X-staging. */
enum sh_uhow {
	SH_UH_REPLAY,	/* rebased onto F's, X's saves replayed (7.6.2) */
	SH_UH_TARGET,	/* F's placeholder: cloned from the target */
	SH_UH_OWN	/* X's own: cloned from its from-save */
};

struct sh_uds {
	char		ud_rel[SH_UPDATE_NAME];	/* below X; "" for X */
	enum sh_uhow	ud_how;
	int		ud_placeholder;	/* canmount=off, X's or F's */
	const struct sh_node *ud_props;	/* the recipe's, or NULL */
};

struct sh_uplan {
	enum sh_uverb	up_verb;
	char		up_name[SH_UPDATE_NAME];	/* X */
	char		up_stagename[SH_UPDATE_NAME];	/* X-staging */
	char		up_found[SH_UPDATE_NAME];	/* F */
	char		up_old[SH_UPDATE_NAME];		/* X's version */
	char		up_version[SH_UPDATE_NAME];	/* the target */
	char		up_run[SH_UPDATE_NAME];		/* run/containers/X */
	char		up_stage[SH_UPDATE_NAME];	/* .../X-staging */
	char		up_runf[SH_UPDATE_NAME];	/* run/foundations/F */
	char		up_config[SH_UPDATE_NAME];	/* the config root */
	char		up_jailfile[1024];		/* X's block */
	char		up_stagejail[1024];		/* X-staging's */
	char		up_rundir[1024];		/* SH_UPDATE_DIR/X */
	unsigned long	up_depth;			/* update.depth */
	int		up_beam;			/* V not on zrun */
	struct sh_beam	up_beamplan;
	struct sh_recipe up_recipe;			/* containers/X */
	struct sh_uds	up_dss[SH_UPDATE_DS];
	size_t		up_nds;
	/* X's saves replayed, oldest first; the from-save last, once taken */
	char		up_saves[SH_UPDATE_SAVES][SH_UPDATE_NAME];
	size_t		up_nsaves;
	char		up_cfgsnap[SH_UPDATE_NAME * 2];	/* at the end */
};

/*
 * The plan for VERB on TARGET (X, or containers/X), into P, its recipe
 * read from the configuration root ROOT, with the roots S found and the
 * site's settings SITE (update.suffix, update.depth). VERSION (NULL:
 * F's newest version or -pkg) is a new update's target. JAILDIR
 * (SH_JAIL_DIR) and RUNDIR (SH_UPDATE_DIR) are the tests' scratch.
 * Refused, with every reason (tests/MATRIX.md U1). Warnings to W.
 */
int sh_update_plan(struct sh_zfs *z, const struct sh_spaces *s,
    const struct sh_node *site, const char *root, const char *target,
    const char *version, enum sh_uverb verb, const char *jaildir,
    const char *rundir, struct sh_uplan *p, struct sh_err *e,
    struct sh_err *w);

/* A new update's plan, as the person confirming it reads it. */
void sh_update_show(FILE *out, const struct sh_uplan *p);

/*
 * The plan P carried out at NOW, each step said on OUT (7.6), rebases
 * through RB, the tools T; TTY says a person is at the terminal, for
 * the picker at a gate. 0: X-staging made, started and saved; 1: paused
 * at a conflicts gate, the record written, --continue to take it up;
 * -1: refused or failed, and undone as --abort undoes it (all but X's
 * from-save and the config snapshot), said in E.
 */
int sh_update_run(struct sh_zfs *z, struct sh_uplan *p,
    const struct sh_spaces *s, const struct sh_tools *t,
    const struct sh_rebaser *rb, int tty, time_t now, FILE *out,
    struct sh_err *e);

/* Where an update of X stands, as ZFS says. */
enum sh_ustate {
	SH_US_NONE,	/* no staging tree */
	SH_US_UNDERWAY,	/* a staging tree, not done: paused, or cut short */
	SH_US_DONE	/* the staging tree snapshotted as its version */
};

/*
 * The update of X (RUN, run/containers/X) into STAGE, read from ZFS:
 * DONE when STAGE carries a snapshot of shtetl:kind version and no
 * rebase is open in it; its target into VERSION and X's from-save (the
 * newest of X's saves marked SH_UPDATE_MARK) into FROM, "" with RUN
 * gone (a cutover part way). -1 refused.
 */
int sh_update_state(struct sh_zfs *z, const char *run, const char *stage,
    char *version, size_t vlen, char *from, size_t flen, struct sh_err *e);

void sh_update_free(struct sh_uplan *p);

#endif	/* SH_UPDATE_H */
