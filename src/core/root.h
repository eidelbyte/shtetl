/*
 * The configuration root (zconfig, plan 4): the files named
 * by their path in it. Every path handed here has passed
 * sh_syn_relpath, or is one shtetl makes itself.
 */

#ifndef	SH_ROOT_H
#define	SH_ROOT_H

#include "core/err.h"
#include "core/tree.h"

/* ROOT/REL, allocated. */
char *sh_root_path(const char *root, const char *rel);

/*
 * What ROOT/REL is: 'f' a regular file, 'd' a directory, '?'
 * something else, 0 nothing (or nothing that can be looked at).
 */
int sh_root_kind(const char *root, const char *rel);

/*
 * The file NAME of ROOT, parsed with NAME as its layer, which must
 * outlive the tree. When MISSING is not NULL, a file that does not
 * exist sets it and is reported to no one.
 */
struct sh_node *sh_root_parse(const char *root, const char *name,
    int *missing, struct sh_err *e);

#endif	/* SH_ROOT_H */
