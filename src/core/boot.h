/*
 * shtetl bootstrap (planning.md 4.2, 4.3): datasets the user already
 * has, made roots of the four spaces and laid out. Planned in full
 * first, shown, then applied; the apply sets shtetl:space on each
 * root last, so a bootstrap stopped part way is finished by running
 * the same command again.
 */

#ifndef	SH_BOOT_H
#define	SH_BOOT_H

#include <stddef.h>
#include <stdio.h>

#include "core/err.h"
#include "core/spaces.h"
#include "core/tree.h"
#include "core/zfs.h"

#define	SH_BOOT_MAX	SH_SP_N	/* roots in one bootstrap: one a space */
#define	SH_BOOT_ROWS	24	/* properties of one root */
#define	SH_BOOT_KIDS	8	/* layout below one root */
#define	SH_BOOT_NAME	256	/* a dataset's name */
#define	SH_BOOT_VAL	256	/* a property's value, as shown */

/* What the user asked for. */
struct sh_bootreq {
	const char	*br_ds[SH_BOOT_MAX];
	enum sh_space	br_sp[SH_BOOT_MAX];
	size_t		br_n;
	const char	*br_recipes;	/* git URL for the config root */
};

/* One property of a root: a line of the table. */
struct sh_brow {
	char		rw_prop[64];
	char		rw_dflt[SH_BOOT_VAL];	/* built-in, or "-" */
	char		rw_conf[SH_BOOT_VAL];	/* shtetl.conf's, "-", ... */
	char		rw_use[SH_BOOT_VAL];	/* what it will be */
	char		rw_now[SH_BOOT_VAL];
	enum sh_zsrc	rw_src;
	int		rw_set;			/* bootstrap sets it */
};

/* One dataset of a root's layout. */
struct sh_bkid {
	char		bk_ds[SH_BOOT_NAME];
	struct sh_zprop	bk_props[2];
	size_t		bk_n;
	int		bk_there;	/* there already, as shtetl makes it */
	int		bk_mount;	/* mounted once made */
};

struct sh_broot {
	char		rt_ds[SH_BOOT_NAME];
	enum sh_space	rt_sp;
	struct sh_brow	rt_rows[SH_BOOT_ROWS];
	size_t		rt_nrows;
	struct sh_bkid	rt_kids[SH_BOOT_KIDS];
	size_t		rt_nkids;
	char		rt_dir[SH_BOOT_VAL];	/* config, build: mountpoint */
	int		rt_mount;		/* config: mount it */
};

struct sh_bpool {
	char	pl_pool[SH_BOOT_NAME];
	char	pl_ashift[32];
	char	pl_autotrim[32];
	int	pl_set;			/* autotrim set on */
};

struct sh_bplan {
	struct sh_broot	bt_roots[SH_BOOT_MAX];
	size_t		bt_n;
	struct sh_bpool	bt_pools[SH_BOOT_MAX];
	size_t		bt_npools;
	const char	*bt_recipes;
};

/*
 * The plan for REQ into P, given the roots S already found and the
 * settings SITE (the merged tree of src/core/site.c). Refused, with
 * every reason: a dataset that is not there, or is a root already; a
 * second root of a space; a root inside or around another; a
 * lake or run root with children that are not shtetl's layout, or
 * with layout that is not as shtetl makes it; a config or build root
 * with no mountpoint.
 */
int sh_boot_plan(struct sh_zfs *z, const struct sh_spaces *s,
    const struct sh_node *site, const struct sh_bootreq *req,
    struct sh_bplan *p, struct sh_err *e);

/* The plan, as the person confirming it reads it. */
void sh_boot_show(FILE *out, const struct sh_bplan *p);

/*
 * The plan carried out, in order: pools, the roots' properties, their
 * layout, mounts, the config root's clone, and shtetl:space on every
 * root last; each step said on OUT as it is done. -1 at the first
 * failure, with no root marked.
 */
int sh_boot_apply(struct sh_zfs *z, const struct sh_bplan *p, FILE *out,
    struct sh_err *e);

/*
 * Whether to go on with a plan shown: YES (-y) says so; otherwise,
 * where there is a terminal (TTY), the question goes to OUT and y or
 * yes on IN is the answer; anything else, and no terminal, is no.
 */
int sh_boot_confirm(FILE *in, FILE *out, int yes, int tty);

#endif	/* SH_BOOT_H */
