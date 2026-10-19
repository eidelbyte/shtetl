# m2-shake: the shaken recipe, stacked, written and read

## What was built

- The shaken recipe's text is what show prints of a recipe merged:
  UCL with each value's layer in a comment, which parses back to the
  same tree (family CW). No function of its own: a roll prints it
  into a buffer and writes it. foundation.conf's src commit and
  patches are m3-roll's to add.
- Stacking (cascade.c): sh_recipe_resolve takes UNDER, the parsed
  shaken recipe of the foundation version a house stands on, as the
  first layer (ld_under), named as its nodes are and taken as the
  default is: what the house gives again overrides it with a warning,
  alike silently; what stacks, stacks, the foundation's first; the
  foundation's kind alone (src_branch, kernconf, buildopts, patches)
  is left out as any other kind's is. A commands block in a shaken
  recipe is already an object of pieces keyed by their files; mg_piece
  takes those pieces as they are, so a piece the house's default gives
  again, alike, stays one piece.
- src/core/shake.[ch]: sh_shaken_write writes the text as REL in a
  tree (var/db/shtetl/foundation.conf or house.conf), replacing what is
  there (its flags cleared first, a link removed as a name), mode 0444
  and the flags given (SF_IMMUTABLE, schg, as shtetl writes it);
  sh_shaken_read reads REL of a snapshot through DIR/.zfs/snapshot/SNAP
  and parses it. Both open every component beneath the one before it,
  never through a link: a replayed save can hold one anywhere.
- sh_parse_fd: sh_parse_file's reading of an open file, which the read
  from a snapshot shares.
- tests: check_cascade test_stack (CC46 - CC52), check_shake (I1 - I8,
  with nodump, which a user may set); tests/box/shake.c, build/box_shake
  (I9 - I14, schg as root on ZFS, a real .zfs view), run by
  tests/box/run.sh between a dataset made and its snapshot.

## Decisions

- The shaken layer is the default's kind of layer. The house's own
  default (shtetl.conf) usually gives again what the foundation's
  shaken recipe holds, a derivation or a piece, and that must not be
  a redefinition.
- A link on the way is said "a link" by looking at the component once
  it is refused (fstatat without following it), not by the errno: the
  mac's open says ELOOP, its openat with O_DIRECTORY ENOTDIR, FreeBSD
  EMLINK. That explains a refusal; it checks nothing in advance.
- shtetl show stays the recipe alone, unstacked: which foundation
  version a house is on is ZFS's to say, from M5.
- No src/core/shake's "make": the text is show's (the issue listed a
  shake function; one would only rename sh_show_recipe into a buffer).

## Verified

- make check: check_cascade 328 checks with CC46 - CC52, check_shake
  I1 - I8, all the rest; make doc unchanged (38 commands).
- leaks: only libucl's known refused-duplicate leak.
- gate, xcheck (tests/box/shake.c among the sources).
- Owed on the box: tests/box/run.sh's family I (I9 - I14), schg and a
  real .zfs view, as root.

## Gotchas

- FreeBSD ZFS has no user immutable flag (uchg), and only root sets
  schg (the zfs_rebase project's box-flags finding); the unit test uses
  nodump, which both a mac user and a ZFS user may set.
- A scratch tree holding a flagged file must have the flag taken off
  before rm -rf; check_shake does so.

## Commit

The commit after 25896fd, "shake: a house stacked on its foundation's,
written schg, read from a snapshot".

## Box run 3

KIDON, 2026-10-20, at 230f87d (results c8df572): 73 ok, 0 failed.
Family I as root on ZFS: I9 - I14 ok. schg holds against root (no
write, no unlink), the file is replaced and schg again, read back
through the snapshot's .zfs view, a link in the snapshot is refused,
and the live file's change after the snapshot is not what is read.
Tested.
