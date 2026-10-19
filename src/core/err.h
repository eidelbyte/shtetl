/*
 * Where refusals go. Every report passes through a sink, so the caller
 * decides where the words land -- a person's terminal, the reply to a
 * command, syslog -- and a test reads exactly what a person would. A
 * check of the configuration reports through it instead of stopping at
 * the first problem, so that one run names everything wrong.
 */

#ifndef	SH_ERR_H
#define	SH_ERR_H

#include <stddef.h>
#include <stdio.h>

struct sh_err {
	FILE		*se_out;
	const char	*se_prefix;	/* "shtetl: ", or "" in tests */
	unsigned	se_count;	/* messages so far */
};

void sh_err_init(struct sh_err *e, FILE *out, const char *prefix);
extern void sh_errf(struct sh_err *e, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

/*
 * A key path, for messages: datasets."usr/local".readonly, or
 * import[2]. A component that is not a bare word is quoted, as show
 * quotes the key. A push returns the length to pop back to; a path too
 * long for the buffer ends in "...".
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
 * A refusal at a place in a file: "FILE: PATH: message", or "FILE:
 * message" while the path is empty.
 */
extern void sh_err_at(struct sh_err *e, const char *file,
    const struct sh_path *p, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));

#endif	/* SH_ERR_H */
