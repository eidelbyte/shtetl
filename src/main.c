/*
 * shtetl builds FreeBSD from source into foundations and runs houses
 * on them (sprints/sprint-1/implementation-plan.md). `shtetl daemon`
 * is the daemon; every other use of shtetl is the command, which asks
 * the daemon.
 */

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include "core/daemon.h"
#include "core/err.h"
#include "core/zfs.h"

/* A line at a time to syslog, at the priority the cookie holds. */
static int
logged(void *cookie, const char *p, int n)
{
	syslog((int)(intptr_t)cookie, "%.*s",
	    n > 0 && p[n - 1] == '\n' ? n - 1 : n, p);
	return (n);
}

/* A stream to syslog at PRI, a line at a time. */
static FILE *
logstream(int pri)
{
	FILE *f;

	if ((f = funopen((void *)(intptr_t)pri, NULL, logged, NULL,
	    NULL)) == NULL) {
		syslog(LOG_ERR, "out of memory");
		exit(1);
	}
	(void) setvbuf(f, NULL, _IOLBF, 0);
	return (f);
}

int
main(int argc, char **argv)
{
	struct sh_zfs *z;
	struct sh_err e;
	FILE *log;
	int lk, sk;

	(void) signal(SIGPIPE, SIG_IGN);
	if (argc != 2 || strcmp(argv[1], "daemon") != 0)
		return (sh_command(SH_SOCKET, argc - 1, argv + 1, stdout,
		    stderr));
	openlog("shtetl", LOG_PID | LOG_PERROR, LOG_DAEMON);
	/* what children write is info; what goes wrong, an error */
	log = logstream(LOG_INFO);
	sh_err_init(&e, logstream(LOG_ERR), "");
	if (sh_daemon_listen(SH_PIDFILE, SH_SOCKET, &lk, &sk, &e) != 0 ||
	    sh_zfs_open(&z, &e) != 0 ||
	    sh_daemon_run(sk, SH_SOCKET, z, log, &e) != 0)
		return (1);
	return (0);
}
