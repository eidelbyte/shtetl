/*
 * Jobs and the builder, src/core/job.c and builder.c: families J and
 * T of tests/MATRIX.md. The children are sh -c scripts.
 */

#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>

#include <errno.h>
#include <signal.h>

#include "check.h"
#include "core/builder.h"
#include "core/job.h"

static struct cap c;		/* the failed child's last lines */
static char *logbuf;
static size_t loglen;
static FILE *logf;
static struct sh_job job;
static int ended;
static char why[1024];

static void
end(struct sh_job *j, const char *w, void *arg)
{
	(void) j;
	(void) arg;
	ended++;
	(void) snprintf(why, sizeof (why), "%s", w != NULL ? w : "(well)");
}

/* A fresh job named t, its log and its sink empty. */
static void
fresh(void)
{
	if (logf != NULL)
		(void) fclose(logf);
	free(logbuf);
	logbuf = NULL;
	CHECK((logf = open_memstream(&logbuf, &loglen)) != NULL);
	cap_reset(&c);
	sh_job_init(&job, "t", logf, &c.c_err, end, NULL);
	ended = 0;
}

static void
child(const char *step, const char *script)
{
	const char *argv[] = { "sh", "-c", script, NULL };

	sh_job_child(&job, step, argv);
}

/* KQ's events handed to the job until it ends: no more than 20 s. */
static void
wait_end(int kq)
{
	struct timespec ts = { 20, 0 };
	struct kevent ev[8];
	int i, n;

	while (!ended) {
		CHECK((n = kevent(kq, NULL, 0, ev, 8, &ts)) > 0);
		for (i = 0; i < n; i++)
			sh_job_event(ev[i].udata, kq, &ev[i]);
	}
}

/* The job run to its end: its why, the log in logbuf. */
static const char *
run(void)
{
	int kq;

	CHECK((kq = kqueue()) >= 0);
	sh_job_run(&job, kq);
	wait_end(kq);
	(void) close(kq);
	(void) fflush(logf);
	CHECK(ended == 1);
	return (why);
}

static int
logged(const char *s)
{
	(void) fflush(logf);
	return (logbuf != NULL && strstr(logbuf, s) != NULL);
}

/* Calls: one that adds a child, one that fails, one that notes it ran. */
static int called;

static int
call_adds(struct sh_job *j, void *arg, struct sh_err *e)
{
	(void) j;
	(void) arg;
	(void) e;
	child("added", "echo added");
	return (0);
}

static int
call_fails(struct sh_job *j, void *arg, struct sh_err *e)
{
	(void) j;
	(void) arg;
	sh_errf(e, "var/db: a link");
	return (-1);
}

static int
call_notes(struct sh_job *j, void *arg, struct sh_err *e)
{
	(void) j;
	(void) arg;
	(void) e;
	called++;
	return (0);
}

/* Whether the process PID is gone, waited for up to 5 s. */
static int
gone(pid_t pid)
{
	int i;

	for (i = 0; i < 50; i++) {
		if (kill(pid, 0) != 0 && errno == ESRCH)
			return (1);
		(void) usleep(100000);
	}
	return (0);
}

/* The number a line "t: N" of the log holds. */
static pid_t
logged_pid(void)
{
	(void) fflush(logf);
	return ((pid_t)strtol(logbuf + 3, NULL, 10));
}

static int order[4], norder;

static void
turn(void *arg)
{
	order[norder++] = (int)(intptr_t)arg;
}

