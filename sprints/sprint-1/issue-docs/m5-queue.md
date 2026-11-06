# m5-queue: the queue, freeze, the attended roll

## What was built

Plan 7.2 and 7.4, ruled 2026-10-19 (tests/MATRIX.md family Q).

1. The queue. A house rolls through every version of its foundation
   that arrives on the run, in order, each only when it is idle and
   not stalled. What waits is read from ZFS, not kept: the copy's
   snapshots newer than the active version's origin that have arrived
   (the copy's top snapshot holds shtetl:kind, which the beam sets
   last) and whose shtetl:branch the house's ver allows, the house's
   recipe resolved from zconfig as it is now. sh_house_next is the
   automatic roll: the house active, not stalled, a version waiting,
   and a roll as shtetl roll H's begins, which a conflict stalls;
   nothing waiting, nothing said; any other refusal before it begins
   (a recipe zconfig cannot resolve) stalls the house, "next: why",
   said once, so that it is not asked again at every convergence. The
   daemon asks every house when a convergence of the beam is over,
   after its prune (after a roll, the prune, then the next), and a
   house when its act is, by the new env callback he_idle, the last
   thing a start, an upgrade, a create and a roll do at their end. A
   roll a crash cut short runs again so: the start cleans up the
   incomplete version and then asks.
2. A version's branch: each foundation version's snapshot holds
   shtetl:branch (F21), a create or update roll's the recipe's
   src_branch, a pkg roll's the head's shaken src_branch; the beam
   carries it to the copy with the other shtetl:* properties.
3. shtetl freeze CONTAINER: shtetl:stall "frozen" on the active
   version, a stall the person sets (plan 7.2's proposal, under the
   ruling that freeze pins a house); a stalled house is frozen in its
   stead; refused when busy. shtetl roll H lifts any stall first, a
   freeze among them, and then rolls or says "H: up to date".
4. The attended roll. The command sends its standard input, output
   and error with its request (SCM_RIGHTS) where standard input is a
   terminal; every act but a house's roll lets them go at once. A
   house's roll asked with them is attended: the daemon holds its
   reply until the roll ends, and the house gets a struct
   sh_hattend, the descriptors and a done callback. Each replay step
   is given a then (J19) for zfs_rebase's 1, stopped at conflicts: next
   the step "picker", zfs_rebase -c -m -i IDENT on the person's
   descriptors, IDENT the run's name as the tool knows it (the clone
   form's result, the dataset form's pre-apply snapshot), the house
   down meanwhile; its 0 and the roll goes on, anything else and it is
   undone and stalled. The reply: nothing and 0 rolled, "H: stalled
   STEP: why" and 1, or after the update snapshot "H: STEP: why" and
   1. The command's SIGINT, SIGTERM and SIGHUP half-close its socket
   and it waits on; the daemon reads the end of a held connection as
   the hang-up (sh_house_hangup): a picker under way is sent SIGTERM,
   ending it and not the roll, and no later conflict of the roll waits.
   The command puts its terminal's modes and its signals back as it
   ends.
5. The job (J19): sh_job_fds runs the last step added on given
   descriptors, no pipe; sh_job_then answers one exit status of it
   with a call whose steps run next; sh_job_kill sends the child under
   way SIGTERM.

## Found on the way, fixed in their own commits

- W17 (m5-roll): zfs_rebase(8)'s --abort of a run in applying2 only
  steps back to the conflicts gate, its holds kept; the undo and the
  start's clean aborted once. aborts() reads zfs_rebase:phase and
  plans two for applying2.
- W18 (m5-roll): the undo ran aborts, destroy and stall before the old
  version's start, so a refusal there left the house down, against
  plan 7.2's "the start on every way out". Now: the stall, never a
  failure (kept in memory whatever ZFS says); the start; then the
  aborts, the destroy and the copy, a refusal said once with the house
  running and stalled.

## Decisions beyond the plan and the issue

- What waits is asked twice, at the convergence's end and at an act's,
  because a house busy at the first (starting, upgrading) would
  otherwise wait for the next period.
- An automatic roll's refusal stalls the house. Said once and not
  asked again is the plan's rule for a stall; without it a broken
  recipe would be said at every convergence.
