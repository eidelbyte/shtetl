# Sprint 1: houses that roll

Status: plan of record, 2026-10-19. Opened by the author's message of
2026-10-18, ruled by their answers to fourteen questions the same day
and to three of four more the next; all are quoted in section 18.
Nothing is built yet but the M0 probes, whose first box run is in
section 14.1. What waits on the author is in open-questions.md beside
this file. A point marked PROPOSAL is a decision made where the
rulings are silent and is not yet confirmed.

The tracker is shtetl-sprint-1.json beside this file:

    tools/tracker.py list

It holds the issues of M0 (section 14): the probes and the ruling that
folds their results in. The issues of M1 to M7 are written with that
ruling.


## 1. What shtetl is

shtetl builds FreeBSD from source into foundations and runs houses on
them. A house is a container (a jail) or a system (a bootable root).
The two are handled alike and differ only in how one is stopped,
swapped and started.

Everything is ZFS. A foundation is one dataset in zlake; each build is
a snapshot of it, and beaming carries it to zrun. A house is a version
dataset on zrun, cloned from a foundation version; its saves and
package upgrades are snapshots of that version. When a new version of
its foundation reaches zrun, the house rolls onto it: it is stopped,
saved, its saves replayed with zfs_rebase into a new version named for
the new foundation version, the new version swapped in, and started.

It is a cascade, foundation to house, with automatic rolls. Each
foundation and each house is an actor that advances on its own, stops
on its own when something fails, says so, and holds nobody up.


## 2. Ground rules

CLAUDE.md has the working rules. The design rules every section below
follows:

1. State lives in ZFS. An actor starting reads where it stands from
   ZFS: datasets, snapshots, properties, origins, zfs_rebase's own
   durable record. No state files and no records beside the data, so
   a crash and a restart are the same as a first start.
2. Write new, swap once. A roll writes into something new; what runs
   is touched only by the swap, done last. A failure before it drops
   the new thing, and nothing is put back.
3. One owner per dataset. A foundation is written in zlake and a
   house on zrun (ruled); each dataset's other copy converges to its
   owner's.
4. Two triggers and no more. A foundation rolls by its schedule or by
   hand; a house rolls when a new version of its foundation reaches
   zrun, or by hand (ruled). A schedule is a timer sending the
   message a person could send.
5. Nothing blocks on the network. Beaming is convergence that resumes
   where it stopped.
6. Facts come from their binary source; names are for people.
7. C, in one daemon (ruled). FreeBSD 15.1 is the floor. zfs_rebase is
   run, not linked.
8. Nothing is copied from main by default. What carries over is
   listed by name in section 12.


## 3. Words

| word | meaning |
|------|---------|
| foundation | FreeBSD built from source by a recipe: one dataset in zlake, its versions its snapshots |
| house | a container or a system |
| version | one dataset of a house, cloned from one foundation version; a house has one for each foundation version it has rolled onto |
| active | the version a house runs from: its newest complete version, the only one mounted |
| roll | for a foundation, its next build; for a house, its move onto the next version of its foundation |
| swap | the one act that makes a house's new version active |
| stall | a roll that failed; the object rolls no more until a person restarts it |
| shake | merge a recipe's layers into one whole recipe |
| shaken recipe | the result, injected read-only into what it built |
| remote | a named place shtetl reaches: a git repository, or the lake |
| beam | a dataset's copy converging to its owner's |
| owner | the space where a dataset is written |


## 4. The spaces

Four spaces, as on main: each one dataset the user already has, found
by its `shtetl:space` property.

- zconfig: the recipes and the remotes, nothing else (ruled). It is a
  ZFS dataset; the person may snapshot it, shtetl never does (ruled).
  The remotes (ruled): the FreeBSD src remote, the patch repositories,
  the recipe repositories, and the lake remote, which is ZFS, not git:
  where zlake is when it is on another host. Each recipe repository is
  cloned into a directory of zconfig of its own, named for its remote;
  a recipe is found by its name across them, and one name in two
  repositories is refused, as a redefinition is in the language
  (ruled). shtetl.conf, the remotes in it, stays the site's own at
  zconfig's top.
- zbuild: the FreeBSD src checkout.
- zlake: where foundations are built and owned, their obj caches, and
  the copy of every house version beamed from zrun, kept deeper than
  zrun keeps it.
