# Sprint 1: houses that roll

Status: draft for ruling, 2026-10-18. Opened by the author's message
of 2026-10-18 (section 18, in their words): zoom out, re-architect
shtetl as actors, in a new history. Nothing here is built. The
questions that wait on the author are in open-questions.md beside
this file. A point marked PROPOSAL is a decision made where the
author's message is silent and is not yet confirmed.

The tracker is shtetl-sprint-1.json beside this file:

    tools/tracker.py --file sprints/sprint-1/shtetl-sprint-1.json list

It holds the issues of M0 (section 14): the probes, the runtime spike
and the ruling. The issues of M1 to M7 are written when that ruling
lands, since their files depend on it (section 13).


## 1. What shtetl is

shtetl builds FreeBSD from source into foundations and runs houses on
them. A house is a container (a jail) or a system (a bootable root).
The two are handled alike and differ only in how one is stopped and
started.

Everything is ZFS. A foundation is one dataset; each update is a
snapshot of it. A house is a clone of a foundation version; its saves
and package upgrades are snapshots of that clone. Moving a house to a
newer foundation version replays its saves, with zfs_rebase, into a
new dataset beside the old one. The house moves to the new one at its
next switch, which stops and starts it, by schedule or by hand.

Each foundation and each house is an actor. It advances on its own,
stops on its own when something fails, says so, and holds nobody up.
The first history (branch main) built the same things as verbs a
person ran in order; this one lets every object roll.


## 2. Ground rules

CLAUDE.md has the working rules. The design rules every section
below follows:

1. State lives in ZFS. An actor starting reads where it stands from
   ZFS: datasets, snapshots, properties, origins, zfs_rebase's own
   durable record. There are no state files and no records beside the
   data, so a crash and a restart are the same as a first start.
2. Write new, switch once. A roll writes into a new dataset; what
   runs is touched only by a switch that is one atomic act, done
   last. A failure before it drops the new dataset; nothing is put
   back.
3. One owner per dataset. Everything is written on zrun; zlake
   converges to it (PROPOSAL, open question 1).
4. Every act is a message. A schedule is a timer that sends the
   message a person could send by hand, so scheduled and manual are
   one path.
5. Nothing blocks on the network. Beaming is convergence that
   resumes where it stopped.
6. Facts come from their binary source; names are for people.
7. FreeBSD 15.1 is the floor. zfs_rebase is run, not linked.
8. Nothing is copied from main by default. What carries over is
   listed by name in section 12.


## 3. Words

| word | meaning |
|------|---------|
| foundation | FreeBSD built from source by a recipe: one dataset, its versions its snapshots |
| house | a container or a system |
| version | one dataset of a house, cloned from one foundation version; a house has one for each foundation version it has rolled onto |
| live | the version a house runs from |
| roll | for a foundation, build its next version; for a house, replay its saves onto a newer foundation version into a new version |
| ready | a house version rolled and not yet live |
| switch | make the ready version live: stop, replay the final save, snapshot, switch, start |
| stall | a roll or switch that could not finish; the actor stops rolling and tells the person |
| shake | merge a recipe's layers into one whole recipe |
| shaken recipe | the result, injected read-only into what it built |
| beam | zlake converging to zrun |
| owner | the space where a dataset is written |


## 4. The spaces

Four spaces, as on main: each one dataset the user already has,
found by its `shtetl:space` property.

- zconfig: recipes and remotes, nothing else. It is never
  snapshotted (section 5; open questions 3 and 4).
- zbuild: the FreeBSD src checkout.
- zrun: everything that runs, and the owner of every dataset.
- zlake: the copy of zrun that beaming keeps, deeper than zrun keeps
  it, and the obj caches.

    <run root>/
        foundations/
            generic/                            @15.1-releng-<date>-create
                                                @15.1-releng-<date>-update ...
        containers/                             mountpoint=/containers
            nginx -> 15.1-releng-2026-10-18T09_30_12Z-nginx
            15.1-releng-2026-10-11T08_02_40Z-nginx/
            15.1-releng-2026-10-18T09_30_12Z-nginx/
                usr/                            canmount=off placeholder
                    local/
                var/
                www/                            the house's own
        systems/                                the boot environment root
            15.1-releng-2026-10-18T09_30_12Z-kidon/

    <lake root>/
        foundations/generic/                    versions kept by reference
        foundation-builds/generic/              obj cache
        containers/<version>-<house>/           as many as lake.keep
        systems/<version>-<house>/

