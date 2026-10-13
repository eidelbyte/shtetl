# M3: foundations (worklog)

2026-10-12, branch impl/m3. planning.md 12's M3: foundation build and
update into zlake, patches and the pause included; pkg-upgrade. Cut
into the tracker issues m3-version, m3-zfs, m3-build, m3-patches,
m3-update and m3-pkg-upgrade (tracker/shtetl.json, f118120).

## m3-version (ac8101a)

A version's name (6.1) and the ver check (7.1 step 1), in
src/core/version.c, portable, over values the build will hand in: the
recipe's branch, the checkout's REVISION, the build's time and the
checkout's commit.

- **The branch is a spelling now.** src_branch was any string; 6.1's
  names need main, stable/N or releng/N.M, so the schema refuses
  anything else when the recipe is read (sh_syn_branch in syntax.c,
  with the other spellings).
- **REVISION from newvers.sh -V.** main's major is in the checkout's
  sys/conf/newvers.sh. Asking it (-V REVISION, its documented way)
  rather than reading the shell file; its one line, REVISION="N.M",
  is the only output taken, anything else refused.
- **Two checks.** Before the checkout only the branch is known:
  releng/N.M must satisfy every ver, stable/N leave some N.x they
  all allow (Helly in one dimension: the constraints already meet,
  so each meeting N.x is enough), main passes. After the checkout
  REVISION is checked exactly; stable/15's REVISION may be 15.3.
- **The day is UTC.** gmtime_r, whatever TZ says; the test moves TZ
  to UTC+14 and UTC-11 around midnight.
- **The counter** compares whole names: the -pkg twin and another
  day's name do not count. Names are read only for this; versions are
  ordered by createtxg (6.1).

Verification: check_version, 97 checks (V1 - V8); check_schema for
CK23; make check, gate, xcheck.

Gotchas for the build: sh_version_name wants the full commit (40 hex)
or at least 12 hex digits, lowercase as git prints them; the build
asks git for HEAD's full sha.

## m3-zfs (901975c)

What a build asks of ZFS beyond M2, in src/core/zfs.h, with libzfs,
the stub and the fake each answering it.

- **One call for several snapshots.** sh_zfs_snapshot took one name;
  a version is a snapshot of F and every child at once, so it now
  takes a list, one lzc_snapshot: all of them with their properties,
  or none. Bootstrap's one snapshot passes a list of one.
- **Snapshots are read by name with an @.** sh_zfs_get and
  sh_zfs_set open a snapshot when the name has one (zz_open picks
  ZFS_TYPE_SNAPSHOT); a snapshot's user properties are its own or its
  filesystem's, inherited, as ZFS reports them.
- **Rollback only to the newest** (lzc_rollback_to). The patches'
  rollback to @shtetl-patch-0 destroys the later patch snapshots
  first, newest first, then rolls back: no recursive rollback hidden
  in a call.
- **Destroy is one dataset.** A snapshot, or a filesystem with no
  children and no snapshots; a tree is the caller's walk, children
  first.
- **The fake answers as ZFS does**: snapshots are records with
  properties, every snapshot of one call shares a createtxg, guids
  are unique, rollback refuses an older snapshot and is remembered
  (zfake_last_rollback) for the tests to see.

Verification: check_zfs, 93 checks (Z1 - Z10), make check, gate,
xcheck. Deferred: Z11, the cells against libzfs on real pools, with
the M3 box run. tracker: m3-zfs testing until then.

Gotcha, mine, twice now: a command chain that runs the gate and then
commits with `;` commits whatever the gate says. 901975c was amended
before the push for a long line. Chain the commit behind
`grep -q 'gate: clean' &&`.

## m3-build (fb5b00f, the plan; 613f066, the run)

shtetl build F for a new foundation (7.1, without patches), in
src/core/build.c: a plan made and checked in full, shown, confirmed,
then run with every change recorded for undoing.

