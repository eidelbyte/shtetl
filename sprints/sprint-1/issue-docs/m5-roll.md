# m5-roll: a container's roll

## What was built

`shtetl roll H` moves a container onto the next version of its
foundation that the run's copy holds, the oldest newer than its active
version's origin (plan 7.2, 7.3; tests/MATRIX.md family W).
sh_house_roll is one job of the house's, "house H rolling STEP" in
status:

1. jail: the jail stopped, `jls -j H` first, so a jail not running is
   no failure (N19).
2. save: the final save, the save's take(), kind save, on the active
   version.
3. replay: the copy mounted, since zfs_rebase reads every side through
   a mounted dataset; the saves, the active version's snapshots
   oldest first, roll.depth deep (0 every one, a count, an interval);
   the new version named as a create names one; then one set of steps
   for each dataset of the next version's tree, a parent first. A
   placeholder is cloned, canmount off. A dataset the active version
   has is replayed: the oldest save by zfs_rebase's clone form onto
   the foundation version, the result placed (canmount noauto,
   readonly and mountpoint inherited, mounted at its own path, the
   root given shtetl:house), each later save by the dataset form, -u,
   -b the save before it, the pre-apply snapshot named for that save,
   -m always. A dataset new in the foundation is cloned as a create
   clones one.
4. own: the container's own datasets, those of the active version
   that are clones of no snapshot of the copy, cloned from the final
   save, canmount noauto and mounted; their placeholders made again.
5. recipe: the recipe resolved on the next version's foundation.conf,
   read from the copy's snapshot, not from the tree the replay wrote;
   the commands' steps, a piece the old house.conf holds with the
   same text under the same key and label not run again (cm_had), the
   container's own packages installed.
