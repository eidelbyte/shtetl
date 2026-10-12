/*
 * Running a program. One fork, one exec, one wait; the output read
 * from pipes while the child runs.
 *
 * The signal handling follows zfs_rebase's launch.c, with one
 * difference. The four stopping signals are blocked around the fork,
 * so that the handler either sees the child's group or the signal is
 * still pending, and the handler does the one thing it may, kill(2).
 * The difference: the child here has a process group of its own,
 * since what shtetl runs (make with its jobs, git with its helpers)
 * is a tree, and one kill(-pgrp) reaches all of it. The first signal
 * sends SIGTERM to the group, a second SIGKILL. The terminal's Ctrl-C
 * reaches only shtetl, which passes it on this way; and stdin is
 * /dev/null, so a child in a background group never stops on reading
 * the terminal.
 *
 * The child's verdict on exec comes back through a pipe that exec
 * closes: an errno if exec failed, end-of-file if it did not. A child
 * that leaves something running with its output (a daemon a recipe's
 * command started) does not hold the run: once the child is reaped,
 * what its pipes already hold is read and the run returns.
 */

#include <sys/types.h>
#include <sys/wait.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "core/err.h"
#include "core/run.h"

extern char **environ;

#define	RU_NSIG		4
#define	RU_BUF		65536
#define	RU_POLL_MS	200

static const int ru_sigs[RU_NSIG] = { SIGINT, SIGTERM, SIGQUIT, SIGHUP };

/* The child's process group while one runs, else 0. */
static volatile sig_atomic_t ru_pgrp;

/* The first stopping signal shtetl got during this run, or 0. */
static volatile sig_atomic_t ru_caught;

static void
ru_handler(int sig)
{
	int saved = errno;
	pid_t pg = (pid_t)ru_pgrp;

	if (pg > 0)
		(void) kill(-pg, ru_caught == 0 ? SIGTERM : SIGKILL);
	if (ru_caught == 0)
		ru_caught = sig;
	errno = saved;
}

/* A signal's name, for messages. */
static const char *
ru_signame(int sig, char *buf, size_t len)
{
	static const struct {
		int		s_sig;
		const char	*s_name;
	} names[] = {
		{ SIGHUP, "SIGHUP" }, { SIGINT, "SIGINT" },
		{ SIGQUIT, "SIGQUIT" }, { SIGILL, "SIGILL" },
		{ SIGABRT, "SIGABRT" }, { SIGKILL, "SIGKILL" },
		{ SIGBUS, "SIGBUS" }, { SIGSEGV, "SIGSEGV" },
		{ SIGPIPE, "SIGPIPE" }, { SIGALRM, "SIGALRM" },
		{ SIGTERM, "SIGTERM" }
	};
	size_t i;

	for (i = 0; i < sizeof (names) / sizeof (names[0]); i++) {
		if (names[i].s_sig == sig)
			return (names[i].s_name);
	}
	(void) snprintf(buf, len, "signal %d", sig);
	return (buf);
}

/*
 * The file to exec for NAME: NAME itself when it has a '/', else the
 * first executable NAME on SH_RUN_PATH. 0, or -1 if there is none.
 */
static int
ru_find(const char *name, char *path, size_t len)
{
	const char *p, *end;

	if (strchr(name, '/') != NULL) {
		if (strlen(name) >= len)
			return (-1);
		(void) strcpy(path, name);
		return (0);
	}
	for (p = SH_RUN_PATH; *p != '\0'; p = *end != '\0' ? end + 1 : end) {
		if ((end = strchr(p, ':')) == NULL)
			end = p + strlen(p);
		if (snprintf(path, len, "%.*s/%s", (int)(end - p), p,
		    name) >= (int)len)
			continue;
		if (access(path, X_OK) == 0)
			return (0);
	}
	return (-1);
}

static int
ru_pipe(int fd[2])
{
	if (pipe(fd) != 0)
		return (-1);
	if (fcntl(fd[0], F_SETFD, FD_CLOEXEC) == -1 ||
	    fcntl(fd[1], F_SETFD, FD_CLOEXEC) == -1) {
		(void) close(fd[0]);
		(void) close(fd[1]);
		return (-1);
	}
	return (0);
}

static void
ru_close(int *fd)
{
	if (*fd != -1) {
		(void) close(*fd);
		*fd = -1;
	}
}

/* Every descriptor from LOW up closed, in the child. */
static void
ru_closefrom(int low)
{
#if defined(__FreeBSD__)
	closefrom(low);
#else
	long max;
	int fd;

	max = sysconf(_SC_OPEN_MAX);
	if (max < 0 || max > 65536)
		max = 65536;
	for (fd = low; fd < max; fd++)
		(void) close(fd);
#endif
}

/*
 * The child, after the fork: its group, the default dispositions and
 * shtetl's old mask, its three descriptors and nothing else, its
 * directory, and exec. On a failure the errno goes back through the
 * status pipe, moved to descriptor 3 so that nothing below closes it.
 */
