/*
 * One file of shtetl's configuration, parsed under the rules of
 * docs/configuration.md 2 into the format-free tree. This module is
 * the only one that knows the format is UCL.
 */

#ifndef	SH_PARSE_H
#define	SH_PARSE_H

#include <stddef.h>

#include "core/err.h"
#include "core/tree.h"

/* The most of one file shtetl reads. */
#define	SH_PARSE_MAX	(4 * 1024 * 1024)

/* How deep a value may nest. */
#define	SH_PARSE_DEPTH	64

/*
 * NAME is how messages and layers name the file: its path in the
 * configuration root, such as containers/nginx.conf. It is stored in
 * every node, so it must outlive the tree. PATH is where the file is
 * read from. Either returns the top object, or NULL with every reason
 * reported to E.
 */
struct sh_node *sh_parse_file(const char *path, const char *name,
    struct sh_err *e);
struct sh_node *sh_parse_buf(const char *buf, size_t len, const char *name,
    struct sh_err *e);

/*
 * The rules of 5.3 that libucl keeps for shtetl, tried on the linked
 * library: a macro refused at its line, $ literal, a key written twice
 * refused. The parse runs it once before its first file and refuses to
 * go on if it fails; 0 if it passed.
 */
int sh_parse_selfcheck(struct sh_err *e);

#endif	/* SH_PARSE_H */