- zrun: where houses run and are owned, and the copies of the
  foundation versions their houses are on.

    <lake root>/
        foundations/
            generic/                    built here; @15.1-releng-<date>-create,
                                        @15.1-releng-<date>-update, ...
        foundation-builds/
            generic/                    obj cache
        containers/<version>/           as many as lake.keep
        systems/<version>/

    <run root>/
        foundations/
            generic/                    beamed from the lake; readonly
        containers/                     mountpoint=/containers
            15.1-releng-2026-10-11T08_02_40Z-nginx/
            15.1-releng-2026-10-18T09_30_12Z-nginx/
                usr/                    canmount=off placeholder
                    local/
                var/
                www/                    the house's own
        systems/                        the boot environment root
            15.1-releng-2026-10-18T09_30_12Z-kidon/

A house version is named for the foundation snapshot it was cloned
from, its kind left off, and then the house (ruled):
`15.1-releng-2026-10-18T09_30_12Z-nginx`. The name is new for every
foundation version, so it is never taken: there is no staging tree,
no archive tree and no rename. Old versions stay where they are until
pruned by count. The name is for people; shtetl knows a version's
house by `shtetl:house` and its foundation version by `origin`.


## 5. The configuration

The language stays as built on main and as its docs/configuration.md
describes it: UCL, everything stacks, a redefinition is refused,
messages are short. So does the layering, in the author's words "the
two layer defaults & imports over object configs and imports":

1. the default: shtetl.conf, after its imports and its block for the
   recipe's kind;
2. the object: the recipe, after its imports.

A house's recipe stacks on the shaken recipe its foundation version
carries (ruled): their ver constraints both hold, pkg lists join, and
a command piece the foundation ran is not run again. A change to a
foundation's recipe takes effect with that foundation's next build,
which the person guides.

Shaking makes the shaken recipe: one UCL document, every value
merged. It is injected into what it built, read-only and schg:

- a foundation version: /var/db/shtetl/foundation.conf, with the src
  commit and the patches applied;
- a house version: /var/db/shtetl/house.conf.

The two names differ so that a house's file and its foundation's
never meet in a rebase: the foundation's changes are only on onto's
side, the house's only on from's. Since every snapshot carries the
recipe it was made from, nothing else has to: no config snapshots, no
`shtetl:config`, `shtetl:patches` or `shtetl:revision`.

A jail's tree is not trusted. A jailed root cannot change an schg
file, but it can rename the directory holding it, so nothing the host
does reads a file a jail could have written. What the host needs from
a house's shaken recipe it reads from the version's create or update
snapshot, found by `shtetl:kind`, which shtetl took before the version
ever ran (7.2 step 7).


## 6. Foundations

One dataset per foundation recipe, in zlake; one snapshot per build.
Linear.

The roll, in order:

1. src: zbuild's checkout reset to the head of the recipe's
   src_branch at its remote.
2. patches, in order from their remotes; a clean three-way merge goes
   on.
3. build: buildworld and buildkernel over the foundation's obj cache.
4. install into the foundation's head, mounted for the build:
   delete-old, installworld, the kernel through a scratch directory and
   install -C, distribution, installworld's backups removed.
5. pkg, commands-external, commands.
6. foundation.conf injected.
7. snapshot `F@<ver>-<branch>-<date>-<create|update>`, with
   `shtetl:kind` and `shtetl:commit`.

The beam then carries the version to zrun (8.1), and its arrival there
is what the foundation's houses roll on (7.2).

A failure at any step rolls the dataset back to its newest snapshot,
the latest working version, resets zbuild with git, and stalls the
foundation until the person steps in and restarts it (ruled; 7.4).
There is nothing to resume: `shtetl roll F` starts again at step 1
with zconfig as it is then, and the obj cache makes that a rebuild of
what changed. main's pause, with --continue, --skip and --abort, is
gone.

A crash part way is not a failure. Starting, the actor finds the head
written since the newest snapshot (written@, after a sync) and rolls
it back, without a stall; the next roll comes from the clock or a
person.

A pkg upgrade is a roll of steps 5 to 7 with only pkg in step 5, kind
pkg.

Builds share zbuild and the CPU, so foundation actors ask one builder
for a turn (section 9). Two builds never run at once, and nothing
polls.


