# m3-install: the build and the install

## What was built

- src/core/install.[ch], carried from main as knowledge (its build.c),
  written again small:
  - sh_install_steps: the kernel config (an include of the config
    kernconf names, the foundation's name in capitals as ident, the
    lines) and src.conf (buildopts) written into the obj cache's
    shtetl directory; the scratch directory emptied; then the steps:
    build (make -jN buildworld buildkernel), on an update delete-old
    (delete-old delete-old-libs, BATCH_DELETE_OLD_FILES), installworld
    (INSTALL_CMD=install -C), installkernel into the obj cache's
    kernel-install, a call syncing it into the head, distribution, and
    on an update a call removing installworld's backups. Each make is
    env MAKEOBJDIRPREFIX=OBJ make -C SRC ... SRCCONF KERNCONFDIR
    KERNCONF.
  - sh_install_kernel: the sync, as install -C leaves files: one alike
    kept (its mode and owner set), another copied; in boot/kernel and
    usr/lib/debug/boot/kernel, what the new kernel has not removed,
    and such a directory it has none of.
  - sh_install_backups: boot's *.old (kernel.old among them),
    libexec's ld-elf*.so.1.old, sbin/init.bak.
  - sh_install_rmtree: flags cleared first, as installworld leaves
    schg on some files.
- roll.c: the version step now queues obj, which makes F's obj cache
  (<lake>/foundation-builds/F, the site's lake properties) where it is
  missing, mounts it, adds the install's steps and then the inject.
  The roll's environment gains re_obj; the daemon sets it.
- spaces.c: the lake's skeleton gains foundation-builds, canmount off.
- tests/bin/make: the fake make; tests/check_install.c, K1 - K8;
  check_roll, K9 - K18; check_spaces' S8 the new skeleton.

## Decisions, beyond or against the issue

- A step's calls queue what follows them: steps a call adds go to the
  end of the job, so version queues obj, and obj queues the install's
  steps and then inject. m3-commands adds its steps in obj, between
  the install's and the inject.
- Copied kernel files take the time of their copy, as install -C
  gives a file it replaces: main kept the times (install -p), which
  needed an __APPLE__ case for the tests' sake.
- The obj cache is mounted for the roll and left mounted: it is a
  cache, rolled back by nothing, and whether it is mounted matters to
  nothing. A failed roll does not touch it.
- The kernel config's include of a bare name is the checkout's
  sys/<machine>/conf/NAME, uname's machine, as main had it; with no
  kernconf at all, GENERIC (the docs say the shipped shtetl.conf gives
  it; the built-in settings do not, so the default is the code's).
- tests/bin/make backs a file up only where one was there, as install
  does: a first roll has no backups to remove.

## Verified

- make check: all 15 test programs; check_install K1 - K8 on files,
  check_roll K9 - K18 with the fake make, as well as F1 - F15 now
  running through the build.
- Sixteen mutations of install.c, each caught (MATRIX.md, family K).
- leaks: none in check_install; libucl's known 96 bytes in check_roll.
- gate (67 files), xcheck.
- Found by K18 on the way: joining "" and a name gave "/boot", so the
  sync's relative paths never matched a kernel directory and nothing
  was removed; the double slash in the real paths had hidden it.
- Owed: K19, a real world and kernel, in m3-box; check_install and
  check_roll on FreeBSD with the next box run (uchg, lchflags).

## Gotchas

- check_roll prepends tests/bin to PATH; its children find the fake
  make, the env(1) of the system runs it.
- The fake make is steered by FAKE_KERNEL, FAKE_FAIL and FAKE_SLEEP;
  a test that sets one unsets it after.

## Commit

The commit after d58acd9, "install: the build and the install".

## Box run 4 and build run 1

Box run 4: KIDON, 2026-10-20, run.sh at 0d4768e (results
b55fa79): 113 ok, 1 failed. check_install FAIL: K7 set uchg as the
user, and FreeBSD's ZFS has no uchg (as m2-shake found of the shaken
recipe). Fixed in 942c699: nodump, a user's flag; the clearing of
schg is R5's, as root.
Build run 1: KIDON, 2026-10-20, tests/box/build.sh at b55fa79 (results
39daa4e): 25 ok, 1 failed. K19: a world and a kernel in the head (R3), the
update over the obj cache the first build left (R5), installworld's
schg backups removed. Testing: check_install's next run.sh owed.

## Box run 5 and build run 2

Box run 5: KIDON, 2026-10-20, run.sh at 26d3903 (results
f57133c): 114 ok, 0 failed. check_install ok with nodump. Build run 2: KIDON, 2026-10-20, build.sh at f57133c (results
86c8ac0): 27 ok, 0 failed.
K19 again (R3, R5). Tested.
