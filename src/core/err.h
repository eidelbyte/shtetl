/*
 * Where refusals go. Every report passes through a sink, so the caller
 * decides where the words land -- a person's terminal, the reply to a
 * command, syslog -- and a test reads exactly what a person would.
 */

#ifndef	SH_ERR_H
#define	SH_ERR_H

#include <stdio.h>

struct sh_err {
	FILE		*se_out;
	const char	*se_prefix;	/* "shtetl: ", or "" in tests */
};

void sh_err_init(struct sh_err *e, FILE *out, const char *prefix);
extern void sh_errf(struct sh_err *e, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

#endif	/* SH_ERR_H */
