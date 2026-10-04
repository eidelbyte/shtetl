# shtetl: planning

Status: draft, 2026-10-04. Nothing here is built. Sections marked
PROPOSAL are my reading of the brief and still need a ruling; the
numbered list in section 11 collects every open question in one place.

## 1. What shtetl is

shtetl is a system automation tool for FreeBSD. It builds FreeBSD from
source into *foundations*, and runs *containers* (jails) and *systems*
(bootable roots) as ZFS clones of those foundations. It saves each
container and system as a delta, and when a foundation moves forward it
carries every container and system onto the new foundation without
losing what the admin changed.

It is the third tool in a line:

| tool    | state                    | how it tracked admin changes                  |
|---------|--------------------------|-----------------------------------------------|
| mishkan | C, stub (746 lines)      | design only: git + recipes + mtree            |
| parasa  | POSIX sh, ~6200 lines; C rewrite stub | git branches per target, mtree drift, classification prompts |
| shtetl  | C, this plan             | ZFS only: clones, snapshots, send/recv, zfs_rebase |

The change that makes shtetl simpler is zfs_rebase
(github.com/eidelbyte/zfs_rebase_tool): a userspace tool that replays
one ZFS filesystem's changes onto another and reports exactly which
files it could not decide. With it, the whole git layer of parasa goes
away:

- no `foundation.git`, `parasa.git` or `.git` datasets inside trees;
- no mtree baseline and no drift classification prompts;
- no two-commit save; a save is `zfs snapshot` + `zfs send -i`;
- an update is one `zfs_rebase` per dataset instead of `git rebase`.

Recipes stay. They live in their own plain-text git repo,
zshtetl-recipes, cloned into zconfig (section 5). That repo is the one
place git remains, and only as a way to share recipe text.

## 2. Names

Four logical spaces. Working short names, with the parasa pool each
replaces:

| shtetl  | role                                   | parasa                |
|---------|----------------------------------------|-----------------------|
| zlake   | backup of the last save of everything; foundation archive | zbamidbar |
| zconfig | shtetl.conf and the zshtetl-recipes clone | zshemot (config half) |
| zbuild  | FreeBSD src and obj                    | zshemot (build half)  |
| zrun    | everything that runs                   | zbereshit             |

Better names are still wanted (question Q1).

Each space is a pool or a dataset the user already has. shtetl never
partitions, encrypts, labels or creates pools.

## 3. Ground rules

- C, userspace, linked against libzfs / libzfs_core, laid out like
  zfs_rebase: a portable core that builds and tests on the mac with
  `make check`, and a FreeBSD layer that only the box runs.
- ASCII only, in code and docs.
- FreeBSD 15.1 is the floor (zfs_rebase needs it).
- BSD 3-Clause. Commits signed off as Eidel Solomon, no co-author line.
- The rebase tool is run, not linked: shtetl execs `zfs_rebase` with an
  argv array and reads its exit status and manifest.
- Facts come from their binary source where one exists: dataset roles
  from user properties, origins from the `origin` property, "anything
  written since the snapshot" from `written@snap`. No parsing of
  `zfs list` text.
- Least mounted: anything that can be unmounted is.

## 4. The four spaces

### 4.1 Discovery (PROPOSAL)

shtetl needs to find zconfig before it can read shtetl.conf. Proposal:
bootstrap sets a local user property on the root dataset of each
space:

    shtetl:space = lake | config | build | run

At startup shtetl iterates the imported pools for datasets carrying
`shtetl:space` (read as a local value only; an inherited value is not a
space root). Exactly one root per role, or shtetl refuses and names
both. No pointer file on the host; the host is itself a shtetl system
and its files are rebased (question Q2).

### 4.2 Bootstrap

1. The user names four locations, each a pool or a dataset. One pool
   may hold several spaces; zrun must be a single pool (clones cannot
   cross pools).