static void
ru_child(const struct sh_run *r, const char *path, int out, int err,
    int st, const sigset_t *mask)
{
	int i, fd, xerr;

	(void) setpgid(0, 0);
	for (i = 0; i < RU_NSIG; i++)
		(void) signal(ru_sigs[i], SIG_DFL);
	(void) signal(SIGPIPE, SIG_DFL);
	(void) sigprocmask(SIG_SETMASK, mask, NULL);
	if ((fd = open("/dev/null", O_RDONLY)) == -1 || dup2(fd, 0) == -1 ||
	    dup2(out, 1) == -1 || dup2(err, 2) == -1)
		goto fail;
	if (dup2(st, 3) == -1 || fcntl(3, F_SETFD, FD_CLOEXEC) == -1)
		goto fail;
	st = 3;
	ru_closefrom(4);
	if (r->ru_dir != NULL && chdir(r->ru_dir) == -1)
		goto fail;
	(void) execve(path, r->ru_argv,
	    r->ru_env != NULL ? r->ru_env : environ);
fail:
	xerr = errno;
	(void) write(st, &xerr, sizeof (xerr));
	_exit(127);
}

static int
ru_write(int fd, const char *buf, size_t len)
{
	ssize_t n;

	while (len > 0) {
		n = write(fd, buf, len);
		if (n == -1 && errno == EINTR)
			continue;
		if (n <= 0)
			return (-1);
		buf += n;
		len -= (size_t)n;
	}
	return (0);
}

/* What the reading found, beyond the bytes. */
struct ru_read {
	int	rr_overflow;	/* more stdout than ru_capmax */
	int	rr_logerr;	/* errno of a failed write to the log */
	int	rr_nomem;
};

/* BUF, LEN read from the child: to the capture, or the log and echo. */
static void
ru_deliver(struct sh_run *r, struct ru_read *rr, int captured,
    const char *buf, size_t len)
{
	char *p;

	if (captured) {
		if (rr->rr_overflow || rr->rr_nomem)
			return;
		if (r->ru_outlen + len > r->ru_capmax) {
			rr->rr_overflow = 1;
			return;
		}
		if ((p = realloc(r->ru_out, r->ru_outlen + len + 1)) == NULL) {
			rr->rr_nomem = 1;
			return;
		}
		r->ru_out = p;
		(void) memcpy(p + r->ru_outlen, buf, len);
		r->ru_outlen += len;
		p[r->ru_outlen] = '\0';
		return;
	}
	if (r->ru_log != -1 && rr->rr_logerr == 0 &&
	    ru_write(r->ru_log, buf, len) != 0)
		rr->rr_logerr = errno != 0 ? errno : EIO;
	if (r->ru_echo)
		(void) ru_write(STDERR_FILENO, buf, len);
}

/*
 * Reads FD until it would block or ends: 1 if it ended (and is closed
 * now), 0 if it is still open.
 */
static int
ru_take(struct sh_run *r, struct ru_read *rr, int *fd, int captured,
    char *buf)
{
	ssize_t n;

	for (;;) {
		n = read(*fd, buf, RU_BUF);
		if (n > 0) {
			ru_deliver(r, rr, captured, buf, (size_t)n);
			continue;
		}
		if (n == -1 && errno == EINTR)
			continue;
		if (n == -1 && errno == EAGAIN)
			return (0);
		ru_close(fd);
		return (1);
	}
}

/*
 * The child's output, until it is reaped and what it left is read, or
 * every pipe has ended. *REAPED and *STATUS say whether waitpid has
 * collected it here.
 */
static void
ru_drain(struct sh_run *r, struct ru_read *rr, pid_t pid, int out, int err,
    int *reaped, int *status)
{
	struct pollfd pf[2];
	char *buf;
	int fds[2], cap[2], nfd, i, last;

	fds[0] = out;
	cap[0] = r->ru_capmax > 0;
	fds[1] = err;
	cap[1] = 0;
	if ((buf = malloc(RU_BUF)) == NULL) {
		rr->rr_nomem = 1;
		return;
	}
	for (i = 0; i < 2; i++) {
		if (fds[i] != -1)
			(void) fcntl(fds[i], F_SETFL, O_NONBLOCK);
	}
	for (last = 0; !last; ) {
		if (!*reaped && waitpid(pid, status, WNOHANG) == pid)
			*reaped = 1;
		/* the child is gone: one last read of what it left */
		last = *reaped;
		for (nfd = 0, i = 0; i < 2; i++) {
			if (fds[i] == -1)
				continue;
			pf[nfd].fd = fds[i];
			pf[nfd].events = POLLIN;
			pf[nfd].revents = 0;
			nfd++;
		}
		if (nfd == 0)
			break;
		if (!last && poll(pf, (nfds_t)nfd, RU_POLL_MS) == -1 &&
		    errno != EINTR)
			break;
		for (i = 0; i < 2; i++) {
			if (fds[i] != -1)
				(void) ru_take(r, rr, &fds[i], cap[i], buf);
		}
	}
	for (i = 0; i < 2; i++)
		ru_close(&fds[i]);
	free(buf);
}

