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
