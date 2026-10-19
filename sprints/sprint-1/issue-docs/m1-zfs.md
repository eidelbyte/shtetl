# m1-zfs: the ZFS layer

## What was built

- src/core/zfs.h: twelve calls, each refusal given in words: open,
  close, local (the filesystems where a user property is set
  locally), user (a user property, set there or inherited; 1 when
  nothing sets it), children, create, snapshot (several in one
  transaction, properties from birth), snapshots (oldest first),
  rollback (to the newest), destroy, mount, unmount, mounted (where).
- src/fbsd/zfs.c over libzfs and libzfs_core, carried from main and
  cut to these calls.
- tests/zfake.c, the fake, carried from main and cut the same way. Its
  data is words: zfake_put writes one into a mounted filesystem,
  zfake_has looks for it, a snapshot keeps them, a rollback puts them
  back.
- tests/zcells.c, the cells of family Z, written once and run twice:
  tests/check_zfs.c over the fake in make check, tests/box/zcheck.c
  (build/box_zcheck) over libzfs on the box. Each driver gives the
  cells two hooks, to put a word and to look for it: the fake's words,
  or files at the mounted path.
- tests/box/run.sh, the box runner, on the pools the probes make
  (probes/lib.sh's pool_make, pools_down, send); its log goes to
  tests/box/results/<host>-<time>.log and is sent back as the probes'
  are. send() now takes SENDER, so the commit says "box:" or
  "probes:".
- tools/xcheck-freebsd.sh, carried: the FreeBSD sources compiled
  against a FreeBSD tree's headers on the mac, a syntax check; make
  xcheck.

## Decisions

- Binary facts, not strings. main's sh_zfs_get gave every property as
  zfs get prints it, "-" for none. Here a user property is read by
  sh_zfs_user, with "nothing sets it" a return value, and a mount by
  sh_zfs_mounted. No native property is read in M1. A later issue
  adds a typed call for each fact it needs (guid and createtxg in
  m4-zfs).
- The words come from what libzfs answered, never from looking first
  (CLAUDE.md: a precheck is a second copy of a rule). create: libzfs's
  EZFS_NOENT from check_parents is "no parent" and EZFS_EXISTS is
  "exists". main looked for the name and the parent before asking.
  unmount: EZFS_BUSY is "busy". Two checks before asking remain,
  because nothing would refuse them: snapshot's malformed name, and a
  name given twice, which one nvlist key would silently take once.
- unmount does nothing for a filesystem that is not mounted, as
  zfs_unmount does (lib/libzfs/libzfs_mount.c checks the mount table
  first). So no caller asks whether something is mounted before
  unmounting it.
- nvlists are built with libnvpair's fnvlist calls, which end the
  process on an allocation failure, so no allocation is checked (plan
  10).
- No stub. main linked the portable shtetl against a stub that
  refused every call. Here the portable flavor compiles the program's
  objects and links only the tests; make freebsd links shtetl and
  box_zcheck.
- One set of cells, not two. main had tests/check_zfs.c over the fake
  and tests/box/zcheck.c over libzfs as separate cells. Written once,
  the fake and the box answer the same questions by construction.

## Verified

- make check: check_zfs, 46 cells over the fake, all ok.
- Four mutations of the fake, each caught by a cell:
  - a rollback that restores nothing: Z7;
  - destroy without its snapshot check: Z9;
  - user properties not inherited: Z2 and Z5;
  - unmount never busy: Z8 and Z9.
- make xcheck against ~/freebsd-development/freebsd: every source
  passes, src/fbsd/zfs.c and tests/box/zcheck.c included.
- make gate clean.
- Owed: the box run, `make freebsd && sudo sh tests/box/run.sh`.

## Gotchas for the next issues

- ZFS walks children in no fixed order (ZAP order); the cells sort
  what they saw. Nothing may rely on the order of children.
- A pool imported with an altroot mounts below it, and sh_zfs_mounted
  gives the real path, the altroot included.
- shtetl:space is a user property, so the datasets below a space's
  root inherit it. Discovery must use sh_zfs_local (m1-spaces), as
  must anything that finds objects by a property that inherits
  (shtetl:house on a version's root).

## Commit

The commit after 371973f, "zfs: the layer, its fake, and one set of
cells for both".

## After the commit

Writing m1-spaces found the fake answering a native property's name
through sh_zfs_user with its value, where libzfs's user properties
hold none: a test would pass over the fake and fail on the box. The
fake now answers 1 as the real layer does, Z2 has the cell, and
zfake_local gives tests what the fake holds on a filesystem. The fix
is its own commit, since 0486a19 was pushed.

## Box run 1

KIDON, 2026-10-20, at 48050c7 (results 7b6513e): family Z 46 of 47.
Z6 "the walk stopped, FN's value returned" failed: libzfs's
zfs_iter_snapshots_sorted_v2 calls the callback for every snapshot
whatever it returns (it ORs the returns), so the second call wrote
FN's 0 over its 7, the walk saw both snapshots, and the layer reported
libzfs's last, stale, description. main's zz_snap had the guard; it was
lost in the trim. Back, with the reason in its comment, in its own
commit (0486a19 is pushed). The fake stops at once, which is what the
layer now does too. A rerun of Z6 is owed.
