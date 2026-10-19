# m3-builder: jobs, watched children, and the builder's turns

## What was built

- src/core/job.[ch]: a job is the steps an act takes, in order. A
  step is a child the daemon runs and watches, or a call it makes; a
  call may add steps (a roll knows its patches only once their
  repositories are fetched). The next step begins when the last has
  ended well. The end is told once: with why ("build: exited 2",
  "inject: var/db: a link"), or with nothing when every step ended
  well.
- A child:
  - It runs in a process group of its own.
  - SIGPIPE, SIGTERM and SIGINT are put back to their defaults before
    exec, since the daemon ignores them for kqueue, and an ignored
    disposition outlives exec.
  - Its standard input is /dev/null; its standard output and error are
    one pipe, read with EVFILT_READ. Each line goes to the job's log,
    "NAME: line"; a line past 4096 bytes goes in pieces.
  - Its exit, seen with EVFILT_PROC, ends the step. The pipe is then
    read to the last, the child reaped, and what is left of its group
    sent SIGTERM, so no step's processes outlive it.
  - A failed child's last 8 lines are told again to the error sink.
    The log is syslog at info in production, which syslogd keeps only
    where told to; the failure is where a person looks.
- sh_job_stop: the child and its group sent SIGTERM and reaped, as when
  the daemon stops.
- src/core/builder.[ch]: one turn at a time across foundations' rolls,
  in the order asked; a turn given back goes to the next. Nothing
  polls.
- enum sh_ev: what a kevent's udata is, a connection or a job, first in
  each, so the daemon's loop can tell them apart (m3-roll uses it).

## Decisions, beyond or against the issue

- No message queue per actor. The issue said an actor takes one
  message at a time and a message arriving meanwhile waits. But what a
  house waits for is read from ZFS (plan 7.2), and a foundation's roll
  asked for while one is under way is refused (m3-roll), so no queue
  would ever hold anything: an actor busy with a job refuses an act
  that needs it idle, and a nudge (a foundation version arrived) is a
  fresh read of ZFS. The builder's queue is the one queue.
- A child's directory and environment are the daemon's: git takes -C,
  make -C, and a chroot is chroot(8) in argv. No field until one is
  needed.
- The daemon's integration (jobs in its loop, SIGTERM stopping them)
  comes with m3-roll, the first act that makes a job, so that no code
  sits in the loop with nothing to drive it.
- A job may not be freed while kqueue can still name it: jobs live in
  their actors, which outlive them.

## Verified

- make check: check_job, J1 - J12 and T1 - T3, with sh -c children,
  among them a child that leaves a background process (J10), a stop
  while a grandchild sleeps (J9), and the ignored-signal reset (J11).
- Two mutations caught: the signals not put back (J11), no SIGTERM to
  a step's leftovers (J10).
- No leaks (leaks --atExit under an alarm).
- gate, xcheck.
- Owed: the next box run, whose make check-freebsd runs check_job on
  FreeBSD's kqueue.

## Gotchas

- A test that waits on kqueue must give it a timeout, so a mistake
  fails rather than hangs; the first run of check_job hung on a loop
  that read the log's length before flushing it.
- leaks --atExit over a test that forks: run it under an alarm (perl
  -e 'alarm 90; exec @ARGV' leaks ...).

## Commit

4d85567, "job: steps, watched children, and the builder's turns". It
went out with one line of job.c past 80 columns (the gate's failure
was hidden by a pipe); the next commit rewrites that loop to fit.
