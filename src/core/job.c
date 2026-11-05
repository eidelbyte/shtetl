/*
 * Jobs: steps run in order, children watched with EVFILT_PROC and their
 * output read with EVFILT_READ.
 */

#include <sys/types.h>
#include <sys/event.h>
#include <sys/wait.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "core/err.h"
#include "core/job.h"
#include "core/mem.h"
#include "core/wire.h"

#define	JOB_LINE	4096	/* a line longer is told in pieces */

struct sh_step {
	char		*sp_name;
	char		**sp_argv;	/* a child's, NULL-ended; else a call */
	int		(*sp_fn)(struct sh_job *, void *, struct sh_err *);
	void		*sp_arg;
	int		sp_fork;	/* the call made in a child */
	int		sp_term;	/* the child on sp_fds, no pipe */
	int		sp_fds[3];
	int		sp_then;	/* the exit sp_tfn answers, or -1 */
	int		(*sp_tfn)(struct sh_job *, void *, struct sh_err *);
	void		*sp_targ;
	struct sh_step	*sp_next;
};

/* A step added at the end, or next, while a then's call adds steps. */
static struct sh_step *
step_add(struct sh_job *j, const char *name)
{
	struct sh_step *s = sh_calloc(1, sizeof (*s));

	s->sp_name = sh_strdup(name);
	s->sp_then = -1;
	if (j->j_ins != NULL) {
		s->sp_next = *j->j_ins;
		*j->j_ins = s;
		j->j_ins = &s->sp_next;
		if (s->sp_next == NULL)
			j->j_last = &s->sp_next;
	} else {
		*j->j_last = s;
		j->j_last = &s->sp_next;
	}
	j->j_added = s;
	return (s);
}

/* The first step taken off and freed. */
static void
step_drop(struct sh_job *j)
{
	struct sh_step *s = j->j_steps;
	char **a;

	if ((j->j_steps = s->sp_next) == NULL)
		j->j_last = &j->j_steps;
	if (j->j_added == s)
		j->j_added = NULL;
	for (a = s->sp_argv; a != NULL && *a != NULL; a++)
		free(*a);
	free(s->sp_argv);
	free(s->sp_name);
	free(s);
}

void
sh_job_init(struct sh_job *j, FILE *log,
    void (*end)(struct sh_job *, const char *, void *), void *arg)
{
	(void) memset(j, 0, sizeof (*j));
	j->j_ev = SH_EV_JOB;
	j->j_log = log;
	j->j_end = end;
	j->j_arg = arg;
	j->j_last = &j->j_steps;
	j->j_fd = -1;
}

void
sh_job_child(struct sh_job *j, const char *step, const char *const *argv)
{
	struct sh_step *s = step_add(j, step);
	size_t i, n;

	for (n = 0; argv[n] != NULL; n++)
		;
	s->sp_argv = sh_calloc(n + 1, sizeof (char *));
	for (i = 0; i < n; i++)
		s->sp_argv[i] = sh_strdup(argv[i]);
}

void
sh_job_call(struct sh_job *j, const char *step,
    int (*fn)(struct sh_job *, void *, struct sh_err *), void *arg)
{
	struct sh_step *s = step_add(j, step);

	s->sp_fn = fn;
	s->sp_arg = arg;
}

void
sh_job_fork(struct sh_job *j, const char *step,
    int (*fn)(struct sh_job *, void *, struct sh_err *), void *arg)
{
	struct sh_step *s = step_add(j, step);

	s->sp_fn = fn;
	s->sp_arg = arg;
	s->sp_fork = 1;
}

void
sh_job_fds(struct sh_job *j, const int fds[3])
{
	j->j_added->sp_term = 1;
	(void) memcpy(j->j_added->sp_fds, fds, sizeof (j->j_added->sp_fds));
}

void
sh_job_then(struct sh_job *j, int status,
    int (*fn)(struct sh_job *, void *, struct sh_err *), void *arg)
{
	j->j_added->sp_then = status;
	j->j_added->sp_tfn = fn;
	j->j_added->sp_targ = arg;
}

void
sh_job_kill(struct sh_job *j)
{
	if (j->j_pid != 0)
		(void) kill(-j->j_pid, SIGTERM);
}

const char *
sh_job_step(const struct sh_job *j)
{
	return (j->j_steps != NULL ? j->j_steps->sp_name : NULL);
}

/* A line the child wrote, logged, and kept as its last. */
static void
job_line(struct sh_job *j, const char *p, size_t len)
{
	(void) fprintf(j->j_log, "%.*s\n", (int)len, p);
	(void) snprintf(j->j_said, sizeof (j->j_said), "%.*s", (int)len, p);
}

