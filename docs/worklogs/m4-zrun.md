# M4: zrun (worklog)

Beam-down, new containers, saves with messages, the zlake mirror
(planning.md 7.3 - 7.5, 6.2, 7.10). Tracker: tracker/shtetl.json,
m4-zfs, m4-beam, m4-save, m4-create.

## m4-zfs

The ZFS calls M4 needs, in src/core/zfs.h, over libzfs and
libzfs_core, no zfs(8), with the fake (tests/zfake.c) defining their
words and the box holding libzfs to them.

- sh_zfs_clone: a clone of a snapshot, properties from its birth.
  Over libzfs's zfs_clone rather than lzc_clone: the kernel takes a
  native property like compression only in its own type, which
  zfs_valid_proplist converts to and lzc_clone does not.
- sh_zfs_send: one call for a whole send, an incremental, and a clone
  stream received as a clone of a given origin (7.5's first save of a
  foundation-inherited dataset). lzc_send in a forked child, writing a
  pipe (SIGPIPE ignored, so a receiver that stops gives it EPIPE),
  lzc_receive in shtetl reading it; the stream as zfs send -Lec, and
  nothing of -p. What each would refuse is looked at first so it is
  said in the contract's words; after, the sender's EXDEV and the
  receiver's ENODEV, EEXIST, ETXTBSY (module/zfs/dmu_recv.c).
- sh_zfs_userprops: a dataset's or snapshot's own user properties, its
  source its own name, for copying shtetl:* after a receive (6.2).
- "origin" read; destroying a snapshot with clones refused "has
  clones" (EEXIST), with holds "has holds" (EBUSY), as
  dsl_destroy_snapshot_check_impl says.
- The fake keeps a snapshot's guid across a receive, as ZFS does, and
  takes counterparts by guid, as an incremental receive does.

Verification: check_zfs 182 checks (Z14 - Z19), zcheck over the fake
60 cells, make check, gate, xcheck. The box runs zcheck across its two
pools (box_zcheck POOL/DATASET OTHER/DATASET); run.sh destroys both
with -R, a clone and its origin being in each. tracker: m4-zfs
testing.

Later, in a commit of its own just before create's: a mounted
filesystem is not destroyed. lzc_destroy refuses it EBUSY, the mount
being a long hold that dsl_destroy_head_check_impl looks at before
snapshots; the fake destroyed it, so an undo that left a mount would
have passed on the mac and failed on the box. The fake refuses
"<fs>: mounted" now, and libzfs's EBUSY is read the same way (Z20;
check_zfs 190 checks, zcheck over the fake 63 cells). check_build
passed unchanged: build's undo already unmounts first.

Box run 4 (run.sh at f04c6e7, results dc592a7): 111 ok, 3 failed, all
one cell, Z16's "newest snapshot is not FROM's counterpart" (Z10 and
Z11 follow from it). The send of s@v3 from s@v1 onto a target at v2
succeeded: dmu_recv.c's recv_begin_check_existing_impl looks back
through the target's snapshots for FROM's guid, and without -F
refuses only a target changed since it (ETXTBSY); zcheck's snapshots
hold no data, so nothing had. The contract, the fake and shtetl's
sends (beam and save from the target's newest) all mean the newest,
so the real layer now looks at it first (zz_newest_is), by guid, as
it looks at every other refusal. Z1 - Z15, Z17 - Z20 green on the
box, Z20 among them.

Box run 5 (run.sh at 18b78bf, results 0904925): 111 ok, 3 failed, the
next Z16 cell. A base that is no earlier (s@v1 from s@v3) was refused
for the target's newest, the new check, where the fake says "not an
earlier snapshot": over libzfs that was only lzc_send's EXDEV, after
the target. The real send now looks in the fake's order: SNAP, FROM,
TO's name, FROM earlier (by createtxg, or the clone's origin), then
the target (ORIGIN there, the target absent, its parent, ORIGIN
FROM's counterpart; or the target there, its newest FROM's
counterpart), guids and txgs read as numbers (zfs_prop_get_int). The
errnos after the stream stay, for a change between. Lesson: a check
added to the real layer goes where the fake has it, every Z cell
walked against both before the box.