A house version is named `<foundation version>-<house>`, the author's
`zrun/[Type]/<Foundation>-<House Name>` (open question 2 asks which
string the foundation part is). The name is new for every foundation
version, so it is never taken: there is no staging tree, no archive
tree, and no rename. Old versions stay where they are until pruned by
count. The name is for people; shtetl knows a version's house by
`shtetl:house` and its foundation version by `origin`.

The `nginx` link is how a container's live version is chosen
(section 7.4, PROPOSAL, open question 6). A system's live version is
the pool's bootfs.


## 5. The configuration

The language stays as built on main and as its docs/configuration.md
describes it: UCL, everything stacks, a redefinition is refused,
messages are short. So does the layering, in the author's words "the
two layer defaults & imports over object configs and imports":

1. the default: shtetl.conf, after its imports and its block for the
   recipe's kind;
2. the object: the recipe, after its imports.

A house's recipe stacks on its foundation's: their ver constraints
both hold, pkg lists join, and a command piece the foundation ran is
not run again. PROPOSAL (open question 5): the foundation layer a
house stacks on is the shaken recipe its foundation version carries,
not the foundation's recipe in zconfig as it is today. What a version
was built with is what it is.

Shaking makes the shaken recipe: one UCL document, every value
merged. It is injected into what it built, read-only and schg:

- a foundation version: /var/db/shtetl/foundation.conf, with the src
  commit and the patches applied;
- a house version: /var/db/shtetl/house.conf.

The two names differ so that a house's file and its foundation's
never meet in a rebase: the foundation's changes are only on onto's
side, the house's only on from's. Since every snapshot carries the
recipe it was made from, nothing else has to: no config snapshots, no
`shtetl:config`, `shtetl:patches` or `shtetl:revision`, and zconfig
is never snapshotted.

A jail's tree is not trusted. A jailed root cannot change an schg
file, but it can rename the directory holding it, so nothing the
host does reads a file a jail could have written. What the host
needs from a house's shaken recipe it reads from the version's create
or update snapshot, found by `shtetl:kind`, which shtetl took before
the version ever ran (7.3 step 5).


## 6. Foundations

One dataset per foundation recipe; one snapshot per roll. Linear.

The roll, in order:

1. src: zbuild's checkout reset to the head of the recipe's
   src_branch at its remote.
2. patches, in order from their remotes; a clean three-way merge
   goes on.
3. build: buildworld and buildkernel over the foundation's obj cache.
4. install into the foundation's head: delete-old, installworld, the
   kernel through a scratch directory and install -C, distribution,
   installworld's backups removed.
5. pkg, commands-external, commands.
6. foundation.conf injected.
7. snapshot `F@<ver>-<branch>-<date>-<create|update>`, with
   `shtetl:kind` and `shtetl:commit`.

A failure at any step rolls the dataset back to its newest snapshot,
resets zbuild with git, and stalls the foundation with the step and
the reason (7.5). There is nothing to resume. Once the person has
fixed the cause, `shtetl roll F` starts again at step 1 with zconfig
as it is then, and the obj cache makes that a rebuild of what
changed. This takes the place of main's pause with --continue, --skip
and --abort (open question 7).

A crash part way is not a failure. Starting, the actor finds the head
written since the newest snapshot (written@, after a sync) and rolls
it back, without a stall; the next roll comes from the clock or a
person.

A pkg upgrade is a roll of steps 5 to 7 with only pkg in step 5,
kind pkg.

Rolls share zbuild and the CPU, so foundation actors ask one builder
for a turn (section 9). Two builds never run at once, and nothing
polls.


## 7. Houses

### 7.1 Versions

A house is the set of its versions, each carrying `shtetl:house`.
One is live. Each version's snapshots are linear: the saves its roll
replayed but the last (none for a new house), its create or update
snapshot, which holds the last replayed save, and, once it is live,
its saves and pkg upgrades. A version that is not live takes no
saves of its own.

