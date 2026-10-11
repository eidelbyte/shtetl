# shtetl: planning

Status: draft 4, 2026-10-09. Nothing here is built. Drafts 1 to 3 were
reviewed and every ruling is folded in; section 14 lists them so each
can be traced. No question is open (section 15). A detail marked
PROPOSAL has not been ruled on but blocks nothing before M1.
docs/story.html walks one cycle of this design frame by frame.

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
- no two-commit save; a save is a snapshot carrying a commit message,
  and an incremental send;
- an update stops the container and replays its saves onto the new
  foundation, one `zfs_rebase` per dataset and save, instead of
  `git rebase`.

Recipes stay. They live in their own plain-text git repo,
zshtetl-recipes, whose clone is zconfig (section 5). That repo is the
one place git remains, and only as a way to share recipe text.

## 2. Names

Four logical spaces, names settled:

| space   | role                                   | parasa                |
|---------|----------------------------------------|-----------------------|
| zlake   | foundation archive, obj caches, a mirror of zrun as of each entity's last save, archives | zbamidbar |
| zconfig | the zshtetl-recipes clone, shtetl.conf included | zshemot (config half) |
| zbuild  | the FreeBSD src checkout               | zshemot (build half)  |
| zrun    | everything that runs                   | zbereshit             |

Each space is one or more datasets (a pool's root dataset counts) that
the user already has. shtetl never partitions, encrypts, labels or
creates pools.

## 3. Ground rules

- C, userspace, linked against libzfs / libzfs_core, laid out like
  zfs_rebase: a portable core that builds and tests on the mac with
  `make check`, and a FreeBSD layer that only the box runs.
- ASCII only, in code and docs.
- FreeBSD 15.1 is the floor (zfs_rebase needs it).
- BSD 3-Clause. Commits signed off as Eidel Solomon, no co-author line.
- The rebase tool is run, not linked: shtetl execs `zfs_rebase` with an
  argv array, reads its exit status, and reads and writes its
  resolution document.
- Facts come from their binary source where one exists: roles from
  user properties, origins from `origin`, "written since" from
  `written@snap`, snapshot meaning from `shtetl:kind`. Snapshot and
  dataset names are for people; shtetl does not parse them.
- Least mounted: anything that can be unmounted is.

## 4. The four spaces

### 4.1 Discovery and multi-rooting

Bootstrap sets a local user property on each root:

    shtetl:space = lake | config | build | run

At startup shtetl iterates the imported pools for datasets carrying a
**local** `shtetl:space` (an inherited value is not a root).

- **lake** and **run** may have several roots, in one pool or across
  pools (each pool with whatever vdevs it has; the tag is on datasets,
  not vdevs). A child such as `foundations/F` is looked for under every
  root of its space. Exactly one match is required; two matches is an
  error naming both.
- **config** and **build** have exactly one root each.

Where things are created:

- **An entity lives in the root that holds its foundation.** A clone
  cannot cross pools, so a container or system is created in the zrun
  root that holds its foundation's zrun copy, and its zlake mirror in
  the zlake root that holds the zlake foundation (the mirror is made
  of clone streams of it, 7.5).
- A new foundation, or a foundation's first copy on zrun, goes to the
  root named by `spaces.lake.default` / `spaces.run.default` in
  shtetl.conf. With one root the key may be omitted; with several it
  is required.
- Consequence of "exactly one match": a foundation has one zrun copy,
  so all containers and systems of that foundation share one pool. To
  spread load, build a second foundation under another name
  (accepted 2026-10-09).

### 4.2 Bootstrap

1. The user names the locations for each space: one dataset each for
   config and build, one or more for lake and run.
2. shtetl shows the property table (4.3) for each root: property,
   default, the value shtetl.conf sets if any, and the value it will
   use. Pool properties it cannot set (ashift) are shown read-only with
   a warning when they differ from the recommended value.
3. On confirmation it creates the layout under each root (4.4 - 4.7)
   with those properties, and sets `shtetl:space` on each root.
4. If the config root is empty, it clones zshtetl-recipes into it.

**A zrun root on a pool that cannot boot is allowed** (ruled). shtetl
still builds system datasets there and lets the user create systems,
but never sets `mountpoint=/` on them and warns when the user tries to
enable one. Whether a pool can boot: it is the pool in
`vfs.root.mountfrom`, or it has `bootfs` set (to confirm in M6).

### 4.3 Property defaults (from parasa)

Defaults, overridden per space by shtetl.conf (5.2). Taken from
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
| zrun/foundations | mountpoint=/shtetl/foundations, inherited below |
| zrun/foundations/F and children | canmount=noauto, readonly=on; mounted only while an update reads them (9) |
| zrun/systems/S | mountpoint=none until deployed; then `zfs set -u mountpoint=/` |
| zlake/** | mountpoint=none, canmount=noauto; mounted only while built into |
| config root, build root | a mountpoint; config is read on every run |

Not carried over from parasa: GELI (`-B none -l 256 -s 4096`), glabel,
vdev topology, the boot disk's GPT layout. shtetl consumes existing
devices.

### 4.4 zconfig: the recipes clone

The config root **is** the zshtetl-recipes working tree:

    <config root>/
        .git/
        .gitignore             upstream: ignore everything it does not ship
        shtetl.conf            site settings (5.2)
        default.conf           base layer of every recipe
        foundations/
        containers/
        systems/

**ZFS is the record; git is only for sharing** (ruled 2026-10-09).
Upstream's `.gitignore` ignores everything upstream does not ship, so
nothing of the site's reaches a remote unless the user says so, file by
file. The history of the site's files is zconfig's snapshots:

- shtetl snapshots the config root before any change it makes there
  and before any git operation;
- every snapshot shtetl takes anywhere (a foundation version, a save,
  an update's result) records the config snapshot it was made with in
  `shtetl:config` (name and guid), so any build or save leads back to
  the exact recipes that produced it.

Sharing a file (PROPOSAL): `shtetl share containers/nginx.conf` runs
`git add -f` on it. A tracked file is no longer subject to
`.gitignore`, so the shared `.gitignore` is never edited locally and
never conflicts with upstream's.

A hazard, tested 2026-10-09 with git on the mac: git treats ignored
files as expendable. A local, ignored `containers/nginx.conf` was
overwritten without a word when `git pull` brought an upstream file at
the same path. So `shtetl recipes pull` snapshots the config root
first, compares the incoming paths with the ignored files present, and
refuses on a collision, naming the file.

### 4.5 zbuild: the src checkout

The build root **is** the FreeBSD src checkout (git clone of
freebsd-src). Nothing else lives in zbuild. obj caches live in zlake
(4.6), one per foundation.

### 4.6 zlake

    <lake root>/
        foundations/
            F/                 built into directly by zbuild; @v1, @v2, ...
                usr/           children the foundation recipe pre-splits;
                    local/     usr is a canmount=off placeholder
                var/
        foundation-builds/
            F/                 obj cache for F: MAKEOBJDIRPREFIX
        containers/            a mirror of zrun/containers (7.10)
            X/                 clone of foundations/F@vN, as of X's last save
                var/           clone of foundations/F/var@vN
                www/           container-only
            X-staging/         mirror of the staging tree during a soak
        systems/
            S/
        archive/
            containers/
                X-<ISO-date>/  the old tree retired at a cutover
            systems/

The obj cache sits on zlake's disks. If those are slow, builds are
slower than with obj on fast storage; the cache pays off on rebuilds,
best with `WITH_META_MODE` (needs filemon).

### 4.7 zrun

    <run root>/
        foundations/
            F/                 recv'd from zlake; readonly, canmount=noauto
                usr/           canmount=off placeholder
                    local/
                var/
        containers/            mountpoint=/containers
            X/                 clone of foundations/F@vN
                usr/           clone of foundations/F/usr@vN (placeholder)
                    local/     clone of foundations/F/usr/local@vN
                var/           clone of foundations/F/var@vN
                www/           container-only, created by X's recipe
            X-staging/         an update under soak (7.8)
        systems/
            S/                 same shape; mounted at / when booted

All running resources live on zrun. Children mount by inheritance
(`/containers/X/var`). An inherited mountpoint is the parent's plus the
child's name, so a dataset path must mirror the directory it holds:
`usr/local` is a `usr` dataset with `canmount=off` that holds nothing,
and `usr/local` under it, as FreeBSD's own zroot does (parasa's
`usr-local` only worked because parasa set every mountpoint by hand).
With inheritance, a container needs no `mount.fstab` for its
own datasets and a system needs no fstab entries for its own; renames
carry the mountpoints along. So a container needs no `mount.fstab` for its
own datasets and a system needs no fstab entries for its own. nullfs
remains only for one container mounting another's dataset
(`requires`).

## 5. Configuration

### 5.1 Format: UCL (kept, with the comparison asked for)

The question was whether, since UCL needs a port anyway, a more common
format would be better. Every option except sysrc and Lua needs a port,
and those two were already ruled out (no lists; config as code with a
private interpreter). So the port is not a cost that separates them.
What separates them:

| format | for | against |
|--------|-----|---------|
| UCL | FreeBSD's own: base links it for `pkg(7)`, `ctld(8)`, `bhyve(8)` and `iovctl(8)`; every admin has read `pkg.conf`. Heredocs for kernconf and sh. libucl is the library pkg uses, so it is maintained as long as pkg is | bare words are typed (`off` is a boolean, `15.10` a float); duplicate keys and macros need turning off, which shtetl does (5.3) |
| TOML | strict by spec: a duplicate key is an error, bare words are not allowed, no includes. Literal multi-line strings (`'''`) for kernconf and sh | foreign to FreeBSD's base; arrays of tables (`[[derivations]]`) read awkwardly past one level; the C libraries (tomlc99, tomlc17) are smaller projects; `15.10` is still a float |
| JSON | universal | no comments, no multi-line strings: kernconf and shell become `\n`-escaped lines. Not for hand-written files |
| YAML | familiar | whitespace is syntax (mishkan ruled that out); libyaml only gives events, so typing is ours anyway |

Recommendation: **stay with UCL**. It is the FreeBSD-native answer, it
has the one feature recipes lean on hardest (verbatim heredocs), and
its two sharp edges are switched off by how shtetl parses (5.3). TOML
is the honest runner-up: its strictness is built in rather than
configured. The parse is one module that hands the rest of shtetl a
format-free tree, so a later switch costs that module and the
recipes, not the design.

libucl from base is PRIVATELIB (`lib/libucl/Makefile`); shtetl links
the `textproc/libucl` port.

### 5.2 shtetl.conf (site settings)

Its own schema, separate from the recipe cascade: it holds what is
about this machine, never what is about a recipe.

    spaces {
        lake { default = "tank/shtetl"; }   # needed with several lake roots
        run  { default = "fast/shtetl/run"; }
    }
    properties {
        default { compression = "zstd"; }
        lake    { compression = "zstd-9"; }
    }
    build {
        make_jobs = 0;                  # 0 = hw.ncpu
    }
    update {
        suffix = "staging";             # X-staging
        depth = 0;                      # saves replayed per update (7.6.2);
                                        # 0 = every save zrun holds
    }
    retention {                         # 6.3; 0 = keep everything
        run     = 10;                   # bare number: the newest 10 saves
        archive = "1y";                 # date(1) -v units: quoted
    }

There is no `retention.lake`: zlake's mirror holds exactly what zrun
holds (ruled 2026-10-09).

### 5.3 Parsing rules

- Each file is parsed with `UCL_DUPLICATE_ERROR`: a key written twice
  in one file is an error, never a silent implicit array.
- UCL macros (`.include`, `.priority`, `.load`, `.inherit`) are
  errors. libucl's `UCL_PARSER_DISABLE_MACRO` turns them into comments,
  which would silently drop a recipe's `.include`; shtetl must refuse
  instead (how: M1).
- No variables: `$` is literal.
- Unknown keys, and keys that belong to another kind (`kernconf` in a
  container), are errors with file and line.

### 5.4 Recipe cascade: lists are additive

Layers, nearest last:

1. built-in recipe defaults
2. `default.conf`
3. each recipe named in the target's `import`, in order
4. the target recipe (`foundations/F.conf`, `containers/X.conf`,
   `systems/S.conf`)

Lists are additive (ruled 2026-10-09, H): each layer adds to what the
layers below it gave. Q5 of draft 1 asked how a recipe replaces a list
instead; look at what recipes actually need to do to an inherited list:

- drop one element: no `apache24` from a shared web recipe;
- change one element: a different command for `etc/pwd.db`, a dataset
  made readonly;
- add elements: the common case;
- throw the whole inherited list away: rare ("exactly these packages").

Whole-list replacement is the rare case, and editing one element is
the common one. So the useful answer is not a way to replace lists,
but to stop having lists that cannot be edited by element. Every
list in the schema already has a natural identity:

| key | identity |
|-----|----------|
| pkg | package name |
| datasets | dataset path |
| derivations | derived file path |
| requires | container name |
| commands | a label the recipe gives the block |

So every collection is an object keyed by that identity, and the
cascade merges objects the way it already merges any object, key by
key, nearer layer winning. Then:

- add: write a new key;
- change one: write the same key with new fields (fields merge);
- drop one: set it to `false`;
- drop the whole inherited collection: `reset = [ "pkg" ];` at the top
  of the layer, which empties that key before this layer merges in.

Order is kept: libucl iterates object keys in insertion order and a
replaced key keeps its place (`DL_APPEND`, `DL_REPLACE_ELEM` in
ucl_hash.c), and the cascade does the same across layers. A
`commands` block overridden by a later layer runs where it was first
defined.

The common case stays short: an array of names is accepted as
shorthand for an object of `name = true` entries.

    # default.conf
    pkg = [ "sudo", "tmux" ];
    datasets {
        tmp {}
        var {}
        "usr/local" {}
    }

    # common/web.conf
    pkg = [ "apache24", "py311-certbot" ];
    commands {
        certbot = <<EOD
    sysrc certbot_enable=YES
    EOD
    }

    # containers/nginx.conf
    import = [ "common/web.conf" ];
    pkg { nginx = true; apache24 = false; }
    datasets {
        www { readonly = true; }
    }
    commands {
        nginx = <<EOD
    sysrc nginx_enable=YES
    EOD
    }

Merged for nginx: pkg = sudo, tmux, py311-certbot, nginx; datasets =
tmp, var, usr/local, www; commands = certbot, then nginx.

A drop is legal only for what the container's own layers added:
`apache24` came from `common/web.conf`. `tmp = false` or `tmux = false`
would be errors here, because `default.conf` is a layer of the
foundation's recipe too, so the foundation already splits `tmp` and
installs `tmux`, and a container must end with a superset of what its
foundation provides (7.4).

Alternatives considered:

- **A `-` prefix in arrays** (`pkg = [ "-apache24", "nginx" ];`,
  like Portage USE flags). Concise for name lists, but it cannot change
  one field of a structured element, and a leading `-` must then be
  forbidden in every name.
- **Verb objects per key** (`pkg { add = [...]; remove = [...];
  replace = [...]; }`). Explicit, but three spellings for one idea and
  still no way to change a field.
- **Separate keys** (`pkg_replace`, `pkg_remove`). Doubles the schema.
- **UCL priorities**. Rejected with the macros: the result depends on
  numbers scattered across files.

### 5.5 Recipe keys (from mishkan, adapted)

| key | kinds | type | meaning |
|-----|-------|------|---------|
| import | all | names | recipes merged in before this file |
| reset | all | key names | empty these inherited collections first |
| ver | all | string | version constraint: `">=15"`, `"=15.1"` |
| src_branch | foundation | string | FreeBSD branch |
| kernconf | foundation | string or heredoc | config name, or an inline kernel config |
| buildopts | foundation | heredoc | make(1) variables / src.conf |
| datasets | all | keyed | child datasets, created before anything is installed |
| pkg | all | keyed | packages, installed at creation |
| commands | all | keyed heredocs, sh | run once at creation, after pkg; never on update |
| derivations | all | keyed by derived path: {source, command} | how to regenerate a binary from its text source when a rebase conflicts on it (7.7) |
| requires | container | keyed | containers that must exist first |
| jailconf | container | heredoc | appended to the jail's jail.conf block |

A heredoc may name a file instead (`kernconf_file = "gaming.kernconf"`)
so kernel configs and shell stay in files their own tools can check.

A container or system recipe names no foundation (ruled 2026-10-09).
It fits any foundation whose version satisfies its `ver`; the foundation
is chosen when the container is created (`shtetl create nginx --on
generic`) and recorded by the clone's `origin`, as parasa did.

`make_jobs` moved to shtetl.conf: it is about the machine, not the
recipe.

## 6. Snapshots

### 6.1 Names

Names are for people; shtetl reads `shtetl:kind` and other properties,
never the name.

| what | name | shtetl:kind |
|------|------|-------------|
| foundation dataset | `{recipe}`: `generic`, `gaming` | |
| foundation version | `@{ver}-{branch-type}-{ISO-date}-{short-sha}`: `@15.1-releng-2026-10-08-0f691888dc56` | version |
| foundation after a pkg upgrade | the same with `-pkg` | pkg |
| container/system after an update | the version it was rebased onto | version |
| container/system after a pkg upgrade | the same with `-pkg` | pkg |
| save | `@save-<ISO-date>-<HHMMSS>` (UTC) | save |
| a save replayed by an update (7.6.2) | the original's name | save, plus `shtetl:replayed-from` |

The version lives in the snapshot name, not the dataset name (ruled
2026-10-09). A foundation is one dataset for its whole life:
`generic@15.1-releng-...` is followed on the same snapshot chain by
`generic@15.2-releng-...`, so moving a container to a new release is an
ordinary update, gated by its recipe's `ver`.

`ver` and `branch-type` come from `src_branch`: `releng/15.1` is
`15.1-releng`, `stable/15` is `15-stable`, `main` is `<major>-current`
with the major from `sys/conf/newvers.sh`. The short sha is 12
characters, as `uname` shows it. A second build on the same day from
the same sha (a recipe change) gets `.2`, `.3`. Names sort as text
(`15.10` before `15.2`); shtetl orders versions by `createtxg`, never by
name.

### 6.2 Commit messages

Every snapshot shtetl takes carries `shtetl:commit`, the message, set
at creation (`zfs snapshot -r -o shtetl:commit=...`, which libzfs_core
does atomically for every snapshot of the `-r`). `shtetl save X -m MSG`,
or an editor like `git commit` when `-m` is absent. Each snapshot also
carries `shtetl:config`, the config snapshot it was made with (4.4).
They travel with `zfs send` only under `-p` (P10), and `-p` also
carries every property set locally on the dataset: a deployed system's
`mountpoint=/` would land on its zlake mirror, and nothing in zlake
would stop it mounting there (`canmount` does not inherit, P7). So
every send is without `-p`, and after each receive shtetl copies the
source snapshot's `shtetl:*` properties onto the received one (ruled
2026-10-11). Only what shtetl names travels. A received snapshot with
no `shtetl:kind` is one whose copy was interrupted; the next run
copies again from the source, which still has them.

### 6.3 Retention (ruled)

Three policies (ruled 2026-10-09), all written the same way:

| policy | what it bounds |
|--------|----------------|
| `retention.run` | saves kept on zrun; zlake's mirror keeps exactly the same |
| `retention.archive` | retired trees kept in zlake's archive |
| `update.depth` | saves an update replays onto the new foundation (7.6.2); never more than `retention.run` keeps |

Each is one of:

- `0`: everything (the default);
- a bare number: that many, newest first;
- a quoted date(1) `-v` interval (`"7d"`, `"6m"`, `"1y"`): what is
  younger; `retention` is enforced from cron by `shtetl prune`.

Quote the interval: unquoted, UCL reads `6m` as 6000000 and `30d` as a
time in seconds. shtetl reads the string with date(1) units (y, m, w,
d, H, M, S).

zlake does not keep more saves than zrun: a deeper zlake would mean
replaying from zlake rather than zrun, and an update should rebase on
one side only.

Never pruned, whatever the policy: a snapshot that is a clone's origin
(its `clones` property is non-empty), one with holds (zfs_rebase holds
its inputs), the newest save of a dataset, and on zrun the newest
snapshot zlake also has (the next incremental's base).

## 7. Lifecycles

### 7.1 Build a new foundation F

F is named after its recipe (6.1): `foundations/generic.conf` builds
`foundations/generic`.

1. Resolve the recipe cascade; check `ver` against `src_branch`.
2. Create `zlake/foundations/F` and one child per recipe dataset, all
   empty, **before anything is installed**. This pre-split gives every
   container dataset a foundation counterpart to rebase onto.
3. Create `zlake/foundation-builds/F` if missing. Mount it and F
   (children follow by inheritance) at build paths; mount the src
   root.
4. In the src root: check out `src_branch`; `make buildworld
   buildkernel` with `MAKEOBJDIRPREFIX` on foundation-builds/F.
5. `make installworld installkernel distribution DESTDIR=<F mount>`.
6. Foundation pkg and commands, in a chroot of F.
7. Unmount; `zfs snapshot -r` F with the version name (6.1).
8. Unmount foundation-builds/F and src.

### 7.2 Update foundation F

7.1 steps 3 - 8 on the **same** datasets, plus
`make delete-old delete-old-libs DESTDIR=<F mount>` after the install.
One dataset per foundation keeps every version on one snapshot chain,
which is what zfs_rebase needs to derive a base and what makes
zlake -> zrun an incremental send.

A new release is an update like any other: a recipe moved from
`releng/15.1` to `releng/15.2` builds into the same datasets, and the
new version is `@15.2-releng-...`. Installing a newer world over an
older one and running delete-old is FreeBSD's own source-upgrade path.
The datasets of every version of a foundation must match the previous
version's (checked). They are fixed for the foundation's life (ruled
2026-10-09): a different split is a different foundation recipe, and
moving containers to it is out of scope (14, L).

Then, separately and optionally, `shtetl pkg-upgrade F`: pkg upgrade
in a chroot, snapshot `-pkg`. Base and base-plus-packages are both
versions to rebase onto.

Known cost, accepted: installworld rewrites every file, so on the onto
side every file has a new object, generation and ctime, and the rebase
decides each one on content instead of pruning it. A fix exists:
`install -C` leaves a byte-identical target alone, mtime and ctime
included (install(1)), and FreeBSD already installs every library that
way (`INSTALLFLAGS+= -C` in `share/mk/bsd.lib.mk`). Programs and other
files do not get `-C`. Extending it to the whole install keeps
unchanged files' object, generation and ctime, so zfs_rebase prunes
them without reading a byte. Probe P12 tries it; files that
ObsoleteFiles.inc does not list survive delete-old either way (P6).

### 7.3 Bring a foundation to zrun

`zfs send` each dataset of F (full the first time, `-i` from the
newest version zrun has after that) into the zrun copy of F:
`readonly=on`, `canmount=noauto`, and the mountpoint inherited from
`zrun/foundations` (`/shtetl/foundations/F/...`). No `-p`; the
version's `shtetl:*` properties are copied after the receive (6.2). It is never mounted
except while an update reads it: zfs_rebase reads every side at its
mountpoint and refuses one that is not mounted (9).

### 7.4 New container X

`shtetl create X --on F [--version V]`.

1. Checks, beyond parse and schema: X's recipe `ver` allows F's newest
   version (or V); X's own datasets do not collide with F's; and every
   list F provides is kept. Lists are additive; a recipe that overrides
   one (`reset`, or `name = false` for an inherited element) must still
   end with a superset of F's: every dataset F splits and every package
   F installed (ruled 2026-10-09). F's lists at version V are its recipe
   as of the config snapshot V records in `shtetl:config`.
2. The foundation version on zrun (7.3), building it first if asked.
3. `zfs clone` each dataset of `F@vN` to the matching name under
   `containers/X` (clone is not recursive; one per dataset).
4. Create the recipe's container-only datasets as children of X.
5. Write the jail.conf block; start the jail.
6. `pkg -j X install`, then commands. A command block the foundation
   already ran (same label, same text, in the foundation's recipe as of
   its `shtetl:config`) is not run again (ruled 2026-10-09):
   `default.conf` is a layer of both recipes, so without this its
   blocks would run twice. Packages need no such rule: pkg skips what
   is installed.
7. Save (7.5).

### 7.5 Save X

1. `zfs snapshot -r -o shtetl:kind=save -o shtetl:commit=MSG
   containers/X@save-...`.
2. Per dataset, into the matching name under the lake root's
   `containers/X`:
   - first save of a foundation-inherited dataset: a clone stream
     (`send -i <zrun F/d@vN> X/d@save`) received as a clone of the zlake
     `foundations/F/d@vN`, so zlake holds only the delta;
   - first save of a container-only dataset: a full stream;
   - later saves: `send -i` from the previous save.

   Never `-p`: after each receive, the save's `shtetl:*` properties
   are copied onto zlake's snapshot (6.2).
3. Prune by retention (6.3).

### 7.6 Update X onto F@v2 (into staging)

`shtetl update X [--to V]`. The target is F's newest version, or V,
and X's recipe `ver` must allow it: a recipe that pins `"=15.1"` refuses
a 15.2 target at check time.

1. Checks: the target is on zrun (7.3) and its datasets match X's
   current version's; no staging tree for X exists; no open rebase.
2. **Stop X** (ruled 2026-10-09). X stays stopped until cutover or
   abandon. Nothing writes to it during the update, so the staging
   tree cannot drift from it, and X is the rollback. Its datasets stay
   mounted: the rebases read them.
3. From-snapshot: a save of X (7.5 step 1), message "update to
   F@<version>".
4. Mount the target foundation's datasets on zrun, read-only, at their
   mountpoints (9).
5. Per dataset inherited from F, parent first:
   - a placeholder (`canmount=off`, like `usr`) is never mounted, so it
     can never be a rebase's side; it holds nothing, and shtetl clones
     it from the target instead: `zfs clone F/usr@<target>
     X-staging/usr`;
   - any other dataset is replayed (7.6.1, 7.6.2). Step 1 is a clone
     form rebase that creates `X-staging/d`; shtetl then places it
     (`zfs inherit mountpoint`, `readonly=off`, mounted at
     `/containers/X-staging/...`), because steps 2 and on are dataset
     form rebases of it, and the dataset form takes only a mounted
     dataset.
   Backing out runs `--abort` on every rebase this update started.
6. Container-only datasets: `zfs clone X/www@<from> X-staging/www`.
   Nothing in a foundation update touches them, so a clone is the
   whole of their "rebase"; it is promoted at cutover (7.9).
7. Unmount the foundation's datasets.
8. Snapshot X-staging with the version name of the target (6.1).
9. Start X-staging with X's own jail settings, address included: X is
   stopped, so nothing collides.
10. Save X-staging (7.10 mirrors it into zlake). This first sends X's
    from-snapshot to zlake if it is not there yet: the mirror's
    container-only children are clones of it.

The suffix is `update.suffix` in shtetl.conf, `staging` by default.

### 7.6.1 One rebase

    zfs_rebase --from containers/X/d@<save> \
        --onto foundations/F/d@<target> \
        --result containers/X-staging/d

The tool derives base `F/d@<X's version>` from X/d's origin. Exit 0 =
done; 1 = stopped at conflicts (7.7); 2 = refused before touching
anything; 3 = failed part way, or drift at the final check. Base, from
and onto must all be mounted at their mountpoints (9).

### 7.6.2 History replay (`update.depth`)

zfs_rebase moves one snapshot at a time, so keeping a container's
history across an update means replaying its saves in order (ruled
2026-10-09). With X's saves on zrun, oldest first, as many as
`update.depth` allows, `s1 ... sn` (sn is the from-snapshot):

    step 1   zfs_rebase --from X/d@s1 --onto F/d@<target> \
                 --result X-staging/d
             base derived: F/d@<X's version>; conflicts answered here
    step k   zfs_rebase --allow-unrelated --base X/d@s(k-1) \
                 --from X/d@sk --onto X-staging/d --result <name of s(k-1)>
             dataset form: the pre-apply snapshot the tool takes is the
             replayed s(k-1)
    last     snapshot X-staging/d with sn's name

**Why step k names its base** (ruled 2026-10-09). In every step, base
and onto are the same point in history, before and after the move:

| step | from | onto | base |
|------|------|------|------|
| 1 | X@s1 | F@<target> | F@<X's version>, the divergence point (derived) |
| 2 | X@s2 | s1' | X@s1 |
| k | X@sk | s(k-1)' | X@s(k-1) |

So each step carries only what the admin did between two consecutive
saves onto the already-moved copy. Step 1 is the special case: its
"same point" is the foundation itself, before and after.
Left to derive it, the tool walks both origins back to the foundation
and takes `F/d@<X's version>`, the divergence point, at every step.
That is right for step 1 and wrong after it, because onto is no longer
the foundation but the result so far, which already holds s1. Two
cases, with the foundation update leaving `rc.conf` alone:

- A file edited twice. The admin adds line A to `rc.conf` before s1
  and line B before s2. Step 2 with base v0: base has neither line,
  from has A and B, onto (s1') has A. Both sides changed the file
  differently from base: a conflict, though the admin only added B.
  With base s1: base has A, from A and B, onto A; only from changed,
  and B goes in cleanly.
- A conflict already answered. `master.passwd` conflicted in step 1
  and was merged into M; the admin did not touch it after s1. Step 2
  with base v0: from holds the admin's version, onto holds M, both
  differ from v0: the same conflict again, and again at every later
  step. With base s1: from equals base, so M stands.

This is how git replays a branch: each commit is cherry-picked with its
own parent as the merge base, not the branch point.

zfs_rebase does this today: `--allow-unrelated --base` takes any
snapshot no newer than either side (`createtxg`), and the dataset form
takes the pre-apply snapshot under the name `--result` gives
(`unrelated_base()`, `preconditions()`, `onto_open()` in src/run.c).
It is lightly tested: the box suite runs `--allow-unrelated --base`
only as a dry run (tests/box/run-fixture.sh step 0a); a real apply
under it (matrix row ZX5) is planned, and the dataset form under it has
never run. Probe P14.

**Cost.** `--allow-unrelated` turns the tool's prune off, so each step
after the first reads every file of the dataset on all three sides.
Two possible changes to zfs_rebase (Q-N, ruled 2026-10-09: neither
now; P14 measures first):

1. Prune from's side only. The prune's own contract
   (`zr_oracle_prune` in src/yellow.h) says object numbers mean the
   same thing "when the side really descends from base", and the call
   site already prunes one side at a time (`zr_oracle_prune(..., 1,
   ...)` and `(..., 2, ...)` in src/run.c, behind one `r->prune`
   flag). In a replay step from is a later snapshot of base's own
   dataset, so from's side qualifies and onto's does not. The change:
   accept `--base` without `--allow-unrelated` when it is an earlier
   snapshot of from's dataset, and prune side 1 only. This skips the
   base-against-from reads, roughly half of what a step reads today.
2. Compare on demand. `zr_oracle_assign` compares every pair of trees
   holding one name, up front, so onto would still be read against base
   for every file. For a name from left unchanged, the outcome is onto's
   whatever onto holds, and the dataset form already holds it; only
   names from changed need onto compared. Comparing only what a
   decision needs would bring a step down to about its own changes. It
   reaches into the oracle and the decision rules (a hardlink pool
   from left alone may still need onto's classes), so it must be
   checked against the v4 theory, whose green-pooling note is frozen.

Onto's side cannot simply be pruned too. Base and onto do share an
ancestor (the old foundation version), but after it the two lineages
allocate object numbers independently, and a match would rest on
generation and ctime coinciding by chance, which the prune does not
accept as proof.

**Names.** Each replayed snapshot keeps its original's name and
`shtetl:commit`, plus `shtetl:replayed-from` (the original's guid), so
the staging tree's snapshots line up by name with the container-only
datasets', which carry the originals themselves after the promote.

`update.depth = 1` replays only the from-snapshot: one rebase per
dataset. Container-only datasets are never replayed.

### 7.7 Conflicts and derivations: the text first

A rebase that exits 1 stops at its conflicts gate with every clean
action already applied, and a resolution document beside its manifest:
one line per conflicted name, choice `-`, to be answered `keep`,
`onto` or `from`.

zfs_rebase merges nothing by itself. A file both sides changed is a
conflict however far apart the edits are; the three-way merge lives
only in the picker, runs when a person opens the name, and writes only
on `w` (v4-merge3.md: "nothing merges automatically"). So a conflicted
derived file almost always has a conflicted source behind it: whoever
changed the binary changed its source too. The derivation can only run
once the source is merged, so the text goes first (ruled 2026-10-08):

1. For each conflicted name that is a derivation's derived path,
   shtetl answers the line `keep` and notes the derivation. The picker
   will show these lines already answered.
2. `zfs_rebase --continue -i --no-merge IDENT`: the person merges the
   text in the picker. `--no-merge` holds the gate even when the
   document is complete.
3. For each noted derivation whose source is now in the tree (its line
   answered `keep`, a hand merge, or `onto`, which the clone already
   holds; or a source that was never in conflict): run the command once
   per distinct command, in a chroot of the result's private mount.
4. `zfs_rebase --continue IDENT`: applying2, the final check, done.
5. A source answered `from` reaches the tree only in applying2, so its
   derivation runs after done instead, before the result is placed
   (7.6 step 5).

Derivations run only here. A derived file that is not conflicted is
consistent with its source already: whichever side changed the text
also regenerated the binary.

The headless future: zfs_rebase's planned merge drivers
(sprints/future-features.md, "Merge drivers") run a rules file at the
gate before the picker, exit 0 = merged (`keep`), 1 = left for the
picker. A built-in text driver that accepts only merges with no
conflict chunk, followed by shtetl's derivations as rules, would do
steps 1 - 3 without a person whenever the text merges cleanly. shtetl
needs one thing added to that design: rules run in the order written,
so sources settle before the files derived from them.

Open detail: a derivation whose source and output are in different
datasets spans two rebases and two private mounts.

### 7.8 Soak

X-staging runs; X is stopped and untouched. Rolling back is `shtetl
abandon X` (7.9). `shtetl pkg-upgrade X-staging` during the soak takes a
`-pkg` snapshot, so the plain and upgraded states are both there to
pick from. If the admin starts X again during the soak, X can drift from
the staging tree, and cutover says so (7.9).

### 7.9 Cutover and abandon (human verbs)

Destroying the old tree is the user's decision, made after the soak.

`shtetl cutover X`:

1. If any dataset of X has `written@<from>` above zero (X was started
   again during the soak), say how much and ask; on yes, take a final
   save of X so its archive holds those writes.
2. Stop X-staging.
3. `zfs promote` each container-only clone in X-staging; it takes over
   the snapshots up to `<from>`, so its history continues (probe P11).
4. Destroy the old X tree on zrun (it is in zlake, 7.10).
5. `zfs rename containers/X-staging containers/X`; mountpoints follow
   by inheritance. Rename the jail.conf block.
6. Start X. Save.

`shtetl abandon X`: stop X-staging; destroy it and its zlake mirror;
start X.

### 7.10 zlake mirrors zrun

zlake/containers and zlake/systems are a mirror of zrun's dataset graph
as of each entity's last save: same names, same clone relationships,
one pool over. Every clone, promote and rename shtetl does on zrun is
done to the mirror at the next save:

- during a soak, X-staging's mirror is a clone of the zlake F@v2, and
  its container-only children are clones of the zlake `X/www@<from>`;
- at cutover, the mirror promotes the same children, the old X moves
  to `archive/containers/X-<ISO-date>`, and X-staging is renamed to X.

The archive keeps the old tree whole, including anything written after
the from-snapshot if X was started again during the soak. Archives are
pruned by `retention.archive`.

### 7.11 Systems

The same as containers, except:

- deploy: `zfs set -u mountpoint=/ systems/S` and a one-shot boot;
  children mount at /var, /usr/local and so on by inheritance;
- **a system is never updated while it runs, and everything happens
  from outer space, cutover included, so nothing can drift** (ruled
  2026-10-09);
- soak: boot S-staging once (the one-shot mechanism `bectl activate -t`
  uses), and come back to outer space to cut over or abandon.

Outer space is a root-on-RAM system reached with `reboot -r` (ruled
2026-10-09; the user's doc on it is pending). A shutdown hook was
considered and declined.

### 7.12 Restore

Any entity can be rebuilt on zrun from zlake: its foundation version,
then its mirror, sent back. Exercised as a test, not only documented
(M7).

## 8. History (discussion)

History moves forward with the container (ruled 2026-10-09):

- **Rebased datasets** carry their saves by replay (7.6.2), as deep as
  `update.depth` allows. The replayed snapshots have the originals'
  names and messages and point back at them with
  `shtetl:replayed-from`.
- **Container-only datasets** carry theirs by the promote at cutover.
- **The old tree** goes to zlake's archive whole, kept by
  `retention.archive`.
- **zrun and zlake's mirror** keep the same saves, by `retention.run`.
- **Foundations** keep one chain of versions until pruned.

Proposal: `shtetl log X` walks the live tree's snapshots, then each
archive through `shtetl:archive` (on the new root: the archive it
replaced), without parsing names.

## 9. Using zfs_rebase

Facts from the tool's README that shape shtetl:

- One dataset per run. The walk reads `.zfs/snapshot`, where a child
  dataset is an empty directory, and a mount inside the tree is refused
  (`nested mount`, src/walk.c). shtetl loops over datasets.
- The tool derives the base: from and onto must share an origin
  lineage. This is why foundation recipe datasets are pre-split (7.1
  step 2). The one exception is `--allow-unrelated --base`, which
  history replay uses for every step after the first (7.6.2).
- Clone form (`--onto` a snapshot) creates `--result` and never places
  it. shtetl places results in the staging tree (7.6 step 5).
- The resolution document is the interface for 7.7: shtetl writes
  `keep` lines into it and runs `--continue`.
- Every side must be mounted at its own mountpoint. The tool reads a
  snapshot at `<mountpoint property>/.zfs/snapshot/<name>` (`snapdir()`
  in src/run.c) and refuses a dataset that is not mounted
  (`preconditions()`), base included. Hence: the zrun foundation copy
  has a real mountpoint and is mounted while an update reads it (7.3);
  a `canmount=off` placeholder is cloned, never rebased; and a staging
  dataset is placed before a dataset form step takes it (7.6 step 5).
- The tool holds its input snapshots for the life of the rebase and
  releases them at done or --abort. On FreeBSD a hold release
  force-unmounts that snapshot's `.zfs` view: shtetl must not have
  anything open under `.zfs/snapshot` of an input when a rebase ends.
- A verb names its rebase by IDENT; shtetl uses the result dataset
  name. shtetl never runs two verbs on one rebase at once.

## 10. Implementation (C)

Layout, after zfs_rebase:

    src/core/      config, cascade, schema, planning: portable, make check on the mac
    src/fbsd/      libzfs, jail, exec of make/pkg/git/zfs_rebase
    tests/         per-phase matrices first (tests/MATRIX.md), then tests

Libraries: libzfs, libzfs_core and libnvpair (headers from /usr/src,
as zfs_rebase's Makefile does), libucl (port), libjail (base), curses
(base) for the TUI. Programs run with argv arrays, never a shell: make,
pkg, git, jail, zfs_rebase.

Pain points to settle early (M1/M2):

- libzfs is not a stable API; the build needs the OpenZFS headers from
  /usr/src. Copy zfs_rebase's Makefile block.
- send/recv: lzc_send/lzc_receive cover one dataset; clone streams and
  receiving as a clone may need libzfs `zfs_send_one`/`zfs_receive`
  with flag structs. Fallback: two exec'd `zfs` processes in a pipe,
  both statuses checked.
- Every exit path must undo what it did: mounts, started jails,
  half-made datasets, open rebases. A cleanup stack in the core, tested
  by kills at every step as the rebase harness does.
- The parse is one module behind a format-free tree (5.1).

## 11. Testing

Per the project rule: plot each phase's problem space as a matrix
(dimensions crossed into cells, each cell a test, a mapping or a
documented deferral) before writing its tests. Phases: config and
cascade, discovery, bootstrap, foundation build, beam-down, create,
save, update, history replay, gate handling, soak, cutover, abandon,
mirror, restore, prune. Kill tests at every step of create, save, update and cutover.

## 12. Milestones

| M  | scope | where |
|----|-------|-------|
| M0 | probes (section 13) | box |
| M1 | skeleton, UCL loader, schema, cascade, `shtetl config show` | mac |
| M2 | discovery and multi-rooting, bootstrap, property table | box |
| M3 | foundation build and update into zlake; pkg-upgrade | box |
| M4 | beam-down, new container, save with messages, the zlake mirror | box |
| M5 | update into staging, history replay, gate handling and derivations, cutover, abandon | box |
| M6 | systems: outer space, one-shot boot, update | box/VM |
| M7 | restore, prune, archives | box |
| M8 | TUI | mac + box |

## 13. Probes (M0): standalone tests before the design commits

Each probe is a script on a throwaway pool on the box, written down
with what it printed. The scripts are in `probes/` (`probes/README.md`):
`sudo sh probes/run.sh` runs the ZFS ones, `--world` adds P6, P12 and
P15, and the transcripts land in `probes/results/`.

P1. Clone-stream receive: `zfs send -i zrun/foundations/F@v1
zrun/containers/X@s1 | zfs recv zlake/containers/X` where
zlake/foundations/F@v1 exists. Does it land as a clone of it? Does it
need `-o origin=`?

P2. A child's lineage under a parent that changes lineage:
zlake/containers/X is a clone of F@v1 with a child www carrying @s1.
Receive the new X as a clone of F@v2 at a new name, move www under it,
destroy the old X, rename the new X into place. Does `send -i www@s1
www@s2` still receive?

P3. P2 on zrun with clone and promote instead of move (7.6 step 6,
7.9 step 3).

P4. A clone form rebase creating `containers/X-staging/var` under a
placed, mounted `X-staging`; then a dataset form step on the child
while the parent stays mounted.

P5. Rebuild in place, then rebase a container whose admin edited
/etc/rc.conf where v1 -> v2 left rc.conf's bytes unchanged. Expect no
conflict. Measure the rebase's time and the incremental send's size.

P6. `make delete-old delete-old-libs DESTDIR=` after an in-place
install: what remains that a fresh install would not have?

P7. Mountpoint inheritance for clones under containers/ and across
pools; `zfs mount -a` order.

P8. `written@<from>` on a container started again during the soak:
the cutover warning.

P9. Clone per child at creation: does anything require the parent
clone first?

P10. Does `shtetl:commit` on a snapshot arrive with `zfs send -i`
(with and without `-p`)?

P11. Promote a container-only clone at cutover, then mirror the
promote in zlake: does the next incremental save still receive?

P12. installworld with `-C` for every file: what still changes when
nothing changed, and how much faster is the rebase?

P13. Multi-rooting: two lake roots in two pools; lookup, the
"exactly one match" error, and a mirror created in the root holding
its foundation.

P14. History replay step k (7.6.2): the dataset form with
`--allow-unrelated --base X/d@s(k-1)`; the pre-apply snapshot as the
replayed s(k-1); timing and the bytes compared (`-v`) against the
dataset's size, for Q-N.

P15. A cross-release update: install 15.2 over a 15.1 foundation with
delete-old, then rebase a container from 15.1 onto it.

P16. The mount requirement end to end: a zrun foundation with
`canmount=noauto` and an inherited mountpoint, mounted read-only for an
update and unmounted after; a `canmount=off` placeholder refused as a
side (expected) and cloned instead.

## 14. Rulings

### 2026-10-11 (P10)

- Sends never use `-p`; shtetl copies the `shtetl:*` properties onto
  each received snapshot (6.2). `-p` would carry the dataset's local
  properties too, a deployed system's `mountpoint=/` among them.

### 2026-10-09 (open questions of draft 2)

- A: zshtetl-recipes ships a catch-all `.gitignore`; a user whitelists
  what they share. git is only for sharing; ZFS snapshots are the
  record of the site's configuration.
- B: a foundation has one zrun copy, so its containers share a pool.
- C: a container is stopped for its update; the staging tree takes its
  address; the old tree stays stopped as the rollback.
- D: systems are updated entirely from outer space, cutover included.
- E: three policies, `retention.run` (zlake's mirror follows it),
  `retention.archive` and `update.depth`; history moves forward by
  replaying saves one at a time; container-only datasets never need
  replaying.
- F: a container or system cannot drop a dataset its foundation
  provides.
- G: recipes name no foundation; `ver` gates which foundations they
  fit. A foundation dataset is `{recipe}`, and the version is in the
  snapshot name, `@{ver}-{branch-type}-{ISO-date}-{short-sha}[-pkg]`,
  so a new release continues the same dataset.

### 2026-10-09 (the 7.4 proposal)

- A container does not run a command block its foundation already ran
  (same label, same text, in the foundation's recipe as of its
  `shtetl:config`).

### 2026-10-09 (Q-N)

- Neither change to zfs_rebase's prune now (7.6.2, "Cost"). Probe P14
  measures a replay step (`-v` prints the bytes compared); the numbers
  decide, and change 1 (prune from's side only) comes first if needed.

### 2026-10-09 (Q-J)

- History replay: step 1's base is the divergence point; every later
  step's base is the previous original save, and its onto that save's
  replayed copy (7.6.2). It runs on today's `--allow-unrelated --base`.
- The mount findings of 9 (foundation copies mounted during updates,
  placeholders cloned, staging placed before step 2) are accepted.

### 2026-10-09 (questions H - M of draft 3)

- H: lists are additive. A recipe that overrides a list the foundation
  provides must still end with a superset of it: datasets and packages
  alike.
- I: a foundation's datasets are fixed for its life.
- K: no dry-run preview before an update.
- L: moving a container to a different foundation recipe is out of
  scope: there is no common base to rebase on.
- M: systems use outer space (a RAM root after `reboot -r`); no
  shutdown hook.

### 2026-10-08 (questions on the story)

- Conflicts resolve the text first; derivations follow (7.7).

### 2026-10-08 (review of draft 1)

- Names zlake, zconfig, zbuild, zrun are fine.
- Discovery by `shtetl:space`; multi-rooting for lake and run; more
  than one match for a child is an error.
- zbuild is the tagged src dataset; no zbuild/obj. obj caches are per
  foundation in zlake/foundation-builds.
- shtetl.conf lives in the recipes repo; default .gitignore; a
  contributor un-ignores what they upstream.
- A zrun that cannot boot is allowed; systems are disabled there.
- Every save snapshot carries a commit message in `shtetl:commit`.
- Retention: 0 = infinite (default); bare number = trailing count;
  date(1) interval = age, pruned from cron.
- Foundation names `{recipe}-{ver}-{branch-type}@<ISO-date>-<short-sha>`.
- Commands never rerun on update. pkg upgrades are decoupled from
  rebases and get their own `-pkg` snapshot.
- Derivations run only to resolve a rebase conflict on a derived
  binary, answered `keep` before the picker opens.
- An update needs only a from-snapshot; X may be running. Warn about
  data written after it, and again at cutover.
- Updates land in X-<suffix> for a soak; cutover is a human decision;
  both old and new are kept in zlake, the old as an archive.
- A system is never updated while it runs; updates run from a RAM root
  after `reboot -r`.
- The install-rewrite cost is accepted; compare-before-write is a
  later improvement.

## 15. Open questions

None. The next step is M0: the probes of section 13 on the box.

## 16. Dropped from parasa

foundation.git, foundation.zfs (replaced by zlake/foundations),
parasa.git, recipes.git on the data pool, .git datasets in trees,
mtree.dist, diff.sh drift classification, the two-commit save,
pkg.list (packages are rebased with /usr/local), compose.sh replay on
update (commands run once), the container-data and system-data trees,
mount.fstab for a container's own datasets, stage0 disk preparation,
the separate build workspace and DESTDIR.
