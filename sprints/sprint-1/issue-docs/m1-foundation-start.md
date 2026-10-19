# m1-foundation-start: the first actor

## What was built

src/core/foundation.[ch]: sh_foundations_start, one foundation for
each filesystem under <lake>/foundations, each started, sorted by name;
sh_foundation_status, its line of `shtetl status`. The daemon starts
them after the skeleton, when there is a lake, and status prints them
after the spaces:

    space lake tank/lake
    foundation generic idle 15.1-releng-2026-10-18T09_30_12Z-update
    foundation old stalled build: make exited 2

The start is one rule: the foundation's tree, children first, every
dataset unmounted, then rolled back to its newest snapshot, or
destroyed where it has none. A foundation whose head has no snapshot
is a first build that never finished. It is undone, logged ("foundation
F: a first build undone"), and is no actor. After that, shtetl:stall
(read as zfs get reads it, inherited too) makes it stalled with that
text; otherwise it is idle at its newest version.

## Decisions

- No written@. The plan (6) had the start ask what was written since
  the newest snapshot, after a sync, and roll back only then. A
  rollback to the newest snapshot changes nothing where nothing was
  written, so the question, the sync and the branch go, and plan 6
  says so now. The same rule covers a crash anywhere in a roll, the
  first build included, where the plan had no word for a head with no
  snapshot.
- Unmounted at the start too: an idle foundation is not mounted, and
  the roll mounts its head itself (M3).
- Children first, since a filesystem with another mounted inside it
  will not unmount, and one with children will not be destroyed (the
  fake says "busy" and "has children" as ZFS does).
- A step ZFS refuses stalls that foundation in memory, with the
  refusal's words as its reason, logged; the others start all the
  same. It sets no shtetl:stall: the next start tries again, since
  what ZFS shows is still the truth to start from. A stall a roll
  sets is a property (m3-roll).
- The walk of <lake>/foundations refused stops the daemon's start. It
  was made by the skeleton a moment before, so that is ZFS failing,
  not a state to carry on in.
- Status lines are for people: a kind, a name, a state, the newest
  version or the stall's text. Nothing parses them.

## Verified

- make check: check_foundation, A1 to A10, over the fake.
  check_daemon now starts a foundation too, and D1 shows its line.
- Four mutations of foundation.c, each caught:
  - the parent unmounted before its children: A3;
  - nothing unmounted: A2;
  - the stall never read: A1;
  - a head with no version kept: A3.
- No leaks (leaks --atExit on check_foundation); xcheck; gate.
- Owed on the box: tests/box/run.sh's family A. A daemon is started
  over a lake pool holding a version with a file written after it, a
  first build with no version, and a stalled foundation. Then its
  status is read, the file is checked gone, the first build is
  checked gone and logged, and the daemon gets SIGTERM, which ends it
  with exit 0 and removes its socket.

## Gotchas

- The box's family A needs a box with no space marked and no daemon
  running; run.sh refuses otherwise, since the test daemon would find
  the box's spaces, or the running one would answer in its place.
- probes/lib.sh's m and die write to the probes' summary; tests/box
  uses x and its own "FAIL setup" instead.

## Commit

The commit after 287d27f, "foundation: the actor's start from what ZFS
shows".

## Box run 1

KIDON, 2026-10-20, at 48050c7 (results 7b6513e), tests/box/run.sh: 53
ok, 1 failed (Z6, m1-zfs's).
Family A on a real lake pool: A1 status as written, A2 the file
written after v1 gone and the head unmounted, A3 the first build
with no version destroyed and logged; D10 too. Tested.