int
main(void)
{
	struct sh_builder b;
	char big[5000];
	int kq;

	cap_open(&c);

	/* J1: children in order, each line logged with the job's name */
	fresh();
	child("a", "echo one; echo two");
	child("b", "echo three");
	CHECK(strcmp(run(), "(well)") == 0);
	CHECK(strcmp(logbuf, "t: one\nt: two\nt: three\n") == 0);
	CHECK(sh_job_step(&job) == NULL);

	/* J2: a child that fails ends it; its last lines told again */
	fresh();
	child("a", "echo a");
	child("b", "echo b1; echo b2 >&2; exit 3");
	child("c", "echo c");
	CHECK(strcmp(run(), "b: exited 3") == 0);
	CHECK(logged("t: b1\n") && logged("t: b2\n") && !logged("t: c"));
	CHECK(strcmp(cap_text(&c), "t: b1\nt: b2\n") == 0);

	/* J3: only the last eight lines told again */
	fresh();
	child("many", "i=1; while [ $i -le 20 ]; do echo l$i; i=$((i+1)); "
	    "done; exit 1");
	CHECK(strcmp(run(), "many: exited 1") == 0);
	CHECK(strncmp(cap_text(&c), "t: l13\n", 7) == 0 &&
	    cap_has(&c, "t: l20\n") && !cap_has(&c, "t: l12\n"));

	/* J4: calls, one adding a step, one failing with its words */
	fresh();
	called = 0;
	sh_job_call(&job, "adds", call_adds, NULL);
	sh_job_call(&job, "notes", call_notes, NULL);
	CHECK(strcmp(run(), "(well)") == 0);
	CHECK(called == 1 && logged("t: added\n"));
	fresh();
	sh_job_call(&job, "inject", call_fails, NULL);
	child("after", "echo after");
	CHECK(strcmp(run(), "inject: var/db: a link") == 0);
	CHECK(!logged("after"));

	/* J5: killed by a signal */
	fresh();
	child("k", "kill -9 $$");
	CHECK(strcmp(run(), "k: killed by signal 9") == 0);

	/* J6: a program not there */
	fresh();
	{
		const char *argv[] = { "/nonexistent/shtetl-x", NULL };

		sh_job_child(&job, "x", argv);
	}
	CHECK(strcmp(run(), "x: exited 127") == 0);
	CHECK(logged("t: /nonexistent/shtetl-x: No such file or directory"));

	/* J7: a last line with no newline told all the same */
	fresh();
	child("n", "printf 'no newline'");
	CHECK(strcmp(run(), "(well)") == 0);
	CHECK(strcmp(logbuf, "t: no newline\n") == 0);

	/* J8: a line longer than 4096 told in pieces */
	fresh();
	child("long", "i=0; while [ $i -lt 5000 ]; do printf x; i=$((i+1)); "
	    "done; echo");
	CHECK(strcmp(run(), "(well)") == 0);
	(void) memset(big, 'x', 4096);
	big[4096] = '\n';
	big[4097] = '\0';
	CHECK(strncmp(logbuf + 3, big, 4097) == 0 &&
	    strlen(logbuf) == 3 + 4097 + 3 + 904 + 1);

	/* J9: stopped: the child and its group killed, reaped, no end */
	fresh();
	child("sleep", "sleep 30 & echo $!; wait");
	CHECK((kq = kqueue()) >= 0);
	sh_job_run(&job, kq);
	while (!logged("\n")) {
		struct timespec ts = { 20, 0 };
		struct kevent ev;

		CHECK(kevent(kq, NULL, 0, &ev, 1, &ts) == 1);
		sh_job_event(ev.udata, kq, &ev);
	}
	CHECK(strcmp(sh_job_step(&job), "sleep") == 0);
	{
		pid_t g = logged_pid();

		sh_job_stop(&job);
		CHECK(ended == 0 && gone(g));
	}
	(void) close(kq);

	/* J10: what a step leaves behind sent SIGTERM at its end */
	fresh();
	child("leaves", "sleep 30 & echo $!");
	CHECK(strcmp(run(), "(well)") == 0);
	CHECK(gone(logged_pid()));

	/* J11: the daemon's ignored signals are the child's own again */
	(void) signal(SIGTERM, SIG_IGN);
	fresh();
	child("term", "kill -TERM $$; exit 0");
	CHECK(strcmp(run(), "term: killed by signal 15") == 0);
	(void) signal(SIGTERM, SIG_DFL);

	/* J12: standard input is /dev/null: a reader ends */
	fresh();
	child("cat", "cat");
	CHECK(strcmp(run(), "(well)") == 0);

	/* T1, T2, T3: one turn at a time, in the order asked */
	sh_builder_init(&b);
	sh_builder_ask(&b, turn, (void *)1);
	CHECK(norder == 1 && order[0] == 1);
	sh_builder_ask(&b, turn, (void *)2);
	sh_builder_ask(&b, turn, (void *)3);
	CHECK(norder == 1);
	sh_builder_give(&b);
	CHECK(norder == 2 && order[1] == 2);
	sh_builder_give(&b);
	CHECK(norder == 3 && order[2] == 3);
	sh_builder_give(&b);
	CHECK(b.b_busy == 0);
	sh_builder_ask(&b, turn, (void *)4);
	CHECK(norder == 4 && order[3] == 4);
	sh_builder_give(&b);

	(void) fclose(logf);
	free(logbuf);
	cap_close(&c);
	return (0);
}