- A version that is a clone of no foundation version, only a hand's,
  has none waiting: shtetl roll H says "H: up to date" of it, and the
  "H: no foundation version" m5-roll added is gone. check_daemon's
  houses are such versions.
- shtetl roll H lifts the stall even where nothing waits, so that a
  frozen house is unfrozen by the command the plan names, whatever the
  run holds.
- The command decides nothing about attending: it sends a terminal's
  descriptors with any request, and only a house's roll takes them.
  build.sh asks its rolls from /dev/null so that none waits on a
  person.
- The attended roll's reply is the roll's end, not "at once": W14 says
  so now.
- A picker's process is in a group of its own and the person's
  terminal is not its controlling terminal, so the terminal's
  job-control signals go to the command, whose handler half-closes.
  SIGWINCH does not reach the picker.
- Same-second snapshot names (ruled out of scope 2026-10-20) meet the
  tests twice: two rolls ending in one second (the fake rebaser's -2
  ends a run a second later) and a roll run again in the second the
  first took its final save (a sleep).

## Verification

- Matrix first (cdc7def): family Q, Q1 - Q19, and W17, F21, J19, D26,
  D27; W18 with its fix.
- make check, 20 programs. check_hroll Q1 - Q12, Q14, Q15 asking as
  the daemon asks; check_daemon Q11 - Q13, Q15 - Q17, D26, D27, an
  attended roll through the daemon on a house frozen so that the queue
  leaves it to the test, ZFS refusing each roll's final save; check_job
  J19; check_roll F21.
- Mutants, 21. Caught, 16: what waits not asked whether it arrived,
  nor held to the ver; a version of no foundation a refusal; a
  stalled house taken; an automatic refusal not kept as a stall; an
  upgrade's end, and a roll's, not asking; the freeze not in memory;
  shtetl roll not lifting the stall; a hang-up not heard by a later
  conflict, or not killing the picker; no then on a replay (the
  compiler, its call left unused; Q14 holds the behaviour); the
  person told nothing; the daemon never attending; no descriptors
  taken; the command's signals not caught. Survived, 5, each a gap
  named: the daemon's let-go just after an act (redundant, the
  connection's hang-up lets go; taken out, d2859f2); the convergence's
  ask (build.sh Q19 holds it on the box); the idle ask's wiring in the
  daemon (caught only by the compiler; no test on the mac or the box
  tells it from the convergence's ask); the daemon reading a held
  connection's end as the hang-up, and the command's half-close (a
  picker under way in the daemon: by hand).
- J19's own mutants: a then's steps appended at the end; a terminal's
  child given the pipe. W17's: one abort. W18's: the old order. Each
  fails.
- gate; xcheck-freebsd.
- Not held on the mac, owed to the box or a person: the daemon asking
  houses at a convergence's end and at an act's end, and a half-close
  or a command's going reaching a picker under way in the daemon (each
  needs a real roll in the daemon: build.sh's Q19 holds the first); the
  picker in a person's terminal and the modes put back (by hand).

## Gotchas for the next issues

- m5-derivations: the picker opens for every conflict an attended roll
  meets; derived files must be answered keep and regenerated before
  the gate, and a conflict in one of them should not reach the person.
- The run's copy must carry shtetl:branch for a house to roll onto a
  version: versions made before F21 have none and wait for no one.
- A house's own datasets at a path its foundation later adds: replayed
  onto the foundation's, then "exists" at own (m5-roll's note).

## Owed

- run.sh: check_hroll, check_daemon, check_job, check_roll on FreeBSD.
- build.sh: Q19, s2app rolled by the queue onto box's next version;
  and W16, H20 again, their rolls now asked from no terminal.
- By hand, once, from a terminal on the box: shtetl roll H onto a
  version that conflicts, the picker answered, then again with ^C or a
  closed terminal, the roll stalled and the terminal's modes back.

## Commits

cdc7def (matrix), a6bdc5a (W17), 8eec031 (F21), eddb845 (the queue),
2896e2b (freeze), 53ca40b (J19), 4598d07 (the attended roll, the
house), d2859f2 (the daemon and the command), 22b6393 (W18), 4c4e04d
(build.sh Q19), and this worklog's.
