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
	struct sh_step	*sp_next;
};

static struct sh_step *
step_add(struct sh_job *j, const char *name)
{
	struct sh_step *s = sh_calloc(1, sizeof (*s));

	s->sp_name = sh_strdup(name);
	*j->j_last = s;
	j->j_last = &s->sp_next;
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
	for (a = s->sp_argv; a != NULL && *a != NULL; a++)
		free(*a);
	free(s->sp_argv);
	free(s->sp_name);
	free(s);
}

void
sh_job_init(struct sh_job *j, const char *name, FILE *log,
    struct sh_err *e, void (*end)(struct sh_job *, const char *, void *),
    void *arg)
{
	(void) memset(j, 0, sizeof (*j));
	j->j_ev = SH_EV_JOB;
	j->j_name = name;
	j->j_log = log;
	j->j_err = e;
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

const char *
sh_job_step(const struct sh_job *j)
{
	return (j->j_steps != NULL ? j->j_steps->sp_name : NULL);
}

/* A line the child wrote: logged, and kept among the last. */
static void
job_line(struct sh_job *j, const char *p, size_t len)
{
	char **slot = &j->j_tail[j->j_ntail++ % SH_JOB_TAIL];

	(void) fprintf(j->j_log, "%s: %.*s\n", j->j_name, (int)len, p);
	free(*slot);
	*slot = sh_calloc(1, len + 1);
	(void) memcpy(*slot, p, len);
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

/* The last lines forgotten: a child's are its own. */
static void
job_untail(struct sh_job *j)
{
	unsigned i;

	for (i = 0; i < SH_JOB_TAIL; i++) {
		free(j->j_tail[i]);
		j->j_tail[i] = NULL;
	}
	j->j_ntail = 0;
}

/* Everything J holds let go, and END told WHY. */
static void
job_end(struct sh_job *j, const char *why)
{
	while (j->j_steps != NULL)
		step_drop(j);
	job_untail(j);
	free(j->j_line.b_data);
	(void) memset(&j->j_line, 0, sizeof (j->j_line));
	j->j_end(j, why, j->j_arg);
}

static void job_exited(struct sh_job *j, int kq);

/*
 * The first step's child begun and watched: 0; -1 with errno when it
 * could not be.
 */
static int
job_spawn(struct sh_job *j, int kq)
{
	char **argv = j->j_steps->sp_argv;
	struct kevent ev;
	int fds[2], null;
	pid_t pid;

	job_untail(j);
	if (pipe(fds) != 0)
		return (-1);
	if ((pid = fork()) == 0) {
		/* the daemon's ignored signals would outlive the exec */
		(void) setpgid(0, 0);
		(void) signal(SIGPIPE, SIG_DFL);
		(void) signal(SIGTERM, SIG_DFL);
		(void) signal(SIGINT, SIG_DFL);
		null = open("/dev/null", O_RDONLY);
		(void) dup2(null, 0);
		(void) dup2(fds[1], 1);
		(void) dup2(fds[1], 2);
		(void) execvp(argv[0], argv);
		(void) dprintf(2, "%s: %s\n", argv[0], strerror(errno));
		_exit(127);
	}
	(void) close(fds[1]);
	if (pid < 0) {
		(void) close(fds[0]);
		return (-1);
	}
	(void) setpgid(pid, pid);
	(void) fcntl(fds[0], F_SETFD, FD_CLOEXEC);
	(void) fcntl(fds[0], F_SETFL, O_NONBLOCK);
	j->j_pid = pid;
	j->j_fd = fds[0];
	EV_SET(&ev, fds[0], EVFILT_READ, EV_ADD, 0, 0, j);
	(void) kevent(kq, &ev, 1, NULL, 0, NULL);
	EV_SET(&ev, pid, EVFILT_PROC, EV_ADD, NOTE_EXIT, 0, j);
	/* gone before it could be watched: its exit is now */
	if (kevent(kq, &ev, 1, NULL, 0, NULL) != 0 && errno == ESRCH)
		job_exited(j, kq);
	return (0);
}

/*
 * The steps from the first on: calls made, until a child is begun or
 * the job ends.
 */
static void
job_next(struct sh_job *j, int kq)
{
	struct sh_step *s;
	struct sh_err e;
	char *buf = NULL, why[1024];
	size_t len = 0;
	FILE *f;
	int r;

	while ((s = j->j_steps) != NULL) {
		if (s->sp_argv != NULL) {
			if (job_spawn(j, kq) == 0)
				return;
			(void) snprintf(why, sizeof (why), "%s: %s",
			    s->sp_name, strerror(errno));
			job_end(j, why);
			return;
		}
		f = sh_memstream(&buf, &len);
		sh_err_init(&e, f, "");
		r = s->sp_fn(j, s->sp_arg, &e);
		(void) fclose(f);
		if (r != 0) {
			(void) snprintf(why, sizeof (why), "%s: %.*s",
			    s->sp_name, (int)strcspn(buf, "\n"), buf);
			free(buf);
			job_end(j, why);
			return;
		}
		free(buf);
		buf = NULL;
		step_drop(j);
	}
	job_end(j, NULL);
}

/*
 * The first step's child has exited: its output read to the last, it
 * reaped, what is left of its group sent SIGTERM; the next step, or
 * the end, with the last lines told again when it failed.
 */
static void
job_exited(struct sh_job *j, int kq)
{
	char why[1024];
	unsigned i;
	int st;

	job_read(j);
	if (j->j_line.b_len > 0)
		job_line(j, j->j_line.b_data, j->j_line.b_len);
	(void) close(j->j_fd);
	j->j_fd = -1;
	while (waitpid(j->j_pid, &st, 0) == -1 && errno == EINTR)
		;
	(void) kill(-j->j_pid, SIGTERM);
	j->j_pid = 0;
	if (WIFEXITED(st) && WEXITSTATUS(st) == 0) {
		step_drop(j);
		job_next(j, kq);
		return;
	}
	if (WIFEXITED(st))
		(void) snprintf(why, sizeof (why), "%s: exited %d",
		    j->j_steps->sp_name, WEXITSTATUS(st));
	else
		(void) snprintf(why, sizeof (why), "%s: killed by signal %d",
		    j->j_steps->sp_name, WTERMSIG(st));
	i = j->j_ntail > SH_JOB_TAIL ? j->j_ntail - SH_JOB_TAIL : 0;
	for (; i < j->j_ntail; i++)
		sh_errf(j->j_err, "%s: %s", j->j_name,
		    j->j_tail[i % SH_JOB_TAIL]);
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
	if (ev->filter == EVFILT_READ)
		job_read(j);
	else
		job_exited(j, kq);
}

void
sh_job_stop(struct sh_job *j)
{
	int st;

	if (j->j_pid != 0) {
		(void) kill(-j->j_pid, SIGTERM);
		while (waitpid(j->j_pid, &st, 0) == -1 && errno == EINTR)
			;
		(void) close(j->j_fd);
	}
	while (j->j_steps != NULL)
		step_drop(j);
	job_untail(j);
	free(j->j_line.b_data);
	sh_job_init(j, j->j_name, j->j_log, j->j_err, j->j_end, j->j_arg);
}
