# m4-beam: every dataset's copy converging to its owner's

Plan 8.1 and 9. Built in five commits, each a unit: the ZFS layer, the
forked call, the skeleton, the fake, and the beam.

## What was built

- zfs.h (1ff5464): sh_zfs_set and sh_zfs_inherit take a snapshot as
  well as a filesystem, and sh_zfs_props walks the user properties
  set on a name itself. The beam copies a version's shtetl:* after
  each receive (no send -p), and a version's are on its snapshots.
  Cells Z16, Z17.
- job.[ch] (69a4861): sh_job_fork, a step that is a call made in a
  child of its own, forked and watched as a program is: its own
  process group, the daemon's ignored signals its own again, its
  output the job's pipe, its group sent SIGTERM at its end and at a
  stop. What it changes is its copy's. A failing one fails as a call
  does, "STEP: its words": the job keeps the last line a child wrote.
  A send can take hours; plan 9 names "a forked lzc_send" as a process
  an actor watches. Cells J13 - J16.
- spaces.c (d3735ed): the beam's skeleton rows. lake/containers and
  lake/systems mountpoint none and readonly; run/foundations canmount
  off and readonly; run/containers mountpoint=/containers (m5-create's
  row, made now because the beam walks it); run/systems canmount off.
  Cell S12.
- tests/zfake.[ch] (3c5f14c): the fake's state in an anonymous shared
  mapping, as ZFS's is in the kernel, so what a forked send receives
  every process sees; zfake_cut_nth and zfake_export. Cell Z18 holds
  both layers to it: a filesystem made in a forked child is seen by
  its parent.