2. shtetl shows the property table (4.3) for each space: property,
   default, the value shtetl.conf sets if any, and the value it will
   use. Pool properties it cannot set (ashift) are shown read-only with
   a warning when they differ from the recommended value.
3. On confirmation it creates the layout under each root (4.4 - 4.7)
   with those properties, and sets `shtetl:space` on each root.
4. It clones zshtetl-recipes into zconfig and writes a starter
   shtetl.conf if none exists.

Systems need zrun to be a bootable pool (the loader boots
`zrun/systems/X` via `zfs set -u mountpoint=/` and `nextboot`). A zrun
that is a dataset in a non-boot pool can run containers only; bootstrap
says so.

### 4.3 Property defaults (from parasa)

Defaults, overridden per space by shtetl.conf (section 5). Taken from
parasa's `stage0-bootstrap.sh` and dataset scripts.

Pool properties (reported, set only where `zpool set` can):

| property | default | note |
|----------|---------|------|
| ashift   | 12      | fixed at pool creation; report only |
| autotrim | on      | settable |

Dataset properties on each space root (inherited below):

| property  | default     |
|-----------|-------------|
| atime     | off         |
| compression | zstd      |
| aclmode   | passthrough |
| mountpoint | none       |
| canmount  | noauto      |

Per-role overrides inside the layout:

| dataset | properties |
|---------|------------|
| zrun/containers | mountpoint=/containers, canmount=on (the one auto-mounted parent) |
| zrun/foundations/F and children | mountpoint=none, readonly=on |
| zrun/systems/X | mountpoint=none until deploy; then `zfs set -u mountpoint=/` |
| zlake/** | mountpoint=none, canmount=noauto; mounted only while built into |
| zbuild/src, zbuild/obj | mountpoint under a build path, canmount=noauto |
| zconfig root | mountpoint set (shtetl reads it on every run) |

Not carried over from parasa: GELI (`-B none -l 256 -s 4096`), glabel,
vdev topology, the boot disk's GPT layout. shtetl consumes existing
devices.

### 4.4 zconfig

    zconfig/
        shtetl.conf            site settings and property overrides
        zshtetl-recipes/       git clone, plain text
            default.conf
            foundations/
            containers/
            systems/

zconfig is a dataset; besides git for the recipe repo, it gets a
snapshot on every shtetl change to shtetl.conf (cheap history without
git for the site file).

### 4.5 zbuild

    zbuild/
        src/                   FreeBSD source (git checkout of freebsd-src)
        obj/                   MAKEOBJDIRPREFIX

Nothing else. There is no DESTDIR on zbuild: the build installs straight
into the zlake foundation dataset, mounted for the length of the build
(6.1). obj is backed up to zlake after each build so a rebuilt zbuild
starts warm (question Q4 for the name in zlake).

### 4.6 zlake

    zlake/
        foundations/
            F/                 built into directly by zbuild; @v1, @v2, ...
                usr-local/     children the foundation recipe pre-splits
                var/
        containers/
            X/                 delta of zrun/containers/X (a clone of zlake/foundations/F@vN)
                var/           delta of X/var (clone of zlake/foundations/F/var@vN)
                www/           container-only dataset, full stream then deltas
        systems/
            S/                 same shape as containers
        build/obj              obj cache backup (name open, Q4)

zlake is the backup of the last save of each of zrun's entities, plus
the foundation archive. Nothing in zlake runs. container-data/ and
system-data/ from parasa are gone: every dataset of a container is a
child of the container, and so is its backup.

### 4.7 zrun

    zrun/
        foundations/
            F/                 recv'd from zlake/foundations/F; readonly
                usr-local/
                var/
        containers/            mountpoint=/containers
            X/                 clone of foundations/F@vN
                usr-local/     clone of foundations/F/usr-local@vN
                var/           clone of foundations/F/var@vN
                www/           container-only, created by the recipe
        systems/
            S/                 same shape; mounted at / when booted

All running resources live on zrun. Children mount by inheritance
(`/containers/X/var`), so containers need no `mount.fstab` for their
own datasets and systems need no fstab entries for theirs. nullfs is
left only for one container mounting another's dataset (`requires`).

## 5. Configuration

### 5.1 Format: UCL

All configuration is UCL (libucl, the format of `pkg.conf`), read with
`textproc/libucl` from ports. FreeBSD base builds its libucl as
PRIVATELIB (`lib/libucl/Makefile`), so third-party software cannot link
it. See docs/ucl-primer.md.

Two rules fixed now:

- Each file is parsed with `UCL_DUPLICATE_ERROR`: a key written twice
  in one file is an error, never a silent implicit array.
- UCL macros (`.include`, `.priority`, `.load`, `.inherit`) are refused
  with an error. The cascade is shtetl's (5.2), not UCL's. Note:
  `UCL_PARSER_DISABLE_MACRO` turns macros into comments, which would
  silently drop a recipe's `.include`; the refusal must be an error,
  and how to get one from libucl is an implementation item (M1).

### 5.2 Cascade (PROPOSAL)

Layers, nearest wins:

1. built-in defaults (the tables in 4.3, SRC_BRANCH=stable/15,
   KERNCONF=GENERIC, MAKE_JOBS=hw.ncpu)
2. zconfig/shtetl.conf
3. zshtetl-recipes/default.conf
4. the recipe named by the target (foundations/F.conf,
   containers/X.conf, systems/S.conf), and anything it `import`s

Merge by key type, from a schema shtetl owns:

- scalar: nearer layer replaces.
- object: merged key by key, recursively.
- list: nearer layer appends (mishkan's `+=` was the common case:
  `datasets += [home]`, `commands += {...}`). Replacing a list needs an
  explicit form; which one is question Q5.

Unknown keys are errors, with file and line. Keys valid only for one
kind (`kernconf` for foundations, `requires` and `jailconf` for
containers) are errors elsewhere, not ignored.

### 5.3 Recipe keys (from mishkan, adapted)

| key | kinds | type | meaning |
|-----|-------|------|---------|
| import | all | string list | recipes merged in before this file |
| ver | all | string | version constraint: `>=15`, `=15.1` |
| src_branch | foundation | string | FreeBSD branch |
| kernconf | foundation | string or heredoc | config name, or an inline kernel config |
| buildopts | foundation | heredoc | make(1) variables / src.conf |
| make_jobs | foundation | integer | 0 = hw.ncpu |
| datasets | all | list | child datasets, created before anything is installed |
| pkg | all | list | packages |
| commands | all | heredoc, sh | run once at creation, after pkg |
| derivations | all | list of {source, derived, command} | binaries regenerated from text after a rebase |
| requires | container | list | containers that must exist first |
| jailconf | container | heredoc | appended to the jail's jail.conf block |

Sidecar files: a heredoc can name a file instead
(`kernconf_file = "gaming.kernconf"`) so kernel configs and shell stay
in files their own tools can check.

### 5.4 shtetl.conf (PROPOSAL, sketch)

    properties {
        default {
            compression = "zstd";
        }
        lake {
            compression = "zstd-9";
        }
    }
    build {
        make_jobs = 0;
    }

Space locations are not in shtetl.conf (they are found by 4.1).

## 6. Lifecycles

Every lifecycle ends with a save. zlake always holds the last save of
every zrun entity.

### 6.1 Build a new foundation F

1. Resolve the recipe cascade; check `ver` against `src_branch`.
2. Create `zlake/foundations/F` and one child per recipe `datasets`
   entry, all empty, **before anything is installed**. This pre-split
   is what gives every container dataset a foundation counterpart to
   rebase onto.
3. Mount F (children follow by inheritance) at a build path on the
   build host.
4. In zbuild/src: check out `src_branch`; `make buildworld
   buildkernel` with MAKEOBJDIRPREFIX on zbuild/obj.
5. `make installworld installkernel distribution DESTDIR=<F mount>`.
6. Foundation pkg, commands, derivations (inside a chroot of F).
7. Unmount; `zfs snapshot -r zlake/foundations/F@<vname>`.
8. Back up zbuild/obj to zlake.

### 6.2 Update foundation F

As 6.1 steps 3 - 8 on the **same** datasets, plus
`make delete-old delete-old-libs DESTDIR=<F mount>` after install.
Keeping the build on one dataset keeps every version on one snapshot
chain, which is what zfs_rebase needs to derive a base, and what lets
zlake -> zrun be an incremental send.

Known cost: installworld replaces every file, so every file has a new
object, generation and ctime on the onto side, and the rebase decides
every file on content rather than pruning it (probe P5). Files that
ObsoleteFiles.inc does not list survive delete-old (P6).

### 6.3 Bring a foundation to zrun

`zfs send` each dataset of F (full the first time, `-i` from the
newest version zrun has after that) into `zrun/foundations/F`, readonly,
mountpoint none. Triggered by container/system creation or update when
zrun lacks the version.

### 6.4 New container X

1. Foundation version on zrun (6.3), building or updating it first if
   asked.
2. `zfs clone` each dataset of `F@vN` to the matching name under
   `zrun/containers/X` (clone is not recursive; one per dataset).
3. Create the recipe's container-only datasets as children of X.
4. Write the jail.conf block; start the jail.
5. `pkg -j X install`, then commands, then derivations.
6. Save (6.5).

### 6.5 Save X

1. `zfs snapshot -r zrun/containers/X@<save>`.
2. Per dataset, into the matching name under zlake/containers/X:
   - first save of a foundation-inherited dataset: a clone stream
     (`send -i zrun/foundations/F/d@vN X/d@save`) received as a clone
     of `zlake/foundations/F/d@vN`, so zlake holds only the delta;
   - first save of a container-only dataset: a full stream;
   - later saves: `send -i` from the previous save.
3. Older save snapshots are pruned on both sides by policy (Q8).

How zfs receives each of these (and in what order, since a child needs
its parent to exist) is probes P1 - P3.

### 6.6 Update X onto F@v2

1. Preflight: F@v2 on zrun (6.3); no open rebase on X; the jail is
   saved.
2. Save (6.5) with the save snapshot named `@pre-<id>`.
3. For each foundation-inherited dataset d of X, parent first:

        zfs_rebase --from zrun/containers/X/d@pre-<id> \
            --onto zrun/foundations/F/d@v2 \
            --result zrun/containers/X-new/d

   The tool derives base `F/d@v1` from X/d's origin. Exit 0 = done,
   1 = stopped at conflicts (shtetl opens the picker with `-i` and
   continues), 2 = refused before touching anything, 3 = failed part
   way or drift at the final check. Backing out runs `--abort` on every
   rebase this update started.
4. Derivations: rerun each derivation whose source changed.
5. Finalize:
   1. Stop the jail.
   2. Check `written@pre-<id>` on every rebased dataset of X. Nonzero
      means the jail wrote after the save and the rebase missed it
      (Q6).
   3. Carry the container-only datasets across:
      `zfs rename zrun/containers/X/www zrun/containers/X-new/www`.
      Nothing in a foundation update touches them, so there is nothing
      to rebase.
   4. Destroy the old X tree; rename X-new to X.
   5. Place: `zfs inherit mountpoint`, `readonly=off` on each rebased
      dataset (the tool hands results back unmounted, readonly,
      mountpoint none, and never places them).
   6. Start the jail.
6. Save, re-homing the zlake copy: X is now a clone of F@v2, so its
   zlake delta must become a clone of `zlake/foundations/F@v2`, and the
   container-only children must move under it with their own
   incremental lineage intact (probe P2, the user's open question).

### 6.7 Systems

The same as containers, except:

- deploy: `zfs set -u mountpoint=/ zrun/systems/S` and `nextboot`;
  children mount by inheritance (/var, /usr/local, ...);
- the running system cannot be stopped to finalize; the update builds
  S-new, and finalize happens across a reboot into S-new. Writes to the
  old system after `@pre` are the same hazard as 6.6 step 5.2, larger
  (Q6, Q7).

### 6.8 Restore

Any entity can be rebuilt on zrun from zlake: the foundation version it
names, then its delta and children, sent back. Exercised as a test, not
only documented (M7).

### 6.9 Garbage

A foundation snapshot may go once its `clones` property is empty on
both zlake and zrun. shtetl never destroys a snapshot something still
clones.

## 7. Using zfs_rebase

Facts from the tool's README that shape shtetl:

- One dataset per run. The walk reads `.zfs/snapshot`, where a child
  dataset is an empty directory, and a mount inside the tree is refused
  (`nested mount`, src/walk.c). shtetl loops over datasets.
- The tool takes no base: from and onto must share an origin lineage.
  Every container dataset that should be rebased must therefore be a
  clone of a foundation dataset; this is why the foundation recipe's
  datasets are pre-split (6.1 step 2).
- Clone form (`--onto` a snapshot) creates `--result` and never places
  it. shtetl places results at finalize.
- The tool holds its input snapshots for the life of the rebase and
  releases them at done or --abort. On FreeBSD a hold release
  force-unmounts that snapshot's `.zfs` view: shtetl must not have
  anything open under `.zfs/snapshot` of an input when a rebase ends.
- A verb names its rebase by IDENT; shtetl uses the result dataset name.
- shtetl never runs two verbs on one rebase at once (the tool also
  records `zfs_rebase:active`).

## 8. Implementation (C)

Layout, after zfs_rebase:

    src/core/      config, cascade, schema, planning: portable, make check on the mac
    src/fbsd/      libzfs, jail, exec of make/pkg/git/zfs_rebase
    tests/         per-phase matrices first (tests/MATRIX.md), then tests

Libraries: libzfs + libzfs_core + libnvpair (headers from /usr/src,
as zfs_rebase's Makefile does), libucl (ports), libjail (base),
curses (base) for the TUI. Exec with argv arrays, never a shell:
make, pkg, git, jail, zfs_rebase.

Pain points to settle early (M1/M2):

- libzfs is not a stable API; build needs the OpenZFS headers from
  /usr/src. Copy zfs_rebase's Makefile block.
- send/recv: lzc_send/lzc_receive cover one dataset; clone streams and
  recv with origin may need libzfs `zfs_send_one`/`zfs_receive` with
  flag structs. Fallback: pipe two exec'd `zfs` processes and check
  both statuses.
- Every exit path must undo: mounts, started jails, half-made datasets,
  open rebases. A cleanup stack in the core, tested by kills at every
  step as the rebase harness does.
- Mac development: the portable core carries the logic; the FreeBSD
  layer is thin and tested on the box.

## 9. Testing

Per the project rule: plot each phase's problem space as a matrix
(dimensions crossed into cells, each cell a test, a mapping or a
documented deferral) before writing its tests. Phases: config/cascade,
bootstrap, foundation build, beam-down, create, save, update, finalize,
restore, gc. Kill tests at every step of create, save, update and
finalize.

## 10. Milestones

| M  | scope | where |
|----|-------|-------|
| M0 | probes P1 - P9 (section 12); rulings on section 11 | box |
| M1 | skeleton, UCL loader, schema, cascade, `shtetl config show` | mac |
| M2 | discovery, bootstrap, property table | box |
| M3 | foundation build and update into zlake | box |
| M4 | beam-down, new container, save | box |
| M5 | update container via zfs_rebase, finalize | box |
| M6 | systems: deploy, nextboot, update across reboot | box/VM |
| M7 | restore and gc | box |
| M8 | TUI | mac + box |

## 11. Open questions

Q1. The four spaces need better names than zlake, zconfig, zbuild and
zrun. Keep the short names for now?

Q2. Discovery: should shtetl find its spaces by a `shtetl:space` user
property on each root (4.1), or by a pointer file on the host?

Q3. Should bootstrap refuse a zrun that is not a bootable pool, or
allow it and disable systems?

Q4. Where in zlake does the obj cache backup live, and is it one
dataset per src branch?

Q5. In the cascade, lists append by default. How should a recipe say
"replace this list" instead: a separate key (`pkg_replace`), an object
form (`pkg { replace = [...] }`), or not at all?

Q6. A container keeps running between the `@pre` save and finalize,
and anything it writes in that window is not in the rebased datasets.
Should shtetl stop the jail for the whole update, or keep it running
and refuse finalize (offering a redo) when `written@pre` is nonzero?

Q7. A running system cannot be stopped. Does a system update rebase
from `@pre`, reboot into S-new, and accept losing writes made after
`@pre` (logs, mostly), or should finalize re-save from the old system
after it stops at shutdown?

Q8. How many saves does zlake keep per entity, and how many does zrun
keep?

Q9. Foundation naming: mishkan named foundations
`{ver}-{branchtype}-{recipe}` (15.1-releng-gaming), parasa named
snapshots `<branch>_<ISO-date>_<short-sha>`. Use the first for the
dataset and the second (minus the branch) for the version snapshot?

Q10. Do packages and `commands` ever rerun on update? With /usr/local
and /var/db/pkg rebased, nothing needs reinstalling within a major
version. A major-version foundation change probably needs
`pkg upgrade -f`; is that an update step or a new container?

## 12. Probes (M0): standalone tests before the design commits

Each probe is a script on a throwaway pool on the box, written down
with what it printed.

P1. Clone-stream receive: `zfs send -i zrun/foundations/F@v1
zrun/containers/X@s1 | zfs recv zlake/containers/X` where
zlake/foundations/F@v1 exists. Does it land as a clone of it? Does it
need `-o origin=`?

P2. Re-homing after an update (the open question): zlake/containers/X
is a clone of F@v1 with a child www carrying @s1. Receive the new X as
a clone of F@v2 at a new name, `zfs rename` www under it, destroy the
old X, rename the new X into place. Does `send -i www@s1 www@s2` still
receive into the renamed www? Does a child's incremental care that its
parent's lineage changed?

P3. Same as P2 on zrun: rename a container-only child from X to X-new
while X-new is a fresh rebase result; then save.

P4. zfs_rebase with `--result zrun/containers/X-new/var` while
`X-new` is itself the result of an open rebase: accepted? private
mounts collide?

P5. Rebuild in place: install v2 over v1, then rebase a container
whose admin edited /etc/rc.conf and whose v1 -> v2 left rc.conf's bytes
unchanged. Expect no conflict; measure the rebase's time and the
incremental send size.

P6. `make delete-old delete-old-libs DESTDIR=` on an in-place install:
what remains that a fresh install would not have?

P7. Mountpoint inheritance for clones under zrun/containers and
across pools; `zfs mount -a` order.

P8. `written@snap` on a clone's children while the jail runs: the
finalize check of Q6.

P9. Clone per child at creation: `zfs clone` each F child into X's
tree; does anything require the parent clone first?

## 13. Dropped from parasa

foundation.git, foundation.zfs (replaced by zlake/foundations), parasa.git,
recipes.git on the data pool, .git datasets in trees, mtree.dist,
diff.sh drift classification, two-commit save, pkg.list (packages are
rebased), compose.sh replay on update (commands run once), the
container-data and system-data trees, mount.fstab for own datasets,
stage0 disk preparation.
