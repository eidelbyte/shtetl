/*
 * The spaces (plan 4): lake, config, build and run, each the one
 * filesystem where shtetl:space is set locally to that word. Marking a
 * space is the person's zfs set; shtetl finds them, and makes below
 * each root the skeleton it keeps there.
 */

#ifndef	SH_SPACES_H
#define	SH_SPACES_H

#include "core/err.h"
#include "core/zfs.h"

enum sh_space {
	SH_SP_LAKE,
	SH_SP_CONFIG,
	SH_SP_BUILD,
	SH_SP_RUN,
	SH_SP_N
};

struct sh_spaces {
	char	*ss_root[SH_SP_N];	/* NULL: nobody marked one */
};

/*
 * The spaces into SP. Refused, every one of them said: a value that
 * is no space's, two roots of one space, a root inside another.
 */
int sh_spaces_find(struct sh_zfs *z, struct sh_spaces *sp,
    struct sh_err *e);
void sh_spaces_free(struct sh_spaces *sp);

/* The skeleton below each root found, what is missing of it made. */
int sh_spaces_skeleton(struct sh_zfs *z, const struct sh_spaces *sp,
    struct sh_err *e);

#endif	/* SH_SPACES_H */
