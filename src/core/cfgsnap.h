/*
 * The config root's snapshots: taken only when the root has changed,
 * named three words.
 */

#ifndef	SH_CFGSNAP_H
#define	SH_CFGSNAP_H

#include <stddef.h>
#include <stdio.h>
#include <time.h>

#include "core/err.h"
#include "core/zfs.h"

#define	SH_CFGSNAP_WORDS	256	/* the words a name is drawn from */
#define	SH_CFGSNAP_NAME		256

/* The name NOW's TRY'th draw gives, word-word-word, into BUF. */
void sh_cfgsnap_name(time_t now, unsigned try, char *buf, size_t len);

/*
 * The snapshot of the config root ROOT that a build, create or update
 * records in shtetl:config, its full name into SNAP: ROOT's newest
 * shtetl:kind config snapshot when nothing has been written to ROOT
 * since it, else one taken now, NOW's first free three-word name,
 * shtetl:commit MSG, said on OUT. -1 refused.
 */
int sh_cfgsnap(struct sh_zfs *z, const char *root, const char *msg,
    time_t now, char *snap, size_t len, FILE *out, struct sh_err *e);

#endif	/* SH_CFGSNAP_H */
