/*
 * shtetl's allocators. A failure is logged to syslog and ends the
 * process (plan 10, ruled: "This will just never happen. If it does,
 * syslog and die"), so nothing checks what they return.
 */

#ifndef	SH_MEM_H
#define	SH_MEM_H

#include <stddef.h>

void *sh_calloc(size_t n, size_t size);
/* SIZE is never 0: a buffer only grows. */
void *sh_realloc(void *p, size_t size);
char *sh_strdup(const char *s);

#endif	/* SH_MEM_H */
