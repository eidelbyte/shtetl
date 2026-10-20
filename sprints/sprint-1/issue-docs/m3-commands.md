# m3-commands: the packages and the commands

## What was built

- src/core/commands.[ch], carried from main as knowledge (its
  fb_external, fb_inside and fb_chroot), as steps of the roll, added
  by the obj step after the install's:
  - each piece of each commands-external label: env SHTETL_ROOT=HEAD
    SHTETL_NAME=F SHTETL_KIND=foundation sh -e -c PIECE, on the host,
    the step "external: LABEL";
  - where the recipe gives pkg or commands: mount -t devfs devfs
    HEAD/dev; with pkg, the host's resolv.conf lent where the head has
    none, then env ASSUME_ALWAYS_YES=yes pkg -c HEAD install -y --
    NAMES; each piece of each commands label by chroot HEAD /bin/sh -e
    -c PIECE, the step "commands: LABEL"; umount HEAD/dev; the
    resolv.conf lent taken back.
- foundation.c: sh_foundation_idle unmounts whatever is at a mounted
  dataset's dev (unmount(2), MNT_FORCE, its refusal unsaid) before the
  dataset: a failure, a stop or a crash in a chroot step leaves devfs
  there, and the head would not unmount.
- The roll's environment gains re_resolv, the host's resolv.conf: the
  daemon's is /etc/resolv.conf, a test's a file of its own.
- tests/bin: pkg, chroot, mount, umount, faked; every fake now logs
  "NAME words" to FAKE_LOG, and the recipes' commands-external write
  there too, so one log holds the order.

## Decisions, beyond or against the issue

- The order is docs/configuration.md's, as main's was: commands-external
  first ("for a foundation after its installs and before its
  packages"), then pkg, then commands. Plan 6's step 5 lists pkg first;
  the plan's list is not an order, and the docs are the contract.
- Every roll runs every command again: installworld and distribution
  write the head's base again on each roll, so what a command changed
  there would be lost. "Runs once" in the docs is a house's rule.
- devfs is mounted and unmounted by children (mount, umount), so the
  mac's tests can fake them; the undo after a failure is a system call
  in sh_foundation_idle, the one place every failure and every start
  passes through, so no failure path needs steps of its own.
- pkg -c, as main used: the host's pkg in a chroot of the head, which
  needs devfs and, to fetch, a resolv.conf.

## Verified

- make check: all 15 test programs; X1 - X7 in check_roll (X5 by K10's
  exact log of a recipe with neither pkg nor commands).
- Ten mutations of commands.c, each caught (MATRIX.md, family X); the
  last needed a label with two pieces, which the fixture's r7 now has
  through an import.
- leaks: libucl's known 96 bytes only. gate (69 files), xcheck.
- Owed to m3-box: X8 (devfs really mounted, a roll stopped in a command
  and started again, the head unmounting) and X9 (real pkg and chroot).

## Gotchas

- A fake that runs nothing must not run anything on the host: the fake
  chroot logs and exits. commands-external in a test runs for real, on
  the host, so the fixture's write only into the head or the log.
- The host's resolv.conf may be missing (a mac off the network); the
  tests lend their own through re_resolv.

## Commit

The commit after 146d446, "commands: the packages and the commands".

## Box run 4 and build run 1

Build run 1: KIDON, 2026-10-20, tests/box/build.sh at b55fa79 (results
39daa4e): 25 ok, 1 failed. X9: the package (tree) installed
into usr/local by pkg -c, the command's motd written in the chroot, no
resolv.conf left (R3). X8: the daemon stopped in a command left devfs
on the head; started again, devfs gone and the head unmounted (R9).
Tested.
