/*
 * A foundation's build and install (plan 6 steps 3 and 4), as steps of
 * its roll, carried from main: buildworld and buildkernel over the
 * foundation's obj cache; into the head, delete-old first on an
 * update, installworld with install -C, the kernel installed into a
 * scratch directory of the obj cache and synced into the head as
 * install -C would leave it, distribution, and on an update
 * installworld's backups removed. make reads MAKEOBJDIRPREFIX from its
 * environment only, so each make runs under env(1).
 *
 * installkernel into the head itself would remove its boot/kernel
 * first (sys/conf/kern.post.mk keeps a .old only over the running
 * kernel's), every file new on every update; synced, a file alike is
 * left as it was, and only what changed is new in the version.
 */

#ifndef	SH_INSTALL_H
#define	SH_INSTALL_H

#include "core/cascade.h"
#include "core/err.h"
#include "core/job.h"

struct sh_install {
	const char		*in_src;	/* the checkout */
	const char		*in_config;	/* zconfig's mountpoint */
	const char		*in_obj;	/* the obj cache's */
	const char		*in_head;	/* the head's */
	const char		*in_name;	/* the foundation's */
	const struct sh_recipe	*in_recipe;
	long			in_jobs;	/* make -j; 0: the CPUs */
	int			in_update;	/* the head holds a version */
};

/*
 * IN's steps added to J, once the kernel config and src.conf are
 * written into the obj cache's directory shtetl: an include of the
 * config kernconf names (a name in the checkout's conf directory for
 * this machine, or a file of zconfig in the recipe's place), the
 * foundation's name in capitals as ident, kernconf's lines; and
 * buildopts. 0, or -1 with why said to E. IN outlives J's steps.
 */
int sh_install_steps(struct sh_job *j, struct sh_install *in,
    struct sh_err *e);

/*
 * The kernel installed into the tree FROM put into the head HEAD as
 * install -C would leave it: a file alike left as it is, its mode and
 * owner set; another copied, with its mode, owner and times; in the
 * kernel's directories, boot/kernel and its debug files', what FROM
 * has not removed, and such a directory FROM has none of. 0, or -1
 * said to E.
 */
int sh_install_kernel(const char *from, const char *head,
    struct sh_err *e);

/*
 * installworld's own backups in the head HEAD removed: boot/kernel.old
 * and boot's other *.old, libexec/ld-elf*.so.1.old, sbin/init.bak.
 */
int sh_install_backups(const char *head, struct sh_err *e);

/* PATH and all below it removed, flags cleared first; gone is done. */
int sh_install_rmtree(const char *path, struct sh_err *e);

#endif	/* SH_INSTALL_H */
