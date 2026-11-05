# m5-save-pkg: a house's saves and pkg upgrades

## What was built

A house's own snapshots while its version is active (plan 7.1;
tests/MATRIX.md family V):

1. `shtetl save H` (sh_house_save): the active version snapshotted at
   once, every dataset of its tree, `<date>-save`, shtetl:kind save,
   and the beam told. The reply is empty; ZFS's words where it
   refuses.
2. `shtetl pkg H` (sh_house_pkg): a job of the house's, "house H
   upgrading STEP" in status. Step packages reads house.conf and
   foundation.conf from the version's create or update snapshot,
   through the dataset holding var/db/shtetl, as the jail's block is
   read; the house's own packages are those house.conf lists less
   those foundation.conf lists; pkg runs `env ASSUME_ALWAYS_YES=yes
   pkg -j H upgrade -y --` with them in the running jail; snapshot
   takes `<date>-pkg`, kind pkg; at the end the beam is told. A
   failure is said once, "house H: STEP: why", and leaves no
   snapshot.
3. Refusals: a house not active refuses both with its state, "H:
   creating", "H: starting", "H: upgrading", "H: failed".
4. The daemon: `shtetl save CONTAINER`; `shtetl pkg
   CONTAINER|FOUNDATION`, the house's upgrade or the foundation's pkg
   roll, whichever the name is, "NAME: no such house or foundation"
   where neither; `shtetl create` refuses a foundation's name, "H: a
   foundation", and `shtetl roll` a house's, "F: a house".

## Rulings asked and given (2026-11-05, plan 18)

- Which packages a house upgrades: only its own, house.conf's less
  foundation.conf's. The foundation's are its pkg roll's and reach the
  container when it rolls, so the two never upgrade the same package.
- One namespace: a house and a foundation never share a name, held
  where names are made, at create and at a foundation's first roll.
  `shtetl roll NAME` will act on whichever it is from m5-roll.

## Decisions beyond the plan and the issue

- A container with none of its own packages runs no pkg, and is
  snapshotted all the same, kind pkg, as a foundation's pkg roll with
  none listed is: `pkg upgrade` with no names upgrades everything, so
  an empty list must never reach it.
- A create's end tells the beam too, so a new version reaches the lake
  at once rather than at the next period. The issue asked it of saves
  and pkg upgrades; a create's version is the same kind of new thing.
- The save is no job: one snapshot call, in the act, the reply after
  it. The pkg upgrade is a job, pkg being a child that takes long.
- No message on a save (main had `-m`): no issue asks for one.
- shaken() is start_block's reading taken out, now the start's and the
  upgrade's; take() is the create snapshot's code, now a save's and an
  upgrade's too; the start's and the upgrade's ends are one, act_end,
  which tells the beam where an upgrade ended well.
- Same-second snapshots of one kind collide, ZFS refusing the name;
  out of scope, as for foundations (ruled 2026-10-20).

## Verification

- Matrix first (c8ece8b): family V, V1 - V10.
- make check, 19 programs: check_house V1 - V6 and V9 (a save, its
  kinds on every dataset, the beam's count; an upgrade, nginx its own
  and tmux the foundation's; bare, whose only package is the
  foundation's, upgrading nothing; pkg failing; foundation.conf gone
  from the snapshot; refusals creating, upgrading, starting, failed;
  a save ZFS refuses); check_daemon V7 and V8 (the namespace, save's
  and pkg's words, web saved and upgraded through the daemon).
- The fixture's foundation.conf in check_house wrote pkg as recipe
  shorthand, an array; a shipped foundation.conf is show's output, an
  object, which the code reads. The fixture writes show's form now.
- Mutants, all caught: the foundation's packages not taken away; pkg
  run with no names; the upgrade, the create and the save not telling
  the beam; busy() never refusing; the save's and the upgrade's kinds
  swapped; the daemon's two namespace refusals and pkg's house lookup
  gone.
- gate; xcheck-freebsd.
- Owed to the box: run.sh (check_house, check_daemon on FreeBSD) and
  build.sh's V10 (8fa0197): s2web, with its own package jq and the
  host's resolv.conf copied in by a commands-external, saved and
  upgraded while it runs, the save on the lake's copy.

## Gotchas for the next issues

- m5-roll: the final save is sh_house_save's take(), kind save, on the
  active version; `shtetl roll H` must look up the house first, as
  `shtetl pkg` does, act_roll's "F: a house" refusal giving way to it.
- A pkg upgrade changes var/db/pkg/local.sqlite in the house, and a
  foundation's pkg roll the same file in the foundation: replaying a
  pkg save onto a new foundation version will meet it on both sides
  (m5-derivations, m5-roll).
- A container that wants pkg -j to work needs its own resolv.conf; the
  docs' container block copies the host's with a commands-external.

## Commits

c8ece8b (matrix, rulings), a2d675b (save, pkg, the daemon), 8fa0197
(build.sh V10), and this worklog's.

## Box run 13 and build run 11

- Box run 13: run.sh at 5fdb0d7 (results 9b1a32a): 140 ok, 0 failed.
  check_house and check_daemon on FreeBSD with V1 - V9, and with Q-H1's
  H18 and H19, which box run 12 came before.
- Build run 11: build.sh at 9b1a32a (results 0646114): 66 ok, 3
  failed, every failure V10's cells', not the code's. Ok: the save on
  the lake's copy after its convergence, s2web active again after the
  upgrade, nothing in syslog; every R, B, P, H17, N cell as before.
  FAIL "saved: every filesystem, kind save" and "every filesystem, kind
  pkg": got "3 3", which is what every filesystem having one means;
  the cells wanted "3 1". FAIL "pkg -j ran": the cell looked for the
  line after "# packages" in s2web.log, but only a child's step writes
  its line (job_spawn), and packages is a call: there is no such line.
  Fixed in 70d7cee: "$_n $_n", and the log's last step line, pkg's,
  where the upgrade had a package of its own.
- Owed: the next build run, for V10 with its cells mended; what it
  will show that this run could not is that pkg ran, jq being s2web's
  own.

## Build run 12

Build run 12: build.sh at a1a2f31 (results d22b33c): 69 ok, 0 failed.
V10 all six: the save on every filesystem and on the lake's copy;
s2web active again after shtetl pkg, the pkg snapshot on every
filesystem; pkg's the last step line in s2web.log, so pkg -j ran,
jq being s2web's own and tree the foundation's; nothing in syslog.
Every other cell as before; syslog held the R6 stall's line alone.

Tested: every cell of family V has passed where its disposition says.
