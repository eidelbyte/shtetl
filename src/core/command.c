/*
 * The shtetl command: it decides nothing. Its arguments go to the
 * daemon as they are, with its terminal's descriptors where it has one,
 * and the reply comes back as records, each copied to the stream it
 * names until the exit status. A reply the daemon holds, an attended
 * roll's (m5-queue), is waited for: the command's SIGINT, SIGTERM and
 * SIGHUP meanwhile half-close the socket, which ends the picker and
 * not the roll, and it waits on; its terminal's modes are put back as
 * it ends, whatever the picker left them.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "core/daemon.h"
#include "core/err.h"
#include "core/wire.h"

static volatile sig_atomic_t sigged;

static void
caught(int sig)
{
	(void) sig;
	sigged = 1;
}

/* IO's bytes to FD, FDS with the first of them; 0, or -1 said to E. */
static int
send_all(int fd, struct sh_buf *io, const int *fds, const char *sock,
    struct sh_err *e)
{
	char cbuf[CMSG_SPACE(3 * sizeof (int))];
	struct cmsghdr *cm;
	struct msghdr m;
	struct iovec iov;
	ssize_t n;

	for (; io->b_off < io->b_len; io->b_off += (size_t)n) {
		(void) memset(&m, 0, sizeof (m));
		iov.iov_base = io->b_data + io->b_off;
		iov.iov_len = io->b_len - io->b_off;
		m.msg_iov = &iov;
		m.msg_iovlen = 1;
		if (fds != NULL && io->b_off == 0) {
			(void) memset(cbuf, 0, sizeof (cbuf));
			m.msg_control = cbuf;
			m.msg_controllen = sizeof (cbuf);
			cm = CMSG_FIRSTHDR(&m);
			cm->cmsg_level = SOL_SOCKET;
			cm->cmsg_type = SCM_RIGHTS;
			cm->cmsg_len = CMSG_LEN(3 * sizeof (int));
			(void) memcpy(CMSG_DATA(cm), fds, 3 * sizeof (int));
		}
		if ((n = sendmsg(fd, &m, 0)) < 0) {
			sh_errf(e, "%s: %s", sock, strerror(errno));
			return (-1);
		}
	}
	return (0);
}

int
sh_command(const char *sock, int argc, char *const *argv, const int *fds,
    FILE *out, FILE *err)
{
	static const int sigs[3] = { SIGINT, SIGTERM, SIGHUP };
	struct sigaction sa, old[3];
	struct sockaddr_un sun;
	struct sh_buf args = { NULL, 0, 0 }, io = { NULL, 0, 0 };
	struct termios tio;
	struct sh_err e;
	char buf[4096];
	const char *p;
	size_t len;
	ssize_t n;
	int fd, kind, i, status = -1, kept = 0, held = 0, half = 0;

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
	if (fds != NULL) {
		kept = tcgetattr(fds[0], &tio) == 0;
		sigged = 0;
		(void) memset(&sa, 0, sizeof (sa));
		sa.sa_handler = caught;
		(void) sigemptyset(&sa.sa_mask);
		/* no SA_RESTART: a signal ends the read it comes in */
		for (i = 0; i < 3; i++)
			(void) sigaction(sigs[i], &sa, &old[i]);
		held = 1;
	}
	for (i = 0; i < argc; i++)
		sh_buf_add(&args, argv[i], strlen(argv[i]) + 1);
	sh_wire_put(&io, SH_WIRE_ARGS, args.b_data, args.b_len);
	if (send_all(fd, &io, fds, sock, &e) != 0) {
		status = 1;
		goto out;
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
		if (sigged && !half) {
			(void) shutdown(fd, SHUT_WR);
			half = 1;
		}
		if ((n = read(fd, buf, sizeof (buf))) < 0 && errno == EINTR)
			continue;
		if (n <= 0) {
			sh_errf(&e, "no reply");
			status = 1;
			break;
		}
		sh_buf_add(&io, buf, (size_t)n);
	}
out:
	for (i = 0; held && i < 3; i++)
		(void) sigaction(sigs[i], &old[i], NULL);
	if (kept)
		(void) tcsetattr(fds[0], TCSANOW, &tio);
	if (fd >= 0)
		(void) close(fd);
	free(args.b_data);
	free(io.b_data);
	return (status);
}
