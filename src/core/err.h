/*
 * Where refusals go. Every check of the configuration reports through
 * a sink instead of stopping at the first problem, so that one run
 * names everything wrong with a layer; the tests point the sink at a
 * memory stream and read exactly what a person would.
 */

#ifndef	SH_ERR_H
#define	SH_ERR_H

#include <stddef.h>
#include <stdio.h>

struct sh_err {
	FILE		*se_out;
	const char	*se_prefix;	/* "shtetl: " on stderr, "" in tests */
	unsigned	se_count;	/* messages so far */
};

void sh_err_init(struct sh_err *e, FILE *out, const char *prefix);
extern void sh_errf(struct sh_err *e, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
void sh_err_nomem(struct sh_err *e);

/*
 * A key path, for messages: datasets."usr/local".readonly, or
 * import[2]. A component that is not a bare word is quoted, as config
 * show quotes the key. A push returns the length to pop back to; a
 * path too long for the buffer ends in "...".
 */
#define	SH_PATH_MAX	1024

struct sh_path {
	char	pa_buf[SH_PATH_MAX];
	size_t	pa_len;
};

void sh_path_init(struct sh_path *p);
size_t sh_path_push(struct sh_path *p, const char *key);
size_t sh_path_index(struct sh_path *p, size_t i);
void sh_path_pop(struct sh_path *p, size_t len);

/*
 * A refusal at a place in a file: "FILE: PATH: message", or
 * "FILE: message" while the path is empty.
 */
extern void sh_err_at(struct sh_err *e, const char *file,
    const struct sh_path *p, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));

#endif	/* SH_ERR_H */
