# m3-logs: where a roll's output goes

Ruled 2026-10-20 (plan 18), after build run 1 put a build's whole
output in syslog, 109,000 lines: "the only think in syslog should be
one error print per stall".

## What was built

- job.[ch]: a job writes a line "# STEP" to its log as each child
  begins, then the child's lines as they are; the name prefix, the
  error sink and the last lines told again are gone (sh_job_init takes
  the log, the end and its argument).
- roll.[ch]: a roll's log is F.log in the log directory (re_logdir),
  made anew when the roll is asked, a line at a time. It holds what the
  programs write, and what the roll says of itself: "# version SNAP",
  "# failed: why", a put-back or a zbuild reset that went wrong. The
  environment's error sink, syslog in the daemon, hears one line of a
  roll, "foundation F: why", when it fails. A log that cannot be made
  refuses the ask with its path and why, before a stall is cleared.
- daemon.[ch], main.c: SH_LOGDIR, /var/log/shtetl, made at the start;
  sh_daemon_run takes it in place of the info stream, which is gone;
  the one syslog stream is the error one.
- foundation.c: the start's "a first build undone" goes: it is no
  stall. A start that ZFS refuses still says its one line: that is a
  stall.
- tests/box/build.sh: R2 reads the log, R10 that syslog heard one
  line over every roll of the run, R6's stall; the log of the run
  shows syslog whole and the last roll's step lines.

## Decisions

- A roll that fails with nothing to stall, a first roll, says its one
  line too: it is the failed roll's only report, and the kind of event
  the ruling keeps.
- A daemon that cannot start, or runs out of memory, still says so in
  syslog, as ruled before (10): the ruling is of what a roll says.
- The log is opened when the roll is asked, not when it starts, so a
  log that cannot be made is said to the person asking, and a stall is
  not cleared for a roll that cannot run.
- The lines go into the log as the programs write them, without the
  foundation's name before each: the file is the foundation's.

## Verified

- make check, all 15: check_job's J1, J2 with the step lines, J3
  retired; check_roll's L1 - L5 and K15 reading make's words from the
  log; check_daemon's L6; check_foundation's A3, nothing said.
- Four mutations caught: the failure kept from syslog (L3), the
  version sent there (L1), the log appended to (L2), no step lines
  (L1).
- gate, xcheck, no leaks of ours.
- Owed: R2 and R10 on the box, the next build.sh; run.sh's A3.

## Commit

The commit after 063dea3, "logs: a roll's output in its foundation's
log; syslog one line per failed roll".
