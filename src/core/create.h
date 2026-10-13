/*
 * shtetl create X --on F (planning.md 7.4): a container, cloned from a
 * version of the foundation F on zrun, its jail written and started.
 */

#ifndef	SH_CREATE_H
#define	SH_CREATE_H

#include <stddef.h>
#include <stdio.h>
#include <time.h>

#include "core/beam.h"
#include "core/build.h"
#include "core/cascade.h"
#include "core/err.h"
#include "core/spaces.h"
#include "core/zfs.h"

#define	SH_CREATE_DIR	"/var/db/shtetl/create"	/* the logs */
#define	SH_JAIL_DIR	"/etc/jail.conf.d"	/* the jails' blocks */
#define	SH_CREATE_DS	64
#define	SH_CREATE_NAME	256

/* One dataset of the container: a clone of F's, or X's own. */
struct sh_cds {
	char		cd_rel[SH_CREATE_NAME];	/* below X; "" for X */
	int		cd_clone;	/* of F's, at the version */
	int		cd_placeholder;	/* canmount=off */
	const struct sh_node *cd_props;	/* the recipe's, or NULL */
};

/* A command piece the run runs: its label, file and text. */
struct sh_cpiece {
	const char	*cp_label;
	const char	*cp_file;
	const char	*cp_text;
};

struct sh_cplan {
	char		cp_name[SH_CREATE_NAME];	/* X */
	char		cp_found[SH_CREATE_NAME];	/* F */
	char		cp_version[SH_CREATE_NAME];	/* V */
	char		cp_run[SH_CREATE_NAME];		/* run/containers/X */
	char		cp_runf[SH_CREATE_NAME];	/* run/foundations/F */
	char		cp_config[SH_CREATE_NAME];	/* the config root */
	char		cp_root[1024];			/* its directory */
	char		cp_jailfile[1024];		/* jaildir/X.conf */
	char		cp_logdir[1024];		/* logbase/X */
	char		cp_resolv[1024];	/* the host's resolv.conf */
	int		cp_beam;			/* V not on zrun */
	struct sh_beam	cp_beamplan;
	struct sh_recipe cp_recipe;			/* containers/X */
	struct sh_recipe cp_frecipe;	/* F's, as of V's shtetl:config */
	struct sh_cds	cp_dss[SH_CREATE_DS];
	size_t		cp_nds;
	struct sh_cpiece cp_cmds[SH_CREATE_DS];
	size_t		cp_ncmds;
	size_t		cp_skipped;	/* pieces the foundation ran */
};

/*
 * The plan for creating TARGET (X, or containers/X) on the foundation
 * FOUND at VERSION (NULL: its newest version or -pkg), its recipe read
 * from the configuration root ROOT, with the roots S found, into P.
 * JAILDIR (SH_JAIL_DIR) is where its jail's block goes, LOGBASE
 * (SH_CREATE_DIR) where its log does; the tests' scratch for both.
 * Refused, with every reason (tests/MATRIX.md X1). Warnings to W.
 */
int sh_create_plan(struct sh_zfs *z, const struct sh_spaces *s,
    const char *root, const char *target, const char *found,
    const char *version, const char *jaildir, const char *logbase,
    struct sh_cplan *p, struct sh_err *e, struct sh_err *w);

/* The plan, as the person confirming it reads it. */
void sh_create_show(FILE *out, const struct sh_cplan *p);

/*
 * The plan P carried out at NOW, each step said on OUT (7.4): the
 * config root snapshotted; the version beamed if zrun lacks it; F's
 * datasets cloned, parents first, a placeholder canmount=off, the
 * recipe's properties set locally; X's own made; X mounted; the jail's
 * block written and the jail started; the host's resolv.conf copied in
 * where X has none; pkg -j X install, then each command piece by jexec
 * (a piece the foundation ran left out); the first save, its message
 * MSG or the create's own. A failure before the save undoes all of it
 * but the config snapshot; a mirror failing after the save leaves X.
 */
int sh_create_run(struct sh_zfs *z, struct sh_cplan *p,
    const struct sh_spaces *s, const struct sh_tools *t, const char *msg,
    time_t now, FILE *out, struct sh_err *e);

void sh_create_free(struct sh_cplan *p);

#endif	/* SH_CREATE_H */
