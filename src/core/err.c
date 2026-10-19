/*
 * The error sink.
 */

#include <stdarg.h>
#include <stdio.h>

#include "core/err.h"

void
sh_err_init(struct sh_err *e, FILE *out, const char *prefix)
{
	e->se_out = out;
	e->se_prefix = prefix;
}

void
sh_errf(struct sh_err *e, const char *fmt, ...)
{
	va_list ap;

	(void) fputs(e->se_prefix, e->se_out);
	va_start(ap, fmt);
	(void) vfprintf(e->se_out, fmt, ap);
	va_end(ap);
	(void) fputc('\n', e->se_out);
}