Box run 6 (run.sh at 4c30413, 00912eb): 114 ok, 0 failed. The first
build.sh with M4 (build 9, a460f91): 78 ok, 21 failed, all of X9 and
S9 behind one: create's first save refused "srv: origin does not
apply". Save reads every dataset's origin; libzfs has none to give
for a filesystem that is no clone, and the real sh_zfs_get called
that a refusal where the fake, like zfs get (zfs_main.c's
get_callback), says "-". zcheck read origin only of clones. Now a
native property with no value is "-", from nowhere, over libzfs too
(zfs.h says so), and Z14 has the box cell. Create's undo ran on the
box: the jail stopped, its block gone, the container destroyed. B13,
shtetl-recipes cloned from GitHub by default, green.

## m4-beam (568d9a2, 7b4a227)

shtetl beam [-y] [--version v] F (planning.md 7.3), src/core/beam.c.

- The plan reads F's snapshots in zlake for its versions (shtetl:kind
  version or pkg, by createtxg), its datasets parents first, a
  placeholder by canmount=off; and zrun's F, if there, for its newest.
  Refused with every reason: no lake or run root, no such foundation,
  none of a version, the version not F's, on zrun already, older than
  zrun's newest, datasets differing.
- The run sends each dataset (sh_zfs_send), whole or from zrun's
  newest; a first beam's properties on zrun (readonly on F, inherited;
  canmount noauto, a placeholder off); the version's shtetl:* copied
  onto each received snapshot; an interrupted copy mended first.
- A failure destroys what this beam received.

Decided beyond the plan (PROPOSAL, planning.md 7.3 as built): the
command, its confirmation, the default version (newest version or
-pkg), forward only, the undo.

Also: build.c's target parsing is sh_build_target now, shared; the
fake's sh_zfs_inherit takes a snapshot's user property, as ZFS does,
so a test can make an interrupted copy.

Verification: check_beam 160 checks (M1 - M6), run-cli.sh M7, make
check, gate, xcheck. The box: build.sh bootstraps a run root and ends
with M8, box beamed whole and then incrementally. tracker: m4-beam
testing.

## m4-save

shtetl save [-m msg] X (planning.md 7.5, 6.2, 7.10), src/core/save.c;
the message from an editor, src/core/edit.c.