Datasets a house has and its foundation does not (a recipe's `www`)
are the house's data. On a new version they are clones of the live
version's at the switch's final save (7.3 step 6), and they are
promoted when the old version is pruned (8.2). Probe P5.

### 7.2 The roll

A roll replays onto a new version the live version's saves that the
new version does not have yet, working from the house's recipe shaken
on the target's foundation.conf (section 5). For each dataset that
came from the foundation, parent first, it runs one zfs_rebase a
save, as many saves as roll.depth allows. This is main's history
replay, ruled 2026-10-09: the first step a clone form rebase onto the
target, each later step a dataset form rebase with --allow-unrelated,
--base the previous original save, and its pre-apply snapshot named
for that save; -m always, so a conflict that merges three ways clean
goes on. A canmount=off placeholder is cloned, not replayed.
Derivations: derived files answered keep and regenerated at the gate,
as on main.

Where a roll stands is read from ZFS, never recorded: the new version
exists once step 1 is done, and each later step leaves a snapshot
named for the save before it, so the saves the new version holds are
counted. Run again, a roll does only the steps not done.

A house starts rolling when its foundation has a version newer than
the live version's origin that the house's ver allows, and the house
has no newer version yet (open question 8). It rolls while live,
after each save, up to the newest; a version caught up with the live
version's newest save is ready. Its newest replayed save is not
snapshotted by hand: the switch's replay of the final save takes it
as its pre-apply snapshot, so no name is ever taken twice (probe P6).

The live version runs and saves throughout. A conflict stalls the
roll at zfs_rebase's gate (7.5); the live version is untouched
either way.

### 7.3 The switch

By schedule or by hand, one sequence for both kinds:

1. stop the house (7.4);
2. the final save, on the live version;
3. the roll once more: the final save replayed, so the new version
   holds every save;
4. what the recipe gained since the live version was made: packages
   installed, command pieces that never ran run once (PROPOSAL, open
   question 14);
5. house.conf injected; snapshot
   `<version>@<fver>-<fbranch>-<date>-update`, the snapshot the host
   reads the house's recipe from, taken before the version ever ran;
6. the house's own datasets cloned from the final save;
7. switch: the link or bootfs names the new version, one atomic act;
8. start the house: whatever is live.

The house is down from step 1 to step 8: usually one replayed save
and two snapshots, and packages only when the recipe gained some.

A failure in steps 2 to 6 leaves step 7 undone, so step 8 starts the
version that was live, and the house stalls (open question 9). Steps
2 to 6 write only into the new version and a snapshot of the live
one, so nothing needs putting back. Step 8 runs on every way out.

After the switch: the prune (8.2), then the next roll if a newer
foundation version is there.

### 7.4 Stop and start: the one difference

|       | container | system |
|-------|-----------|--------|
| stop  | jail -r | outer space: reboot -r into a root on a memory disk |
| live  | the link /containers/H | the pool's bootfs |
| start | jail -c, through the link | reboot |

Containers (PROPOSAL, probe P2). The jail's path is /containers/H,
the link. jail(2) looks the path up with FOLLOW when the jail is
created and holds the root vnode (sys/kern/kern_jail.c), so the link
switched under a running jail changes nothing until it is next
started. At each start the host writes /etc/jail.conf.d/H.conf whole
from the live version's shaken recipe, read from its create or update
snapshot (section 5), and never reads it back. So the switch is the link
alone; the jail block follows it at the next start.

Systems (PROPOSAL, probes P3, P4). A system's versions are boot
environments under zrun/systems: each version's root has
`mountpoint=/` and `canmount=noauto`, and rc.d/zfsbe mounts the booted
one's children. The switch is `zpool set bootfs`, and the loader can
boot any kept version by hand. Stop is the outer space protocol
(ruled 2026-10-15): a memory disk made a pool, the system's current
root sent into it, `vfs.root.mountfrom` pointed at it, `reboot -r`.
shtetl starts again there from rc, finds itself on that pool, and
does steps 2 to 5 of the switch for that system and nothing else: in
outer space the system is stopped, so none of its containers start.
Start is a real reboot, which loads the new kernel. A failed switch
reboots into the bootfs it did not change.

### 7.5 Stalls

A stall is a state of the actor, kept in ZFS like every other, and a
stalled object rolls again only when a person says so. There are two
kinds:

- A conflict. A house's roll, the switch's replay of the final save
  included, stops at
  zfs_rebase's conflicts gate, and the open run is the stall:
  zfs_rebase is durable, so it is still there after any restart.
  `shtetl resolve H` opens the picker (zfs_rebase -c -i), and the
  roll goes on from the gate. There is no abort: every conflict can
  be answered (from, onto, or a merge), so a roll ends by its
  answers. Meanwhile the house runs on its live version.
- Any other failure. What was new is dropped (a foundation's head
  rolled back, a house's new version destroyed), and `shtetl:stall`
  is set to the step and the reason: on the foundation's dataset, or
  on the house's live version. While it is set, the object rolls
  neither on its schedule nor when its foundation moves. `shtetl
  roll` clears it and rolls.

Without the property, a house whose roll failed would find its
foundation still newer and roll straight into the same failure.

Every stall goes to the notifier (section 9, open question 10).


## 8. Beam and prune

### 8.1 Beam

zlake converges to zrun. In origin order -- a foundation version
before any house version cloned from it, a dataset's snapshots oldest
first -- every snapshot the lake should hold and lacks is sent, by one
rule found by guid: from the newest snapshot of the dataset's history
whose guid the lake already holds, wherever that is on the lake. A
dataset the lake has gets an incremental; a dataset new to the lake is
received as a clone of the lake's snapshot of that guid (`zfs receive
-o origin=`), sent from the run's snapshot of it wherever a promote
has moved it, so the run's prune (8.2) coming before the beam or after
it makes no difference (probe P5); a dataset with no such guid, a
foundation's first version, gets a full stream. No send -p; the
`shtetl:*` properties are copied after each receive (ruled
2026-10-11).

Every receive is resumable (lzc_receive_resumable). A send that
breaks off leaves `receive_resume_token` on the lake's dataset, and
the next attempt resumes from it (lzc_send_resume, with the token
decoded to an nvlist by zfs_send_resume_token_to_nvlist). Nothing is
destroyed on a failure. A broken beam is a beam not yet converged.

The beam actor converges after every snapshot event and on a timer,
backs off while the lake cannot be reached, and holds nobody up. The
one actor that waits for it is a prune on zrun (8.2, open question
11).

Restore is a beam the other way, zlake to zrun, of one house version
and its foundation version, by hand.

### 8.2 Prune

Counts, as the author asked, not ages:

- run.keep: versions of each house kept on zrun, the live one counted
  (PROPOSAL: 2, the live and the previous);
- lake.keep: versions of each house kept on zlake (PROPOSAL: 5);
- roll.depth: the saves a roll replays, a count or an age (the
  author: "a date length or count length").

A foundation version is kept while a house version on the same space
is cloned from it, and its newest is always kept; there is no setting
(PROPOSAL). A version is pruned by promoting any clone of it (the
newer version's own datasets, 7.1) and destroying it. Saves on a
live version are not pruned; they go with the version, and a roll
carries roll.depth of them forward.

The house actor prunes zrun after a switch, the beam actor prunes
zlake after converging, and the foundation actor prunes a version
once its last clone is gone.


## 9. Actors

| actor | one per | reads when it starts | takes | tells |
|-------|---------|----------------------|-------|-------|
| foundation | foundation recipe | its newest snapshot; written@ since it; `shtetl:stall` | roll, pkg | version, stall |
| house | house | its versions by `shtetl:house`; the live one (link or bootfs); zfs_rebase runs open on them; `shtetl:stall` | roll, switch, save, pkg, resolve | snapshot, switched, stall |
| beam | lake | both spaces' snapshots; resume tokens | converge | on-lake |
| builder | host | nothing | a turn | turn over |
| clock | host | the schedules in the shaken recipes and shtetl.conf | nothing | the messages above, on time |
| notifier | host | nothing | stall | the person |

Everything else is a process an actor runs and watches: zfs_rebase,
make, git, pkg, jail. ZFS itself is reached through one boundary
(section 10).

A roll found under way when the actor starts is continued: a
zfs_rebase run open at its conflicts gate is a stall (7.5), one open
anywhere else is taken on with --continue, and otherwise the roll
replays what the new version is missing (7.2). A crash loses nothing
a roll did, and no act needs a record of its own.

shtetl runs as a daemon, started by rc. The `shtetl` command sends
the daemon a message over a local socket and prints the reply. Every
act a person runs is a message, and so is every scheduled one (open
question 12).


## 10. The runtime: Elixir or C

The author asked whether to rebuild in Elixir or something else built
around actors.

**Viable: yes.** What Erlang/OTP gives, with none of our code:

- gen_statem: one actor per foundation and house, with rolling,
  ready, stalled and switching as explicit states;
- supervisors: an actor that crashes is started again and reads ZFS,
  which is ground rule 1 under OTP's own name ("let it crash");
- ports and monitors: a child process is watched, and its exit
  arrives as a message;
- timers: the clock with no cron file;
- distribution: when the lake is another host, the beam's control
  messages cross over Erlang distribution (TLS), and the data as a
  zfs send stream over a socket;
- releases: `mix release` bundles the runtime into one directory, so
  a host needs no Erlang package. lang/erlang and lang/elixir are in
  ports, and Erlang has run on FreeBSD in production at large scale
  (WhatsApp).

**The boundary.** libzfs, libzfs_core, libucl and nvlists are C. They
are reached through ports: separate processes speaking Erlang's
external term format through erl_interface (ei, shipped with
Erlang). Binary both ways, so no text is parsed; a crash in the C
cannot take the VM down; a long call does not stall a scheduler; and
each port is itself an actor. No NIFs. The C on main lives on as
these ports: src/fbsd/zfs.c's libzfs calls, and the configuration
language over libucl.

**The costs.**

- Two languages, two toolchains, two test suites. The author's C
  style and the cstyle gate do not reach Elixir; `mix format` and
  warnings-as-errors would be its gate.
- The runtime's size, tens of megabytes (to be measured), on every
  host and in outer space's memory disk.
- A daemon and a socket where main had a command and cron.
- The release is native and built on FreeBSD; the ports trail
  upstream releases.

**The same actors in C.** One kqueue loop in one process. An actor
is a struct with a state and a queue. Children are watched with
EVFILT_PROC, timers are EVFILT_TIMER, and the socket is EVFILT_READ.
Supervision is the whole process, restarted by daemon(8) -r, with
every actor read back from ZFS. That means one language and one
gate, and main's C reused in place. It gives no distribution and no
per-actor restart, and the mailbox, timer and supervision code is
ours to write and test.

**Recommendation.** Actors either way; the design in sections 4 to 9
does not change with the language. Choose the language after the
spike (section 14, S1 and S2). I lean to Elixir for the coordinator.
Most of what shtetl does is coordinating long-lived objects that fail
independently and talk to a peer over a network, which is what OTP
was built for, and the work that must be C stays C. If the author
would rather not read and review Elixir, the C reactor is a sound
second. Elixir over Erlang for mix, ExUnit and releases; it is the
same VM.


## 11. What the new model removes

| on main | why it existed | why it is not needed |
|---------|----------------|----------------------|
| the staging tree, update.suffix | an update needed a place beside X | every foundation version gives a new version name |
| cutover and abandon, their undo lists, carrying on when run again | the switch changed X itself | the switch is one atomic act, and a failure starts the live version |
| an update refused while staging exists | one staging name per house | one roll in flight is the actor's state |
| zlake's archive, retention.archive, "archive name taken" | an old tree needed a new name | version names are never reused; old ones are pruned by count |
| config snapshots, the sync before written@, three-word names, shtetl:config, shtetl:patches, shtetl:revision | finding what built a snapshot | the shaken recipe inside every snapshot |
| the patch pause, --continue, --skip, --abort, paused.conf | a patch failing part way through a build | roll back, stall, and start over on a warm obj cache |
| beam's forward-only checks, destroying a half-received beam, "a save whose mirror failed is done" | sends were blocking calls | convergence over resumable receives |
| age retention in date(1) units | pruning by age | counts |
| /usr/local/etc/cron.d/shtetl | schedules | the clock actor |
| S-staging, the one-shot boot, systems' own verbs | systems differed from containers | one house pipeline; only stop and start differ |
| "builds meeting one under way wait their turn" (a TODO) | concurrent builds | the builder's queue |


## 12. Carried from the first history

Code: the configuration language only -- parse, syntax, schema,
cascade, show, err, tree -- with its matrix family C,
docs/configuration.md, docs/ucl-primer.md and the doc checker. The
schema loses what the new model has no use for (update.suffix,
retention.archive, age retention) and gains run.keep, lake.keep and
roll.depth. Whether it is carried as a port or as the core follows
section 10.

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
| M0 | the probes and the spike have run on the box, open-questions.md is answered, and the runtime is chosen |
| M1 | the skeleton: the runtime's tree, its gate and make check; the ZFS boundary with its fake; the daemon, an actor that starts from ZFS after a crash, and the command's socket |
| M2 | shake: the language carried and trimmed; a shaken recipe made and injected |
| M3 | foundations: a real roll on the box, a stall and a retry, pkg, the builder |
| M4 | containers: create, save, pkg, roll, switch, stall and resolve; a real jail rolled across two foundation versions on the box |
| M5 | beam and prune: convergence, resume after a cut, the counts, a restore as a test |
| M6 | systems: outer space, boot environments, a switch through a reboot, on a VM |
| M7 | the clock and the notifier |

Epics: design, probes, spike, skeleton, config, foundations, houses,
beam, systems, clock, housekeeping. The issues of M1 to M7 are
written at the M0 ruling.


## 14. M0: the probes and the spike

The probes are box scripts on throwaway md pools, like main's M0:
`probes/run.sh`, a shared `lib.sh`, results committed back under
`probes/results/<host>-<time>/`. Each probe's checks below are its
matrix.

- P1 resume. zrun to zlake across two pools: a full send, an
  incremental and a clone stream, each cut part way and resumed from
  its token, each ending with the source's guids. A dataset holding a
  token refuses a fresh receive, so "resume if there is a token, else
  send" is the whole rule.
- P2 the live link. A jail created through /containers/H pointing at
  v1; the link switched to v2 while it runs; the jail still on v1 (a
  file written inside lands in v1, jls shows v1's path); jail -r, then
  jail -c lands in v2; devfs mounted under the resolved path and
  unmounted by jail -r.
- P3 boot environments (VM). Two system versions under zrun/systems
  with var and usr/local, roots canmount=noauto; bootfs switched; each
  boots with its own children mounted by rc.d/zfsbe and none of the
  other's; the loader's menu lists both.
- P4 outer space (VM). The protocol; a service that rc starts on the
  memory-disk root snapshots a dataset of the disk pool, sets bootfs
  and reboots; the machine comes up on the new bootfs.
- P5 the house's own data. v1/www with saves; v2/www cloned from
  v1/www@final; v2/www promoted; v1 destroyed; v2/www holds v1's
  saves; the lake's copy converges before and after the promote.
- P6 a roll while live. A house's saves replayed into a new version
  while the live version takes one more save; that save caught up;
  the tree equals a replay of all of them done while stopped. Needs
  zfs_rebase on the box.

The spike:

- S1 an Elixir release built on FreeBSD 15.1: a supervisor, and a
  gen_statem that rolls a toy foundation through a C port speaking
  the external term format over libzfs_core (snapshot, clone, send
  and receive between two md pools); killed part way through and
  started again, it finds where it stood from ZFS. Measured: the
  release's size, its resident memory idle, its start time.
- S2 the S1 release started by rc on P4's memory-disk root, finding
  its state there.

Then the ruling: the author's answers to open-questions.md and the
choice of runtime, folded into this plan, and the M1 to M7 issues
written.


## 15. Verification

- The matrix comes first for each phase (CLAUDE.md). An actor's cells
  are its states crossed with its messages crossed with what it can
  find in ZFS when it starts: fresh, part way through each step, after
  a crash at each step.
- A fake ZFS behind the boundary for the mac, checking in the order
  the real layer does, and a box cell for each behavior the fake
  claims.
- Every act is killed at each of its steps in a test, and the actor
  started again must land in a state the matrix names.
- The box: KIDON for foundations and containers; a VM for systems.


## 16. Risks

- Systems need zrun on the boot pool, since bootfs is a pool
  property. main had the same constraint.
- Space: each house keeps run.keep versions, and each pins its
  foundation version.
- A live version whose foundation never moves collects saves without
  bound (section 8.2).
- Two languages, if Elixir is chosen (section 10).
- With a remote lake, Erlang distribution needs TLS and a guarded
  cookie, or the beam runs over ssh.
- The probes may show a PROPOSAL wrong; P2 and P5 are the ones most
  of section 7 stands on.


## 17. Out of scope

Several run hosts sharing one lake (nothing in the beam rules it out).
A TUI. Moving a house to another foundation recipe. Dropping a
foundation's datasets.


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
what it built, and zconfig is never snapshotted. Foundations are one
dataset with linear snapshots. Houses are containers and systems
alike: linear snapshots on a version, and a new version for each
foundation version they roll onto. A failed patch or rebase stops
that one object rolling and tells the person. Beaming is convergence.
Old versions are pruned by count, and there is no archive. Stop and
start are the one difference between the two kinds of house. Every
update can be scheduled or run by hand. Whether to build it in Elixir
is assessed in section 10, and the choice waits on the spike.
