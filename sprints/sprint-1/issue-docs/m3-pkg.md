# m3-pkg: a foundation's package upgrade

## What was built

- shtetl pkg FOUNDATION (daemon.c): refused for no such foundation;
  else sh_roll_ask with PKG, which refuses a foundation with no version
  ("F: no version") and one rolling, and clears a stall, as shtetl roll
  does.
- A pkg roll (roll.c), its own first step instead of site:
  - head: the newest version's name split (VER-DATE-KIND, the date
    twenty characters) for VER, its shtetl:commit read; F mounted, its
    foundation.conf read from the head as it is (shake.c's reader, now
    with SNAP NULL for the live tree), and the datasets that recipe
    gives mounted (roll_mount, shared with the build's head);
  - the chroot's steps in upgrade mode (commands.c, cm_upgrade): devfs,
    the resolv.conf lent, env ASSUME_ALWAYS_YES=yes pkg -c HEAD upgrade
    -y, devfs off, the resolv.conf taken back; no commands-external, no
    commands;
  - snapshot: F@<ver>-<date>-pkg on every dataset, kind pkg, the
    version's commit.
  A failure rolls back and stalls as any roll does, and leaves zbuild
  alone: the pkg roll never touched it.
- The snapshot's kind is the roll's r_kind: create, update or pkg.

## Decisions, beyond or against the issue

- foundation.conf is not injected again. The issue said it is; but a
  pkg roll starts from the newest version, whose head holds the file,
  schg, as that version's build wrote it, and what the file records
  (the recipe, the src and patch commits) is still what the version is
  built from. Writing it again would change nothing, or, written from
  the recipe as it is now, record a recipe the version was never built
  from (plan 5: a recipe's change takes effect at the next build).
- The pkg roll mounts the datasets of the version's recipe, read from
  its foundation.conf, not the recipe's as it is now: a dataset given
  since is the next build's to make (U4).
- A pkg roll takes the builder's turn like any roll: one roll at a
  time on the host, nothing to tell apart.
- The version's VER is read from its snapshot's name, shtetl's own
  form; houses will read their foundation version's name the same way
  (plan 4: a house version is named for it, its kind left off).

## Verified

- make check, all 15 test programs: U1 - U5 in check_roll, U6 in
  check_daemon, I15 in check_shake (the live read).
- Seven mutations, each caught (MATRIX.md, family U); one, the version
  refusal removed, by the crash of a pkg roll of a foundation with
  none. The mutation harness now counts a crash as caught.
- leaks: libucl's known 96 bytes only. gate, xcheck.
- Owed: U7, a real upgrade, in m3-box.

## Commit

The commit after 1fa0f4b, "pkg: a foundation's package upgrade".

## Box run 4 and build run 1

Build run 1: KIDON, 2026-10-20, tests/box/build.sh at b55fa79 (results
39daa4e): 25 ok, 1 failed. U7: a pkg roll, kind pkg, the version's
commit (R8). Ruled the same day that the packages a pkg roll upgrades
are those its version's foundation.conf lists (plan 18): a53af20
upgrades that list, not every package. Testing: R8 again owed.

## Box run 5 and build run 2

Build run 2: KIDON, 2026-10-20, build.sh at f57133c (results
86c8ac0): 27 ok, 0 failed. R8, a pkg roll of the version's own
packages (a53af20), kind pkg, the version's commit. Tested.
