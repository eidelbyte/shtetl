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
