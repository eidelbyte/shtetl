/*
 * Running a program: the one way shtetl runs anything (planning.md
 * 10). An argument list, never a shell; output to a log, to the
 * terminal, or captured; the exit status read and named.
 */

#ifndef	SH_RUN_H
#define	SH_RUN_H

#include <stddef.h>

#include "core/err.h"

/*
 * Where a program named without a '/' is looked for. Fixed, so that
 * what shtetl runs does not depend on the PATH it was started with.
 */
#define	SH_RUN_PATH	"/sbin:/bin:/usr/sbin:/usr/bin:/usr/local/sbin:" \
	"/usr/local/bin"

struct sh_run {
	/* what to run */
	char *const	*ru_argv;	/* ru_argv[0] names the program */
	char *const	*ru_env;	/* the whole environment, or NULL */
	const char	*ru_dir;	/* the working directory, or NULL */
	int		ru_log;		/* where the output goes, or -1 */
	int		ru_echo;	/* the output to standard error too */
	size_t		ru_capmax;	/* above 0: stdout captured, up to it */

	/* what happened */
	char		*ru_out;	/* captured stdout; the caller frees */
	size_t		ru_outlen;
	int		ru_status;	/* as waitpid(2) gave it */
	int		ru_signal;	/* what interrupted shtetl, or 0 */
};

/*
 * Runs R: 0 when the program ran and exited 0. Otherwise -1, and E
 * says why: not found, not runnable, its exit status, the signal that
 * killed it, too much output to capture, or shtetl interrupted.
 *
 * The child gets /dev/null for stdin, the output pipes for stdout and
 * stderr (one pipe for both unless stdout is captured), no other
 * descriptor of shtetl's, and a process group of its own. While it
 * runs, SIGINT, SIGTERM, SIGQUIT and SIGHUP to shtetl send SIGTERM to
 * that whole group, a second one SIGKILL, and the run is refused as
 * interrupted once the child is reaped. Being alone in its group, the
 * child cannot stop on the terminal and does not see the terminal's
 * Ctrl-C itself: shtetl decides what reaches it. The run returns when
 * the child exits, even if something it started still holds its
 * output.
 */
int sh_run(struct sh_run *r, struct sh_err *e);

/*
 * The file to exec for NAME into PATH: NAME itself when it has a '/',
 * else the first executable NAME on SH_RUN_PATH. 0, or -1 for none.
 */
int sh_run_find(const char *name, char *path, size_t len);

#endif	/* SH_RUN_H */
