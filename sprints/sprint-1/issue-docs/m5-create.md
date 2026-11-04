# m5-create: a house made

## What was built

`shtetl create CONTAINER --on FOUNDATION` makes a container's first
version (tests/MATRIX.md family H):

1. The ask (sh_house_create): refused when the house is there ("H:
   exists") or being made ("H: creating"), when its recipe does not
   resolve (its words), and, in the daemon, for an invalid name, a
   foundation not there or with no version. The house is added,
   creating, its log H.log made anew, and the beam asked to pull F's
   newest version.
2. The pull (sh_beam_pull, 54da69d): the run's copy of F made from
   that version on where the run holds none of F, brought to it where
   behind; the asker told at the end of the convergence and before its
   prune. Told, the house clones the copy's tree at once, inside that
   telling, so the prune that follows sees a clone of the copy: the
   root, its children, each canmount=noauto, a placeholder off as the
   lake's owner is (read with sh_zfs_native, 9ef301d, since every copy
   is noauto); the root shtetl:house. The version is named for F's
   snapshot, its kind left off, then the house:
   15.1-releng-<date>-web, below <run>/containers.
3. Then a job of steps: mount (the tree at its own path,
   <containers>/<version>, placeholders not); recipe (foundation.conf
   read from the clone, nothing having run in it, as the layer
   foundations/F@version; the house's recipe stacked on it; its ver
   held against the foundation's src_branch, sh_recipe_branch,
   8287ea9); datasets (the recipe's that the version lacks, with their
   properties and canmount=noauto, parents as placeholders); the
   commands' steps (commands-external, devfs, pkg -c, chroot pieces),
   the foundation's layer's packages and pieces not run again
   (cm_ran, 8287ea9) and SHTETL_KIND container; house.conf (the shaken
   house recipe, read-only and schg); snapshot (-r, kind create);
   place (the root's mountpoint set to <containers>/<house>, which
   moves the mounted tree there, as libzfs's changelist does,
   eb1c64f).
4. Its end: the house active, its version; or the failure said to
   syslog, "house H: why", the version destroyed, the house gone.
5. The house actor's start (sh_houses_start): each child of
   <run>/containers with shtetl:house; one not complete (no snapshot
   of kind create or update) destroyed, a create a crash cut short;
   the newest complete by its origin's createtxg active, placed and
   mounted where it is not. The daemon starts them after the
   foundations, shows "house H active VERSION" or "house H creating
   STEP" between the foundations and the beam, and frees them at its
   stop.

## Decisions beyond the plan

- `--on F`, the person picking the foundation, as the docs gave it,
  and since ruled (2026-11-03, plan 18): a container's requirements
  are a compatibility check against the foundation picked, not a
  chooser; its ver is the check there is so far.
- The create always pulls, also where the run's copy holds the version
  already: the convergence that follows tells it at once. One path.
- The create snapshot is named <date>-create, not the plan's
  <fver>-<fbranch>-<date>-create: for a main branch the version's
  major comes from the checkout's REVISION, which the house does not
  have, and the version's dataset name already says the foundation
  version.
- Placeholders from the lake's owner's canmount, a fact, not the
  recipe's datasets list.
- The reply at once, as roll's; status shows the create.
- At the start, only the newest complete version is touched; putting
  another back to its own path and unmounted is the swap's, m5-roll's
  (H14 deferred there).
- sh_zfs_set takes native properties and says what a mountpoint set
  moves (eb1c64f); sh_zfs_native reads canmount and mountpoint
  (9ef301d); the roll's ver checks moved to the cascade, shared with
  the house (8287ea9).

## Verification

- make check, 18 programs, check_house new: H1, H2, H4 - H13, H15
  over the fake, the chroot's programs tests/bin's. The fake keeps no
  files in a clone, so the test lays the version's foundation.conf
  and an etc/ where the clone mounts, as a real clone would show the
  foundation's tree.
- check_daemon: H16, create's refusals, and a house with a complete
  version started with the daemon, its status line.
- check_beam: B33 - B35, the pull, its asker told before the prune and
  never inside the call.
- Z20, Z21 over the fake; over libzfs owed to run.sh.
- Mutants caught: eight of the house (the placeholder noauto, the
  foundation's pieces run again, ver not held, a failed version left,
  not placed, an incomplete version kept, the snapshot's kind, own
  datasets not made), five of the pull, two of the fake's remount, one
  of the daemon's wiring.
- gate, xcheck-freebsd.
- Owed to the box: run.sh (Z20, Z21 over libzfs) and m5-box (H17), a
  container made on the M3 foundation, pulled.

## Gotchas for the next issues

- m5-jail: the house's place is <containers>/<house>, read from
  run/containers' mountpoint, not a fixed /containers.
- m5-roll: a second version's www is a clone of the first's final
  save; its snapshots must be named apart from the first's, or the
  prune's promote is refused. The swap must unmount the old version
  and put its mountpoint back before placing the new one (P2.7).
- m5-restore: a restore pulls as a create does.
- house.c and roll.c each walk a tree and destroy one; they share
  nothing yet. A third user should make it one helper.

## Commits

a64ba4a (matrix), 54da69d (the pull), eb1c64f (a native set), 9ef301d
(a native read), 8287ea9 (commands, cascade), "house: a container's
first version made, and the houses started" and "daemon: shtetl
create".

## Build run 8

Build run 8: 2026-11-05, build.sh at eb67efa (results d6e15af): 49 ok,
0 failed: the roll on the box with the commands' kind and layer
(8287ea9) and the ver checks moved to the cascade, R3's chroot pieces
and package ok. Still owed: run.sh, for Z20 and Z21 over libzfs and
check_house on FreeBSD; m5-box (H17).
