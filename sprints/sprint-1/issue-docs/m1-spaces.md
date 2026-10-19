# m1-spaces: the spaces and their skeleton

## What was built

src/core/spaces.[ch]: sh_spaces_find, the four spaces from the
filesystems where shtetl:space is set locally (sh_zfs_local, so the
datasets below a root, which inherit the value, are not roots); and
sh_spaces_skeleton, which makes below each root found what that
space keeps there, from one table. Its one row so far is
<lake>/foundations, canmount=off.

## Decisions

- Refused, each said and the whole find refused, so the daemon does
  not start: a value that is no space's; two roots of one space (both
  named, sorted); a root inside another's. That last one is beyond
  the issue's text: it is main's ruling Q-Y, carried because without
  it a run root under <lake>/foundations would be walked as a
  foundation, and the foundation actor's start would roll it back or
  destroy it. Refusing the shape makes that state impossible.
- No bootstrap verb (PROPOSAL in the issue): the person marks a space
  with zfs set, and the daemon makes the skeleton each start. A row's
  dataset is looked for among the root's children and made when
  missing; one that is there is left as it is.
- A missing space is not an error: what needs it does not run.

## Verified

make check (check_spaces, S1-S11), no leaks (leaks --atExit), make
gate, make xcheck. On the box the find and the skeleton run in the
daemon's start (m1-foundation-start's box cells); their ZFS calls are
family Z's.

## Gotchas

- The fake now answers a native property's name through sh_zfs_user
  with 1, as libzfs does; tests read what the fake holds with
  zfake_local (commit e4d59b1, found here).
- The skeleton table gives each row one property. A row that needs
  two changes the table's shape then, not before.

## Commit

The commit after e4d59b1, "spaces: found by shtetl:space, their
skeleton from one table".
