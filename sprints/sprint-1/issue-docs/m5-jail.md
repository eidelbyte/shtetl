# m5-jail: a container's jail

## What was built

A container is started through its jail, made from a block shtetl
writes whole at each start (plan 7.3; tests/MATRIX.md family N):

1. The name (ae2e46c): sh_syn_name refuses a name of digits only,
   "digits only", for every name it checks: a recipe's, create's, a
   requires entry's. The jail's name is the house's, and jail(8)
   takes digits for a jail's id, so `jail -r 123` could remove another
   jail (ruled 2026-10-14 on main, carried).
2. The block and the steps (899ab58, src/core/jail.[ch]):
   sh_jail_write writes `/etc/jail.conf.d/H.conf` whole, replacing
   what is there: main's fixed lines (the path, host.hostname H,
   exec.start and exec.stop by rc, exec.clean, mount.devfs,
   devfs_ruleset 4, ip4 and ip6 inherit), then the shaken recipe's
   jailconf, a piece from each layer in the layers' order, each line
   a tab in, blank ones left out. sh_jail_start adds the child
   `jail -f FILE -cm H`, sh_jail_stop `jail -f FILE -r H`, both a step
   named jail. tests/bin/jail fakes jail(8).
3. The start (b9aec30, src/core/house.c): a step "block", which finds
   the active version's create or update snapshot, finds the dataset
   holding var/db/shtetl (the one of the version's tree mounted
   nearest above it), reads house.conf through that dataset's
   .zfs/snapshot, and writes the block with the root's mount as its
   path; then the jail step. A create adds the start after the place,
   so the jail starts last; the house actor's start (sh_houses_start)
   runs a start for every house, a job of the house's own, status
   "house H starting STEP" until it ends. A start that fails is said
   to the sink, "house H: why", and in H.log; the house stays active.
   A create whose start fails is a failed create: the version
   destroyed, the block removed if it wrote one, the house gone.
4. The daemon: SH_JAILDIR, /etc/jail.conf.d, passed to sh_daemon_run;
   the houses started once the kqueue and the house env are there,
   after the log directory is made. At its stop no jail is stopped.

## Decisions beyond the plan

