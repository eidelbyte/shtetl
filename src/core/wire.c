/*
 * The records of the socket.
 */

#include <string.h>

#include "core/mem.h"
#include "core/wire.h"

void
sh_buf_add(struct sh_buf *b, const void *p, size_t len)
{
	if (len == 0)
		return;
	b->b_data = sh_realloc(b->b_data, b->b_len + len);
	(void) memcpy(b->b_data + b->b_len, p, len);
	b->b_len += len;
}

void
sh_wire_put(struct sh_buf *b, int kind, const void *p, size_t len)
{
	unsigned char h[SH_WIRE_HDR];

	h[0] = (unsigned char)kind;
	h[1] = (unsigned char)(len >> 24);
	h[2] = (unsigned char)(len >> 16);
	h[3] = (unsigned char)(len >> 8);
	h[4] = (unsigned char)len;
	sh_buf_add(b, h, sizeof (h));
	sh_buf_add(b, p, len);
}

int
sh_wire_get(struct sh_buf *b, int *kind, const char **p, size_t *len)
{
	const unsigned char *h;
	size_t n;

	if (b->b_len - b->b_off < SH_WIRE_HDR)
		return (0);
	h = (const unsigned char *)b->b_data + b->b_off;
	n = (size_t)h[1] << 24 | (size_t)h[2] << 16 | (size_t)h[3] << 8 | h[4];
	if (b->b_len - b->b_off - SH_WIRE_HDR < n)
		return (0);
	*kind = h[0];
	*p = b->b_data + b->b_off + SH_WIRE_HDR;
	*len = n;
	b->b_off += SH_WIRE_HDR + n;
	return (1);
}