- beam.[ch], and roll.[ch], daemon.[ch] (this commit): the actor.
  - A convergence is one job. Its first step, a look, reads both
    spaces whole: each filesystem below the root, its origin, whether
    a receive to resume stands on it, its snapshots and their guids.
  - A receive to resume below a copy's root is resumed first, a step
    each, and that look does nothing else.
  - Otherwise the copies' shtetl:* properties are made their owners',
    set or let go: each filesystem's, and each snapshot's once all of
    its version is there. Each dataset's lacking snapshots are then
    planned by the guid rule: a parent before its children and a
    clone's origin before the clone, each a step. The streams a look
    plans are added to its picture of the receiving space, so a
    clone's origin sent in that look is the clone stream's.
  - A look that planned anything is followed by another; one that
    plans nothing ends the convergence.
  - The roll tells the beam of each version it makes (re_made, once
    the roll is over). The daemon makes the beam when it finds both
    the lake and the run, opens LOGDIR/beam.log, shows "beam ..." in
    status after the foundations, hands it its timer events
    (SH_EV_BEAM), and stops it at exit.
  - Cells B1 - B29 (B28, the box, is m4-box's), F20, D25.

## Decisions beyond the plan

- Arrival: a version has arrived on a space when its top's copy, the
  copy of the root's child it is in, holds its shtetl:* properties.
  They are set last, once every dataset of the version is there. The
  mark is durable, so a house actor starting in M5 reads it from
  ZFS, as plan 2 rule 1 asks. The beam logs "# arrived SNAP". It
  tells no house actor yet: M5 makes them, and adds the call to the
  beam then. The plan says the tell is what houses roll on.
- Properties converge in every look, not only after a receive: the
  copy's shtetl:* are made the owner's, set or let go. A stall set on
  a foundation and then cleared reaches the copy both times. Only
  shtetl:* is touched, so a property of the copy's own is left alone.
- The beam never says anything to syslog. Ruled 2026-10-20, syslog
  hears one line per stall, and the beam does not stall: a
  convergence that fails is a beam not yet converged (8.1). Its words
  are in beam.log and status, "beam failing WHY". m7-notifier may want
  a beam that fails for long; that is a question for then.
- The timer: hourly after a convergence that ends well
  (SH_BEAM_PERIOD); after one that fails, a minute (SH_BEAM_RETRY),
  doubled with each failure after it, up to the hour. The plan named
  no numbers.
- The first failing stream ends a convergence. A copy that cannot
  take its stream (a snapshot of its own on it, say) holds up the
  streams after it until the person sees to it; status names it.
- A receive to resume whose stream's snapshot is gone is left
  standing ("nothing is destroyed on a failure"). Resuming it fails
  until the person runs receive -A. Pruning waits for the beam, so
  only a person's destroy makes one.
- The receiving roots are readonly, inherited by every copy. A copy
  mounted and read with atime on is written (main's probe P8), and its
  next incremental would be refused. The lake's copies are mountpoint
  none: nothing reads them in place.
- The fake's state is shared with forked children (above), not a seam
  between the daemon and the tests. check_daemon's daemon, a forked
  child, beams over the same fake, and its parent touches the fake
  only between the daemon's runs.
- Family D's cells D19 - D24 (show) had numbers in the prose and no
  rows. The beam's daemon cell is D25, and the rows came with their
  own commit after this one.

## Gotchas for the next issues

- m4-prune: the beam sends what a copy lacks after the newest
  snapshot of the dataset's history that the copy's space holds.
  Pruning a copy's older snapshots or versions brings nothing back.
  Pruning one the owner still has, newer than the copy's newest held,
  would be sent again. The lake must not prune a house version that
  the run still holds: its root's snapshots would go back as a clone
  stream.
- A dataset with no snapshots of its own is not beamed: nothing can
  be sent of it. After a promote on the run, the old version's www is
  such a clone. Its lake copy keeps what it had; a lake that never
  had it lacks it. Restore (m5-restore) should know.
- The beam holds the daemon's loop for a look: both spaces are read
  whole, and every held snapshot's properties are read. That is cheap
  for a host's few hundred snapshots. If it is not, a look can keep
  what it read between looks.
- The props of a version are copied by guid, onto the copy's snapshot
  wherever it is. A promote on the receiving side moves nothing the
  beam needs.
- sh_beam_stop and sh_roll_stop leave queued kevents naming what they
  freed. The daemon closes its kqueue after them; a test that goes on
  must open a new one (check_beam B22).

## Verified

- make check, all 16 programs, check_beam new: B1 - B27 and B29 over
  the fake, its forked streams real children. Cuts at each stream of
  six histories: a first version, later versions, a new dataset, a
  house version, a clone within the pair, and a promoted clone.
- Mutations caught: in the ZFS layer and fake 4, in the forked call 4,
  in the beam 17, in the wiring 3. The beam's: no origin chain, no
  clone streams, no origin order (B29), no resume, no inherit, the
  version not whole (B16, the arrival after the rest), arrival at any
  dataset, the properties parent first (B16, a set failing part way),
  no doubling, no cap, no again, the incremental's base not moved
  on, houses first (B26), resumes anywhere (B25), no properties set,
  no second look, the retry after success. The wiring's: the roll's
  tell (F20), the status line, the beam begun (D25).
- gate, xcheck.
- Leaks: on the mac, leaks --atExit holds a forked child at its exit,
  so its parent waits for ever. The tests that fork were checked
  under lldb, stopped at exit, with leaks pointed at them:
  check_beam, check_zfs and check_spaces 0 leaks. Under lldb the exit
  breakpoint is copied into children that call exit, so check_daemon,
  check_job and check_roll do not run to the end there. check_job's
  J13 - J16 had passed leaks --atExit before the fake was shared.
  AddressSanitizer does not start on this mac: it hangs setting up its
  shadow memory, before main.
- Owed to the box: Z16 - Z18 over libzfs, the next run.sh. The next
  build.sh's daemon beams the box foundation to its run pool, which
  marks a run space; m4-box gives that its cells, B28.

## Commits

1ff5464, 69a4861, d3735ed, 3c5f14c, and "beam: every dataset's copy
converging to its owner's".
