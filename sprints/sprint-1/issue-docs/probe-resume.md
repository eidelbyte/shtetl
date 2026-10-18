# probe-resume

## What was built

probes/P1-resume.sh, cells P1.1 - P1.10: a full stream, an incremental
and a clone stream between s2run and s2lake, each cut by `head -c`
and resumed with `zfs send -t <receive_resume_token>`, each held to
the run's guids; a fresh incremental refused while a token stands;
and a token whose source snapshot is gone, refused, then taken away
with `zfs receive -A`.

## Decisions

- The cut is `head -c 8 MiB` of a stream of 32 one-MiB random files,
  not a killed process, so it is the same on every run.
- No check for leftover %recv datasets: zfs list does not show them,
  so the check could not fail. A partial incremental shows its token
  on the dataset it was going into, which P1.8 counts.
- P1.9 and P1.10 are the case open question 11 asks about: a run
  snapshot pruned before the lake had it.

## Verified

sh -n, dash -n. Box run owed.

## Commit

57e8ed7

## Box run 1

KIDON, 2026-10-19, at 68bc7f8 (results 1c23d85).
P1.1 - P1.10, 14 of 14 ok. Tested.
