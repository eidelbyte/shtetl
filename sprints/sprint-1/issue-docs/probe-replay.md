# probe-replay

Was probe-roll-live (d414712): a roll while the house ran, caught up
at a switch. Ruled 2026-10-18 (plan 18, answers 9 and 14): there is no
switch, no roll while live and no catch-up; a house's roll is one
sequence from stop to start. The probe was rewritten for that.

## What was built

probes/P6-replay.sh, cells P6.1 - P6.9: a stopped house's four saves
replayed with zfs_rebase onto a new foundation version, a conflict
over /etc/motd answered from, the update snapshot taken last, the
replay timed, and the result held equal to one squashed rebase of the
final save (mtree specs: type, mode, owner, size, link, sha256, flags;
the times as an answer).

## Decisions

- The replay's calls are main's (src/core/update.c uq_replay): step 1
  clone form `--from h1@s1 --onto f@2 --result h2`, placed (readonly
  off, a mountpoint, mounted); step k dataset form `-u -b h1@s(k-1)
  --from h1@sk --onto h2 --result s(k-1)`; -m throughout. A dataset
  form rebase is named by its pre-apply snapshot for --continue
  (zfs_rebase.8, EXAMPLES).
- No snapshot is taken by hand between steps: each step's pre-apply
  snapshot is the replayed previous save, and the update snapshot,
  taken last, holds the last. So the new version holds s1 s2 s3 update
  and no name twice (P6.4).
- The comparison is with one rebase of the final save: every step
  reads only snapshots, so this is what tests that stepwise history
  replay gives the same content as a squashed rebase.
- P6.5 times the replay: the house is down for the whole roll (plan
  16).
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

d414712 (the roll while live), 83c3634 (rewritten)

## Box run 1

KIDON, 2026-10-19, at 68bc7f8 (results 1c23d85).
Did not run: KIDON's /usr/local/sbin/zfs_rebase answers `unknown
option "-m"`, so it predates 69d8223. GitHub's main is 69d8223, so a
pull, make freebsd and install of zfs_rebase_tool on the box is the
next step; then a rerun.

## Box run 2

KIDON, 2026-10-19, at 422bf9c (results 7e9be07).
Again not run: /usr/local/sbin/zfs_rebase still refuses -m. A build
that has it prints [--merge-non-conflict] in its usage:
`zfs_rebase 2>&1 | grep merge-non-conflict`.

## Box run 3

KIDON, 2026-10-20, at 7e9be07 (results 388144a), on zfs_rebase 69d8223.
P6.1 - P6.9, 16 of 16 ok. The clone form, then three dataset forms;
step 3 stops at its conflict over etc/motd, the resolution answered
from with the sed, --continue takes the pre-apply snapshot's name and
goes through; the new version holds s1 s2 s3 update in that order.
Answers: the four steps took under a second here (P6.5), so the
figure for a real house comes from the M5 box run; file times agree
with the one rebase, and only the times of the directories the replay
wrote in differ, each being its own run's moment (P6.8). For plan 7.2
step 3: the clone form leaves its result read-only, unmounted, with
mountpoint none, so the roll places it (readonly off, its own path,
mounted) before the first dataset form, as the probe's place() does.
Tested.
