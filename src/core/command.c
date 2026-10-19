/*
 * The shtetl command: it decides nothing. Its arguments go to the
 * daemon as they are, and the reply comes back as records, each
 * copied to the stream it names until the exit status.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "core/daemon.h"
#include "core/err.h"
#include "core/wire.h"

int
sh_command(const char *sock, int argc, char *const *argv, FILE *out,
    FILE *err)
{
	struct sockaddr_un sun;
	struct sh_buf args = { NULL, 0, 0 }, io = { NULL, 0, 0 };
	struct sh_err e;
	char buf[4096];
	const char *p;
	size_t len;
	ssize_t n;
	int fd, kind, i, status = -1;

	sh_err_init(&e, err, "shtetl: ");
	(void) memset(&sun, 0, sizeof (sun));
	sun.sun_family = AF_UNIX;
	(void) strlcpy(sun.sun_path, sock, sizeof (sun.sun_path));
	if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0 ||
	    connect(fd, (struct sockaddr *)&sun, sizeof (sun)) != 0) {
		sh_errf(&e, "%s: %s", sock, strerror(errno));
		status = 1;
		goto out;
	}
	for (i = 0; i < argc; i++)
		sh_buf_add(&args, argv[i], strlen(argv[i]) + 1);
	sh_wire_put(&io, SH_WIRE_ARGS, args.b_data, args.b_len);
	for (; io.b_off < io.b_len; io.b_off += (size_t)n) {
		if ((n = write(fd, io.b_data + io.b_off,
		    io.b_len - io.b_off)) < 0) {
			sh_errf(&e, "%s: %s", sock, strerror(errno));
			status = 1;
			goto out;
		}
	}
	io.b_len = io.b_off = 0;
	while (status < 0) {
		while (status < 0 && sh_wire_get(&io, &kind, &p, &len)) {
			if (kind == SH_WIRE_EXIT && len == 1)
				status = (unsigned char)p[0];
			else if (kind == SH_WIRE_OUT || kind == SH_WIRE_ERR)
				(void) fwrite(p, 1, len, kind == SH_WIRE_OUT ?
				    out : err);
		}
		(void) fflush(out);
		if (status >= 0)
			break;
		if ((n = read(fd, buf, sizeof (buf))) <= 0) {
			sh_errf(&e, "no reply");
			status = 1;
			break;
		}
		sh_buf_add(&io, buf, (size_t)n);
	}
out:
	if (fd >= 0)
		(void) close(fd);
	free(args.b_data);
	free(io.b_data);
	return (status);
}
