# shtetl: planning

Status: draft 4, 2026-10-09. M0 is done (13.1); M1, the configuration,
and M2, discovery and bootstrap, are built and pass on the box
(2026-10-11/12, section 12); M3 is under way. Everything is on one
branch, main. Drafts 1 to 3 were reviewed and every ruling
is folded in; section 14 lists them so each can be traced. Section 15
gathers what M1 and M2 decided where the plan was silent, for a
ruling. A detail marked PROPOSAL has not been ruled on but blocks
nothing.
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

Each space is one dataset, its root (a pool's root dataset counts),
that the user already has. shtetl never partitions, encrypts, labels or
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

### 4.1 Discovery

Bootstrap sets a local user property on each root:

    shtetl:space = lake | config | build | run

At startup shtetl iterates the imported pools for datasets carrying a
**local** `shtetl:space` (an inherited value is not a root).

**One root to a space** (ruled 2026-10-12, 14). Its layout, the
skeleton of 4.4 - 4.7, is there once, made or taken by bootstrap:
`foundations/` under the lake root, `containers/` under the run root,
and so on. So every name has one place, and where something new goes
is no choice: a foundation in the lake root's `foundations/`, its
copy in the run root's, a container in the run root's `containers/`.
Everything a space holds clones or renames within it (a container is
a clone of its foundation, a mirror a clone stream of the lake's, an
archive a rename), and a clone cannot cross pools, so a space being
one dataset keeps it in one pool.

As built (M2, `src/core/spaces.c`; `shtetl spaces` lists the roots):
the walk is libzfs's, every imported pool's filesystems, a user
property counted only where its source is the dataset itself. Refused:
a value that names no space; two roots of a space; a root inside
another root (PROPOSAL, Q-Y), since each root's layout is shtetl's
own. A pool whose root will not open refuses the walk, naming it: a
root missed could hide a second one. Without `-C`, the configuration
is read from the config root's mountpoint, where it is mounted.

### 4.2 Bootstrap

1. The user names the location of each space: one dataset each.
2. shtetl shows the property table (4.3) for each root: property,
   default, the value shtetl.conf sets if any, and the value it will
   use. Pool properties it cannot set (ashift) are shown read-only with
   a warning when they differ from the recommended value.
3. On confirmation it creates the layout under each root (4.4 - 4.7)
   with those properties, and sets `shtetl:space` on each root.
4. If the config root is empty, it clones zshtetl-recipes into it.

As built (M2, `src/core/boot.c`):

    shtetl bootstrap [--lake ds] [--run ds] [--config ds]
        [--build ds] [--recipes url] [-y]

- The datasets must exist: the spaces are datasets the user has, and
  bootstrap creates none of the roots (PROPOSAL, Q-Z). Each root's
  whole skeleton (4.6, 4.7) is there once bootstrap is done: what is
  missing it makes, and what is there as shtetl makes it, it takes.
- Everything is planned and checked before anything is done, and the
  plan is shown: per root, each property with its built-in default,
  shtetl.conf's value, the value used, the value now and its source,
  a mark on what changes; the layout it makes; each pool's ashift
  (the top-level vdevs', with the warning) and autotrim. It acts on
  `-y`, or on y or yes from a terminal; with neither, nothing is done.
- Refused before anything is done: a space named twice (a usage
  error); a dataset that is not there, or is a root already; a second
  root of a space, named or there already; a root inside or
  around another; a lake or run root with any child that is not
  shtetl's layout, so that what bootstrap sets reaches nothing of the
  user's (PROPOSAL, Q-Z); layout there but not as shtetl makes it; a
  config or build root with no mountpoint.
- Order: pools (autotrim), the roots' properties, the layout, mounts,
  the recipes clone, and `shtetl:space` on each root **last**. A
  bootstrap stopped part way is finished by the same command again,
  which takes the layout already made.
- The recipes are cloned only with `--recipes`, since zshtetl-recipes
  does not exist yet, and only into an empty config root, after a
  snapshot of it (4.4). A failed clone marks no root (PROPOSAL, Q-AB).
- A pool imported with an altroot shows mountpoints below it; the
  layout already there is read below the altroot.

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
| ashift   | 12      | fixed at pool creation; report only (M2: the top-level vdevs' ashift, from the pool's config; the pool property of that name is only the default for vdevs added later) |
| autotrim | on      | settable |

Dataset properties on each space root, inherited below except for
`canmount`, which ZFS never inherits (P7): on a root it keeps the root
itself unmounted and nothing more, and every other dataset that must
not mount is given its own (below, 7.3, 7.4):

| property  | default     |
|-----------|-------------|
| atime     | off         |
| compression | zstd      |
| aclmode   | passthrough |
| mountpoint | none       |
| canmount  | noauto (the root only) |

Per-role overrides inside the layout:

| dataset | properties |
|---------|------------|
| zrun/containers | mountpoint=/containers, canmount=on: the directory every container is in (ruled 2026-10-11) |
| zrun/foundations | mountpoint=/shtetl/foundations, inherited below; canmount=off, never mounted itself (ruled 2026-10-11) |
| zrun/foundations/F and children | canmount=noauto, readonly=on; mounted only while an update reads them (9): zfs_rebase reads a snapshot through the mounted dataset's .zfs |
| zrun/systems/S | mountpoint=none until deployed; then `zfs set -u mountpoint=/` |
| zlake/** | mountpoint=none, inherited from the root: this is what keeps zlake unmounted; mounted only while built into |
| config root, build root | a mountpoint; config is read on every run |

Not carried over from parasa: GELI (`-B none -l 256 -s 4096`), glabel,
vdev topology, the boot disk's GPT layout. shtetl consumes existing
devices.

### 4.4 zconfig: the recipes clone

The config root **is** the zshtetl-recipes working tree:

    <config root>/
        .git/
        .gitignore             upstream: ignore everything it does not ship
        shtetl.conf            site settings (5.2), and the default
                               layer of every recipe (5.4)
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

Upstream ships `shtetl.conf` with the default recipe in it
(`ver = ">=15.1"`, `kernconf = "GENERIC"`; 5.4), and the site
writes its settings beside it (5.2). It is tracked, so the site's
changes to it are local changes git keeps: a pull that brings an
upstream change to it stops until they are settled. Until the
zshtetl-recipes repository exists, its seed lives in shtetl's own
repository, `shtetl-recipes/`: the shipped `shtetl.conf`, the
`.gitignore` and a README.

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

The reference for every setting and recipe key as built, with run
examples of how files are read and stack, is docs/configuration.md.

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

It holds two things (ruled 2026-10-12): the site's settings, below,
in their own schema, about this machine and never about a recipe; and
beside them, at the top level, the default recipe (5.4), every key any
kind of recipe takes. The settings are its sections (`spaces`,
`properties`, `build`, `update`, `retention`); any other top-level key
is the default recipe's and is checked by the recipe schema, so a
setting never reaches a recipe and a recipe key never reaches the
settings.

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

Built in M1 (`src/core/parse.c`, the one file that includes `ucl.h`):

- Each file is parsed with `UCL_DUPLICATE_ERROR`: a key written twice
  in one file is an error, never a silent implicit array, and an
  object written twice is not merged.
- UCL macros (`.include`, `.try_include`, `.includes`, `.priority`,
  `.load`, `.inherit`, or any name) are errors at their line. How
  (M1): `UCL_PARSER_DISABLE_MACRO`. libucl's header says the flag
  reads a macro as a comment; tried on libucl 0.9.4 (FreeBSD's
  contrib copy), it does not: a macro becomes a key that does not
  begin with a letter, a parse error at its `.`, which shtetl names as
  a macro. Without the flag, the default parser read `/etc/passwd`
  into a test file through `.include`. Since the header and the code
  disagree, shtetl trusts neither: before its first file it tries the
  linked libucl (a macro refused at line 1, `$` literal, a duplicate
  refused) and reads nothing with a library that fails.
- No variables: `$` is literal. In 0.9.4 `UCL_PARSER_DISABLE_MACRO`
  turns expansion off altogether, and `UCL_PARSER_NO_FILEVARS` keeps
  libucl from defining its own.
- Times are left to libucl: an unquoted `30d` arrives as a number, so
  the schema refuses it as no quoted interval (6.3).
- Unknown keys, and keys that belong to another kind (`kernconf` in
  a container's files), are errors with the file and the key path
  (`datasets."usr/local".readonly`), not the line: libucl keeps no
  line on the objects it builds, only on a parse error, and a key
  path names one place, keys being unique in a file. Every problem in
  every layer is reported, not only the first.

### 5.4 Recipe cascade: everything stacks

Ruled 2026-10-12 (14). There are two layers: a foundation's recipe,
and a container's or system's, which stacks on its foundation's when
it is created (7.4). Each is made of files, nearest last:

1. the default: `shtetl.conf`, after what it imports
2. each recipe named in `import`, in order, each after its own imports
3. the recipe itself (`foundations/F.conf`, `containers/X.conf`,
   `systems/S.conf`)

What every recipe starts from is the site's `shtetl.conf`, which
upstream ships with `ver = ">=15.1"` and `kernconf = "GENERIC"`
(4.4). Without a `shtetl.conf` a recipe has no default layer.

Everything stacks: each file adds to what the files below it gave,
and no file takes anything away or replaces a value: `false` as an
entry is refused, dropping values being illegal. How each key
stacks:

| key | stacks as |
|-----|-----------|
| import | names files; no value of its own |
| ver | every file's constraint kept, in order; all must hold together, the foundation's and the container's, or the recipe is refused naming two that cannot |
| buildopts, jailconf, kernconf's lines | text: every file's piece kept, keyed by its file, lower files first |
| pkg, requires | the union |
| datasets, patches | the union by identity (below); an entry's fields compose |
| derivations | the union by identity; each one given once, whole |
| commands | the union by label; a label's text stacks, a piece from each file that gives it, keyed by the file |
| src_branch | one value: every file that gives it must give the same |
| kernconf's config | one value, given by one file of the stack |

A redefinition is refused (ruled 2026-10-12, Q-AC): what one file
gives, another file does not give again, since two files that need
the same element defined two ways are not a composition of the two.
So two files giving a one-value key two values, a derivation given in
two files, or a field of an entry (a patch source's `ref`) given
twice, each refuse the recipe, naming both files. Listing an entry
again with nothing new (`sudo` in two `pkg` lists, `www {}` beside
`www { readonly = true; }`) is no redefinition. Two exceptions:

- a dataset's property may be given again, the nearer file's value
  standing, with a warning naming the file it overrides: the property
  is the clone's own (P9), so a container may set what its
  foundation's dataset had, and shtetl sets it locally on every clone
  it makes, at create (7.4) and on each update's staging clones
  (7.6);
- a commands label stacks, each file's text a piece of its own; a
  piece runs once (7.4).

**The default** (ruled 2026-10-12) is the one file a recipe may
override:

- It may hold any kind's keys; each recipe takes those of its kind
  and leaves the rest (a container takes no `kernconf`, a
  foundation no `jailconf`). shtetl.conf's settings are no recipe's.
- What would be a redefinition of the default's value is an override
  instead, with a warning naming the value replaced and its file: a
  recipe's `src_branch` or `kernconf` config over the default's, its
  derivation over the default's for the same path, its field or
  dataset property over the default's (ruled 2026-10-12).
- What stacks, stacks with the default too: its `ver`, `pkg`,
  datasets, texts and commands reach every recipe of the kinds that
  take them. To be rid of one, the site takes it out of shtetl.conf.
- The files shtetl.conf imports are of the default as well, and hold
  recipe keys only.

Every collection is an object keyed by its element's identity:

| key | identity |
|-----|----------|
| pkg | package name |
| datasets | dataset path |
| derivations | derived file path |
| requires | container name |
| commands | a label the recipe gives the block |
| patches | the source's location |

Order is kept: libucl iterates object keys in insertion order
(`DL_APPEND` in ucl_hash.c), and the cascade does the same across
files, so a `commands` block runs where it was first defined. The
common case stays short: an array of names is accepted as shorthand
for an object of `name = true` entries.

    # shtetl.conf (beside the settings)
    ver = ">=15.1";
    kernconf = "GENERIC";
    pkg = [ "sudo", "tmux" ];
    datasets {
        tmp {}
        var {}
        "usr/local" {}
    }

    # common/web.conf
    ver = ">=15";
    pkg = [ "apache24", "py311-certbot" ];
    commands {
        certbot = <<EOD
    sysrc certbot_enable=YES
    EOD
    }

    # containers/nginx.conf
    import = [ "common/web.conf" ];
    ver = "<16";
    pkg { nginx = true; }
    datasets {
        www { readonly = true; }
    }
    commands {
        nginx = <<EOD
    sysrc nginx_enable=YES
    EOD
    }

Merged for nginx: pkg = sudo, tmux, apache24, py311-certbot, nginx;
datasets = tmp, var, usr/local, www; commands = certbot, then nginx;
ver = `>=15.1`, `>=15` and `<16`, all to hold; no `kernconf`, a
container taking none. `shtetl config show containers/nginx` prints
this, each value with the file it came from, a stacked key as a list
and a text or a commands block as an object of pieces keyed by file
(tests/cli/cl4-nginx.out).

A container keeps everything its foundation has (7.4, ruled H) by
construction now: nothing can be taken away, by the container's files
or by anyone's.

As built (`src/core/cascade.c`):

- `import` is followed in every file, shtetl.conf's included: a
  file's imports merge before it, each file once, at the first place
  it is reached, and a cycle is an error naming its files (PROPOSAL,
  Q-U). A file shtetl.conf imports stays of the default when a
  recipe imports it again.
- `true` over an entry with fields leaves the fields.
- shtetl.conf is checked whole, its settings too, wherever it is
  read, a recipe's resolve included (ruled 2026-10-12); the files it
  imports as files of any kind, holding no settings; every other file
  against the recipe's kind, so a key only a foundation takes is
  refused in a container's files.
- Every file is read and checked before anything merges, so one run
  reports the problems of all of them.
- The warnings go to stderr, apart from what `config show` prints.

### 5.5 Recipe keys (from mishkan, adapted)

| key | kinds | type | meaning |
|-----|-------|------|---------|
| import | all | names | recipes merged in before this file |
| ver | all | string, or a list of them | version constraint: `">=15"`, `"=15.1"` |
| src_branch | foundation | string | FreeBSD branch: main, stable/N or releng/N.M |
| kernconf | foundation | string, or heredoc | a string: the kernel config to start from, a name in the src tree (`GENERIC`) or a path in zconfig; a heredoc: config lines added to it |
| buildopts | foundation | heredoc | make(1) variables, the lines of the src.conf shtetl writes |
| patches | foundation | keyed by location: {ref, dir} | patch files applied to the src tree before the build (7.1.1) |
| datasets | all | keyed | child datasets, created before anything is installed |
| pkg | all | keyed | packages, installed at creation |
| commands | all | keyed heredocs, sh | run once at creation, after pkg; never on update |
| derivations | all | keyed by derived path: {source, command} | how to regenerate a binary from its text source when a rebase conflicts on it (7.7) |
| requires | container | keyed | containers that must exist first |
| jailconf | container | heredoc | options, inside the jail's block shtetl writes |

shtetl writes the files these go into, whole: the kernel config
(`include` of kernconf's config, then `ident` and the lines), the
src.conf, and the jail's `NAME { ... }` block in jail.conf. Whether
`kernconf` is the config or lines is how it is written: a heredoc is
lines even when it holds one line, a quoted string always the config
(the parse keeps libucl's mark of a heredoc, and the start-up check
refuses a libucl that does not set it). A file gives a key once, so
a foundation that starts from a config of its own and adds lines
gives one of the two in a file it imports; giving both in one file is
future work (tracker/shtetl.json, kernconf-config-and-lines). What
`jailconf` gives twice (`allow.raw_sockets` in two files) shtetl does
not judge: jail(8) does.

A container or system recipe names no foundation (ruled 2026-10-09).
It fits any foundation whose version satisfies its `ver`; the foundation
is chosen when the container is created (`shtetl create nginx --on
generic`) and recorded by the clone's `origin`, as parasa did.

`make_jobs` is in shtetl.conf: it is about the machine, not the
recipe.

The forms of an entry, as built (`src/core/schema.c`; `false` is
refused: dropping values is illegal): `pkg` and `requires` take
`true`;
`datasets` take `true` or ZFS properties (`www { readonly = true;
recordsize = "1M"; }`: strings, or true and false for on and off; a
number is refused, "must be a quoted string", since UCL reads `1M`
as 1000000;
`mountpoint`, `canmount` and `shtetl:*` are shtetl's own); `commands`
a string or heredoc; `derivations` `{ source; command; }`, both in one
file; `patches` `true` or `{ ref; dir; }`. A list of names stands for
`name = true` where an entry needs nothing more (pkg, datasets,
requires, patches). In `ver`, `N.N` is that version and a bare `N`
every `N.x`: `=15` is any 15.x, `<=15` takes 15.x, `>15` begins at
16.0 (ruled 2026-10-12). A `ver` no version can satisfy (`">=16"` and
`"<15"`) is refused when the recipe is read, naming the two files.
Names: a recipe's (and a `requires` entry, `update.suffix`) takes
letters, digits, `-` and `_`, so that it is at once a dataset
component, a jail name and a file name (PROPOSAL, Q-W); a dataset
path takes what ZFS names take (`valid_char`).

Checked after the merge: a foundation has `src_branch`; every `ver`
holds together; a kernconf config's path (one with a `/`) and a
patches directory in zconfig exist; `requires` names container
recipes that exist, and not the container itself.

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
3. Mount the src root; check out `src_branch`; apply the recipe's
   patches (7.1.1). A patch that does not apply cleanly pauses the
   build here, before anything is built or installed. Then create
   `zlake/foundation-builds/F` if missing, and mount it and F
   (children follow by inheritance) at build paths.
4. In the src root: `make buildworld buildkernel` with
   `MAKEOBJDIRPREFIX` on foundation-builds/F.
5. `make installworld installkernel distribution DESTDIR=<F mount>
   INSTALL_CMD="install -C"` (7.2).
6. Foundation pkg and commands, in a chroot of F.
7. Unmount; `zfs snapshot -r` F with the version name (6.1),
   carrying `shtetl:patches` (7.1.1).
8. Unmount foundation-builds/F; roll the src root back to before the
   patches (7.1.1) and unmount it.

As built (M3, `src/core/build.c`; patches in 7.1.1; updates to come):

    shtetl build [-y] [-m msg] F

- The plan is shown first and confirmed as bootstrap's is; every
  check is made before anything is done: the roots, the recipe,
  src_branch against ver, the build root mounted and free, no
  foundation of that name yet. After the checkout, REVISION (asked of
  the checkout's newvers.sh) is checked against ver again.
- The recipe is read where the config root is mounted, the directory
  the build snapshots: `-C` may name only that one. The snapshot is
  `<config root>@build-F-<YYYYmmdd-HHMMSS>` (`-2`, `-3` after it for
  another build in the same second), `shtetl:kind=config`,
  and the version records it in two properties, `shtetl:config` (its
  name) and `shtetl:config-guid` (PROPOSAL).
- One build at a time: `shtetl:build=F` on the build root while it
  runs (PROPOSAL).
- The obj cache is mounted at `<run dir>/obj` and F at
  `<run dir>/root`, its datasets below by inheritance, placeholders
  not, by setting the mountpoints for the build and inheriting them
  back after (PROPOSAL). The run directory is
  `/var/db/shtetl/build/F`; it keeps the last build's log
  (`build.log`), the kernel config and src.conf.
- The kernel config shtetl writes is `<run dir>/<F in capitals>`:
  an include of the config named (a name: the checkout's
  `sys/<machine>/conf/<name>`, the machine uname(3)'s; a path: in the
  configuration root), `ident <F in capitals>`, the lines. make gets
  `KERNCONFDIR`, `KERNCONF`, `SRCCONF` and `-j` (`build.make_jobs`,
  or the CPUs online), and `MAKEOBJDIRPREFIX` in its environment, the
  only place make reads it.
- Packages: `pkg -c <root> install -y`, with devfs mounted in F and,
  where F has no resolv.conf, the host's copied in while pkg runs.
  Commands: each piece `chroot <root> /bin/sh -e -c`, in order
  (PROPOSAL).
- The version is every dataset of F at once, with `shtetl:kind`,
  `shtetl:commit` (`-m`, or "build <branch> at <commit>"),
  `shtetl:config` and `shtetl:config-guid`.
- Every step that changes something is recorded and undone on a
  failure anywhere: mounts, mountpoints, the mark, and a new F's
  datasets, which are destroyed. The config snapshot, the obj cache
  and the log stay.

### 7.1.1 Patches

Requested 2026-10-11. A foundation recipe may name patches for the src
tree, applied after the checkout and before the build:

    patches = [ "github.com/eidelbyte/freebsd-patches" ];

or, with options:

    patches {
        "github.com/eidelbyte/freebsd-patches" {
            ref = "releng-15.1";    # branch, tag or commit; default: the remote's HEAD
            dir = "15.1";           # a subdirectory; default: the top level
        }
        "patches/generic" {}        # a directory in zconfig
    }

A source is keyed by its location, like every collection (5.4), and
sources stack, as everything does (5.4). A
location with no scheme is https (`github.com/eidelbyte/freebsd-patches`
is `https://github.com/eidelbyte/freebsd-patches`); a relative path is
a directory in zconfig, which zconfig's snapshot (`shtetl:config`)
already records. Told apart (M1, PROPOSAL, Q-X): a location whose
first component has a `.` is a host; an scp-style `git@host:path` is
refused, to be written `ssh://...`. `patches` is a foundation key: it changes the src
tree, and only a foundation builds one.

**What is applied.** Every file ending in `.patch` at the top of the
source (or of its `dir`), in natural order: runs of digits compare as
numbers, everything else byte by byte. So `0-first.patch`,
`1-second.patch`, ..., `9-...`, `10-...`. Sources go one after the
other, each in its entirety, in the order the recipe lists them (ruled
2026-10-11, Q-Q). Both spellings keep that order: a UCL array is
ordered, and libucl keeps an object's keys in the order written
(`DL_APPEND` in `ucl_hash_insert()`). Across the cascade a lower
layer's sources come first, and a source a nearer layer changes keeps
its place (5.4).

**How** (7.1 step 3):

1. Fetch. Each git source is cloned shallow at its `ref` into the
   build's run directory (`/var/db/shtetl/build/F`), and its commit
   noted. The list of patches is fixed there, so a resumed build
   applies exactly what it started with.
2. Snapshot the src root after the checkout (`@shtetl-patch-0`). Each
   patch N is applied with `git apply`, after a snapshot of its own
   (`@shtetl-patch-N`).
3. A patch that applies cleanly: on to the next. A patch whose reverse
   applies cleanly (`git apply -R --check`) is already in the tree,
   typically merged upstream since it was written: skipped, and the
   build says so.
4. Anything else pauses the build (below).
5. When the build is done (step 8) or abandoned, the src root is
   rolled back to `@shtetl-patch-0` and the patch snapshots destroyed,
   so the checkout is clean for the next build. The fetched sources
   were brought in before the snapshot, so nothing is fetched twice.

**The pause.** shtetl applies the failing patch as far as it can:
`git apply --3way`, which merges against the patch's base blobs and
leaves conflict markers where it cannot, or, where the checkout lacks
those blobs, `git apply --reject`, which leaves `.rej` files. It prints
the patch, the files left to fix and the three commands below, and
stops. Nothing has been built or installed, and F is not mounted: the
mounts of step 3 come after the patches. The src root stays mounted for the person to work in, and
the paused build is recorded on it (`shtetl:build=F`) and in its run
directory. No other build starts while one is paused; there is one src
checkout.

The person fixes the tree, then:

- `shtetl build F --continue`: checked first, no conflict marker left
  in the files the patch touches and no `.rej` file in the tree. The
  patch counts as applied by hand, and the build goes on with the next
  patch, then builds. The person's fix is written out as a replacement
  patch (`<run dir>/<name>.fixed`: the tree's difference from
  `@shtetl-patch-N` to the fixed tree), ready to commit to the patch
  source. A fix made only in the tree lasts only for this build; the
  next update applies the source's patch again and pauses again.
- `shtetl build F --skip`: the tree goes back to `@shtetl-patch-N`,
  the patch is left out, and the build goes on.
- `shtetl build F --abort`: the src root goes back to
  `@shtetl-patch-0` and the run directory is removed; nothing was
  built.

A fix made in the patch source itself: `--abort`, then build again, and
the fetch takes the new patch.

**The record.** The version snapshot carries `shtetl:patches`: each
source with the commit it was fetched at, and each patch that was
skipped as already applied, skipped by hand or fixed by hand. A patch
fixed by hand also keeps its `.fixed` file in the foundation's obj
cache (`foundation-builds/F/patches/<version>/`), since the run
directory goes when the build ends. A patched tree is uncommitted
changes to the checkout, so the build's version string says
`<sha>-dirty` (newvers.sh). That is kept (ruled 2026-10-11, Q-R): it
names the real FreeBSD commit and shows that patches went in.

As built (M3, `src/core/patch.c`, `src/core/build.c`):

    shtetl build F [--continue | --skip | --abort]

- The fetch: source n into `<run dir>/patches/<n>`, a git source by
  `git init` and `git fetch --depth 1 <url> <ref>` (the remote's HEAD
  without a ref), checked out, its commit noted; a directory of the
  configuration root's by its `*.patch` files copied. The list is
  read from there, so a resumed build applies what it began with.
- Natural order: runs of digits compare as numbers, the rest byte by
  byte, names equal so far by their bytes. Only regular files ending
  `.patch` at the top count; hidden files are left out.
- The pause: the tree is staged (`git add -A`) and written as a git
  tree, the base of the fix; then `git apply --3way`, or, where that
  leaves no conflict marker and did not merge, `git apply --reject`.
  A merge without conflict pauses all the same, for a person to look
  over: nothing merges unseen (PROPOSAL). The record is
  `<run dir>/paused.conf`, UCL read with shtetl's own parse (PROPOSAL);
  shtetl exits 3 (PROPOSAL). A build while one is paused is refused,
  saying so.
- `--continue` and `--skip` read the record and the recipe from the
  config snapshot the build began with, `<config root>/.zfs/snapshot/
  <name>`, so a recipe changed meanwhile is not what gets built. A
  failure taking the pause up leaves it paused.
- `--continue` is refused while a file the patch touches (`git apply
  --numstat`) holds a line of seven `<`, `|` or `>` and a space or its
  end, or has a `.rej` beside it (PROPOSAL; `=======` alone is too
  common to tell). The fix is `git diff --binary` from the base to the
  tree, everything added (a stray file goes in with it), written to
  `<run dir>/fixed/<n>/<name>` and kept in the obj cache as
  `patches/<version>/<n>/<name>`.
- `--abort` needs no recipe: F's datasets are walked and destroyed.
  The log stays, as it does after any build (7.1 as built), where the
  plan above said the run directory goes.
- `shtetl:patches` (PROPOSAL): `<location>[@<commit>][: <name>
  <already|skipped|fixed>, ...]`, the sources joined by `; `; not set
  when the recipe has no patches.

### 7.2 Update foundation F

7.1 steps 3 - 8 on the **same** datasets, with step 5 in three parts:

1. `make delete-old delete-old-libs DESTDIR=<F mount>
   BATCH_DELETE_OLD_FILES=yes`, the new tree's list of obsolete files,
   applied **before** the install. FreeBSD's live upgrade deletes after
   installing because the running system needs its old files while the
   new ones go in; nothing runs from a foundation, and installworld's
   tools come from the build. Deleting first matters for one thing:
   installworld ends with `certctl rehash`, and a delete-old after it
   removes the certificates the new release dropped but leaves their
   entries in `/etc/ssl`, where the foundation would go on trusting
   them (P6 found 56). Ruled 2026-10-11; P6 rerun in this order
   leaves no `/etc/ssl` entry and no backup (13.1).
2. The install, every file with `install -C` (ruled 2026-10-11, below).
3. installworld's own backups deleted, their schg flags cleared first:
   `boot/kernel.old` (installkernel's), the `boot/*.old` loaders,
   `libexec/ld-elf*.so.1.old`, `sbin/init.bak`. Insurance on a live
   system, clutter in a foundation (ruled 2026-10-11).

Files ObsoleteFiles.inc does not list stay, as they do after FreeBSD's
own source upgrade (ruled 2026-10-11; P6 found about 30, LLVM 19's
private libraries among them).

One dataset per foundation keeps every version on one snapshot chain,
which is what zfs_rebase needs to derive a base and what makes
zlake -> zrun an incremental send.

A new release is an update like any other: a recipe moved from
`releng/15.1` to `releng/15.2` builds into the same datasets, and the
new version is `@15.2-releng-...`. Installing a newer world over an
older one and running delete-old is FreeBSD's own source-upgrade path,
in the other order (1 above).
The datasets of every version of a foundation must match the previous
version's (checked). They are fixed for the foundation's life (ruled
2026-10-09): a different split is a different foundation recipe, and
moving containers to it is out of scope (14, L).

Then, separately and optionally, `shtetl pkg-upgrade F`: pkg upgrade
in a chroot, snapshot `-pkg`. Base and base-plus-packages are both
versions to rebase onto.

As built (M3, `src/core/build.c`): `shtetl build F` with F there and
a version is an update; there with none, it is refused.

- The plan says so, `foundation F, an update of @<last>`, each dataset
  kept (`=`). The recipe's datasets must be F's: one the recipe adds,
  or one F has that it does not name, is refused by name. A
  securelevel above 0 is refused (`kern.securelevel`).
- The run makes no dataset; `make delete-old delete-old-libs
  DESTDIR=<F> BATCH_DELETE_OLD_FILES=yes` after the build and before
  the installs; installworld's backups after them, each file's flags
  cleared first, then removed: `boot/kernel.old` and boot's other
  `*.old`, `libexec/ld-elf*.so.1.old`, `sbin/init.bak`.
- A failed update, and `--abort` of a paused one, roll every dataset
  of F back to the last version (PROPOSAL): F is the foundation's
  history, which an update never destroys. A paused build is an
  update when F has a version.

**`install -C`** (ruled 2026-10-11, Q-O). Left to itself, installworld
rewrites every file, so on the onto side every file has a new object,
generation and ctime, and the rebase decides each one on content
instead of pruning it. `install -C` leaves a byte-identical target
alone (install(1)), and FreeBSD already installs every library that
way (`INSTALLFLAGS+= -C` in `share/mk/bsd.lib.mk`). P12 measured it on a 16.0-CURRENT world
installed over itself (13.1). `INSTALL_CMD="install -C"` on the make
line of installworld and distribution needs no change to the tree; it
keeps 24,590 objects as they were (15,478 without it), cuts the
incremental send from 1,010 MiB to 38 MiB and what the rebase compares
from 2.0 GB to 93 MB, and the install takes the same 55 s. What it
still changes is links: install remakes every hardlink (15,959 objects
keep their identity and take a new ctime, nearly all under
`/usr/share`) and every symlink (1,918 new objects: `lib*.so` links,
`/usr/bin` aliases, certctl's hash links). So every install shtetl
runs passes `INSTALL_CMD="install -C"`: installworld, installkernel and
distribution alike.

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
   list F provides is kept. Everything stacks (ruled 2026-10-12), so
   X ends with a superset of F's by construction: every dataset F
   splits and every package F installed; and every `ver` of F's files
   and of X's must hold for F's version. F's lists at version V are its
   recipe as of the config snapshot V records in `shtetl:config`.
2. The foundation version on zrun (7.3), building it first if asked.
3. `zfs clone` each dataset of `F@vN` to the matching name under
   `containers/X` (clone is not recursive; one per dataset), parent
   first. A clone keeps none of its origin's local properties, so a
   placeholder is cloned `-o canmount=off`; without it `usr` mounts
   and hides the root dataset's `usr/` (P9). Never `zfs clone -p`: it
   makes a missing parent as a plain dataset, not a clone (P9). A
   property the container's recipe gives a dataset is set on its
   clone, locally (5.4).
4. Create the recipe's container-only datasets as children of X.
5. Write the jail.conf block; start the jail.
6. `pkg -j X install`, then commands. A command block the foundation
   already ran is not run again (ruled 2026-10-09), and neither is a
   piece of one (ruled 2026-10-12, Q-AC): a block is made of pieces,
   one from each file that gives its label (5.4), and a piece with
   the same label, file and text in the foundation's recipe as of its
   `shtetl:config` was run when the foundation was made. shtetl.conf
   is a layer of both recipes, so without this its blocks would run
   twice. Packages need no such rule: pkg skips what
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
     it from the target instead: `zfs clone -o canmount=off
     F/usr@<target> X-staging/usr`;
   - any other dataset is replayed (7.6.1, 7.6.2). Step 1 is a clone
     form rebase that creates `X-staging/d`; shtetl then places it
     (`zfs inherit mountpoint`, `readonly=off`, mounted at
     `/containers/X-staging/...`), because steps 2 and on are dataset
     form rebases of it, and the dataset form takes only a mounted
     dataset.
   Each staging dataset then gets, set locally, the properties X's
   recipe gives it (5.4; ruled 2026-10-12, Q-AC).
   Each dataset's whole replay, its conflicts gate included, ends
   before any child of it is made: a dataset form step on a parent with
   a child mounted below it is refused (exit 2, in use), while both
   forms work on a child under a mounted parent (P4).
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
The tool's own box suite runs `--allow-unrelated --base` only as a dry
run (tests/box/run-fixture.sh step 0a); P14 ran the real thing, the
dataset form included, and it works (13.1). It also showed the ruling
above on a real run: the base the tool would derive raised a conflict
at step 2 that `--base X@s1` does not.

**Cost.** `--allow-unrelated` turns the tool's prune off, so each step
after the first reads every file of the dataset on all three sides.
P14 measured it on an 865 MiB tree of 31,358 names: step 1, pruning,
compared 0.5 MB in 5.9 s, nearly all of it the three walks; steps 2
and 3 compared 10.3 GB each, in 8.6 s and 8.9 s; one rebase of the
newest save alone (depth 1) took 6.1 s. That is with the tree in the
ARC. Read from disk, 10 GB a step is the cost that matters, times
`update.depth`, times the container's datasets.
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
   save of X so its archive holds those writes. `written@` counts
   atime updates too: with `atime=off` (4.3) it stays 0 through
   remounts and reads, but a dataset with `atime=on` shows bytes after
   reads alone (P8).
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

As built in M1, src/core: `parse` (libucl, 5.3) hands `tree` (the
format-free tree, every node with its layer) to `schema` (5.5) and
`site` (5.2), `cascade` (5.4) merges, `show` prints; `syntax` holds
the spellings of names, `root` the paths in zconfig, `err` the sink
every refusal goes through. `src/main.c` is the command table. The
portable build compiles libucl from a FreeBSD tree's `contrib/libucl`
(`FREEBSD_SRC`, default /usr/src); `make freebsd` links the port.

As built in M2: `src/core/zfs.h` is what the core asks of ZFS, with
three implementations picked at the link: `src/fbsd/zfs.c` over
libzfs and libzfs_core, `src/core/zstub.c` (the portable build: every
call refused), and `tests/zfake.c` (pools in memory, for the tests).
`src/core/run.c` runs every program shtetl runs (git first). `spaces`
is discovery, `boot` bootstrap. `tools/xcheck-freebsd.sh` compiles
every source for FreeBSD on the mac against a FreeBSD tree's headers;
`tests/box/run.sh` runs the commands on the box against pools on
memory disks.

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
cascade, discovery, bootstrap, patches and the pause, foundation build,
beam-down, create, save, update, history replay, gate handling, soak,
cutover, abandon, mirror, restore, prune. Kill tests at every step of
patching, create, save, update and cutover.

The matrices are in tests/MATRIX.md. Family C, the configuration (M1),
is plotted and every cell covered: by unit tests (tests/check_*.c) and
by the command line run over two configuration roots
(tests/run-cli.sh, tests/conf).

## 12. Milestones

| M  | scope | where |
|----|-------|-------|
| M0 | probes (section 13) | box |
| M1 | skeleton, UCL loader, schema, cascade, `shtetl config show`; built 2026-10-11 (tests/MATRIX.md family C, all covered; make check and make check-freebsd pass on the box); the cascade reworked 2026-10-12 for the stacking rulings and the default layer (docs/worklogs/m3-stacking.md) | mac |
| M2 | discovery, bootstrap, property table; built 2026-10-11 (tests/MATRIX.md families R, D, B on the mac; tests/box/run.sh passes on the box, 2026-10-12) | box |
| M3 | foundation build and update into zlake, patches and the pause included; pkg-upgrade; under way: versions, ZFS calls and the build of a new foundation built and tested on the mac (tracker/shtetl.json, docs/worklogs/m3-foundations.md) | box |
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

### 13.1 Results (2026-10-11)

On KIDON: FreeBSD 15.1-RELEASE-p2, OpenZFS 2.4.2, zfs_rebase fdab53d.
The world probes installed 16.0-CURRENT (8f02ff221666) and started
from 15.0-RELEASE's base.txz. Transcripts in `probes/results/`:
`KIDON-20261011T135848Z` (the ZFS probes), `KIDON-20261011T140103Z`
(all sixteen, with `--world`), `KIDON-20261011T141420Z` (P14 on
865 MiB), `KIDON-20261012T025426Z` (P6 and P15 again on the old
order, identical to the first) and `KIDON-20261012T031318Z` (P6 and
P15 in 7.2's ruled order). Every check came out as the plan expected; the answers
below that change the plan are folded into the sections named.

| probe | answer | lands in |
|-------|--------|----------|
| P1 | A first save's clone streams land in zlake as clones of the zlake foundation without `-o origin`; zlake holds only the delta (0.1 MiB against F's 0.9 MiB). | 7.5 as written |
| P2 | A child moved under a parent of another lineage still takes its incrementals. | 7.10 as written |
| P3 | A promoted clone keeps s1 and its guid, and its next incremental is received. | 7.9 as written |
| P4 | Both forms work on a child under a placed, mounted parent. A dataset form step on a parent with a child mounted below it is refused (exit 2, in use). | 7.6 step 5 |
| P5 | A rewrite of every file (same bytes, new objects) leaves the admin's rc.conf edit unconflicted. It costs the send (44.1 MiB against 0.2 MiB) and the compare (84.5 MB against 0.5 MB); on 24 MiB, not the time. | 7.2 |
| P6 | Over 15.0's base.txz, installworld, distribution and delete-old leave 524 paths a fresh install lacks: etcupdate's database of 15.0's `/etc` (428) and the `/sys` link, which come with a release tarball and not with shtetl's builds; certctl's `/etc/ssl` entries (56), left because installworld's rehash ran before delete-old removed the dropped certificates; installworld's backups (`*.old` loaders, `ld-elf.so.1.old`, `init.bak`); about 30 files ObsoleteFiles.inc does not list (LLVM 19's private libraries, lib9p, binutils man pages, headers). Nothing a fresh install has is missing. In 7.2's ruled order (2026-10-12): 461 remain, none in `/etc/ssl` and no backup; what stays is etcupdate's database (429), `/sys`, and 31 files ObsoleteFiles.inc does not list. | 7.2 |
| P7 | Clones under `containers/` mount by inheritance, the placeholder hides nothing, renames carry mountpoints, an import mounts parents first, a clone cannot cross pools, zlake stays unmounted. `canmount` does not inherit. | 4.3 |
| P8 | `written@from` stays 0 through a remount and reads with atime off; reads with atime on make it 59,392 bytes; one real write adds 2 KiB. | 7.9 step 1 |
| P9 | A child needs its parent first; `clone -p` makes a plain parent; a clone keeps none of its origin's local properties, so a placeholder cloned without `-o canmount=off` mounts and hides the root's `usr/`. | 7.4 step 3, 7.6 step 5 |
| P10 | `shtetl:*` travel only with `send -p`, which carries the dataset's local properties too; `zfs set` on a received snapshot works. | ruled, 6.2 |
| P11 | The whole cutover mirrored in zlake: the next save received for every dataset. The archive's `www` is a clone of the live `www@s1`, so s1 stays while the archive does (6.3 already keeps a clone's origin). | 7.9, 7.10 as written |
| P12 | `install -C`: 7.2. No conflicts either way, none from the admin's user, hosts and rc.conf either (P5 on a real world). | 7.2, adopted |
| P13 | Roots found by local `shtetl:space`, pool roots and dataset roots alike; one match and two told apart; a mirror lands only in the root holding its foundation. | 4.1 as written |
| P14 | The replay works, the dataset form under `--allow-unrelated --base` included; the derived base raises a conflict at step 2, `--base X@s1` none. Costs in 7.6.2. | 7.6.2; Q-N's numbers |
| P15 | 15.0 to 16.0-CURRENT: one conflict, `/etc/group`, changed by the admin's `pw useradd` and by the new release. `master.passwd`, `passwd`, `pwd.db` and `spwd.db` came from the container cleanly, the release having left them as they were. 5.2 s; the foundation's incremental 1,745 MiB. In 7.2's ruled order with `install -C`: the same one conflict, 13.6 MB compared against 26.5 MB, the incremental 1,735 MiB. | 7.7 as written |
| P16 | The zrun foundation mounts read-only on demand and unmounts after; the placeholder is refused as a side (exit 2) and cloned instead. | 7.3, 9 as written |

## 14. Rulings

### 2026-10-12 (one root to a space)

On `spaces.lake.default` in configuration.md: "why does this matter?
why does the lake have a "root"? We should just sniff for
shtetl:space=lake and find all static expected dataset parents under
those spaces", then "let's say there are two lake roots. ONLY ONE
should have foundations/. One MUST have this dataset, detected in
bootstrap. this is true of the entire skeleton of shtetl. all static
skeleton members on run, lake, and config must exist during
bootstrapping." Asked what bootstrap does with a piece missing from
every root ("ask which root to place it in. we'll make this a
selection in the eventual tui"), and, clones and renames not crossing
pools, whether to keep several roots to a space: "One root per
space", and "we should drop multirooting entirely".

So (4.1, 4.2): each space has one root; its skeleton is there once,
made or taken by bootstrap; where something new goes follows from
the skeleton, and `spaces.lake.default` and `spaces.run.default` are
gone with the several roots they chose between. Were several roots
ever to come back, a piece missing from all of them would be the
user's choice, a selection in the TUI.

### 2026-10-12 (simple messages; kernconf one key)

On reading configuration.md's messages:

- "don't have old support type messages like "kernconf no longer
  supported". We haven't built anything out yet. no old kruft. just
  keep us clean and at the most recent understanding of the
  language."
- On "false drops nothing: the layers stack, and what one gives stays
  (ruled 2026-10-12)": "This message sucks. just write "dropping
  values is illegal"."
- On "pkgs: not a key a recipe takes": "should just be: "pkgs:
  invalid key". For a valid key but in the wrong conf type just write
  "kernconf_lines: valid only in foundation recipe". Your error
  messages should be much more declarative and free of AI writing
  quirks."
- "same with kernconf. not a valid key, don't make a special case.
  nothing exists yet. don't write ruling dates. just be sane and
  write simple error messages."
- "(though really, we should just go back to kernconf being: -
  heredoc -> lines, stackable - string or path -> kernconf file,
  stacking disallowed. one per stack only. and then the heredoc lines
  are appended to the autogenerated final file)."
- "remember. simple simple simple for errors. don't support or talk
  about or acknowledge legacy opinions."

So: every refusal is the shortest true statement of what is wrong at
its key path; a key that was removed is an invalid key like any
other, with no message of its own; and `kernconf` is one key again
(5.5): a heredoc is lines, which stack, and a string the config to
start from, given by one file of the stack, the default's overridden
with a warning. The design sections and configuration.md say what the
language is, not what it was; this section keeps the history.

### 2026-10-12 (overrides warn, shtetl.conf checked, a bare major)

Asked, after the configuration reference was written: should every
override of the default warn, not only a dataset property's; should a
recipe be refused when shtetl.conf's settings are; and is `"=15"`
15.0 alone, or every 15.x.

- "1. yes, all should warn. 2. shtetl.conf should be checked for
  correctness in its own right 3. =15 matches 15.x"

So:

- (5.4) Every override of the default warns, naming the value it
  replaces and its file: a one-value key, a derivation, a field, a
  dataset property. The default's value given again alike is no
  override.
- (5.4) shtetl.conf is checked whole, its settings too, wherever it
  is read: a refused setting refuses every recipe, not only `config
  show`, `spaces` and `bootstrap`.
- (5.5) In `ver`, a bare major is every minor of it: `=15` is any
  15.x, `<=15` takes 15.x, and `>15` begins at 16.0; `>=15` and `<15`
  are as they were. The ruling named `=`; asked about `>`, the author
  confirmed the reading: ">15 starts at 16."

### 2026-10-12 (Q-AD: the default's edges)

Asked: are the files shtetl.conf imports of the default too; does
overriding a default's dataset property warn, as another layer's
does; and does the default's `ver` stack, so that a recipe whose
`ver` cannot meet `>=15.1` is refused.

- "1: sounds good 2: give warning for all layers including default
  3: agreed"

So (5.4): shtetl.conf's imports are of the default; a dataset
property's override warns whichever layer gave the value it
replaces; the default's `ver` stacks like any list.

### 2026-10-12 (the default: shtetl.conf)

Asked: may the default file hold any kind's keys, each recipe taking
those of its kind (A), or should there be a default per kind (B); are
its values defaults a recipe may replace, or a floor; and does
`buildopts_file` go, as `jailconf_file` did.

- "1. option A) default.conf takes all, only provides as necessary
  2. default is a special overridable file. 3. yes. key takeaways.
  default provides everything, and all standard parse errors are
  overrides. all stackables simply stack. user needs to delete a
  stackable value if they want it overriden. and yes, shtetl.conf is
  the default.conf. default.conf isn't a real file. yes we need to
  add it to shtetl-recipes/ git"

So (5.4): the default layer is shtetl.conf, beside its settings, with
no built-in recipe under it; it holds any kind's keys and each recipe
takes those of its kind; what would be a redefinition of its values
(a one-value key, a derivation, a field, a dataset's property) is an
override; what stacks stacks with it. `buildopts_file` is gone. Its
seed, with `ver = ">=15.1"` and `kernconf_file = "GENERIC"`, is in
`shtetl-recipes/` (4.4).

### 2026-10-12 (Q-AC: keys that hold one value, and redefinitions)

- "splitting kernconf seems ok. Additionally, overriding dataset
  parameters seems ok. I think commands label should stack comands.
  Two commands for one derivations path is a parse error. and
  src_branch is all-must-agree-if-set."
- On the table of every key that followed, with nine points asked:
  "so is there a kernconf_lines and kernconf_file and kernconf now? I
  think there should just be a _file and _lines option now.
  jailconf_file is just options, right? we actually write the final
  jailconf file with the NAME {[shtetl options here]} wrapper, so
  that should be disallowed. datasets, warn about overrides of
  properties. yes, shtetl.conf over built-in is override, same with
  properties.<space>. 1./2. Both redefinitions of a key are errors.
  Two included files that require redefinitions of the same elements
  of the greater patches/derivations/whatever keys are not true
  compositions. we should take this philosophy. 3. each piece runs
  once, yes 4. ver can't hold is a parse error, yes 5. get the
  default from the default conf. (this is the shtetl.conf in the
  repo. yes we should set ver >= 15.1 and kenconf=GENERIC) 6. ok 7.
  agree, let jail(8) be the problem 8. I like this 9. kernconf_lines
  is fine"

So (5.4, 5.5):

- `kernconf_file` (one value, all must agree) and `kernconf_lines`
  (stacked) replace `kernconf`; `jailconf_file` is gone, shtetl
  writing the jail's block.
- What one file gives, another does not give again: a patch source's
  `ref` or `dir` (1), a derivation, even with the same text (2), any
  field of an entry. A derivation is given whole in one file.
- A dataset's property may be given again, with a warning; shtetl
  sets it locally on every clone it makes, at create and on each
  update's staging clones (6).
- A commands label stacks, and each piece runs once (3; 7.4).
- A `ver` no version satisfies is refused when the recipe is read (4).
- The default comes from shtetl.conf, the recipes repository's (5;
  then the default ruling, above).
- What `jailconf` gives twice is jail(8)'s to settle (7).
- `config show containers/X --on F`, the two layers stacked as create
  stacks them (8), is agreed; it is not built yet.
- The site's overrides stay: shtetl.conf over the built-in settings,
  `properties.<space>` over `properties.default`.

### 2026-10-12 (Q-S, Q-T, Q-X: everything stacks)

- Q-S, Q-T: "To be clear, there are really only two layers,
  foundation and container/system. Each foundation or system
  component can be multiple included files however. For all layers,
  including foundation and container/system interplay, yes, all
  stacking constraints must hold. This is true of stacking buildopts
  too (Q-T). In fact, I don't think there's a reason to have an
  overwrite (eg in overwrite list dropping previous, or overwrite key)
  option at all. Everything must stack, except "name"."
  So: no `false` drops and no `reset` (5.4's drops are gone); every
  file's `ver` constraint must hold, the foundation's and the
  container's together; `buildopts` (and `jailconf`) append, lower
  layers first. What a key holding one value does when two files set
  it, and whether a container may set a property on a dataset its
  foundation has, were asked back the same day and ruled (Q-AC,
  above).
- Q-X: "Good enough for now", with an issue in the tracker's future
  work to settle hosts against directories (top-level domains, and
  the author's idea of trying each dotted candidate in DNS before
  looking locally): tracker/shtetl.json, patch-location-hosts.

### 2026-10-11 (the run layout's mounts; UCL kept)

- "zrun/containers is the root dir of all containers and should be
  mounted at /containers. zrun/foundations doesn't need to be mounted
  anywhere ever, including all its children (I think)." Built so:
  containers at /containers and mounted; foundations canmount=off.
  The foundations F below it keep a mountpoint and canmount=noauto,
  since an update mounts them read-only while zfs_rebase reads their
  snapshots (ruled 2026-10-09; P16); they are never mounted otherwise.
- Lua as the configuration language was asked about and UCL kept
  ("ok I'm ok keeping UCL for now"): a Lua table does not keep the
  order keys were written in, which commands and patches rely on
  (Q-Q), and takes a key written twice without a word.

### 2026-10-11 (Q-Q, Q-R: patches)

- Q-Q: patch sources are applied one after the other, each in its
  entirety, in the order the recipe lists them (7.1.1).
- Q-R: a patched build keeps newvers.sh's `-dirty`: the version string
  names the real FreeBSD commit and shows that patches went in.

### 2026-10-11 (Q-O, Q-P)

- Q-O: every foundation install uses `install -C`
  (`INSTALL_CMD="install -C"`), overturning the 2026-10-08 acceptance
  of the rewrite cost (7.2).
- Q-P: installworld's backups are deleted after the install; files
  ObsoleteFiles.inc does not list stay; etcupdate's database and the
  `/sys` link need nothing, since they come only with a release
  tarball. The certificate entries: installworld's closing rehash ran
  before delete-old removed the dropped certificates, so a foundation
  update runs delete-old before installworld (7.2).

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
  later improvement. Overturned 2026-10-11 (Q-O): every install uses
  `install -C`.

## 15. Open questions

Q-AC is ruled (14, 2026-10-12), and with it the default; so is
Q-AD, the default's edges.

M1 and M2 decided these where the plan was silent; each is built as
written and marked PROPOSAL where it lives. None blocks M3. Q-S, Q-T
and Q-X are ruled (14, 2026-10-12), and Q-V is moot with drops gone.

M2's:

- **Q-Y, nested roots.** Built: a root inside another root, of any
  space, is refused, since each root's layout is shtetl's own.
- **Q-Z, what bootstrap takes.** Built: only datasets that exist (it
  creates no root), and a lake or run root only if it has no child
  but shtetl's layout, so that the properties it sets on the root
  reach nothing of the user's. The other reading: create a missing
  root, and accept children, listing them as affected.
- **Q-AB, the recipes clone.** Built: only with `--recipes URL`, since
  zshtetl-recipes does not exist yet; once it does, the URL can be the
  default. Its seed is `shtetl-recipes/` in this repository (4.4);
  making the repository from it is the author's to do.

M1's:

- **Q-S, `ver` across layers.** RULED 2026-10-12 (14): every
  constraint must hold; the rest of this entry is what M1 built first.
  Built: a nearer layer's `ver`
  replaces a lower layer's, as every scalar does. The other reading:
  every layer's constraint must hold, so a fragment that needs `>=15`
  keeps needing it whatever the recipe that imports it says.
- **Q-T, heredocs across layers.** RULED 2026-10-12 (14): they
  append. Built first: `jailconf` and `buildopts`
  are replaced by a nearer layer. The other reading: appended layer by
  layer, since 5.5 says jailconf is "appended to the jail's
  jail.conf block".
- **Q-U, imports of imports.** Built: followed, each file merged
  once, cycles refused, as 5.5's "recipes merged in before this file"
  reads for every file.
- **Q-V, a drop of nothing.** Moot: there are no drops (14,
  2026-10-12). Built first: `x = false` where no lower layer
  gives `x` is an error, being a misspelling or a layer imported where
  it does not belong. The other reading: a no-op, so a fragment can
  say "never x" wherever it is imported.
- **Q-W, names.** Built: a recipe's name takes letters, digits, `-`
  and `_` only (no `.`), since it is a dataset component, a jail name
  (where `.` means a child jail) and a file name at once.
- **Q-X, patch locations.** ACCEPTED 2026-10-12 for now (14), with
  the tracker issue patch-location-hosts. Built: with no scheme, a location whose
  first component has a `.` is a host and anything else a zconfig
  directory, so a zconfig directory whose first component has a `.`
  cannot be named.

## 16. Dropped from parasa

foundation.git, foundation.zfs (replaced by zlake/foundations),
parasa.git, recipes.git on the data pool, .git datasets in trees,
mtree.dist, diff.sh drift classification, the two-commit save,
pkg.list (packages are rebased with /usr/local), compose.sh replay on
update (commands run once), the container-data and system-data trees,
mount.fstab for a container's own datasets, stage0 disk preparation,
the separate build workspace and DESTDIR.
