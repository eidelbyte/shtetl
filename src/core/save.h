/*
 * shtetl save X (planning.md 7.5, 6.2): every dataset of a container
 * snapshotted at once, with its message, and mirrored into zlake as
 * deltas (7.10).
 */

#ifndef	SH_SAVE_H
#define	SH_SAVE_H

#include <stddef.h>
#include <stdio.h>
#include <time.h>

#include "core/err.h"
#include "core/spaces.h"
#include "core/zfs.h"

#define	SH_SAVE_DS	64	/* datasets of one container */
#define	SH_SAVE_NAME	256

struct sh_save {
	char	sv_name[SH_SAVE_NAME];		/* X */
	char	sv_run[SH_SAVE_NAME];		/* run/containers/X */
	char	sv_lake[SH_SAVE_NAME];		/* lake/containers/X */
	char	sv_runroot[SH_SAVE_NAME];	/* the run root */
	char	sv_lakeroot[SH_SAVE_NAME];	/* the lake root */
	char	sv_rel[SH_SAVE_DS][SH_SAVE_NAME];	/* "" for X */
	char	sv_origin[SH_SAVE_DS][SH_SAVE_NAME * 2];	/* "": own */
	size_t	sv_n;
	char	sv_config[SH_SAVE_NAME * 2];	/* the config snapshot */
	char	sv_guid[64];
};

/*
 * TARGET, X or containers/X, as the container's name X into NAME.
 * Refused: another kind's path, a bad name.
 */
int sh_save_target(const char *target, char *name, size_t len,
    struct sh_err *e);

/*
 * The plan for saving the container TARGET into P: its datasets,
 * parents first, each a clone of a foundation's version on zrun or
 * the container's own; shtetl:config and its guid from CONFIG, a
 * config snapshot, or else from X's newest save. Refused, with every
 * reason: no lake or run root; no such container; a dataset a clone
 * of something not a foundation's; nothing to take shtetl:config
 * from.
 */
int sh_save_plan(struct sh_zfs *z, const struct sh_spaces *s,
    const char *target, const char *config, struct sh_save *p,
    struct sh_err *e);

/*
 * P carried out at NOW, MSG the message: every dataset snapshotted at
 * once as save-<YYYY-mm-dd>-<HHMMSS> (UTC; -2 and on for another in
 * the same second), shtetl:kind=save, shtetl:commit=MSG and
 * shtetl:config; then mirrored into the lake root's containers/X,
 * parents first: a dataset's first save a clone stream received as a
 * clone of zlake's counterpart of its origin, or whole for X's own;
 * a later one from that dataset's newest mirrored save; the save's
 * shtetl:* copied onto each (6.2). The save taken, a mirror that
 * fails is said and refused (-1), and the next save's brings every
 * dataset up to it. The save's name into SNAP.
 */
int sh_save_run(struct sh_zfs *z, const struct sh_save *p, const char *msg,
    time_t now, char *snap, size_t len, FILE *out, struct sh_err *e);

#endif	/* SH_SAVE_H */
