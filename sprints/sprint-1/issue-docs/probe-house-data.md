# probe-house-data

## What was built

probes/P5-house-data.sh, cells P5.1 - P5.8: a house's www through two
version changes on the run (clone from the final save, promote,
destroy the old version), with the lake converging once before the
run's promote and once after it.

## Decisions

- The edge case: a new dataset reaches the lake as a clone stream
  before the run's promote, but as a plain incremental into a dataset
  that is not there after it, so the beam would need two commands.
  The rule that removes it, now in plan 8.1: send from the run's
  snapshot carrying the guid the lake already has, and receive a new
  dataset with `-o origin=` the lake's snapshot of that guid. Both
  orders use it, and the probe holds them to it.
- The lake prunes the way the run does, promote then destroy (P5.5,
  P5.8).

## Verified

sh -n, dash -n. Box run owed.

## Commits

3209a81 (probe), 554b00c (plan 8.1)

## Box run 1

KIDON, 2026-10-19, at 68bc7f8 (results 1c23d85).
P5.1 - P5.8, 10 of 10 ok: the guid rule with -o origin= works before
the run's promote and after it. Tested.
