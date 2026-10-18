# probe-roll-live

## What was built

probes/P6-roll-live.sh, cells P6.1 - P6.9: a live version's saves
replayed with zfs_rebase onto a new foundation version while the live
version is written in the background, a conflict over /etc/motd
answered from, a fourth save caught up the same way, and the result
held equal to one squashed rebase of the fourth save (mtree specs:
type, mode, owner, size, link, sha256, flags; times as an answer).

## Decisions

- The replay's calls are main's (src/core/update.c uq_replay): step 1
  clone form `--from h1@s1 --onto f@2 --result h2`, then placed
  (readonly off, a mountpoint, mounted); step k dataset form `-u -b
  h1@s(k-1) --from h1@sk --onto h2 --result s(k-1)`; -m throughout. A
  dataset form rebase is named by its pre-apply snapshot for
  --continue (zfs_rebase.8, EXAMPLES).
- The edge case: the drafted roll snapshotted the last replayed save
  and the update snapshot, so the catch-up's pre-apply snapshot would
  need a name already taken. Plan 7.2 and 7.3 now make the roll one
  replay of whatever is missing, run while live and once more at the
  switch, with the newest replayed save never snapshotted by hand.
  Packages, house.conf and the update snapshot move to the switch.
  The probe replays that way: h2 must hold s1 s2 s3 and nothing twice.
- Stricter than the tracker's description: every step reads only
  snapshots, so "rolled while live equals rolled while stopped" is
  near true by construction. The background writer tests that a busy
  live dataset changes nothing, and the comparison is with one rebase
  of the final save, which tests that stepwise history replay gives
  the same content as a squashed rebase.
- The resolution is an indented tree ("/", "    etc/", "        motd
  conflict 1 -"; zfs_rebase_tool tests/check_manifest.c), so a name is
  matched at its depth; answering edits " conflict N -" and the
  "#unanswered" header, as main's box script did.
- The two trees are compared as mtree spec texts with the header
  comments out, so a file only one side has counts as a difference
  whatever mtree's own exit status does with extras.

## Verified

sh -n, dash -n. Box run owed, with zfs_rebase -m (69d8223 or later)
installed.

## Commits

d414712 (probe), 554b00c (plan 7.1 - 7.3, 9)