- **The plan checks everything first**: the target, the lake, build
  and config roots, the recipe, src_branch against ver, the build
  root mounted and free, no F yet. F's datasets come out parents
  first, a placeholder for each parent the recipe does not name.
- **The recipe is read where the config root is mounted**, the
  directory the build snapshots and records, so the record is what
  was built from; -C may name only that directory.
- **The undo list.** Each step that changes something pushes how to
  undo it; on success the steps undo what they must (mounts,
  mountpoints, the mark) and forget what only a failure undoes (F's
  datasets). A failure anywhere unwinds all of it, newest first. The
  config snapshot, the obj cache (a partial build is worth keeping)
  and the log stay.
- **Mounting by mountpoint**: the obj cache and F get mountpoints
  below the run directory for the build and inherit them back after;
  F's datasets follow by inheritance, placeholders are not mounted.
- **make's environment**: MAKEOBJDIRPREFIX is read from the
  environment only, so make gets PATH and it, nothing else.
- **The choices marked PROPOSAL** are in planning.md 7.1's "As built":
  the config snapshot's name and the two properties recording it, the
  build mark, the mount paths, the kernel config's ident, pkg -c with
  devfs and the host's resolv.conf while pkg runs.

Verification: check_build, 246 checks (F1 - F17); run-cli.sh for F18;
the run over scripts standing in for git, make, pkg, chroot, mount
and umount, a real newvers.sh in a fake checkout. make check, gate,
xcheck. Not yet: a real build on the box (tracker: m3-build testing).

Gotcha: tests/check.h's scratch_path returns one static buffer. Two
calls in one argument list give the same path twice; copy each before
the next. It cost the run's test a long look.

## Box prep, box run 1 (1102eb9 - 3865eef; 3b50a35, e600e2f)

Not a tracker issue of its own: the box side of m3-zfs and m3-build.

- **Z11 (1102eb9).** tests/box/zcheck.c runs family Z's cells
  against libzfs, under a dataset run.sh makes for it ($P1/zc) and
  destroys after. Built by make freebsd as build/box_zcheck.
- **The altroot (80c587d).** Writing build.sh, which imports its pool
  with -R, showed DESTDIR and MAKEOBJDIRPREFIX taking the mountpoint
  as set, not where it is mounted below the altroot. The build now
  reads each mount's path back after mounting (fb_mountat).
- **tests/box/build.sh (375ff96).** A real build on a throwaway pool
  on a sparse file, imported with an altroot: F13, undo on a failed
  fetch, then F11, F12, F16, F17 and F19 after a real build, F left
  mounted at /look for a look. --clean takes it all away.
- **The link (3865eef).** The first make freebsd on the box failed to
  link box_zcheck: err.o needs tree.o, and nothing on the mac had
  linked zcheck.c. make check now links it against the stub.

Box run 1 (run.sh): 77 ok, 2 failed.

- **Z8, the cell wrong (3b50a35).** It inherited f/usr's mountpoint
  with nothing above set, so ZFS rightly said default; check_zfs.c
  sets f's first. zcheck now does too. Z11's failure was the exit.
