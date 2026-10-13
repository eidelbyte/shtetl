/*
 * shtetl beam F (planning.md 7.3): a version of the foundation F,
 * built in zlake, brought to zrun, where containers are cloned from it.
 */

#ifndef	SH_BEAM_H
#define	SH_BEAM_H

#include <stddef.h>
#include <stdio.h>

#include "core/err.h"
#include "core/spaces.h"
#include "core/zfs.h"

#define	SH_BEAM_DS	32	/* datasets of one foundation */
#define	SH_BEAM_NAME	256

struct sh_beam {
	char	bm_name[SH_BEAM_NAME];		/* F */
	char	bm_lake[SH_BEAM_NAME];		/* lake/foundations/F */
	char	bm_run[SH_BEAM_NAME];		/* run/foundations/F */
	char	bm_version[SH_BEAM_NAME];	/* V, after the @ */
	char	bm_from[SH_BEAM_NAME];		/* zrun's newest; "": none */
	char	bm_rel[SH_BEAM_DS][SH_BEAM_NAME];	/* "" for F */
	int	bm_placeholder[SH_BEAM_DS];
	size_t	bm_n;
};

/*
 * The plan for bringing VERSION of TARGET (F or foundations/F; NULL:
 * its newest version, or -pkg of one) to zrun, into P. A first beam
 * sends each dataset whole; a later one from zrun's newest. Refused,
 * with every reason: no lake or run root; no such foundation; F with
 * no version; VERSION not one of F's; it on zrun already, or older
 * than zrun's newest; F's datasets not those it has on zrun.
 */
int sh_beam_plan(struct sh_zfs *z, const struct sh_spaces *s,
    const char *target, const char *version, struct sh_beam *p,
    struct sh_err *e);

/* The plan, as the person confirming it reads it. */
void sh_beam_show(FILE *out, const struct sh_beam *p);

/*
 * The plan P carried out, each dataset said on OUT: a zrun snapshot
 * without shtetl:kind given its zlake counterpart's shtetl:* first
 * (an interrupted copy, 6.2); each dataset sent, parents first; a
 * first beam's datasets readonly=on (F's, inherited), canmount=noauto
 * (off on a placeholder); the version's shtetl:* copied onto each
 * received snapshot. A failure destroys what this beam received.
 */
int sh_beam_run(struct sh_zfs *z, const struct sh_beam *p, FILE *out,
    struct sh_err *e);

#endif	/* SH_BEAM_H */
