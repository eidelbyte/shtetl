/*
 * The allocators.
 */

#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include "core/mem.h"

void
sh_nomem(void)
{
	syslog(LOG_ERR, "out of memory");
	exit(1);
}

static void *
given(void *p)
{
	if (p == NULL)
		sh_nomem();
	return (p);
}

void *
sh_calloc(size_t n, size_t size)
{
	return (given(calloc(n, size)));
}

void *
sh_realloc(void *p, size_t size)
{
	return (given(realloc(p, size)));
}

char *
sh_strdup(const char *s)
{
	return (given(strdup(s)));
}

FILE *
sh_memstream(char **buf, size_t *len)
{
	return (given(open_memstream(buf, len)));
}
