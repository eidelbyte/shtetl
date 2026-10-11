# M1: the configuration (worklog)

2026-10-11, branch impl/m1. planning.md 12's M1: the skeleton, the
UCL loader, the schema, the cascade and `shtetl config show`, all on
the mac.

## What was built, and why so

| commit | unit |
|--------|------|
| cbba7cd | the skeleton: Makefile, the command table, the gate, tests/MATRIX.md with every M1 cell planned |
| 45ee825 | the format-free tree and the UCL parse (5.3) |
| c9950a9 | the recipe schema (5.5) and shtetl.conf (5.2) |
| f9be273 | the recipe cascade (5.4) |
| fd657cc | `config show`, and the command line end to end |

About 3,600 lines of C in src/, 2,000 of tests. Each commit's message
says what it settled; the plan now says it where it belongs (5.3, 5.4,
5.5, 7.1.1, 10, 11, 12) and gathers the decisions without a ruling in
section 15 (Q-S to Q-X).

- **The build.** A plain POSIX Makefile after zfs_rebase's, with the
  same flavor stamp. libucl: the freebsd flavor links the
  textproc/libucl port as the plan says; the portable flavor compiles
  libucl 0.9.4 from a FreeBSD tree's contrib/libucl into build/ucl,
  so the mac needs no package and both builds run libucl's own code.
  It is compiled gnu99 (mum.h's asm), with its own flags and no
  -Werror. Only parse.o gets libucl's include path.
- **The parse** is the one module that knows the format. It hands back
  a tree whose every node and key name the layer they came from, which
  the cascade's drop rule and config show both read.
- **Errors** go through a sink that names the file and the key path,
  and every check reports every problem rather than the first. Tests
  read the sink through open_memstream.
- **The schema** is one table of 5.5's keys, which the cascade reads
  too. shtetl.conf has a small table of its own.
- **The cascade** reads and checks every layer before merging any, so
  one run reports all of them.
- **config show** prints UCL that parses back to what it printed; the
  tests hold it to that by round trip.

## Decisions beyond the plan

Each is in a commit message and in the plan; the ones that want a
ruling are section 15's.

- Macros: tried, not trusted (5.3). `UCL_PARSER_DISABLE_MACRO` in
  0.9.4 makes a macro a parse error, not the comment its header
  promises, and turns variable expansion off. A start-up self-check
  tries the linked libucl and refuses to read with one that takes a
  macro, expands a variable or takes a duplicate. Without the flag,
  libucl read /etc/passwd into a test file through `.include`.
- No line numbers on schema errors: libucl keeps none on objects. The
  key path stands in; keys are unique in a file.
- ZFS property values: strings, or booleans for on and off; numbers
  refused ("quote it": `1M` is 1000000 to UCL). mountpoint, canmount
  and shtetl:* refused.
- The drop rule covers pkg and datasets; a drop of nothing is an
  error (Q-V); imports are followed from every layer (Q-U); scalars
  are replaced, `ver` and the heredocs too (Q-S, Q-T); recipe names
  take no `.` (Q-W); patch locations tell a host by a dot (Q-X).
- The built-in recipe layer exists and holds nothing yet. The
  built-in site settings hold 4.3's properties, 6.3's zeros, the
  staging suffix, and an empty `spaces`, so config show prints
  sections in shtetl.conf's order.
- Option errors are shtetl's own, not getopt's, whose messages name
  argv[0] on macOS and its last component on FreeBSD; the CLI
  expectations must read the same on both.

## Verification

- `make check` on the mac: check_parse (169 checks), check_schema
  (332), check_site (180), check_cascade (168), check_show (46), and
  tests/run-cli.sh (15 cases). tests/MATRIX.md family C: 95 cells,
  all covered.
- `make gate`: ASCII and cstyle clean on 25 C files.
- macOS `leaks`: every test, and `shtetl config show` on a good and a
  refused recipe, lose 96 bytes per refused duplicate key, all inside
  libucl's ucl_parser_add_chunk_full (the self-check refuses one per
  process). libucl's error path, not shtetl's; a refused file ends
  the run.
- Guard Malloc (libgmalloc) over check_parse and check_cascade:
  clean.
- AddressSanitizer: not run. On this macOS its runtime spins in its
  own start-up, before main (sampled: AsanInitInternal ->
  InitializeShadowMemory -> malloc -> AsanInitFromRtl). Worth a run on
  the box.

Deferred to the box:

- `make check` (portable, libucl from /usr/src/contrib/libucl) and
  `make check-freebsd` (the textproc/libucl port: `pkg install
  libucl`). The parse cells are meant to hold for whichever libucl is
  linked; the port's version is the one shtetl will ship against.
- Whether a native property name is one ZFS knows: libzfs's to say
  (M2).

## Gotchas for the next milestones

- On the mac, `export FREEBSD_SRC=~/freebsd-development/freebsd`
  before make; the Makefile says so if it cannot find libucl.
- The ASan hang above: use leaks and libgmalloc on the mac.
- `UPDATE=1 sh tests/run-cli.sh` rewrites the CLI expectations from
  whatever ./shtetl is built; rebuild first, then review with git
  diff.
- A layer's name (sl_file) is the pointer every node of its tree
  carries; sh_recipe_layer compares pointers, not text.
- main.c holds the one command; M2's commands want a file of their
  own beside it (src/cmd or the like).
