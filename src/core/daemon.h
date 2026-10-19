/*
 * The daemon (plan 9, 10): one process, one kqueue loop, holding the
 * spaces and the actors and answering the shtetl command over a local
 * socket; and the command, which sends its arguments there and prints
 * the reply.
 */

#ifndef	SH_DAEMON_H
#define	SH_DAEMON_H

#include <stdio.h>

#include "core/err.h"
#include "core/zfs.h"

#define	SH_PIDFILE	"/var/run/shtetl.pid"
#define	SH_SOCKET	"/var/run/shtetl.sock"

/*
 * The daemon's lock and socket. LOCK, a pidfile, is held with flock
 * for the daemon's life with its pid in it; then SOCK, a socket left
 * there by a daemon that died replaced, mode 0600, so only root
 * reaches it, listening. Their descriptors into *LK and *SK, closed on
 * exec. Refused: "already running", or what the system said of a path.
 */
int sh_daemon_listen(const char *lock, const char *sock, int *lk, int *sk,
    struct sh_err *e);

/*
 * The daemon, on SK, listening at SOCK: the spaces found and their
 * skeleton made, then the loop until SIGTERM or SIGINT, and the socket
 * removed. What it has to say goes to E. -1 when it never started.
 */
int sh_daemon_run(int sk, const char *sock, struct sh_zfs *z,
    struct sh_err *e);

/*
 * The command: its ARGC arguments sent to the daemon at SOCK, and the
 * reply written to OUT and ERR as it comes. The exit status.
 */
int sh_command(const char *sock, int argc, char *const *argv, FILE *out,
    FILE *err);

#endif	/* SH_DAEMON_H */
