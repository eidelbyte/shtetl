# M5: updates (worklog)

Update into staging, history replay, the conflicts gate with
derivations, cutover and abandon (planning.md 7.6 - 7.10, 9).
Tracker: tracker/shtetl.json, m5-zfs, m5-rebase, m5-update, m5-gate,
m5-cutover. The matrix came first (f5571f7): families W, U, G, K and
cells Z21 - Z23, S10.

## m5-zfs

The ZFS calls cutover and abandon need, in src/core/zfs.h:

- sh_zfs_promote, over lzc_promote: refused for no such filesystem and
  for one that is no clone, looked at first so said in the contract's
  words; EEXIST, a snapshot name the origin shares, names it.
- sh_zfs_rename, over zfs_rename, whose changelist moves the mounts:
  refused for no such filesystem, the new name there already and its
  parent missing, each looked at first.
- written@SNAP through sh_zfs_get, by zfs_prop_get_written_int: a
  number, the binary source of it, printed; a snapshot that is not
  there refused.

The fake: promote moves the origin's snapshots up to the clone's by
transaction, clones of them following, and swaps the two origins, as
dsl_dataset_promote does; rename moves a filesystem, its children,
their snapshots and every origin naming them, remounting what was
mounted where its mountpoint now says; zfake_write records bytes
written, which written@ counts after a snapshot's transaction.

zcheck gains the cells for the box. Its written@ cell writes 64 KiB at
the dataset's mountpoint and waits up to 30 s for a transaction group
to count it; over the fake, zcheck-fake.c points the write at
zfake_write (zcheck_write).

Verification: check_zfs 227 checks; zcheck over the fake 75 cells;
make check, gate, xcheck. Box owed (run.sh). tracker: m5-zfs testing.
