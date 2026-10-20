# m4-zfs: the ZFS calls the beam needs

## What was built

src/core/zfs.h gains, over libzfs (src/fbsd/zfs.c) and the fake
(tests/zfake.c):

- sh_zfs_snapinfo: a snapshot's guid and createtxg.
- sh_zfs_origin: what a filesystem is a clone of, or nothing.
- sh_zfs_clone (zfs_clone, properties from birth) and sh_zfs_promote
  (lzc_promote), carried from main.
- sh_zfs_send: lzc_send in a forked child into a pipe, received here
  with lzc_receive_resumable, so a stream broken off leaves a receive to
  resume: whole, incremental, or a clone stream with ORIGIN. Before the
  stream, what it would be refused for is looked at and said in the
  layer's words, in the fake's order (main's lesson, its box runs 4 and
  5): the snapshots, the target, a receive to resume standing there,
  and the base by guid (the target's newest snapshot, or ORIGIN, FROM's
  counterpart), the beam's rule.
- sh_zfs_resumable: whether a receive stands to be resumed. The token
  itself never leaves the layer.
- sh_zfs_resume: the token decoded (zfs_send_resume_token_to_nvlist);
  the snapshot it sends looked for by name and guid ("SNAP: no such
  snapshot" if gone or another), the one it was sent from found by its
  guid among the source's snapshots or its origin ("its base gone");
  lzc_send_resume from the token's object and offset, with the flags
  the stream began with, into lzc_receive_resumable.
- sh_zfs_abort: as zfs receive -A, the hidden DS/%recv destroyed, or DS
  itself where the stream was making it.
- The fake: snapshots carry a guid (a received one its source's) and a
  createtxg; filesystems an origin and a receive to resume; zfake_cut
  breaks the next stream off; promote moves the snapshots and their
  clones as lzc_promote does; a snapshot with a clone is not destroyed.

Cells Z11 - Z15 and Z9's "has clones", written once in tests/zcells.c;
two driver hooks, zc_fill (data enough to cut) and zc_cut (a stream cut
part way): the box's as probe P1 cut its streams, the fake's with
zfake_cut.

## Decisions, beyond or against the issue

- The send stays a synchronous call, as main's was. The beam must not
  block the daemon for hours, so m4-beam runs it in a forked child of a
  job (a job step of a function, which job.c does not have yet: m4-beam
  adds it). The issue's "in a forked child" is that, and the layer's
  own fork of lzc_send.
- No token string leaves the layer: the beam asks whether a receive is
  to be resumed and asks for it resumed (plan 2: facts from their
  binary source).
- libzfs's zfs_send_resume is not used: its failure gives no errno the
  child could hand back, and its own words are not the layer's. The
  token is decoded with libzfs's own function and the stream sent with
  lzc_send_resume, the base found by guid as zfs_send_resume finds it.

## Verified

- make check: Z9 - Z15 over the fake (check_zfs); all 15 test programs.
- A fake whose abort keeps what a whole stream made fails the cell
  that says it goes; every new cell runs (listed under that mutation).
- xcheck: src/fbsd/zfs.c and tests/box/zcheck.c compile against the
  FreeBSD tree's headers (lzc_send_resume, lzc_receive_resumable,
  zfs_send_resume_token_to_nvlist, zfs_clone, lzc_promote).
- gate.
- Owed: the same cells over libzfs, run.sh's zcheck on the box. Words
  libzfs may say otherwise than the fake (zfs_clone's refusals, a
  receive -A) are what that run will show.

## Commit

The commit after fa86471, "zfs: clones, promotes, and streams that
resume".
