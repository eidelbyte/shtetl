/*
 * The allocators.
 */

#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include "core/mem.h"

static void *
given(void *p)
{
	if (p == NULL) {
		syslog(LOG_ERR, "out of memory");
		exit(1);
	}
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