## 7. Houses

### 7.1 Versions and the active one

A house is the set of its versions, each carrying `shtetl:house`, each
cloned from one foundation version on zrun. Only one is mounted at any
time, the active one (ruled); the others are kept, unmounted, until
pruned. While an update builds a new version, that one is mounted
beside it at its own path, since zfs_rebase reads every side at its
mountpoint (section 12); the swap ends it.

The active version is the house's newest complete version (PROPOSAL,
probe P2). Complete means it holds its create or update snapshot,
which an update takes last (7.2 step 7), and an update that fails
destroys what it made. So the newest complete version is always the
one to run, no property marks it, and nothing can disagree with ZFS:
starting, shtetl mounts each container's active version, and a
system's is the one bootfs names. Every version's datasets are
canmount=noauto, so nothing but shtetl, or a system's boot, mounts
one.

A container's active version is mounted at /containers/<house>: its
root's mountpoint is set there, and its children follow by
inheritance. Any other version of it keeps the mountpoint its name
gives it, /containers/<version>, mounted only while an update builds
it. A system's versions are boot environments (7.3).

Each version's snapshots are linear: the saves its roll replayed but
the last, its create or update snapshot, which holds the last
replayed save, and then, while it is active, its saves and pkg
upgrades.

Datasets a house has and its foundation does not (a recipe's `www`)
are the house's data. On a new version they are clones of the active
version's at the roll's final save (7.2 step 6), and they are promoted
when the old version is pruned (8.2). Probe P5.

### 7.2 The roll

A house rolls when a new version of its foundation reaches zrun, or
when the person says so with `shtetl roll H` (ruled). Arrivals queue:
the house takes the next only when it is idle and not stalled, so it
rolls through every foundation version that reaches zrun, in order
(ruled). What waits is read from ZFS, the foundation versions on zrun
newer than the active version's origin that the house's ver allows,
so the queue survives any restart.

One sequence for both kinds of house:

1. stop the house (7.3);
2. the final save, on the active version;
3. replay onto the next foundation version, into the new version, the
   active version's saves, as many as roll.depth allows. For each
   dataset that came from the foundation, parent first, one zfs_rebase
   a save: main's history replay, ruled 2026-10-09 -- the first step a
   clone form rebase onto the foundation version, each later step a
   dataset form rebase with --allow-unrelated, --base the previous
   original save, and its pre-apply snapshot named for that save; -m
   always, so a conflict that merges three ways clean goes on. A
   canmount=off placeholder is cloned, not replayed. Derivations:
   derived files answered keep and regenerated at the gate, as on main
   (probe P6);
4. what the house's recipe gained since the active version was made:
   packages installed, command pieces that never ran run once. A
   changed recipe is picked up by the next roll, automatic or by hand
   (ruled);
5. house.conf injected, shaken on the new foundation version's
   foundation.conf;
6. the house's own datasets cloned from the final save;
7. the update snapshot, `<version>@<fver>-<fbranch>-<date>-update`,
   taken -r over the new version: it holds the last replayed save,
   and, taken last, it is what makes the version complete;
8. swap (7.3);
9. start (7.3).

The house is down from step 1 to step 9. A failure or a conflict in
steps 3 to 7 destroys the new version, aborting any zfs_rebase run on
it; the house is back on its last working foundation, its active
version is started again, and it stalls (7.4). Steps 2 to 7 write only
into the new version and a snapshot of the active one, so nothing
needs putting back, and step 9 runs on every way out.

After a roll: the prune (8.2), then the next foundation version
waiting, if any.

A crash during a roll is not a failure. Starting, the actor finds a
version that is not complete and destroys it, and the roll runs again;
a complete version that is not yet the one mounted is a swap left
undone, and it is done.

### 7.3 Stop, swap, start: the one difference

|       | container | system |
|-------|-----------|--------|
| stop  | jail -r | outer space: reboot -r into a root on a memory disk |
| swap  | the active version unmounted, the new one mounted at /containers/<house> | zpool set bootfs |
| start | jail -c | a real reboot |

Containers (PROPOSAL, probe P2). The jail's path is /containers/
<house>, the same for every version. The swap unmounts the active
version and puts its mountpoint back to its own path, then sets the
new version's to /containers/<house>. At each start the host writes
/etc/jail.conf.d/<house>.conf whole from the active version's shaken
recipe, read from its create or update snapshot (section 5), and never
reads it back. The jail is made and removed from that block: jail -r
undoes mount.devfs, and its other removal steps, only for a jail its
configuration names (usr.sbin/jail/jail.c; box run 1, 14.1).

Systems (PROPOSAL, probes P3, P4). A system's versions are boot
environments under zrun/systems: each version's root has
`mountpoint=/`, and its root and children `canmount=noauto`, so that
zfs mount -a never takes another version's; rc.d/zfsbe, which runs
before it, mounts the booted one's children. The loader can boot any
kept version by hand. Stop is the outer space protocol (ruled
2026-10-15): a memory disk made a pool, the system's current root
sent into it, `vfs.root.mountfrom` pointed at it, `reboot -r`. shtetl
starts again there from rc, finds itself on that pool, and does steps
2 to 8 of the roll for that system and nothing else: in outer space
the system is stopped, so none of its containers start. Start is a
real reboot, which loads the new kernel. A failed roll reboots into
the bootfs it did not change.

### 7.4 Stalls

A stall is a state kept in ZFS like every other: `shtetl:stall`, the
step and the reason, on a foundation's dataset or on a house's active
version. A stalled object does not roll, neither on its schedule nor
when its foundation moves (ruled): it waits at its last working
version until the person restarts it with `shtetl roll`, which clears
the property. A conflict is a stall like any other failure. An
automatic roll never waits on a person; a roll the person starts with
`shtetl roll H` is attended, and at a conflict it waits for them, with
zfs_rebase's picker open in their terminal and the house down while
they answer (ruled).

Without the property, a house whose roll failed would find its
foundation still newer and roll straight into the same failure.

A stall is shown by `shtetl status` and logged to syslog. Notifying
the person is a pluggable interface, worked out at the very end
(ruled; M7).


## 8. Beam and prune

### 8.1 Beam

Every dataset's copy converges to its owner's (ruled): foundation
versions from zlake to zrun, house versions from zrun to zlake. In
origin order -- a foundation version before any house version cloned
from it, a dataset's snapshots oldest first -- every snapshot a space
should hold and lacks is sent, by one rule found by guid: from the
newest snapshot of the dataset's history whose guid the receiving
space already holds, wherever that is there. A dataset it has gets an
incremental; a dataset new to it is received as a clone of its
snapshot of that guid (`zfs receive -o origin=`), sent from the
owner's snapshot of it wherever a promote has moved it, so a prune
(8.2) coming before the beam or after it makes no difference (probe
P5); a dataset with no such guid, a foundation's first version, gets
a full stream. No send -p; the `shtetl:*` properties are copied after
each receive (ruled 2026-10-11).

Every receive is resumable (lzc_receive_resumable). A send that breaks
off leaves `receive_resume_token` on the receiving dataset, and the
next attempt resumes from it (lzc_send_resume, with the token decoded
to an nvlist by zfs_send_resume_token_to_nvlist). Nothing is destroyed
on a failure. A broken beam is a beam not yet converged (probe P1).

When zlake is on another host, the lake remote names it and the
streams cross the network; it is the same convergence. The beam actor
converges after every snapshot and on a timer, backs off while the
other space cannot be reached, and holds nobody up. A foundation
version's arrival on zrun is what its houses roll on.

Restore is a beam the other way, by hand: a house version and its
foundation version from zlake to zrun.

### 8.2 Prune

Counts, not ages:

- run.keep: versions of each house kept on zrun, the active one
  counted (PROPOSAL: 2, the active and the previous);
- lake.keep: versions of each house kept on zlake (PROPOSAL: 5);
- roll.depth: the saves a roll replays, a count or an age (the
  author: "a date length or count length").

Pruning waits for beaming to be consistent (ruled): nothing leaves a
space while the other space still lacks it. A foundation version is
kept while a house version on the same space is cloned from it, and
its newest is always kept; there is no setting (PROPOSAL). A version
is pruned by promoting any clone of it (the newer version's own
datasets, 7.1) and destroying it. Saves on an active version are not
pruned; they go with the version, and a roll carries roll.depth of
them forward.

The house actor prunes zrun after a roll, the beam actor prunes the
copies it keeps, and the foundation actor prunes a version once its
last clone is gone.


## 9. Actors

| actor | one per | reads when it starts | takes | tells |
|-------|---------|----------------------|-------|-------|
| foundation | foundation recipe | its newest snapshot; written@ since it; `shtetl:stall` | roll, pkg | version, stall |
| house | house | its versions by `shtetl:house`, the newest complete one; the foundation versions on zrun newer than its origin; `shtetl:stall` | roll, save, pkg | snapshot, stall |
| beam | pair of spaces | both spaces' snapshots; resume tokens | converge | arrived |
| builder | host | nothing | a turn | turn over |
| clock | host | the schedules in the shaken recipes and shtetl.conf | nothing | the messages above, on time |
| notifier | host | nothing | stall | the person, through its interface |

Everything else is a process an actor runs and watches: zfs_rebase,
make, git, pkg, jail, a forked lzc_send. A step that takes long, a
build or a rebase, is such a child, and the actor answers messages
while it waits, its state naming the step under way; an actor that did
the step itself would answer nothing for an hour.

A roll found under way when the actor starts is continued or redone
from what ZFS shows (6, 7.2), so no act needs a record of its own.

shtetl is a daemon, started by rc (ruled). The `shtetl` command sends
the daemon a message over a local socket and prints the reply. Every
act a person runs is a message, and so is every scheduled one.


## 10. The runtime: C (ruled)

One process, one kqueue loop. An actor is a struct with its state and
a queue of messages, taking one at a time. Children are watched with
EVFILT_PROC, the command's socket with EVFILT_READ, timers with
EVFILT_TIMER. Nothing restarts the daemon by itself (ruled): what
would kill it would kill it again. Started again by a person, it finds
every actor where ZFS says it stands. ZFS is reached through one layer
over libzfs and libzfs_core, with a fake beside it for the mac, as on
main.

The author on the C (ruled): "we need to be very careful about the
advantage of error checking vs over-verbosity. Clean C doesn't have to
be verbose, but verbose C is painful." CLAUDE.md says what that means
for the code. Allocation, the most verbose of all checks, is not
checked at its calls: shtetl's own few allocators log a failure to
syslog and exit (ruled).

Elixir was looked at and found viable, and set aside for one language.


## 11. What the model removes

| on main | why it existed | why it is not needed |
|---------|----------------|----------------------|
| the staging tree, update.suffix | an update needed a place beside X | every foundation version gives a new version name |
| cutover and abandon, their undo lists, carrying on when run again | the update changed X itself | the swap is last, and a failure destroys what was new |
| an update refused while staging exists | one staging name per house | rolls queue, and the queue is read from ZFS |
| zlake's archive, retention.archive, "archive name taken" | an old tree needed a new name | version names are never reused; old ones are pruned by count |
| config snapshots, the sync before written@, three-word names, shtetl:config, shtetl:patches, shtetl:revision | finding what built a snapshot | the shaken recipe inside every snapshot |
| the patch pause, --continue, --skip, --abort, paused.conf | a patch failing part way through a build | roll back, stall, and start over on a warm obj cache |
| beam's forward-only checks, destroying a half-received beam, "a save whose mirror failed is done" | sends were blocking calls | convergence over resumable receives |
| age retention in date(1) units | pruning by age | counts |
| /usr/local/etc/cron.d/shtetl | schedules | the clock actor |
| S-staging, the one-shot boot, systems' own verbs | systems differed from containers | one house roll; only stop, swap and start differ |
| "builds meeting one under way wait their turn" (a TODO) | concurrent builds | the builder's queue |


## 12. Carried from the first history

Code: the configuration language -- parse, syntax, schema, cascade,
show, err, tree -- with its matrix family C, docs/configuration.md,
docs/ucl-primer.md and the doc checker. The schema loses what the new
model has no use for (update.suffix, retention.archive, age
retention) and gains run.keep, lake.keep, roll.depth and the remotes.
And the ZFS layer's lessons with it: src/fbsd/zfs.c over libzfs, its
fake, and the cells that keep the two in step.

Knowledge, as evidence, probed again where this model leans on it in
a new way:

- the M0 probe results of 2026-10-11 (planning.md 13.1 on main);
- zfs_rebase: one dataset per run; base, from and onto mounted at
  their mountpoints; history replay with --allow-unrelated --base;
  -m; a hold's release force-unmounts that snapshot's .zfs view, so
  nothing is open under it when a rebase ends;
- the foundation install: delete-old before installworld, install -C,
  the kernel through a scratch directory, installworld's backups
  removed; the build root reset by git; one obj cache per foundation;
- libzfs: setting a mountpoint mounts canmount=on datasets; written@
  needs a txg sync first; an incremental receive finds FROM anywhere
  among the target's snapshots;
- the outer space protocol (ruled 2026-10-15);
- jails: jexec, pkg -j, digits-only names refused;
- snapshot names `<object>@<fver>-<fbranch>-<date>-<kind>`, the date
  `2026-10-15T09_30_12Z` (ruled 2026-10-15).

Not carried: everything in the first column of section 11.


## 13. Epics and milestones

| milestone | done when |
|-----------|-----------|
| M0 | the probes have run on the box and the VM, and the plan holds their results |
| M1 | the skeleton: the C tree, its gate and make check; the ZFS layer with its fake; the daemon's loop, an actor that starts from ZFS after a crash, and the command over the socket |
| M2 | shake: the language carried and trimmed, the remotes; a shaken recipe made and injected |
| M3 | foundations: a real build in zlake on the box, a stall and a restart, pkg, the builder |
| M4 | beam and prune: both directions, resume after a cut, the counts, pruning that waits for the beam |
| M5 | containers: create, save, pkg, the roll on a foundation's arrival, the swap, a stall; a real jail rolled across two foundation versions on the box |
| M6 | systems: outer space, boot environments, a roll through a reboot, on a VM |
| M7 | status and syslog, the notifier's interface, the clock |

Epics: design, probes, skeleton, config, foundations, beam, houses,
systems, clock, housekeeping. The issues of M1 to M7 are written with
the M0 ruling.


## 14. M0: the probes

Box scripts on throwaway md pools, like main's M0: `probes/run.sh`, a
shared `lib.sh`, results committed back under
`probes/results/<host>-<time>/`. Each probe's cells, listed in its
header, are its matrix.

- P1 resume. A full send, an incremental and a clone stream between
  two pools, each cut part way and resumed from its token, each ending
  with the source's guids; a fresh receive refused while a token
  stands.
- P2 the active version. Two versions of a container, every dataset
  canmount=noauto; the active one mounted at /containers/H with its
  children, a jail on it; the next one mounted at its own path beside
  it, as during a roll; the swap with the jail stopped; the jail
  started again on the new one.
- P3 boot environments (VM). Two system versions under a systems
  dataset with var and usr/local, all canmount=noauto; bootfs switched;
  each boots with its own children mounted by rc.d/zfsbe and none of
  the other's.
- P4 outer space (VM). The protocol; a service rc starts on the
  memory-disk root snapshots a dataset of the disk pool, sets bootfs
  and reboots; the machine comes up on the new bootfs.
- P5 the house's own data. www cloned onto the next version from the
  final save, promoted, the old version destroyed, through two rolls;
  the lake following by the guid rule before and after each promote.
- P6 the replay. A house's saves replayed onto a new foundation
  version as 7.2 step 3 does it, a conflict answered, the result held
  equal to one rebase of the final save, and the replay timed: the
  house is down for it.

Then the ruling: the probes' results folded into this plan, and the
issues of M1 to M7 written.

### 14.1 Results

Box run 1, KIDON, 2026-10-19, at 68bc7f8 (results 1c23d85): 31 ok, 4
failed. FreeBSD 15.1-RELEASE-p2, zfs-2.4.2.

- P1, 14 of 14: a full stream, an incremental and a clone stream,
  each cut part way, resume from their tokens with the source's
  guids; a fresh incremental is refused while a token stands; a token
  whose source snapshot is gone does not resume, and receive -A takes
  it away. 8.1 stands.
- P5, 10 of 10: the house's own data cloned from the final save,
  promoted, the old version destroyed, through two rolls; the lake
  follows by the guid rule with -o origin= before the promote and
  after it. 7.1 and 8.1 stand.
- P2, 8 ok before the failure: canmount=noauto versions stay
  unmounted through an export and import; setting a noauto dataset's
  mountpoint does not mount it; two versions mount side by side at
  their own paths. P2.5 failed on the probe, not the model: its jail
  was made from the command line, and jail -r undoes mount.devfs only
  for a jail its configuration names. The probe now makes and removes
  the jail from its block (b7c72e3), which is how 7.3 says shtetl
  does it; a rerun is owed.
- P6 did not run: the box's zfs_rebase predates -m. GitHub's main
  (69d8223) has it; a rerun is owed after installing it.
- P3 and P4 wait on a VM.

The harness itself worked end to end: the summary, the cleanup after
P2's failed setup (its devfs mount taken away before the pools), and
the send.


## 15. Verification

- The matrix comes first for each phase (CLAUDE.md). An actor's cells
  are its states crossed with its messages crossed with what it can
  find in ZFS when it starts: fresh, part way through each step, after
  a crash at each step.
- A fake ZFS behind the layer for the mac, checking in the order the
  real layer does, and a box cell for each behavior the fake claims.
- Every act is killed at each of its steps in a test, and the actor
  started again must land in a state the matrix names.
- The box: KIDON for foundations and containers; a VM for systems.


## 16. Risks

- A house is down for the whole of its roll, the replay included; it
  grows with roll.depth and with what the saves changed. P6 times it.
- Systems need zrun on the boot pool, since bootfs is a pool property.
  main had the same constraint.
- Space: each house keeps run.keep versions, and each pins its
  foundation version.
- An active version whose foundation never moves collects saves
  without bound (8.2).
- The probes may show a PROPOSAL wrong; P2 and P5 are the ones most of
  section 7 stands on.


## 17. Out of scope

Several run hosts sharing one lake (nothing in the beam rules it out).
A TUI. Moving a house to another foundation recipe. Dropping a
foundation's datasets. The notifier's channels, until M7.


## 18. Rulings

### 2026-10-18 (the re-architecture)

The author's message that opened this history:

    We need to zoom out and re-architect. I'd like a new branch with a
    new history. You should write a claude guideline doc that includes
    "All lines of code must be justified. Simplicity is good. Edge
    cases are a sign of bad pipelining. Instead of catching all
    edge-cases, write interfaces that make them disappear".

    we fundamentally have a config language which has been pretty
    successfully nailed down. That's working well. We shake a config
    and inject it into the final built object. This is all good.
    Because we now inject the config into the built object, we no
    longer need to take config snaps at all. zconfig is JUST a
    collection of remotes and recipes.

    The existing config layering scheme is good. We can keep the two
    layer defaults & imports over object configs and imports scheme.

    Fundamentally we want much more of an actor model. We may even
    want to rebuild in elixer or something built around this concept.
    (Consider if this is viable)

    Foundations are simple. Each foundation gets its own dataset and
    updates are snapshots on that dataset. Easy, nice, linear.

    Containers/Systems (henceforth collectively known as Houses)
    update packages and saves in linear snapshots. They update
    foundations into new datasets. This can be scheduled in a new,
    simpler, world because we can name each House:
    zrun/[Type]/<Foundation>-<House Name>. As the foundation is moved,
    the dataset namespace is new. Pruning is simply a matter of
    removing no longer desired versions.

    This is better than -staging because it rolls instead of
    requiring resolution before the next update.

    When patches fail to apply cleanly on a foundation it stops
    rolling. when rebases fail to apply cleanly on a house, it stops
    rolling. An alert system to the user could be implemented.

    Beaming is a matter of eventual consistency between zrun and
    zlake. This can be treated as a networking problem, NOT a
    blocking call.

    Keeping a date length or count length of snapshots between house
    rebases onto new foundations is still good. Auto pruning the
    number of kept old versions of houses on zrun and zlake can be
    reduced to a count number and not a date length. We also no
    longer need zlake/archive since we can just have zlake/[House
    type]/[house name] now that house names are UUIDs.

    Systems still require a guarantee of stoppage before updating.
    Containers can do this on a running system by calling stop.
    systems must stop the computer to do their final stage of update.
    This can be scheduled or manual. All updates therefore can be
    scheduled or manual. All houses can be treated homogeneously. The
    only real distinction is whether stop is a jail stop or
    outerspace reboot followed by real reboot (to catch new kernel).

And on the planning:

    Make a new sprint folder, and don't be afraid to work from
    scratch. Borrow project planning ideas from the zfs_rebase local
    sprint json as well as its ruleset in that project's memory. It
    works quite well

So: a second history on branch v2, with CLAUDE.md (the author's
sentence first, then the zfs_rebase project's rules) and this sprint
folder with the zfs_rebase project's tracker format. The configuration
language and its two layers are kept. The shaken recipe goes into
what it built. Foundations are one dataset with linear snapshots.
Houses are containers and systems alike: linear snapshots on a
version, and a new version for each foundation version they roll
onto. A failed patch or rebase stops that one object rolling and
tells the person. Beaming is convergence. Old versions are pruned by
count, and there is no archive. Stop and start are the one difference
between the two kinds of house.

### 2026-10-18 (the fourteen answers)

Asked fourteen questions on the first draft of this plan, the author
answered:

    1. that's still correct. A house can't update until a new
    foundation gets to zrun, THAT's the trigger.
    2. Your intuition is correct. the foundation snapshot name will be
    used in the house dataset name
    3. yes, number one. This is a collection of:  FreeBSD src remote.
    Patch repos. Recipe repos (plural). lake remote (not git, but
    zfs).
    4. zconfig is still a zfs dataset. the user may snapshot. but does
    we do not need to any more.
    5. stack with the shaken recipe. A new foundation build will be
    done if the recipe changes and the user can guide all this. We
    don't need to.
    6. Only one house version of a container is mounted at any given
    time. That's the active one. During an update the mounted dataset
    is swapped. simple.
    7. your intuition is correct. Simply roll back to the latest
    working version of that foundation and mark it stalled until the
    user steps in and restarts it.
    8. If a house is stuck in a conflict. it's stalled. Much like a
    foundation, a stalled house simply doesn't react to new
    foundations coming in. It rolls back to its last working
    foundation and waits there. If a house is mid-build and a new
    foundation comes in, this isn't actually a problem. This is a
    message queue. The house doesn't look for a new update message
    until it's in a state where it's ready to update. In this way,
    houses roll through every foundation beamed down.
    9. There is no such thing.
    10. Yes, shtetl status and syslog. No further detail will be
    fleshed out here until the very end. The notification should be a
    pluggable interface.
    11. yes, pruning must wait for beaming consistency
    12. Yes, shtetl is a daemon now
    13. Ok, we keep it in C, but we need to be very careful about the
    advantage of error checking vs over-verbosity. Clean C doesn't
    have to be verbose, but verbose C is painful.
    14. Then it'll be picked up by the next manual or automatic build.
    What is a switch? what is this? Stop the number of triggers
    ballooning. It's a simple Foundation > House cascade with
    automatic rolls

So: foundations are built in zlake and beamed to zrun, and a
foundation version reaching zrun is what a house rolls on; houses are
written on zrun and beamed to zlake. A house version is named for the
foundation snapshot. The remotes are the FreeBSD src remote, the patch
repositories, the recipe repositories and the lake remote. zconfig
stays a dataset that shtetl never snapshots. A house stacks on the
shaken recipe its foundation version carries. Only the active version
of a house is mounted, and a roll swaps it. A foundation that fails
rolls back to its latest working version and stalls until the person
restarts it; so does a house, and a stalled house takes no new
foundation versions. Rolls queue, and a house rolls through every
foundation version that reaches zrun. There is no switch, no roll
while live and no catch-up: a house's roll is one sequence, from
stop to start. Stalls show in shtetl status and syslog, and the
notifier is a pluggable interface left to the end. Pruning waits for
the beam. shtetl is a daemon, written in C, without verbosity. A
changed house recipe is picked up by the house's next roll.

### 2026-10-19 (three of four more)

Asked four questions that the fourteen answers raised (open-questions.md),
the author answered, with box run 1 pushed:

    probe pushed.

    1. Correct
    2. What does this actually mean?
    3. This will just never happen. If it does, syslog and die. Don't
    reboot because literally why would this happen. in the case it
    does, then it'll just happen again. This is paranoid behavior.
    4. yep.

So: an automatic roll never waits on a person and stalls at a
conflict; a roll started by hand is attended and waits for its person
at the picker (7.4). Allocation failure is logged to syslog and the
daemon exits, and nothing restarts it by itself (10). Each recipe
repository is a directory of zconfig of its own, and one recipe name
in two of them is refused (4). Question 2, whether a house may hold a
foundation version that has arrived, is asked again in plainer words.
