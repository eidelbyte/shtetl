/*
 * The error sink, and the key paths its messages name.
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "core/err.h"
#include "core/tree.h"

/* Room kept at the end of a path for "..." and the NUL. */
#define	PA_RESERVE	4

void
sh_err_init(struct sh_err *e, FILE *out, const char *prefix)
{
	e->se_out = out;
	e->se_prefix = prefix;
	e->se_count = 0;
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
	e->se_count++;
}

void
sh_err_at(struct sh_err *e, const char *file, const struct sh_path *p,
    const char *fmt, ...)
{
	va_list ap;

	(void) fprintf(e->se_out, "%s%s: ", e->se_prefix, file);
	if (p != NULL && p->pa_len > 0)
		(void) fprintf(e->se_out, "%s: ", p->pa_buf);
	va_start(ap, fmt);
	(void) vfprintf(e->se_out, fmt, ap);
	va_end(ap);
	(void) fputc('\n', e->se_out);
	e->se_count++;
}

void
sh_path_init(struct sh_path *p)
{
	p->pa_buf[0] = '\0';
	p->pa_len = 0;
}

/*
 * One byte onto the path. Past the room, the path ends in "..." and
 * takes nothing more until it is popped back below that point.
 */
static void
pa_putc(struct sh_path *p, char c)
{
	if (p->pa_len + PA_RESERVE < SH_PATH_MAX) {
		p->pa_buf[p->pa_len++] = c;
		p->pa_buf[p->pa_len] = '\0';
	} else if (p->pa_len + PA_RESERVE == SH_PATH_MAX) {
		(void) memcpy(p->pa_buf + p->pa_len, "...", PA_RESERVE);
		p->pa_len += PA_RESERVE - 1;
	}
}

static void
pa_puts(struct sh_path *p, const char *s)
{
	while (*s != '\0')
		pa_putc(p, *s++);
}

size_t
sh_path_push(struct sh_path *p, const char *key)
{
	size_t was;
	char esc[8];
	const char *q;

	was = p->pa_len;
	if (p->pa_len > 0)
		pa_putc(p, '.');
	if (sh_key_bare(key)) {
		pa_puts(p, key);
		return (was);
	}
	pa_putc(p, '"');
	for (; *key != '\0'; key++) {
		if ((q = sh_escape((unsigned char)*key, esc)) != NULL)
			pa_puts(p, q);
		else
			pa_putc(p, *key);
	}
	pa_putc(p, '"');
	return (was);
}

size_t
sh_path_index(struct sh_path *p, size_t i)
{
	size_t was;
	char buf[32];

	was = p->pa_len;
	(void) snprintf(buf, sizeof (buf), "[%zu]", i);
	pa_puts(p, buf);
	return (was);
}

void
sh_path_pop(struct sh_path *p, size_t len)
{
	if (len < p->pa_len) {
		p->pa_len = len;
		p->pa_buf[len] = '\0';
	}
}