6. house.conf, update (the update snapshot -r, kind update, taken
   last), swap (the old version unmounted and given back its own
   path, the new one's mountpoint set to /containers/H, which moves it
   there mounted), block and jail (the start), copy (the copy
   unmounted where no other house's roll is on it); the beam told.

A failure before the update snapshot: every open zfs_rebase run on
the new version aborted, found by zfs_rebase:tag set on a dataset
itself; the new version destroyed; the copy unmounted; the old
version's jail started again; the house stalled, shtetl:stall on its
active version, "STEP: why", said once to syslog, "house H stalled
STEP: why" in status. A stalled house is active: save and pkg as
ever; the stall is read again at a start; `shtetl roll H` clears it
and rolls. A failure after the update snapshot: said once; the house
failed if the swap was not done, the next start finishing it.

The start (sh_houses_start) now: unmounts every copy on the run
first, a roll a crash cut short having left one mounted; gives every
version of a house but its newest back its own path, unmounted (a swap
cut short); and destroys an incomplete version of a house that has a
complete one in the house's own start job, after the jail, every open
zfs_rebase run on it aborted first. One with no complete version
beside it, a create cut short, is destroyed at once, as before.

The daemon: `shtetl roll CONTAINER|FOUNDATION`, the house's roll where
the name is a house's, the foundation's where it is a foundation's,
zconfig mounted either way; the houses' env given the lake.

zfs_rebase is run as a program, behind src/core/rebase.h
(sh_rebase, a child step). The tests run tests/rbfake.c in its place,
a call forked as the program would be, doing to the fake ZFS what the
program does to ZFS: the clone form's result read-only with mountpoint
none and the record on it; the dataset form's pre-apply snapshot, the
dataset unmounted for the run; --abort undoing either; a run asked to
stop at conflicts, fail or hang does.

## Decisions beyond the plan and the issue

- The order of the last steps is the swap, the start, then the copy
  unmounted, not the copy first as the matrix had it: a refusal to
  unmount the copy after the swap leaves the house running on its new
  version, where before the swap it would have left it down. The
  matrix's W1 now says so.
- The step that plans the replay is called replay, so a tree that
  differs reads "replay: D: not in F@V", as W4 asked.
- A dataset the foundation dropped stays on the run's copy while a
  house's version is a clone of it, so the next version's tree is the
  copy's datasets holding a snapshot at that version, not the copy's
  tree.
- A version that is a clone of no foundation version (only a hand
  makes one) is refused, "H: no foundation version", where before the
  roll would have failed saying nothing.
- An incomplete version beside a complete one is destroyed by the
  house's start job now, not at once: its open runs must be aborted
  first, and an abort is a child. H13 and H19 changed with it: a
  destroy ZFS refuses is said once, "house H: clean: why", and the
  house runs on its complete version all the same.
- cm_had compares pieces only. A house's own packages are all
  installed again, which pkg install takes as done where they are.
- Unforeseen, in earlier work, fixed in their own commits:
  - job (m3-builder): sh_job_stop reaped the child but left its
    EVFILT_PROC watch on the kqueue; the killed child's exit was then
    handed to whatever job stood at that address after. A house's job
    stopped and the houses started again reuses that memory, so a
    later job blocked in waitpid on a child still running. Found by
    W11. sh_job_stop takes the kqueue now and deletes the watch
    before the kill (J17).
  - house (m5-create): a create or a roll that fails part way through
    the commands' steps leaves devfs mounted on the version's dev, and
    ZFS will not unmount a filesystem with one mounted inside it, so
    the version was never destroyed, and a roll's undoing stopped
    there, the old version's jail not started and no stall set. The
    tree's unmount now releases <root>/dev first, as
    sh_foundation_idle does (H20, the box's).
- Same-second snapshots of one kind collide, as for foundations
  (ruled 2026-10-20): a save and then a roll in the same second fail
  the roll's final save and stall it. check_hroll sleeps a second
  where it saves and rolls in a row.
- A conflict stalls every roll for now. The attended roll, `shtetl
  roll H` opening zfs_rebase's picker in the person's terminal (plan
  7.4, ruled), needs the command's descriptors passed to the daemon
  (m5-queue, as m1-daemon's record says) and the picker's handling of
  what is not a derivation (m5-derivations).
- The issue says a crash's incomplete version is destroyed "and the
  roll run again". The start destroys it and starts the old version,
  no stall; what runs the roll again is m5-queue's triggers, which
  read what waits from ZFS at the start and take the next version
  when the house is idle and not stalled.
- Kill tests: W11 stops the daemon inside the replay, in a clone
  form's run and in a dataset form's, the two states with open runs
  to abort. Stopped at the jail or the final save, nothing new exists
  and the start starts the old version, as H12 holds; stopped after
  the replay (own, recipe, house.conf), the version is incomplete with
  no open run, which H13 holds; stopped in the commands, devfs is
  mounted in it as well, which H20 releases (the box's).

## Verification

- Matrix first (16ccbaf): family W, W1 - W16; H14 mapped to W13.
- Units under it: rebase.h and the fake (ec77613), the creation of a
  snapshot (Z23, 58e99da), roll.depth (CS17, 247df06), a stop of a
  jail not running (N19, 2bd5ad5), the fake keeping an unmounted
  filesystem's files (N18, c030c8b), cm_had (38aacbf).
- make check, 20 programs. check_hroll: W1 - W13 and W15, each
  scenario a house made on g at T, then U made on the lake with a
  dataset new in it (opt) and sent to the run. W1, W2, W5, W6: the
  steps' order in the programs' log, the clone form then the dataset
  form per dataset, the gained piece run and the old not, nginx and
  curl installed and not tmux; the new version's origins, canmount,
  snapshots and kinds, its own www and srv/db cloned from the final
  save and their placeholder srv made again; the swap; the copy
  unmounted; the block. W6's decoy: the replayed tree holds a
  foundation.conf on releng/16.0, which web's ver refuses; the roll
  succeeds, so it read the copy's snapshot. W3: depth 0, 3 and "1d".
  W4: var dropped from U. W7: a conflict in the root's dataset form,
  aborted. W8: the final save, pkg, house.conf, the update snapshot
  each failing. W9: up to date, rolling, starting, failed. W10: the
  stall kept across a start and cleared by a roll. W11: the daemon
  stopped in a clone form's and in a dataset form's run. W12: the swap
  refused part way. W13: two complete versions, and a swap a crash cut
  short. check_daemon: W14, the house's roll asked by name.
- Mutants, 36, all caught: the next version's txg >= (rolls onto its
  own); roll.depth's count and interval each undone; the dropped
  dataset's filter and its refusal each gone; -m, the dataset form's
  -b and --result each wrong; the clone form's readonly or mountpoint
  not inherited; a placeholder cloned noauto; an own placeholder
  cloned rather than made; foundation datasets taken for own; cm_had
  not passed; foundation.conf read from the new tree; the update not
  marking the roll complete; the swap not giving the old version its
  path back; the copy never unmounted, or its step gone; the stall
  not set, not cleared, not shown, not read at a start; the copies
  not unmounted at a start; put_back not inheriting; the start's
  aborts and clean gone; incomplete versions never collected; "no
  foundation version" not said; the step named plan; the jail not
  stopped; the final save's kind; the beam not told; the daemon's
  house lookup and usage line. Two (the copy's and the clean's steps
  gone) the compiler caught, their function left unused; the
  behaviour under each is held by another mutant.
- J17 caught its own mutant, the watch deleted by pid 0.
- gate; xcheck-freebsd with FREEBSD_SRC=~/freebsd-development/freebsd.
- Owed to the box: run.sh (check_hroll, check_job, check_house,
  check_daemon on FreeBSD). The real roll, zfs_rebase on real
  datasets, a real devfs released (H20), the daemon's lake given to
  the houses: m5-box (W16).

## Gotchas for the next issues

- The roll does not pull: the next version must already be on the
  run's copy. Which versions the beam carries to a copy, and when, is
  the beam's (m4-beam); m5-box must have the beam put a newer box
  version on the run before it rolls s2web.
- A house's own dataset at a path the foundation later adds is
  replayed onto the foundation's, and then own's clone of it is
  refused, "exists": a stall, not a corruption, and not handled.
- Each roll's own datasets are clones of the version before's, so the
  versions chain; destroying an old version is the prune's and needs
  promote (m5-derivations, the prune).
- zfs_rebase must be on the daemon's PATH on the box (m5-box).

## Commits

16ccbaf (matrix W), ec77613 (rebase.h, rbfake), 58e99da (Z23),
247df06 (CS17), 2bd5ad5 (N19), c030c8b (N18), 38aacbf (cm_had),
dd8b873 (job, J17), 4fbca8e (the roll, W1 - W13, W15), 47a8be3 (the
daemon, W14), 8398b86 (devfs, H20), and this worklog's.

## Box run 14

Box run 14: run.sh at 9d9efe4 (results 978f3f0): 142 ok, 1 failed.
Every unit program but check_hroll passed on FreeBSD, check_job's J17
and check_house's H13 and H19 among them; family Z with Z23 over
libzfs, the creation of a snapshot; families A and I as before.
check_hroll passed W1 - W10 and failed at W11's wait for the fake
rebaser's run to open (tests/check_hroll.c:537); the doc check, after
the units, did not run.

The cause was a spin in job.c, not the roll: a pipe at its end is ever
readable, and its read watch stayed on until the child's exit was
handed on, so every kevent between the two returned at once. A forked
call closes its output as it exits and tears down its copy of the
process after; on FreeBSD the spin spent W11's 400 events before the
run began. W1 - W10 wait in settle(), which counts no events. In the
daemon, a child that closed its output and lived on would have spun a
whole CPU for as long as it lived. Fixed in a09bf71 (J18: the watch
taken off at the end, a forked call that closes everything and lives
a second handed a few events, not millions; its mutant caught) and
1ceaf1e (until_open waits 20 s by the clock). make check, gate and
xcheck-freebsd pass on the mac.

Owed: run.sh again, for check_hroll W11 - W13 and W15, J18 and the
doc check on FreeBSD.

## Box run 15

Box run 15: run.sh at bfd1373 (results 1db04e1): 143 ok, 0 failed.
All 20 unit programs on FreeBSD: check_hroll whole, W1 - W13 and
W15, with W11's wait by the clock; check_job with J17 and J18;
check_house, check_daemon (W14); the doc check; family Z with Z23
over libzfs; families A and I as before.

Every cell of family W whose disposition is a unit program has passed
on the mac and on FreeBSD. Owed to the box: W16, a real container
rolled by the real zfs_rebase, and H20, a real devfs released, both
m5-box's; m5-roll stays testing until they pass.

## The box cells in build.sh

W16 and H20 moved from m5-box into tests/box/build.sh (1410484), as
V10 was for m5-save-pkg. After s2web's cells: s2app, a second
container with a command piece and no package, made on box's newest
version; box pkg-rolled, the beam carrying the new version to the
run's copy; shtetl roll s2app with a piece that fails in the chroot
(H20: stalled on the old version, said once, the new one destroyed
with its devfs released, the old jail running again), then without it
(W16: the real zfs_rebase carrying a file written in the jail and the
command's motd.template onto the new version, placed and started, the
update snapshot on every filesystem, no stall or record left, the
copy unmounted, the old version pruned and the new on the lake once
the beam converges, nothing in syslog).

s2app has no package of its own on purpose: box's pkg roll writes
var/db/pkg, and a container's pkg writes it too, so replaying s2web
would likely stop at a conflict there, which is a derived file's and
m5-derivations' to answer. zfs_rebase clears every zfs_rebase:
property when a run is done (zfs_rebase_tool src/zfsops.h), so the
tag marks only open runs, as the undo's aborts assume.

Owed: the next build run.

## Build run 13

Build run 13: build.sh at 9099541 (results 1409abf): 82 ok, 7 failed.
Every R, B, P, H17, N and V10 cell as before. Of the new: zfs_rebase
on the daemon's PATH, s2app made, box pkg-rolled and its new version
beamed to the run's copy, all ok. Both rolls of s2app stalled at the
replay, before any zfs_rebase ran: "s2box/run/foundations/box/
usr/local: failed to create mountpoint: Read-only file system". The
seven failures are that stall's: H20 wanted the piece's stall, W16 an
active new version. H20's other three passed, but a failure at the
replay proves nothing of devfs.

The cause: the roll mounted the run's copy with tree_mount, every
dataset but those canmount=off, and a copy is canmount=noauto
throughout, its owner's placeholders too, so box/usr, empty and
read-only, was mounted over the root's /usr, hiding the directory
usr/local mounts on. The create asks the lake's owner which datasets
are placeholders; the roll's mount did not. The fake ZFS hides nothing
under a mount, so no unit cell saw it. Fixed in b15bae1: the copy's
datasets of the next version mounted as their steps are planned,
placeholders not; check_hroll holds it at W11's open run, and the old
code fails it.

Also in the log, as in builds 11 and 12: "cannot unmount
'/var/tmp/s2box/containers/s2web': pool or dataset is busy" at the
pool's export, which left s2box imported; that is what the next run.sh
met as "this box has shtetl spaces". It began with build 11, the first
with V10's pkg -j into s2web. build.sh now says what holds the pool
when the export is refused (its mounts, the jails dying too) and
exports it with -f (4d66231). W16's update-snapshot cell passed 0 of
0 on a version not there; it wants at least one now.

Owed: the next build run.

## Build run 14

Build run 14: build.sh at 8e8ec7e (results ea39fe3): 89 ok, 0 failed.
Every R, B, P, H17, N and V10 cell as before, and all of H20 and W16.

- H20: s2app's roll with the failing piece stalled at it, "commands:
  h20: exited 1", on the old version and said once in syslog; the new
  version destroyed with nothing left mounted at its path, so the
  devfs the chroot mounted was released; the old jail running again;
  the run's copy unmounted.
- W16: rolled by the real zfs_rebase. Its own report in s2app.log:
  the root's replay of the final save, 5 actions done, the first
  /root/w16; usr/local's, 0 actions each. The file written in the jail
  and the command's motd.template were on the new version; the jail at
  its path with devfs once; 3 snapshots, the last kind update, on
  every filesystem; no stall or zfs_rebase record left; the copy
  unmounted; after the beam converged the old version was pruned from
  the run and the new one was on the lake; syslog said nothing of the
  roll or the prune. It held only R6's stall and H20's.
- The pool exported without -f this time, so the diagnostics did not
  print; they stay for when it is refused again.

Tested: every cell of family W and H20 has passed where its
disposition says. check_hroll's check of the copy's mount (b15bae1)
has run on the mac only; it runs on FreeBSD at the next run.sh.
