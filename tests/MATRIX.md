# The test matrix

Each phase's problem space is plotted before its tests (CLAUDE.md,
Testing): its dimensions named and crossed into cells, and every cell
given a disposition -- the test that covers it, an existing test it
maps to, or a deferral with its reason and the work that unblocks it.
A cell with no row here is a hole.

A family is a letter and a phase; a cell is the family's letter and a
number, which never changes once given. A retired cell keeps its row,
marked retired with the reason. Each test's comment names the cells
it covers.

## Family M: the allocators (m1-tree)

Dimensions: the allocator (calloc, realloc, strdup) by what the
system does (gives the memory, refuses it). A refusal is logged to
syslog and ends the process with 1 (plan 10).

| cell | allocator | system | expected | disposition |
|------|-----------|--------|----------|-------------|
| M1 | sh_calloc | gives | zeroed memory | tests/check_mem.c |
| M2 | sh_calloc | refuses | "out of memory" logged, exit 1 | tests/check_mem.c (n times size overflows) |
| M3 | sh_realloc | gives | grown, contents kept | tests/check_mem.c |
| M4 | sh_realloc | refuses | "out of memory" logged, exit 1 | tests/check_mem.c (SIZE_MAX) |
| M5 | sh_strdup | gives | the copy | tests/check_mem.c |
| M6 | sh_strdup | refuses | as M2 | deferred: no string can be made long enough to fail it; the path is M2's, through the same function |

## Family Z: the ZFS layer (m1-zfs)

Dimensions: each call of src/core/zfs.h by what it meets: the dataset
there or not, a snapshot or a filesystem, set or inherited, mounted
or not, with snapshots, children or a mount inside it. The cells are
written once, in tests/zcells.c, and run twice: over the fake in make
check (tests/check_zfs.c) and over libzfs on the box
(tests/box/zcheck.c, from tests/box/run.sh). A refusal is read by its
words. The fake's data is words; the box's are files.

| cell | call | what it meets | expected |
|------|------|---------------|----------|
| Z1 | create | a new name, with properties | made, the properties set locally from its birth, nothing mounted |
| Z1 | create | the name taken | "exists" |
| Z1 | create | the parent missing | "no parent" |
| Z2 | user | set on it | its value, 0 |
| Z2 | user | set above it | the ancestor's value, 0 |
| Z2 | user | set nowhere | 1 |
| Z2 | user | no such filesystem | "no such filesystem" |
| Z3 | local | set locally on two, inherited below one | the two, each with its value |
| Z3 | local | FN stops | FN's value returned |
| Z4 | children | children and a grandchild | the children only |
| Z4 | children | FN stops | FN's value returned |
| Z4 | children | no such filesystem | "no such filesystem" |
| Z5 | snapshot | three at once, a property | all taken, the property on each |
| Z5 | user | a snapshot | its own, else its filesystem's |
| Z5 | snapshot | one name taken | "taken", none taken |
| Z5 | snapshot | a filesystem missing | "no such filesystem", none taken |
| Z5 | snapshot | a name twice | "named twice", none taken |
| Z5 | snapshot | a filesystem's name | "not a snapshot's name", none taken |
| Z5 | user | no such snapshot | "no such snapshot" |
| Z6 | snapshots | two | oldest first |
| Z6 | snapshots | a child's | its own only |
| Z6 | snapshots | FN stops | FN's value returned |
| Z6 | snapshots | no such filesystem | "no such filesystem" |
| Z7 | rollback | an older snapshot | "not the newest snapshot" |
| Z7 | rollback | a snapshot not there | "not the newest snapshot" |
| Z7 | rollback | a filesystem with none | "not the newest snapshot" |
| Z7 | rollback | the newest, mounted | what came after it gone, still mounted |
| Z7 | rollback | no such filesystem | "no such filesystem" |
| Z8 | mount | mounted there already | nothing done |
| Z8 | mount, mounted | a child inheriting the mountpoint | mounted below its parent's |
| Z8 | unmount | another mounted inside | "busy" |
| Z8 | unmount | mounted, then not | unmounted; then nothing done |
| Z8 | mount | canmount off | "canmount is off" |
| Z8 | mount | mountpoint none | "no mountpoint" |
| Z8 | mounted | no such filesystem | "no such filesystem" |
| Z9 | destroy | mounted, with snapshots and a child | "mounted" |
| Z9 | destroy | with snapshots and a child | "has snapshots" |
| Z9 | destroy | snapshots | destroyed |
| Z9 | destroy | a child only | "has children" |
| Z9 | destroy | nothing in the way | destroyed |
| Z9 | destroy | a filesystem or a snapshot not there | "no such dataset" |

Disposition of every row above: tests/zcells.c, over the fake (make
check) and over libzfs (tests/box/run.sh). Deferred, with what
unblocks them:

- mount "mounted at PATH, not its mountpoint": the cell needs a
  mountpoint changed under a mounted filesystem, which needs a set;
  it comes with the first issue that sets one (m3-roll).
- destroy of a snapshot "has clones" and "has holds": shtetl makes
  clones from m4-zfs, which adds the cell; it takes no holds.
- a walk refused by libzfs part way, and open refused: nothing a cell
  can do makes libzfs fail them; the fake fails them on request
  (zfake_fail) for the tests of their callers.
