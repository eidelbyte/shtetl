# m1-tree: the C tree

## What was built

- Makefile: plain POSIX make. The portable flavor by default; make
  freebsd the same objects in the FreeBSD flavor, which m1-zfs gives
  its own layer and libraries; build/.flavor says which build/ holds,
  and asking for the other empties it first (main's scheme). make
  check builds each tests/check_*.c against the core and runs it;
  make gate runs tools/gate.sh.
- tools/gate.sh: ASCII over every file of ours (CLAUDE.md, sprints and
  the probes included, tools/cstyle.pl and probes/results left out),
  then cstyle.pl -cpP over src and tests. tools/cstyle.pl copied
  verbatim from main, which had it from zfs_rebase_tool (OpenZFS's,
  CDDL header intact).
- src/core/err.[ch]: main's error sink, the FILE and the prefix only.
  main's message count and its key paths are the language's and come
  back with m2-language; main's sh_err_nomem is gone, since the
  allocators never return a failure.
- src/core/mem.[ch]: sh_calloc, sh_realloc, sh_strdup. A refusal is
  logged to syslog ("out of memory") and the process exits 1 (plan 10,
  ruled). sh_realloc's size is never 0, said in the header, since a
  buffer only grows and realloc's answer to 0 is the implementation's.
- src/main.c: usage, exit 2; the verbs come with m1-daemon.
- tests/check.h carried from main unchanged (CHECK, the memory sink,
  the scratch directory); tests/check_mem.c; tests/MATRIX.md opens
  with how families and cells are kept, and family M.

## Decisions

- No -DNDEBUG and no SH_FREEBSD macro yet: nothing asserts, and
  nothing is FreeBSD's alone until m1-zfs. Each comes with the first
  line that needs it.
- The allocators exit with 1. A test reads the log line through
  openlog's LOG_PERROR in a forked child. On the mac LOG_PERROR prints
  "name[pid] <Error>: message", so the test looks for the message
  alone.

## Verified

make, make check (check_mem: M1-M5), make gate, the flavor switched
both ways, on the mac. M6 deferred (MATRIX.md). make freebsd on the
box is owed, with M1's box run.

## Commit

The commit after bbf60bd, "skeleton: the C tree, its gate and make
check".
