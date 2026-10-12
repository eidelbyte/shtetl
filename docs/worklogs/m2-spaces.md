# M2: discovery and bootstrap (worklog)

2026-10-11, branch impl/m2, from impl/m1. planning.md 12's M2:
discovery and multi-rooting, bootstrap, the property table. Built and
tested on the mac; the box run (tests/box/run.sh) is still to come.

## What was built

| commit | unit |
|--------|------|
| 58ffabc | running a program: src/core/run.c, the one way shtetl runs anything |
| 2a7e1f4 | the ZFS interface (src/core/zfs.h), its stub and its fake, and discovery (4.1) |
| fd3f6a4 | the libzfs layer, src/fbsd/zfs.c, and tools/xcheck-freebsd.sh |
| cccb564 | shtetl spaces; the configuration found without -C |
| c71fc30 | shtetl bootstrap and its property table (4.2, 4.3) |
| 848b8bc | tests/box/run.sh: the commands against pools on memory disks |

About 6,500 lines of C in src/ now. The plan takes in what M2 settled
(4.1, 4.2, 4.3, 10, 12), the user's ruling on the run layout's mounts
and the UCL decision (14), and M2's proposals, Q-Y to Q-AB (15).

- **Running a program** comes first because bootstrap clones the
  recipes with git. An argument list, never a shell; a fixed PATH; the
  output logged, echoed or captured; the child in a process group of
  its own, so one kill reaches make and its jobs; SIGINT, SIGTERM,
  SIGQUIT, SIGHUP passed on as SIGTERM, a second as SIGKILL; stdin
  /dev/null. After zfs_rebase's launch.c, with the group added.
- **ZFS behind one interface**, three implementations picked at the
  link: libzfs on FreeBSD, a stub that refuses in the portable build,
  and a fake for the tests, with ZFS's inheritance (canmount does not
  inherit, a mountpoint inherits as a path), an altroot, snapshots,
  and a way to fail the Nth call of a kind.
- **Discovery**: every imported pool's filesystems, a local
  shtetl:space read the way zfs get reads a user property's source.
  Roots sorted; lookup of a name under a space's roots, one name, one
  place; the default lake or run root; the config root's directory.
- **Bootstrap**: plan, show, confirm, apply, the marks last.

## Decisions beyond the plan

The user ruled the run layout's mounts mid-way (2026-10-11):
containers at /containers and mounted, foundations never mounted
itself. Everything else below is a PROPOSAL in the plan, gathered in
its section 15:

- Q-Y: a root inside another root is refused.
- Q-Z: bootstrap uses datasets the user has and refuses a lake or run
  root with children not shtetl's.
- Q-AA: with several run roots, the first owns /containers; a later
  one's containers has canmount=off.
- Q-AB: the recipes are cloned only with --recipes.

Smaller ones, in the commit messages: the pool's ashift is read from
its top-level vdevs, not the pool property of that name; a pool's
altroot is allowed for when reading the layout already there; a pool
whose root will not open refuses discovery rather than being skipped;
the portable shtetl without -C says it has no ZFS (exit 1) where it
used to give the usage.

## Verification

- mac, make check: check_parse 169, check_schema 332, check_site
  180, check_cascade 168, check_show 46, check_run 107, check_spaces
  89, check_boot 277, and 21 command-line cases. tests/MATRIX.md has
  147 cells, none planned.
- check_run twenty times in a row: no flake. check_run and
  check_boot clean under Guard Malloc; check_spaces under leaks
  loses only libucl's object of the self-check (M1's worklog).
- tools/xcheck-freebsd.sh: every source, src/fbsd/zfs.c included,
  compiles for FreeBSD 15 against the OpenZFS headers of a FreeBSD
  tree (2.4.99 here; the box runs 2.4.2, and every call used is in
  2.2 and later).
- make gate: clean.

Not yet verified:

- The libzfs layer has never been built or run: that is the box's
  make freebsd and tests/box/run.sh.
- AddressSanitizer: still not run (M1's worklog says why on the mac).

## Gotchas

- macOS leaks --atExit interposes _exit: a forked child that exits
  through _exit (check_run's R3) stops itself for an inspection that
  never comes, and the parent waits for it. It would interpose git's
  exit too. Use Guard Malloc for the tests that fork.
- fmemopen takes no empty buffer; an empty answer is /dev/null.
- The box's /usr/src is 16-CURRENT while the box runs 15.1: libzfs is
  built against newer headers than the library, as zfs_rebase is, and
  works for the calls used. Worth keeping in mind if a call misbehaves
  there.
- tests/box refuses to run while any pool has a shtetl root: once the
  box is bootstrapped for real, the tests need a machine, or a jail,
  without one.
