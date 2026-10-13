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