- **The refusals, in errno words (e600e2f).** Every refusal cell
  passed, but zcheck only counted messages, and src/fbsd/zfs.c said
  strerror: "File exists", "Device busy", and Z2's named f@v3 when
  f/usr@v2 was the one taken. Read against the fake, the fake was
  wrong twice: it refused a destroy for children before snapshots,
  and ZFS looks at snapshots first; and it refused destroying a
  missing snapshot, which lzc_destroy_snaps counts as destroyed. The
  libzfs layer now says the fake's words, from the errno where it is
  unambiguous and from the pool where not (rollback's EEXIST is a
  bookmark too, destroy's EBUSY a held dataset too); a missing
  snapshot is refused before the call. zcheck reads each refusal by
  its words and gained the fake's refusals it lacked.

Decision, beyond the plan: **make check runs zcheck's cells over the
fake** (tests/box/zcheck-fake.c, zcheck.c's main built as
zcheck_main), 38 cells. Run 1's zcheck fails exactly its Z8 cell
there; a box cell that disagrees with the fake shows on the mac, and
the box then only says whether libzfs agrees with both.

Verification: make check from clean (check_zfs 101 checks, zcheck
over the fake 38 cells), gate, xcheck. Pending: box run 2 of run.sh,
and the first run of build.sh. tracker: m3-zfs and m3-build testing.

Gotcha: a box cell that only counts refusals passes on any words.
Read refusals by their words, the name included.

## Box run 2, box build 1 (f0a6f76, 90f58e7)

Box run 2 (run.sh at c46e87c): 89 ok, 0 failed, the refusals by their
words included.

Box build 1 (build.sh): F13 passed; F11, the real build, failed after
the fetch: "foundation-builds/box: mountpoint or dataset is busy".

- **A mountpoint mounts (f0a6f76).** fb_mountat sets the obj cache's
  mountpoint and mounts it. libzfs's zfs_prop_set runs a changelist
  whose postfix mounts every dataset taking that mountpoint whose
  canmount is on, mounted before or not (libzfs_changelist.c), and
  zfs_mount then fails EBUSY on a mounted one. The fake never mounted
  on a set, so check_build passed. Now the fake moves mounts on a set
  or inherit of mountpoint as the changelist does, and sh_zfs_mount,
  both sides, does nothing for a dataset mounted at its mountpoint
  and refuses one mounted elsewhere, with no mountpoint, or canmount
  off (zfs_mount passed over those two saying nothing). With the old
  mount, check_build's run fails on the mac as the box's did.
- **Decision: keep libzfs's moves on set and inherit** rather than
  zfs set -u's: bootstrap's mountpoint=none on the lake and run roots
  relies on it to unmount them, and inherit has no -u. The build's
  undo inherits after it unmounts, and the lake's mountpoint is none
  (4.1), so nothing is mounted again; a lake with a real mountpoint
  would see F mounted there after a build.
- **The logs come back by themselves (90f58e7)**, at the user's ask:
  tests/box/send.sh commits and pushes each run's log as the person
  who ran sudo, through su; a push that cannot go is said.

New cells Z12, Z13 (check_zfs 128 checks; zcheck 44 cells over the
fake). Verification: make check from clean, gate, xcheck, box_send
against a scratch remote. Pending: box run 3 (Z12, Z13 on libzfs)
and box build 2.

Gotcha: a fake that is simpler than libzfs hides exactly the bugs a
box finds late. Where libzfs does something on the side (mounts on a
set), the fake has to as well.

## Box run 3, box build 2 (b12f0c2)

Both green at dc2be99, the logs sent back by the scripts themselves
(2f0e789, b12f0c2, committed on the box as its user). run.sh: 95 ok,
Z12 and Z13 on libzfs among them. build.sh: 26 ok, a real releng/15.1
buildworld, buildkernel and install into
shbbuild/shtetl/foundations/box@15.1-releng-2026-10-13-e38a7085f3a8,
with the undo, the version's properties and F19's altroot checked.
tracker: m3-zfs and m3-build tested.

## m3-patches (78d5fbd, 4030885, 46a565d, 8d8645b)

A foundation's patches and the pause (planning.md 7.1.1), in four
commits: the pieces alone (src/core/patch.c, family P written first),
the patches applied in the build, the pause with its three verbs, and
the box's P17.

- **The pieces** (78d5fbd): natural order; a source as git fetches
  it; a directory's .patch files; the files a patch touches, from git
  apply --numstat -z; what a fix leaves; the pause record.
- **Applied** (4030885): the fetch into `<run dir>/patches/<n>`, the
  list fixed there; @shtetl-patch-0 after the checkout and one before
  each patch; applied, or already in when its reverse applies; the
  build root rolled back at the end, success or failure, after the
  version is taken; shtetl:patches on the version.
- **The pause** (46a565d): the tree staged and written as the fix's
  base; --3way, or --reject where that neither merged nor left a
  marker; paused.conf, exit 3. --continue and --skip read the record
  and the recipe from the config snapshot's .zfs/snapshot directory;
  --continue checks for markers and .rej files and writes the fix as
  a git diff; --skip rolls back to the patch's snapshot; --abort
  walks F's datasets to destroy them, needing no recipe.
- **The box** (8d8645b): patches made on the box from the branch's
  own share/misc/bsd-family-tree, so they always fit what is built.

Decisions beyond the plan, marked PROPOSAL in 7.1.1's "As built": a
merge without conflict pauses too (nothing merges unseen); a conflict
marker is seven <, | or > and a space or the line's end, not =======
alone; the record is UCL; exit 3 for a pause; the shtetl:patches
format. Divergence: --abort keeps the log, as every build does since
7.1's as-built, where the plan said the run directory goes.

Also decided in the code: a resume that fails taking the pause up
(the config snapshot gone, the fix not written) leaves it paused, so
the person's work in the tree is never undone by an error of ours;
only a failure after the patches undoes the run.

Verification: check_patch 55 checks; check_build 489, family P over
real git with file:// sources, a branch, a tag and a commit, and the
fake ZFS's snapshots of the build root carried out on the scratch
tree by zfake_hook; mutations seen to fail (the resume reading the
live root instead of the snapshot); run-cli.sh P18; make check, gate,
xcheck. P17 rehearsed on the mac against the real file. Pending: the
box (P17 inside build.sh's real build). tracker: m3-patches testing.

Gotchas: patchworld() makes the patch repository anew each time, so
its commits change: read a commit after making it, not before. git
apply --3way implies --index, which refuses a file whose working copy
differs from the index; earlier patches are applied to the working
tree only, so the tree is staged before the merge. A test that builds
twice in one call (a debug line, say) meets "foundation exists".

## Box build: a kept build root and obj cache (d80d842)

Not a tracker issue: test-harness work, at the user's ask, so the box
build goes faster after the first.

tests/box/build.sh destroyed its pool at the end, the build root and
box's obj cache with it, so each run was hours. Keeping the obj cache
alone would not have helped: make goes by the files' times, and the
build root was a fresh clone of SRC each run. Now the pool's file
stays, exported between runs, with a store, shbbuild/keep: zbuild and
box's obj cache per branch. A run renames them into the layout it
bootstraps and back; the rest is made anew. CLEAN is off by default
since FreeBSD 14 (checked on releng/15.1 and stable/15), so the next
build of the branch is incremental. shtetl needs no change: an obj
cache that stays is 4.6's design.

Gotcha: bootstrap refuses a dataset that is already a root, so the
put-away takes shtetl:space off the build root before it goes back
to the store. A build stopped among its patches leaves the tree
patched: the put-away rolls it back to @shtetl-patch-0 first.

Verification: sh -n, gate; the box's next run is the first with the
store, and builds whole; the one after shows the gain.

## Box build 3 (461df60)

At 5802782: 42 ok, 0 failed. P17 for real: the build paused at
3-two.patch with a conflict marker (exit 3), 2-in already in from the
file:// git source, 1-one applied; --continue refused while the
marker was left; resolved by the script, --continue built world and
kernel in 30 minutes, and F's /usr/share/misc/bsd-family-tree has
both lines, so the patched tree is what was built. shtetl:patches,
the fix, the rollback and the record's end all as planned. tracker:
m3-patches tested.

The kept store (d80d842) was not in this run: the box had not pulled
it, the log's header says 5802782. Its first run builds whole; the
one after shows the gain.

## m3-update (09013bc, c621e73)

shtetl build F with F there and a version: its next version into the
same datasets (planning.md 7.2).

- The plan: an update of @<last>, each dataset kept; the recipe's
  datasets F's or refused by name; F with no version refused; a
  securelevel above 0 refused, read through the tools table so the
  tests can say one (sh_build_plan takes the tools now).
- The run: no dataset made; delete-old and delete-old-libs between
  the build and the installs; installworld's backups removed after,
  their flags cleared (fb_rmtree now clears every file's flags before
  removing it).
- Decision beyond the plan (PROPOSAL): a failed update, and --abort
  of a paused one, roll F back to its last version, never destroy it.

Found by the tests, fixed in the same commit: ld-elf.so.1.old, prefix
and suffix with nothing between, was not matched as a backup, so
every update would have kept it; and two builds in one second took
the same config snapshot name, failing the second (a quick retry after
a quick failure): now -2, -3.

Verification: check_build 575 checks (F20 - F26; the backups' flags
the user's immutable one, which an owner may clear on the mac), make
check, gate, xcheck. Pending: F27 on the box, box built again over
the first in build.sh, schg cleared for real. tracker: m3-update
testing.

Gotcha: a test's fixed time makes every build the same second; the
config snapshot's suffix keeps them apart, and the version's .2 does
the same for a second version on one day.

## m3-pkg-upgrade (4359f03, 28a185d)

shtetl pkg-upgrade F (planning.md 7.2, 6.1): from F's last snapshot,
pkg -c <root> upgrade -y in a chroot of F, and the version's -pkg
(-pkg.2 and on) with shtetl:kind=pkg.

- It takes the build root's mark, so a pkg upgrade and a build never
  overlap, and refuses while one is under way or paused.
- The -pkg carries its version's shtetl:config, config-guid and
  patches over (PROPOSAL), so it says what it was made from; its
  commit message is -m's or "pkg upgrade of <version>".
- A failure rolls F back to its last snapshot, as an update's does.
- fb_chroot (devfs, resolv.conf) is shared with the build's pkg and
  commands now; fb_snapshot takes the kind.

Verification: check_build 628 checks (F28 - F31), run-cli.sh F32,
make check, gate, xcheck. Pending: F33 on the box, after F27.
tracker: m3-pkg-upgrade testing.

Asked of the user: planning.md's rulings say "Commands never rerun
on update", which reads as the containers' (7.6 rebases them); 7.2
has a foundation's update repeat 7.1's steps 3 - 8, pkg and commands
among them, and that is how it was built. Ruled 2026-10-13: a
foundation's update runs its commands again, the install having put
back what they changed (planning.md 14); check_build and the box's
F27 hold it.

## Box build 4 (63df125)

At 97d62c7: 58 ok, 0 failed. The kept store's first run (a new pool,
no obj cache: the first build whole, 30 minutes); P17 again; F27, the
update over the first build's obj cache, in 1 minute; F33, pkg-upgrade,
its -pkg on every dataset. tracker: m3-pkg-upgrade tested.

F27 passed without proving what it meant to: the update rebuilt the
same commit, and with install -C nothing differed, so installworld
made no backup and "no backup left" held of nothing. Read in
releng/15.1: ld-elf.so.1 is installed -C -b and init -b -B.bak, so
their backups come with a changed binary; and installkernel removes
a DESTDIR's /boot/kernel outright, keeping kernel.old only over the
running kernel's directory (sys/conf/kern.post.mk). So a foundation
never has a kernel.old, and every update writes all of /boot/kernel
anew, install -C or not (planning.md 7.2 as built; put to the user).

F27 now patches init and rtld (a __used string appended to each,
made on the box from the branch's files and rehearsed on the mac), so
installworld backs them up and the checks see shtetl remove them,
schg cleared as root; and the changed init is checked in F. Also: the
look's "cannot mount ... already mounted", zfs set mountpoint having
mounted them, is gone (lookat), and the loops no longer reuse P17's
variable. m3-update stays testing until that runs.
