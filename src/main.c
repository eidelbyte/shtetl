/*
 * shtetl builds FreeBSD from source into foundations and runs houses
 * on them (sprints/sprint-1/implementation-plan.md). `shtetl daemon`
 * is the daemon; every other use of shtetl is the command, which asks
 * the daemon.
 */

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>

#include "core/daemon.h"
#include "core/err.h"
#include "core/zfs.h"

/*
 * The daemon's reports, a line at a time, to syslog: why it could not
 * start, and a line for each roll that fails (ruled 2026-10-20); what
 * a roll's programs write is in its log (SH_LOGDIR).
 */
static int
logged(void *cookie, const char *p, int n)
{
	(void) cookie;
	syslog(LOG_ERR, "%.*s", n > 0 && p[n - 1] == '\n' ? n - 1 : n, p);
	return (n);
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
	if ((log = funopen(NULL, NULL, logged, NULL, NULL)) == NULL) {
		syslog(LOG_ERR, "out of memory");
		return (1);
	}
	(void) setvbuf(log, NULL, _IOLBF, 0);
	sh_err_init(&e, log, "");
	if (sh_daemon_listen(SH_PIDFILE, SH_SOCKET, &lk, &sk, &e) != 0 ||
	    sh_zfs_open(&z, &e) != 0 ||
	    sh_daemon_run(sk, SH_SOCKET, z, SH_LOGDIR, &e) != 0)
		return (1);
	return (0);
}
