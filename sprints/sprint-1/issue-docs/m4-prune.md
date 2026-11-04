# m4-prune: versions pruned by count once the beam is consistent

## What was built

- site.c (882c4dc): run.keep must be less than lake.keep, checked on
  the merged settings, "shtetl.conf: run.keep: must be less than
  lake.keep" (ruled 2026-10-20). CS16. sh_site_keep reads a keep.
- prune.[ch]: sh_prune(z, lake, run, run.keep, lake.keep, log, e).
  Both spaces read whole, every filesystem with its origin and its
  snapshots with guid and createtxg, then four rules in an order that
  reaches the end in one prune:
  1. the run's house versions: the newest run.keep complete ones kept,
     the active counted; an older one goes once its lake copy has
     arrived at its newest snapshot;
  2. the lake's copies: the newest lake.keep arrived kept; an older one
     goes once the run holds the version no more;
  3. the run's foundation copies: every version from the oldest one a
     filesystem of the run is a clone of; with none, the copy whole;
  4. the lake's foundations: the newest, any version a filesystem of
     either space is a clone of (by guid), and any not older than the
     newest the run's copy holds.
  A house version goes by promoting every filesystem of its space that
  is a clone of one of its snapshots, then destroying it, children
  first, each one's snapshots first. A foundation's version goes as
  its snapshot on every dataset of the tree. Each is a line of
  beam.log, "# prune NAME".
- beam.h, beam.c: be_done, told at the end of each convergence. Its
  refusal is a failure of the convergence, "prune: why": in beam.log,
  once in syslog ("beam: prune: why"), a line of status while it
  fails (Q-P2, ruled).
- daemon.c: pruned(), the beam's be_done: shtetl.conf read from
  zconfig at each prune, so a keep changed takes at the next
  convergence, then sh_prune with beam.log.
- check_prune.c: P1 - P20 over the fake. check_beam: P21, P22.
  check_daemon: P21, the daemon's prune taking an older version of
  generic and saying so in beam.log.
- build.sh R8 reads the commit of the version before the pkg roll
  before the roll: the prune takes that version once the beam has
  carried the new one.

## Decisions beyond the plan

- One pruner. Plan 8.2 had three: the house actor after a roll, the
  beam its copies, the foundation actor its versions. As built the
  beam prunes all four kinds at the end of each convergence, when it
  has just read both spaces. Every roll tells the beam of its version,
  so what the new version frees goes at the convergence that carries
  it. "Pruning waits for beaming" then holds by construction, and the
  house actor (M5) needs no prune of its own. A roll only ever adds a
  newest version, which no prune takes, so a prune during a roll of
  the same foundation changes nothing the roll reads. The plan's 8.2
  says so now.
- The rules marked (+) in tests/MATRIX.md family P come from 7.2 and
  8.1, not 8.2's words: zrun keeps every foundation version from the
  oldest one a house there is on (a house rolls through each); the
  lake keeps a foundation version a house version in either space is
  a clone of, and every one not older than zrun's copy's newest (its
  next stream's base); a lake copy waits until zrun no longer holds
  the version.
- A foundation's version is a snapshot name across its tree, as a roll
  snapshots the tree at once; its age is the top's snapshot of that
  name's createtxg. A clone of a child dataset's snapshot (V/var of
  g/var@v1) is placed by the name, not by its own txg, which real ZFS
  shares with the top and the fake need not.
- A house version is a child of containers or systems with
  shtetl:house and an origin; complete (or its copy arrived) when a
  snapshot of it holds shtetl:kind create or update. A child with no
  origin or no shtetl:house is not counted and not pruned.
- Arrival, for the run's rule, is the lake copy holding the guid of
  the run version's newest snapshot with shtetl:kind on it, the beam's
  mark that a version's snapshot is whole.

## Verification

- make check, 17 programs, check_prune new; gate; xcheck-freebsd.
- Mutants, each caught by a test: arrival's kind not read (by the P2
  cell added for it: the copy holds the newest snapshot, its
  properties not yet made the owner's), the run's keep not read, a
  lake copy pruned while the run holds it, no promote, an unused run
  copy kept whole, the oldest origin's version destroyed, clones not
  looked for, no base, the lake's newest not kept, completeness not
  read, newest counted the other way. Three of the first tries were
  stopped by the compiler (a self-comparison, an unused parameter or
  function) and were rewritten to compile, so that a test catches
  them.
- Owed to the box: m4-box (P24), a foundation rolled twice with its
  first version pruned in both spaces and a house stand-in kept. The
  next build.sh shows the lake's create version of box pruned after
  its update roll; R8 reads around it.

## Gotchas for the next issues

- m5-create, m5-restore: the pull. A foundation copy on zrun with no
  clone of it is destroyed whole at the next convergence's end, so a
  pull must be followed by the house version's clone before that, or
  the pull marked in ZFS as relied on. A pull cut part way, with
  nothing cloned yet, is destroyed too. Design it with the prune.
- check_daemon's D23 unmounts zconfig; the daemon's prune then says
  "daemon: beam: prune: tank/config: not mounted" on the test's
  standard error, once. That is the daemon's syslog stand-in saying a
  real refusal, not a failing test.
- The prune reads both spaces whole, every snapshot's guid and txg,
  as the beam's look does: cheap for a host's hundreds of snapshots.
- sh_zfs_promote refuses a snapshot name both sides have; versions'
  snapshot names are unique in shtetl (dates, versions), and the
  tests name each version's snapshots apart (u1, s1, u2, ...).

## Required and forward (2026-11-03, Q-F1)

Ruled: no going back on a foundation's timeline; prune unused
foundations from everywhere and keep required + forward, with
foundations made on demand. The lake's rule is now the run's: every
version from the oldest one a filesystem is a clone of, by guid, the
clones counted in either space (required(), one function for both,
foundations(p, RUN) then foundations(p, LAKE)). The lake's per-version
checks, a clone of it and the base of the run copy's next stream, are
gone: the base is never older than the oldest version the run's
clones need, and a run copy with no clone goes whole in the same prune
before the lake's turn. With nothing required, the lake keeps the
newest until foundations are made on demand (sprints/future-work.md);
deleting it now would delete every foundation as soon as it is built.
P15 restated as forward: versions newer than the oldest required,
nothing a clone of them, kept. Mutants caught: the lake's unused
foundation destroyed whole (P21 in check_daemon), the lake blind to
the run's clones (P14), the oldest required destroyed (P1). make check,
gate, xcheck clean.

## Commits

882c4dc (site), f1a312d (matrix, questions), 5cce65b (the beam makes
no foundation copy), dd4ff53 (rulings), 8590066 (the prune), and
"prune: a foundation required and forward in both spaces".
