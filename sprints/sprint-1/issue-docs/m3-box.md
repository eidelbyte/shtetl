# m3-box: M3 on the box

## What was built

- tests/box/build.sh, family R of tests/MATRIX.md: make install, then
  the daemon started by service(8), as rc starts it, on the pool s2box,
  a sparse file kept in IMGDIR and exported between runs; zbuild's
  checkout and the obj cache are kept, so only the first build takes
  hours. Each run destroys the foundation box and writes zconfig anew,
  then: a create roll from the FreeBSD src remote (R1 - R4), an update
  (R5), two patches of one line made from zbuild's own checkout, the
  second a three-way conflict (R6, a stall), the patch taken out and
  the roll asked again (R7), a pkg roll (R8), and the daemon stopped in
  a command, devfs on the head, and started again (R9). Each roll is
  asked with the command and waited out on shtetl status; what came of
  it is read from ZFS and the head. The log carries the run's lines of
  /var/log/daemon.log.
- The cells the families defer to the box (GS10, F17, F18, K19, X8,
  X9, U7) now name build.sh's.
- tests/box/README.md says how to run it.

## Found on the way

- a06997d: a failed roll showed stalled before zbuild's reset under it
  ended, and a roll asked then began beside it and was later let go of
  by the first. Found writing R6, which would have read zbuild while
  the reset ran; fixed in roll.c, with F19.

## Decisions

- The foundation's recipe is the script's, with buildopts that shorten
  the build (WITHOUT_TESTS, WITHOUT_LIB32, WITHOUT_DEBUG_FILES), a
  usr/local dataset (a placeholder usr on the way), one package, tree,
  and one command; GENERIC as the kernel.
- R9 stops the daemon in a command that sleeps, so the stop finds devfs
  mounted and the head written by the install: one cell for X8, and
  for K16 and F10 on real ZFS.
- The conflict's patches touch README.md, which nothing builds, so the
  patch left in for R7 changes no build.

## Verified

- sh -n, ASCII, the gate. The script runs only on the box: the run is
  owed, and with it the next run.sh, whose make check-freebsd runs
  check_job, check_src, check_roll, check_install and check_shake on
  FreeBSD, and whose zcheck runs Z10 over libzfs.

## Commit

The commit after a06997d, "box: M3 on the box, build.sh".