- The plan walks X's datasets, each a clone of a zrun foundation's
  version (its origin under the run root's foundations) or X's own;
  anything else is refused for now. shtetl:config: create's config
  snapshot for the first save, X's newest save's after (PROPOSAL).
- The save is one snapshot of every dataset (sh_zfs_snapshot), named
  by the UTC second, -2 and on after another in it.
- The mirror, parents first: a first save of a cloned dataset is a
  clone stream from its zrun origin, received as a clone of zlake's
  counterpart (the run root's foundations path swapped for the lake
  root's); X's own whole; a later one from the dataset's own newest
  mirrored save, so a mirror cut short heals at the next save.
  sh_shtetl_copy (beam's 6.2 copy, made public) after each.
- The editor: sh_run gives every child /dev/null on stdin, which an
  editor cannot use, so edit.c forks one on the terminal itself, as
  system(3) does, SIGINT and SIGQUIT ignored while it runs.

Verification: check_save 130 checks (S1 - S7, the foundation beamed
by beam itself), run-cli.sh S8, make check, gate, xcheck. S9, the box,
comes with create, which makes the first save. tracker: m4-save
testing.

Gotcha: a fake editor written as printf '%s' 'text' keeps \n
literal; the text must be printf's format.

## m4-create

shtetl create [-y] [-m msg] X --on F [--version v] (planning.md 7.4),
src/core/create.c, on zwalk.c, beam.c and save.c.

- The plan: X's recipe and F's version, every refusal said (X1). The
  version is F's newest version or -pkg in zlake, or the one given;
  X's ver is checked against its shtetl:revision, which the build now
  records for this. It is beamed first when zrun lacks it, by beam's
  own plan. X's datasets are F's at the version, read from zlake
  (canmount=off a placeholder; one F split after the version left
  out), then X's recipe's: a dataset F has takes the recipe's
  properties on its clone, one it lacks is X's own, a placeholder
  before an unnamed parent.
- The commands: F's recipe as of the version's shtetl:config is
  resolved from the config root's .zfs/snapshot/<snap>, and a piece
  of X's (label, file, text) that F's has too is left out, as the
  ruling of 2026-10-12 asks. shtetl.conf being a layer of both, its
  pieces are the ones this catches.
- The run: config snapshot, beam, clones, own datasets, mounts,
  resolv.conf, the jail's block and start, pkg -j, jexec per piece,
  the first save with the config snapshot as its shtetl:config.
  sh_tools gains jail and jexec, so the tests play them by script.
- The undo: the jail stopped, its block removed, each mount undone
  children first, X destroyed by zwalk; the config snapshot kept, as
  a build keeps its own. A failure of the save's mirror is a save's:
  X stays.

Decided beyond the plan (PROPOSAL, 7.4 as built): the jail's block
whole in /etc/jail.conf.d/X.conf and what it holds (parasa's:
rc's start and stop, exec.clean, devfs ruleset 4, the host's network
inherited), jail -f -c to start, rc.conf untouched; commands by
jexec X /bin/sh -e -c; pkg -j; resolv.conf copied in and kept;
requires only checked to be there; a name of digits only refused.

Gotchas:
- The fake's mounts make no directories: the tests make each
  container's etc/ under the scratch mount, as a foundation's clone
  has it, and point cp_resolv at a scratch file.
- The fake destroyed a mounted filesystem, where ZFS refuses it; that
  is fixed in the commit before this one (Z20), and a mutation that
  drops create's unmounts now fails check_create.
- make's one-second timestamps: a mutation copied in within the
  second of the last build is not rebuilt; remove the object first.
- Build 9 showed build.sh's X9 going on after create failed: the
  container's mount read back empty, so the S9 cell's write aimed at
  the host's /srv/www (refused only because KIDON has none), and
  "created again" passed on its exit alone. X9 and S9 are a function
  now (container) that stops after a failed create, writes only below
  the altroot, and reads the second create's refusal for "exists".

Verification: check_create 257 checks (X1 - X7), each of thirteen
mutations of create.c caught; run-cli.sh X8 (52 cases); doccheck with
the usage; make check, gate, xcheck. The box: build.sh's X9 and S9,
after M8: the container shbweb on box, its datasets, its block, its
jail running and named, the command's file, the first save mirrored
as a clone of zlake's box; the jail stopped and started from its
block; create again refused; a second save, both mirrored. Owed: the
box run. tracker: m4-create testing.

## The box: M4 tested

run.sh at 8fa7022 (57e132d): 115 ok, 0 failed. build.sh at 8fa7022
(build 10, f271e2d): 100 ok, 0 failed: M8's beams; X9, shbweb created
on box, its jail started, the command run in it, the first save
mirrored as clones of zlake's box, the jail stopped and started from
its block, create again refused for it exists; S9, the second save,
mirrored incrementally; B13, shtetl-recipes cloned from GitHub by
default. On the way: runs 4 and 5 (Z16, the send's refusals over
libzfs: the target's newest, then the fake's order) and build 9 (a
non-clone's origin "-"). tracker: m4-zfs, m4-beam, m4-save, m4-create
tested.

## Later: commands-external (2026-10-14)

Ruled after M4 (planning.md 14): a recipe key of every kind,
`commands-external`, run from outside the object, on the host, before
anything runs inside it. It stacks as `commands` does (schema.c gives
it the commands collection), so pieces, kind blocks and create's skip
of what the foundation ran all apply as they are.

- build.c: fb_external, after fb_world (the installs, the kernel's
  sync) and before fb_inside (pkg and the chroot's commands), each
  piece by tl_sh -e -c, SHTETL_ROOT the mounted root, SHTETL_NAME,
  SHTETL_KIND foundation; an update runs it again with the rest. The
  plan shows "external:".
- create.c: cr_external after the mounts and before the jail's block,
  SHTETL_KIND container; cr_pieces gives both kinds of piece, each
  with its count of what the foundation ran. cr_resolv and cr_copy are
  gone: the host's resolv.conf is shtetl-recipes' container block's,
  `[ -e "$SHTETL_ROOT/etc/resolv.conf" ] || cp /etc/resolv.conf ...`.
- The environment's names (SHTETL_ROOT, SHTETL_NAME, SHTETL_KIND) are
  a PROPOSAL; parasa and mishkan had none to follow.

Verified: check_build (F35: the piece wrote its name, kind and root
into F's /etc, between the installs and pkg), check_create (the
container block's resolv and a marker on the host before the jail, a
top-level piece the foundation ran left out, an external failure
undone before the jail); a mutation skipping either external step
caught (one that only ignored failure was not, and was redone);
make check, gate, xcheck. The box: build.sh's shtetl.conf gives the
foundation a mark and the container the resolv.conf, both external.
Box run 8 (2d1ef76, at 2c89594): 115 ok, 0 failed. Build 11 (d86ccaf):
101 ok, 0 failed: box's external mark ran in its first build and its
update, seen in the container (F35); the container's resolv.conf came
by its external piece, before the jail (X9).
