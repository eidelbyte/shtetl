/*
 * What the command and the daemon say to each other over the socket:
 * records, each a kind, a length and that many bytes, never text to
 * parse. The command sends its arguments as one record; the daemon
 * answers with what the act wrote to its standard output and its
 * standard error, and then the exit status.
 */

#ifndef	SH_WIRE_H
#define	SH_WIRE_H

#include <stddef.h>

#define	SH_WIRE_ARGS	'a'	/* the arguments, each ended by a NUL */
#define	SH_WIRE_OUT	'o'	/* standard output */
#define	SH_WIRE_ERR	'e'	/* standard error */
#define	SH_WIRE_EXIT	'x'	/* the exit status, one byte */

#define	SH_WIRE_HDR	5	/* the kind, and the length big-endian */

/* Bytes, and how many of them are written or used up already. */
struct sh_buf {
	char	*b_data;
	size_t	b_len;
	size_t	b_off;
};

/* A record of KIND holding LEN bytes of P, added to B. */
void sh_wire_put(struct sh_buf *b, int kind, const void *p, size_t len);

/*
 * The record at B's offset, when all of it is there: its kind, its
 * bytes and their length, and B's offset moved past it; 1. 0 when it
 * is not all there yet.
 */
int sh_wire_get(struct sh_buf *b, int *kind, const char **p, size_t *len);

/* LEN bytes of P added to B. */
void sh_buf_add(struct sh_buf *b, const void *p, size_t len);

#endif	/* SH_WIRE_H */