/* What the child has written so far: each line told as it ends. */
static void
job_read(struct sh_job *j)
{
	struct sh_buf *b = &j->j_line;
	char buf[4096];
	const char *p, *nl;
	ssize_t n;
	size_t len, left;

	while ((n = read(j->j_fd, buf, sizeof (buf))) > 0) {
		sh_buf_add(b, buf, (size_t)n);
		for (;;) {
			p = b->b_data + b->b_off;
			left = b->b_len - b->b_off;
			if ((nl = memchr(p, '\n', left)) != NULL)
				len = (size_t)(nl - p);
			else if (left >= JOB_LINE)
				len = JOB_LINE;
			else
				break;
			job_line(j, p, len);
			b->b_off += len + (nl != NULL);
		}
		(void) memmove(b->b_data, b->b_data + b->b_off,
		    b->b_len - b->b_off);
		b->b_len -= b->b_off;
		b->b_off = 0;
	}
}

/* Everything J holds let go, and END told WHY. */
static void
job_end(struct sh_job *j, const char *why)
{
	while (j->j_steps != NULL)
		step_drop(j);
	free(j->j_line.b_data);
	(void) memset(&j->j_line, 0, sizeof (j->j_line));
	j->j_end(j, why, j->j_arg);
}

static void job_exited(struct sh_job *j, int kq);

/*
 * The first step's child begun and watched, a program or a forked
 * call: 0; -1 with errno when it could not be.
 */
static int
job_spawn(struct sh_job *j, int kq)
{
	struct sh_step *s = j->j_steps;
	char **argv = s->sp_argv;
	struct kevent ev;
	struct sh_err e;
	int fds[2], null;
	pid_t pid;

	(void) fprintf(j->j_log, "# %s\n", s->sp_name);
	j->j_said[0] = '\0';
	if (!s->sp_term && pipe(fds) != 0)
		return (-1);
	if ((pid = fork()) == 0) {
		/* the daemon's ignored signals would outlive the exec */
		(void) setpgid(0, 0);
		(void) signal(SIGPIPE, SIG_DFL);
		(void) signal(SIGTERM, SIG_DFL);
		(void) signal(SIGINT, SIG_DFL);
		if (s->sp_term) {
			(void) dup2(s->sp_fds[0], 0);
			(void) dup2(s->sp_fds[1], 1);
			(void) dup2(s->sp_fds[2], 2);
		} else {
			null = open("/dev/null", O_RDONLY);
			(void) dup2(null, 0);
			(void) dup2(fds[1], 1);
			(void) dup2(fds[1], 2);
		}
		if (s->sp_fork) {
			sh_err_init(&e, stderr, "");
			_exit(s->sp_fn(j, s->sp_arg, &e) != 0);
		}
		(void) execvp(argv[0], argv);
		(void) dprintf(2, "%s: %s\n", argv[0], strerror(errno));
		_exit(127);
	}
	if (!s->sp_term)
		(void) close(fds[1]);
	if (pid < 0) {
		if (!s->sp_term)
			(void) close(fds[0]);
		return (-1);
	}
	(void) setpgid(pid, pid);
	j->j_pid = pid;
	if (!s->sp_term) {
		(void) fcntl(fds[0], F_SETFD, FD_CLOEXEC);
		(void) fcntl(fds[0], F_SETFL, O_NONBLOCK);
		j->j_fd = fds[0];
		EV_SET(&ev, fds[0], EVFILT_READ, EV_ADD, 0, 0, j);
		(void) kevent(kq, &ev, 1, NULL, 0, NULL);
	}
	EV_SET(&ev, pid, EVFILT_PROC, EV_ADD, NOTE_EXIT, 0, j);
	/* gone before it could be watched: its exit is now */
	if (kevent(kq, &ev, 1, NULL, 0, NULL) != 0 && errno == ESRCH)
		job_exited(j, kq);
	return (0);
}

/*
 * FN called with ARG for the first step, as a call is: 0; -1 with WHY
 * "STEP: the first line it reported".
 */
static int
job_call(struct sh_job *j, int (*fn)(struct sh_job *, void *,
    struct sh_err *), void *arg, char *why, size_t wlen)
{
	struct sh_err e;
	char *buf = NULL;
	size_t len = 0;
	FILE *f;
	int r;