int
sh_run(struct sh_run *r, struct sh_err *e)
{
	const char *name = r->ru_argv[0];
	struct sigaction sa, old[RU_NSIG], oldpipe;
	struct ru_read rr;
	sigset_t block, oldmask;
	char path[PATH_MAX], sbuf[32];
	int outp[2] = { -1, -1 }, errp[2] = { -1, -1 }, stp[2] = { -1, -1 };
	int i, xerr, reaped, status, sig;
	ssize_t n;
	pid_t pid;

	r->ru_out = NULL;
	r->ru_outlen = 0;
	r->ru_status = 0;
	r->ru_signal = 0;
	(void) memset(&rr, 0, sizeof (rr));
	if (ru_find(name, path, sizeof (path)) != 0) {
		sh_errf(e, "%s: not found on %s", name, SH_RUN_PATH);
		return (-1);
	}
	if (ru_pipe(outp) != 0 || (r->ru_capmax > 0 && ru_pipe(errp) != 0) ||
	    ru_pipe(stp) != 0) {
		sh_errf(e, "%s: pipe: %s", name, strerror(errno));
		goto closeall;
	}

	(void) sigemptyset(&block);
	for (i = 0; i < RU_NSIG; i++)
		(void) sigaddset(&block, ru_sigs[i]);
	(void) sigprocmask(SIG_BLOCK, &block, &oldmask);
	(void) memset(&sa, 0, sizeof (sa));
	sa.sa_handler = ru_handler;
	sa.sa_mask = block;
	for (i = 0; i < RU_NSIG; i++)
		(void) sigaction(ru_sigs[i], &sa, &old[i]);
	sa.sa_handler = SIG_IGN;
	(void) sigemptyset(&sa.sa_mask);
	(void) sigaction(SIGPIPE, &sa, &oldpipe);
	ru_caught = 0;

	pid = fork();
	if (pid == 0)
		ru_child(r, path, outp[1], errp[1] != -1 ? errp[1] : outp[1],
		    stp[1], &oldmask);
	xerr = errno;
	if (pid > 0) {
		(void) setpgid(pid, pid);
		ru_pgrp = pid;
	}
	(void) sigprocmask(SIG_SETMASK, &oldmask, NULL);
	ru_close(&outp[1]);
	ru_close(&errp[1]);
	ru_close(&stp[1]);
	if (pid == -1) {
		sh_errf(e, "%s: fork: %s", name, strerror(xerr));
		goto restore;
	}

	/* exec's verdict: an errno, or the pipe closed by exec */
	do {
		n = read(stp[0], &xerr, sizeof (xerr));
	} while (n == -1 && errno == EINTR);
	ru_close(&stp[0]);
	reaped = 0;
	if (n != sizeof (xerr))
		ru_drain(r, &rr, pid, outp[0], errp[0], &reaped, &status);
	outp[0] = errp[0] = -1;
	while (!reaped) {
		if (waitpid(pid, &status, 0) == pid)
			reaped = 1;
		else if (errno != EINTR)
			break;
	}
	r->ru_status = reaped ? status : 0;

	if ((sig = ru_caught) != 0) {
		r->ru_signal = sig;
		sh_errf(e, "%s: interrupted by %s", name,
		    ru_signame(sig, sbuf, sizeof (sbuf)));
	} else if (n == sizeof (xerr)) {
		sh_errf(e, "cannot run %s: %s", name, strerror(xerr));
	} else if (!reaped) {
		sh_errf(e, "%s: waitpid: %s", name, strerror(errno));
	} else if (WIFSIGNALED(status)) {
		sh_errf(e, "%s: killed by %s", name,
		    ru_signame(WTERMSIG(status), sbuf, sizeof (sbuf)));
	} else if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
		sh_errf(e, "%s: exit status %d", name, WEXITSTATUS(status));
	} else if (rr.rr_nomem) {
		sh_err_nomem(e);
	} else if (rr.rr_overflow) {
		sh_errf(e, "%s: output larger than %zu bytes", name,
		    r->ru_capmax);
	} else if (rr.rr_logerr != 0) {
		sh_errf(e, "%s: log: %s", name, strerror(rr.rr_logerr));
	} else {
		if (r->ru_capmax > 0 && r->ru_out == NULL &&
		    (r->ru_out = calloc(1, 1)) == NULL) {
			sh_err_nomem(e);
			goto restore;
		}
		ru_pgrp = 0;
		for (i = 0; i < RU_NSIG; i++)
			(void) sigaction(ru_sigs[i], &old[i], NULL);
		(void) sigaction(SIGPIPE, &oldpipe, NULL);
		return (0);
	}
restore:
	ru_pgrp = 0;
	for (i = 0; i < RU_NSIG; i++)
		(void) sigaction(ru_sigs[i], &old[i], NULL);
	(void) sigaction(SIGPIPE, &oldpipe, NULL);
	free(r->ru_out);
	r->ru_out = NULL;
	r->ru_outlen = 0;
closeall:
	ru_close(&outp[0]);
	ru_close(&outp[1]);
	ru_close(&errp[0]);
	ru_close(&errp[1]);
	ru_close(&stp[0]);
	ru_close(&stp[1]);
	return (-1);
}
