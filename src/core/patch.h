/*
 * A foundation's patches (planning.md 7.1.1): the pieces the build
 * applies them with, each alone. src/core/build.c runs them.
 */

#ifndef	SH_PATCH_H
#define	SH_PATCH_H

#include <stddef.h>

#include "core/err.h"

#define	SH_PATCH_MAX	512	/* patches in one build */
#define	SH_PATCH_SRCS	32	/* sources in one recipe */
#define	SH_PATCH_STR	1024

/*
 * A and B in natural order: runs of digits compare as numbers, the
 * rest byte by byte, and names equal so far by their bytes; <0, 0, >0.
 */
int sh_natcmp(const char *a, const char *b);

/*
 * The names of the files ending .patch at the top of DIR, in natural
 * order, NULL-ended; a hidden file, a directory and anything else is
 * left out. NULL, with the reason in E, when DIR cannot be read.
 */
char **sh_patch_list(const char *dir, struct sh_err *e);
void sh_patch_list_free(char **list);

/*
 * The source LOC as git fetches it, into BUF: a URL as written, a host
 * and path as https. NULL for a directory in the configuration root.
 */
const char *sh_patch_url(const char *loc, char *buf, size_t len);

/*
 * The files a patch touches, from what git apply --numstat -z says of
 * it (OUT, LEN bytes): a renamed file by its new name. NULL-ended;
 * NULL, with the reason in E, when that cannot be read.
 */
char **sh_patch_files(const char *out, size_t len, struct sh_err *e);

/*
 * What a fix has left in the tree DIR, among FILES: a file holding a
 * conflict marker, or a .rej beside one. The first said in E (NULL:
 * not said): 1; 0 when nothing is left.
 */
int sh_patch_left(const char *dir, char *const *files, struct sh_err *e);

/* What became of each patch before the one a build paused at. */
enum sh_pout {
	SH_PO_APPLIED,	/* applied as it is */
	SH_PO_ALREADY,	/* already in the tree: its reverse applied */
	SH_PO_SKIPPED,	/* left out by hand, --skip */
	SH_PO_FIXED,	/* fixed by hand, --continue */
	SH_PO_MERGED,	/* merged three ways without conflict */
	SH_PO_N
};

extern const char *const sh_pout_name[];

/*
 * A build paused at a patch, as its run directory records it for
 * shtetl build F --continue, --skip and --abort.
 */
struct sh_pause {
	char		pz_config[SH_PATCH_STR];	/* config snapshot */
	char		pz_version[SH_PATCH_STR];
	char		pz_sha[SH_PATCH_STR];	/* the checkout's commit */
	char		pz_msg[SH_PATCH_STR];	/* -m's; "" for none */
	char		pz_base[SH_PATCH_STR];	/* git's tree before it */
	size_t		pz_at;			/* paused at, from 0 */
	unsigned char	pz_out[SH_PATCH_MAX];	/* each before it */
	size_t		pz_nsrc;
	char		pz_commit[SH_PATCH_SRCS][64];	/* "": a directory */
};

/* PZ written to PATH, and read back; 0, or -1 with the reason in E. */
int sh_pause_write(const char *path, const struct sh_pause *pz,
    struct sh_err *e);
int sh_pause_read(const char *path, struct sh_pause *pz, struct sh_err *e);

#endif	/* SH_PATCH_H */