	f = sh_memstream(&buf, &len);
	sh_err_init(&e, f, "");
	r = fn(j, arg, &e);
	(void) fclose(f);
	if (r != 0)
		(void) snprintf(why, wlen, "%s: %.*s", j->j_steps->sp_name,
		    (int)strcspn(buf, "\n"), buf);
	free(buf);
	return (r);
}

/*
 * The steps from the first on: calls made, until a child is begun or
 * the job ends.
 */
static void
job_next(struct sh_job *j, int kq)
{
	struct sh_step *s;
	char why[1024];

	while ((s = j->j_steps) != NULL) {
		if (s->sp_argv != NULL || s->sp_fork) {
			if (job_spawn(j, kq) == 0)
				return;
			(void) snprintf(why, sizeof (why), "%s: %s",
			    s->sp_name, strerror(errno));
			job_end(j, why);
			return;
		}
		if (job_call(j, s->sp_fn, s->sp_arg, why, sizeof (why)) != 0) {
			job_end(j, why);
			return;
		}
		step_drop(j);
	}
	job_end(j, NULL);
}

/*
 * The first step's child has exited: its output read to the last, it
 * reaped, what is left of its group sent SIGTERM; the next step, or
 * the end: a forked call's why its words, a program's its status. An
 * exit a then answers is its call's, the steps it adds next.
 */
static void
job_exited(struct sh_job *j, int kq)
{
	struct sh_step *s = j->j_steps;
	char why[1024];
	int st, r;

	if (j->j_fd >= 0) {
		job_read(j);
		if (j->j_line.b_len > 0)
			job_line(j, j->j_line.b_data, j->j_line.b_len);
		(void) close(j->j_fd);
		j->j_fd = -1;
	}
	while (waitpid(j->j_pid, &st, 0) == -1 && errno == EINTR)
		;
	(void) kill(-j->j_pid, SIGTERM);
	j->j_pid = 0;
	if (WIFEXITED(st) && WEXITSTATUS(st) == s->sp_then) {
		j->j_ins = &s->sp_next;
		r = job_call(j, s->sp_tfn, s->sp_targ, why, sizeof (why));
		j->j_ins = NULL;
		if (r != 0) {
			job_end(j, why);
			return;
		}
	}
	if (WIFEXITED(st) && (WEXITSTATUS(st) == 0 ||
	    WEXITSTATUS(st) == s->sp_then)) {
		step_drop(j);
		job_next(j, kq);
		return;
	}
	/* a child on a terminal said its words there, not to the job */
	if (WIFEXITED(st) && s->sp_fork && !s->sp_term)
		(void) snprintf(why, sizeof (why), "%s: %s",
		    j->j_steps->sp_name, j->j_said);
	else if (WIFEXITED(st))
		(void) snprintf(why, sizeof (why), "%s: exited %d",
		    j->j_steps->sp_name, WEXITSTATUS(st));
	else
		(void) snprintf(why, sizeof (why), "%s: killed by signal %d",
		    j->j_steps->sp_name, WTERMSIG(st));
	job_end(j, why);
}

void
sh_job_run(struct sh_job *j, int kq)
{
	job_next(j, kq);
}

void
sh_job_event(struct sh_job *j, int kq, const struct kevent *ev)
{
	struct kevent d;

	if (ev->filter != EVFILT_READ) {
		job_exited(j, kq);
		return;
	}
	job_read(j);
	/*
	 * Its end read, the output is watched no more: a pipe at its end
	 * is ever readable, and the watch would be handed back at every
	 * kevent until the exit, a spin for as long as a child that closed
	 * its output lives on.
	 */
	if (ev->flags & EV_EOF) {
		EV_SET(&d, ev->ident, EVFILT_READ, EV_DELETE, 0, 0, NULL);
		(void) kevent(kq, &d, 1, NULL, 0, NULL);
	}
}

void
sh_job_stop(struct sh_job *j, int kq)
{
	struct kevent ev;
	int st;

	if (j->j_pid != 0) {
		/* its exit, queued once it is killed, would find J reused */
		EV_SET(&ev, j->j_pid, EVFILT_PROC, EV_DELETE, 0, 0, NULL);
		(void) kevent(kq, &ev, 1, NULL, 0, NULL);
		(void) kill(-j->j_pid, SIGTERM);
		while (waitpid(j->j_pid, &st, 0) == -1 && errno == EINTR)
			;
		if (j->j_fd >= 0)
			(void) close(j->j_fd);
	}
	while (j->j_steps != NULL)
		step_drop(j);
	free(j->j_line.b_data);
	sh_job_init(j, j->j_log, j->j_end, j->j_arg);
}
