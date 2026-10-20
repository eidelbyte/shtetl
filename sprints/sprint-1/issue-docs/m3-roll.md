# m3-roll: a foundation's roll, its stall, and shtetl roll

m3-roll was split before it was built (592991e): the build and install
went to m3-install, pkg and the commands to m3-commands. What is left
is the roll's frame, in two commits: the ZFS calls the stall needs,
then the roll.

## What was built

- 6ca6b61, zfs: sh_zfs_set and sh_zfs_inherit, a user property set and
  let go (libzfs zfs_prop_set and zfs_prop_inherit; the fake replaces
  or drops the local value). User properties only: libzfs remounts a
  filesystem whose mountpoint it sets, and shtetl sets no native
  property after a filesystem's birth. Cells Z10.
- src/core/roll.[ch]: the roll, one job holding the builder's turn
  from its first step to its last. Its steps, each a call adding the
  next: site (shtetl.conf, then the clones of m3-src), recipe (the
  recipe resolved, ver held to src_branch), head, src and patches
  (m3-src), version (REVISION from sys/conf/newvers.sh, ver held to
  it, the name, the src commit from FETCH_HEAD), inject
  (foundation.conf: sh_src_record's lines over the shaken recipe),
  snapshot (the head unmounted, children first, every dataset of it
  snapshotted at once with shtetl:kind and shtetl:commit).
- A failure: logged "foundation F: STEP: why"; every dataset put back
  by sh_foundation_idle (the start's rule, exported), a first roll's
  destroyed; shtetl:stall set to the why where F has a version; then
  zbuild reset (sh_src_reset), the turn given back when that ends.
- sh_roll_ask (shtetl roll F): refused, "F: rolling", while F waits or
  rolls; a stall inherited away; then the builder asked. sh_roll_stop:
  the child killed, nothing undone, for the daemon's stop.
- foundation.[ch]: foundations held by pointer, each its own
  allocation, so a roll's foundation stays put while a first roll adds
  another to the list (sh_foundation_find); the states waiting and
  rolling, and status lines for them.
- daemon.c: the loop takes one event at a time, a job's to the job by
  enum sh_ev; shtetl roll; every roll stopped on the way out; the
  builder and the roll's environment (the error sink, the log, schg).
  main.c: two syslog streams, children's lines at info, the rest at
  err. etc/rc.d/shtetl: /usr/local's PATH, git's.
- src.c: sh_src_commit (a repository's FETCH_HEAD, its first object id)
  and sh_src_record (what the roll fetched, as comments).

## Decisions, beyond or against the issue

- The head is made and mounted before the src steps, not at step 4:
  any failure or stop then finds it to put back, and the kill tests
  have a written head to stop over. Nothing else changes: a failure
  rolls back either way. Plan 6 says so.
- One kevent at a time in the daemon. A child's exit and its pipe's
  last read can come back in one batch; the exit ends the job, the roll
  frees it, and the read would name freed memory. Taken one at a time,
  the case is gone without a check.
- A first roll that fails leaves the foundation in the list, idle with
  no version and no status line, as the next start would not find it
  at all; the failure is in syslog. Nothing records it in ZFS: there is
  no dataset left to hold a stall.
- The recipe is resolved inside the job, after the clones, so a recipe
  in a repository not yet cloned is found by its first roll. shtetl
  roll answers at once and refuses only what it can see without
  reading zconfig: a bad name, a space missing or not mounted, a roll
  under way.
- The commits are read from FETCH_HEAD, which git writes at a fetch's
  end in a fixed form, not from git's printed words; REVISION from
  newvers.sh's REVISION="N.M" line, as main read it.
- foundation.conf's record is comments: houses parse the file as their
  first layer, and the record is for people; the machine's copy of the
  src commit is shtetl:commit.
- Datasets are made with the site's properties (properties.lake over
  .default) on F, the recipe's on each of its datasets; a parent the
  recipe does not give is a placeholder, canmount off, and never
  mounted. The recipe's datasets are sorted by path, so a parent the
  recipe does give comes before its children and gets its own
  properties. An existing dataset's properties are not set again.
- A version is named to the second, as ruled; two rolls of one
  foundation in one second would collide ("taken": the roll fails and
  stalls). A real roll takes a fetch and, from m3-install, a build;
  the tests wait for the second to turn.

## Verified

- make check: check_roll, F1 - F15 over the fake ZFS with git real on
  tests/src-fixture.sh's repositories (now with newvers.sh, a stable/14
  without it, lake properties, and recipes r1 - r5); F16 in
  check_daemon; Z10 in check_zfs. All 14 test programs pass.
- Twelve mutations of roll.c, each caught (MATRIX.md, family F); a
  fake whose inherit does nothing fails Z10.
- leaks: none of ours, restarts included (libucl's known 96 bytes).
- gate, xcheck.
- Owed to the box: Z10 over libzfs (box run's zcheck), check_roll and
  check_job under make check-freebsd, F17 (rc's PATH, a roll's git
  found under service) and F18 (schg, a roll over libzfs) in m3-box.

## Gotchas

- The fake now makes a mountpoint's directories when it mounts, as zfs
  mount does: a roll writes foundation.conf into the head.
- check_roll moves the fixture's releng/15.1 to B first (as check_src
  does in GS2): c5.patch conflicts only with B.
- A test's conditions on what the fake holds must be NULL-safe:
  strcmp on a property the mutation left unset crashes rather than
  fails, and a mutation run that greps for "check failed" sees
  nothing.
- m3-install adds its steps in roll_version, before the inject; its
  make children are the first long ones, and the kill tests gain a
  head with a world in it.

## Commits

6ca6b61 (zfs), then the commit after it, "roll: a foundation's roll,
its stall, and shtetl roll".

## Box run 4 and build run 1

Box run 4: KIDON, 2026-10-20, run.sh at 0d4768e (results
b55fa79): 113 ok, 1 failed. check_roll ok on FreeBSD; Z10 over libzfs
ok.
Build run 1: KIDON, 2026-10-20, tests/box/build.sh at b55fa79 (results
39daa4e): 25 ok, 1 failed. F17: the daemon rc started (make install, service
onestart) found git and rolled (R1). F18: foundation.conf schg, 0444,
on ZFS (R4). An update (R5), a stall and the roll asked again (R6, R7),
the stop and the start (R9). Tested.