- jail -cm, not -c (plan 7.3's table). -c on a running jail fails,
  "already exists" (usr.sbin/jail/jail.c, JF_START); -cm makes the
  jail where none runs and only updates one that does, running none
  of its exec steps again (JF_START_SET turns into JF_SET). So a start
  after a crash, or after a person restarts the daemon with its
  containers up, is a first start, and nothing checks first whether
  a jail runs.
- house.conf is read through the dataset holding var/db/shtetl, not
  always the root: the root's snapshot does not show a child
  dataset's files, and a foundation's var is often a dataset of its
  own (docs/configuration.md's example has one). The holder is found
  by mount paths, which ZFS gives, not by dataset names.
- The house actor owns its job and its log (h_job, h_log, h_lerr), a
  create's and a start's alike; they were the create's in m5-create.
  m5-roll's acts will use the same job.
- A failed start at the daemon's start is said and the house stays
  active; nothing is kept of it (no stall: a stall is a roll's).
  Status shows the active version, as before.
- A create removes the block only if its own start wrote it, so a
  block a person keeps under the same name is not taken away by a
  create that failed before its start.
- he_lake, set by the daemon and read by nothing, is gone.
- The daemon's stop stops no jail. The issue did not say; asked as
  Q-J1, and ruled 2026-11-05: "stopping the daemon shouldn't stop the
  jails" (plan 18). The containers outlive the daemon.
- The block's comment line and its fixed lines are main's, word for
  word; persist is not set, as on main (the P2 probe's block had it,
  for a jail with no rc).

## Verification

- Matrix first (7fa83e8): family N, N1 - N18.
- make check, 19 programs: check_jail new (N1 - N6); check_house
  (N7, N8, N9, N10, N11, N12, N13, N18, and H rewritten for the start:
  every create now has a var dataset, and restarts wait out the
  start); check_daemon (N15, N16 and N7 by an update snapshot, two
  hand-made houses, their jails started in either order, none stopped
  at the daemon's stop); check_cascade and check_schema (N16).
- The fake (tests/zfake.c, zfake_files, asked by check_house only): a
  snapshot of a mounted filesystem copies its files, not a child
  filesystem's, into MOUNT/.zfs/snapshot/SNAP, and a mountpoint set
  moves a mounted filesystem's directory. Off by default, since a
  foundation's tests walk their trees, in which ZFS hides .zfs. The
  real behaviors are the box's: I12 and I14 (a file read through a
  snapshot, the live one rewritten) and Z20 (a mounted filesystem
  moved by a mountpoint set).
- Mutants, all caught by a test, not the compiler: house.c reading
  through the root only, reading the live tree, the create without
  its start, the actor's start without its steps, the block not
  removed, hm_block not set, update snapshots not complete, the
  start's failure not said, the jailconf not passed, the starting
  state not set; zfake's view taking in a child filesystem; jail.c's
  blank lines kept, -c for -cm, one layer only. GNU make on the mac
  times in whole seconds: a mutant script must remove the mutated
  object, or it tests the last mutant's build.
- gate; xcheck-freebsd with FREEBSD_SRC=~/freebsd-development/freebsd.
- Read in the sources: zfs_umount on FreeBSD unmounts the snapshots
  mounted under .zfs before the dataset
  (module/os/freebsd/zfs/zfs_vfsops.c), so reading the block through
  .zfs leaves the version unmountable, as long as nothing is open
  there; sh_shaken_read closes what it opens.
- Owed to the box: run.sh, for check_house, check_jail and the new
  check_daemon on FreeBSD (every jail there the fake's: each test
  puts tests/bin first in PATH), done in box run 11; build.sh for N14
  (-cm on a running jail) and N17 (a real jail from the block), moved
  there from m5-box (below).

## Gotchas for the next issues

- m5-roll: stop is sh_jail_stop on the house's job, start the same
  steps the create and the actor's start use (start_steps): set
  h_active to the new version before them, since the block step reads
  it. The update snapshot's house.conf is what the new block comes
  from.
- A container's commands run in a chroot of the new version when it
  is made (m5-create), before its jail ever runs. A command that
  starts a service starts it on the host, in that chroot, outside any
  jail, where the job's SIGTERM to its process group does not reach a
  daemon that made its own session. docs/configuration.md's "Every
  key at once" example has `service nginx start` in a container's
  commands; with rc starting what rc.conf enables at the jail's start,
  that line is not needed, and on this model it is wrong. Raised with
  the author and dropped, as ruled 2026-11-05 (98212c9), the docs
  saying the commands start nothing.
- The containers are to start in the order `requires` gives, a
  dependency startup tree (ruled 2026-11-05); that, and the rest of
  what `requires` is to mean, is future work (sprints/future-work.md,
  Container dependencies). Until then the daemon's start starts every
  container at once.

## Commits

7fa83e8 (matrix, Q-J1), ae2e46c (digits only), 899ab58 (the block and
the steps), b9aec30 (the start, the daemon), a1cc52a (the worklog);
after the ruling, 98212c9 (the docs) and 734b41d (the ruling's
record).

## Box run 11

Box run 11: 2026-11-05, run.sh at 734b41d (results 50b39a1): 139 ok,
0 failed, box run 10's 138 and check_jail.

- check_jail ok on FreeBSD (N1 - N6), with tests/bin's jail.
- check_house ok: N7 - N13 and N18, zfake_files's view and move on
  FreeBSD's libc, the start reading house.conf through var's view.
- check_daemon ok: N15 and N7, two houses started with the daemon,
  their jails the fake's, none stopped at its stop.
- check_schema and check_cascade three checks more each: N16, a name
  of digits only refused.
- The lines on stderr are box run 10's; no house's start was said.
- Still owed: N14 (-cm on a running jail) and N17 (a real jail from
  the block), now in build.sh (below).

## The jail's box cells in build.sh

m5-box, where N14 and N17 were, waits on m5-queue, m5-derivations and
m5-restore, and they need none of it: only a real container on a real
foundation, which build.sh builds every run. So build.sh gains house(),
after the rolls, since R10 reads syslog for the rolls alone:

- H17 (moved from m5-box too, being the container N17 needs): the
  run holding no copy of box; shtetl create s2web --on box, active
  on box's newest version, its root a clone of the copy pulled whole,
  placed at the run's containers/s2web.
- N17: the block holds the version's path and the recipe's jailconf;
  the jail runs at that path, devfs at its dev; jexec s2web reads what
  the chroot's command wrote, pkg -j s2web the foundation's package.
- N15 on the box: the daemon stopped, the jail runs on, its jid the
  same.
- N14: the daemon started again, s2web active, the same jid, devfs
  mounted there once, no syslog line of s2web's.
- N17: jail -f the block -r s2web takes the jail and its devfs away.

Two changes to what was there:

- The stand-in S3 is destroyed after R8's P24 cells, before R9 starts
  the daemon again. Since m5-jail that start starts every house's
  jail, and a stand-in holds no house.conf: its start would fail, and
  say so in syslog, which R10 holds to the stall's line alone. With
  no house on the run then, the prune takes box's copy (ruled Q-P1),
  so s2web's create pulls it whole: H17's "pulled".
- pool_export removes s2web's jail from its block, or with jail -R
  where that fails, and the block, before the pool's other mounts and
  the export: the daemon's stop leaves a jail running (ruled
  2026-11-05), and a jail holds the pool busy. It runs at the start
  of a run too, so a run stopped part way leaves nothing for the next.

Verified: sh -n and dash -n; the box's build run is owed.

## Build run 9

Build run 9: 2026-11-05, build.sh at 99b96f1 (results 618de48): 61
ok, 2 failed. Every R, B and P cell ok, R10 among them, the stand-in
gone before R9's restart.

- Ok: H17's pull (no copy of box on the run, then s2web active on
  box's newest version, its root a clone of the copy); every N17 cell
  (the block, the jail at the version's path, devfs at its dev,
  jexec, pkg -j, jail -r), though at the wrong path, as below; N15
  (the daemon stopped, the jail running on).
- FAIL H17 "placed": the version's mountpoint
  /var/tmp/s2box/var/tmp/s2box/containers/s2web, the pool's altroot
  twice. sh_zfs_native gave the run's containers' mountpoint with the
  altroot before it, as zfs get does, and create's place set that
  with /s2web after it into the property, which ZFS puts below the
  altroot again. The jail ran there, the block saying the same path,
  so N17 held together around the wrong place.
- FAIL N14 "started again": no status line. The daemon's start
  compared the version's mountpoint, the altroot twice, with the place
  it computed, once, set it again on the root the running jail used,
  and ZFS said "pool or dataset is busy"; sh_houses_start failed, and
  the daemon never started (syslog, shtetl[2013]). N14's syslog cell
  passed over that line, counting only "house s2web:" lines.
- Fixed: e2eab12, sh_zfs_native's mountpoint without the altroot, so
  what it reads is what sh_zfs_set takes; Z21 exact, Z22 the round
  trip on the box's altroot (m5-create's worklog); e02b849, N14's
  syslog cell counting every line from before the restart.
- Raised: one house's refusal at the daemon's start stops the whole
  daemon, against plan 1's "holds nobody up" (Q-H1).
- Owed: run.sh (Z21, Z22 over libzfs) and the next build run.
