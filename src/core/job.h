/*
 * A job (plan 9): the steps an act takes, in order, each a child the
 * daemon runs and watches, or a call it makes, the next begun when the
 * last has ended well. A step that takes long, a build or a rebase, is
 * a child, and the daemon answers everyone else while it runs.
 *
 * A child runs in a process group of its own, its standard input
 * /dev/null, its standard output and error one pipe the daemon reads:
 * each line goes to the job's log, and the last of them are told again
 * when it fails. Its exit ends the step; whatever of its group is left
 * then is sent SIGTERM, so no step's processes outlive it.
 */

#ifndef	SH_JOB_H
#define	SH_JOB_H

#include <sys/types.h>
#include <sys/event.h>

#include <stdio.h>

#include "core/err.h"
#include "core/wire.h"

/* What the daemon's kqueue tells it about: a connection, or a job. */
enum sh_ev {
	SH_EV_CONN,
	SH_EV_JOB
};

#define	SH_JOB_TAIL	8	/* the lines told again when a child fails */

struct sh_step;

struct sh_job {
	enum sh_ev	j_ev;		/* SH_EV_JOB, first, for the udata */
	const char	*j_name;	/* whose, as the log names it */
	FILE		*j_log;		/* each line a child writes */
	struct sh_err	*j_err;		/* a failed child's last lines */
	/* the end: WHY NULL when every step ended well */
	void		(*j_end)(struct sh_job *, const char *why, void *);
	void		*j_arg;
	struct sh_step	*j_steps;	/* the one under way first */
	struct sh_step	**j_last;
	pid_t		j_pid;		/* the child under way, or 0 */
	int		j_fd;		/* its output, or -1 */
	struct sh_buf	j_line;		/* the line it has not ended yet */
	char		*j_tail[SH_JOB_TAIL];
	unsigned	j_ntail;	/* lines kept so far, all told */
};

/*
 * J made ready, empty. Each line a child writes goes to LOG, and a
 * failed child's last lines to E again; NAME, LOG and E outlive J. END
 * is called once, when a step fails, with why ("STEP: exited 2"), or
 * when the last has ended well, with NULL; J holds nothing then, and
 * END may drop it or use it again.
 */
void sh_job_init(struct sh_job *j, const char *name, FILE *log,
    struct sh_err *e, void (*end)(struct sh_job *, const char *, void *),
    void *arg);

/*
 * A step added at the end: a child running ARGV (NULL-ended, copied;
 * its PATH is the daemon's), or a call of FN with ARG in the daemon,
 * which reports why it failed to E and returns -1, or 0. Either is
 * named STEP, for the log, the end's why and status. A call may add
 * steps.
 */
void sh_job_child(struct sh_job *j, const char *step, char *const *argv);
void sh_job_call(struct sh_job *j, const char *step,
    int (*fn)(struct sh_job *, void *, struct sh_err *), void *arg);

/* J begun on the kqueue KQ: its steps run until a child, or its end. */
void sh_job_run(struct sh_job *j, int kq);

/* An event of KQ whose udata is J. */
void sh_job_event(struct sh_job *j, int kq, const struct kevent *ev);

/*
 * The child under way and its group sent SIGTERM and reaped, as when
 * the daemon stops; END is not called, and J is left as sh_job_init
 * left it.
 */
void sh_job_stop(struct sh_job *j);

/* The step under way, or NULL. */
const char *sh_job_step(const struct sh_job *j);

#endif	/* SH_JOB_H */
