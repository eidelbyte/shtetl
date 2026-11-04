# m4-box: the beam and the prune on the box

## What was built

tests/box/build.sh, among the M3 rolls, cells B28 and P24 (tests/
MATRIX.md families B and P), with a house stand-in standing for what
M5's houses will do:

- After the create roll: no house, so no copy of box on the run
  (P24). box pulled to the run by hand at its first version, one
  dataset at a time, zfs send into receive -u -o canmount=noauto, as
  a house's pull will make it; the stand-in's S1 cloned on the copy at
  once (shtetl:house standin, its snapshot c1 kind create), before a
  convergence could prune a copy nothing is a clone of.
- After the update roll (R5): the beam has brought the update onto
  the pulled copy (same guid), and S1 to the lake as a clone of box's
  first version there, arrived (its kind on the copy's snapshot) and
  canmount=noauto set locally (B28, B32 on the box). S1 the only
  version: everything kept (P24). S2 cloned on the update, 8 MiB
  written in it, its stream to the lake cut by hand as probe P1 cuts
  one: a mebibyte into receive -s, a receive to resume left (B28).
- After the conflict roll (R6, no version, no convergence) and the
  roll asked again (R7): the beam resumed S2 first, S2's copy whole,
  "# resume" in beam.log (B28). The run keeps 1: S1 pruned on the run,
  its copy having arrived; box's first version gone from the run, the
  oldest a house there is on being S2's; the lake keeps S1 and S2, and
  box's first version with them (P24). S3 cloned on R7's version.
- After the pkg roll (R8): the run holds S3 alone, the lake S2 and S3;
  box's first version gone from both spaces; its update kept on the
  lake for S2's copy, gone from the run; S3's version and the pkg on
  both; the six prunings in beam.log, and no others (P24).
- pool_up destroys what the last run made, the stand-ins and their
  copies first (clones of box's versions), then box's copy, then box;
  shtetl.conf has run.keep 1 and lake.keep 2. beamed() waits out a
  convergence: a roll tells the beam as it ends, in the same event, so
  after ask the beam has begun. The run's lines of beam.log close the
  log.

## Decisions

- In build.sh, not a script of its own: its daemon, its rolls and its
  pool are what the beam and the prune act on, and each roll's end is
  the convergence a cell wants. The cells keep their families' names,
  B28 and P24.
- The stand-in is the least a house version is to the beam and the
  prune: a clone of a foundation version with shtetl:house and a
  snapshot of kind create or update. Nothing mounts it but the one
  fill.
- The cut is made by hand before the convergence that resumes it,
  between R5 and R7: R6 stalls and makes no version, so no
  convergence comes between.

## Verification

- sh -n; the new helpers run on the mac against a stub zfs, their
  commands as meant; make check and gate unchanged.
- The box: build run 7, below.

## Gotchas

- The import at the start of the first run after this one meets the
  stand-ins and their copies of the last run; pool_up destroys them
  before box, since they are clones of its versions.
- A run stopped between breakoff and R7 leaves S2's copy with a
  receive to resume; the next run's pool_up destroys it whole.

## Build run 7

Build run 7: 2026-11-04, build.sh at 3230f0f (results 025ed10): 48
ok, 1 failed. The failure was the harness's: P24 "S3's version
and the pkg on both" got [yes yes yes <tab>    yes], the four facts it
checks all yes, a backslash at a line's end inside the cell's double
quotes having joined the next line's indentation into the value
(4617a22 builds it on one line). Every other cell ok:

- B28: box's update beamed onto the copy pulled by hand (one guid);
  S1 on the lake a clone of box's first version there, arrived,
  canmount=noauto set locally; S2's stream cut by hand ("cannot send
  ...: signal received" from the cut send), a receive to resume left,
  and resumed first by the next convergence, "# resume" in beam.log.
- P24: no copy of box on the run before the pull; S1 kept while the
  only version; after R7, S1 pruned on the run, box's first version
  gone from the run and kept on the lake, S1 and S2 on the lake; after
  R8, S3 alone on the run, S2 and S3 on the lake, box's first version
  gone from both, its update kept on the lake for S2 and gone from the
  run, S3's version and the pkg on both; beam.log's six prunings and
  no others.
- R1 - R10 ok, R10 the stall alone. The import was clean: no copy of
  box was left on the run, the beam having made none.

Tested. M4 is done.

## Commits

3230f0f, and 4617a22 (the cell's quoting).

## Build run 8

Build run 8: 2026-11-05, build.sh at eb67efa (results d6e15af): 49 ok,
0 failed. The cell 4617a22 fixed, P24 "S3's version and the pkg on
both", ok; every B28 and P24 cell and R10 ok, beam.log's six
prunings, the import clean.
