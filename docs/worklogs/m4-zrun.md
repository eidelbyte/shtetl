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
