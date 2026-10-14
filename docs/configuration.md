# shtetl configuration

Every configuration option shtetl takes, how a file is read, and how
the files of a recipe stack. As built, 2026-10-12;
planning.md 5 is the design and section 14 there the rulings this
follows. For UCL itself, the format, see ucl-primer.md.

Every example here is run. `tools/doccheck.py` (`make doc`, part of
`make check`) builds each example's configuration root from the
files shown, runs each command against `./shtetl` and compares what
it prints with what is shown, so the outputs below are shtetl's own.
The commands are written as on a machine shtetl has bootstrapped,
where it finds the configuration root itself; the check runs them
with `-C` naming the example's root.

Reading, checking and stacking, and `config show`, are built (M1,
reworked since). What a key does when a foundation is built or a
container created (the kernel config and src.conf shtetl writes, the
patches applied, the commands run) is planning.md's, and comes with
M3 and M4.

Contents:

1. Files
2. Reading a file
3. shtetl.conf: the settings
4. Recipe keys
5. How the files of a recipe stack
6. The default: shtetl.conf
7. Checks after the merge
8. Messages, warnings and exit status
9. Summary: when two files give the same thing

## 1. Files

The configuration root is zconfig, a clone of the recipes repository
(planning.md 4.4), which `shtetl bootstrap` makes of an empty one:
https://github.com/eidelbyte/shtetl-recipes.git, or the URL its
`--recipes` names:

    <config root>/
        shtetl.conf            the site's settings, and the default recipe
        foundations/F.conf     a foundation's recipe
        containers/X.conf      a container's recipe
        systems/S.conf         a system's recipe
        any/other.conf         files recipes import

A recipe is named by its path without `.conf` (`.conf` may be given
too): `foundations/generic`, `containers/nginx`, `systems/router`.
Its directory says its kind; its name, the file's name without
`.conf`, takes letters, digits, `-` and `_`, and begins with a letter
or a digit, since it is a dataset's name, a jail's name and a file's
name at once. Files in other directories are not recipes; a recipe
names them in `import`, by their path in the root, `.conf` included.

`shtetl config show RECIPE` prints a recipe with its files merged,
each value with the file it came from. `shtetl config show`, with no
recipe, prints the site's settings.

## 2. Reading a file

Each file is read alone, by libucl, under rules stricter than UCL's
own:

| rule | why |
|------|-----|
| a key written twice in one file is an error | UCL would make it a list, or merge two objects, without a word |
| no macros (`.include`, `.priority`, any `.name` where a key would stand) | an include with priorities makes the result depend on numbers scattered across files; `import` does it in the open |
| no variables: `$` is literal | nothing is expanded behind the reader's back |
| the file holds keys at its top, not a bare value | a recipe is an object |
| at most 4 MiB, nested at most 64 deep, no NUL byte | a configuration file is small |

A problem libucl finds is reported with the file's line; a problem
shtetl finds afterwards, with the file and the key's path
(`datasets.www.readonly`), since libucl keeps no line on what it
builds and a key's path names one place in a file. Every file is
read and checked before anything merges, and every problem in every
file is reported, not only the first.

UCL reads an unquoted `15.1` as a number, `1M` as 1000000 and `30d`
as a number of seconds. Where shtetl wants a string, a number is
refused: "must be a quoted string".

<!-- example: reading -->

```ucl
# containers/twice.conf
pkg = [ "nginx" ];
pkg = [ "git" ];
```

```ucl
# containers/macro.conf
.include "common/web.conf"
pkg = [ "nginx" ];
```

```ucl
# containers/many.conf
ver = 15.1;
pkgs = [ "nginx" ];
src_branch = "releng/15.1";
requires = [ "web.1" ];
pkg { nginx = true; git = false; }
datasets { www { recordsize = 1M; } }
```

```console
$ shtetl config show containers/twice
shtetl: containers/twice.conf:3: duplicate key
$ shtetl config show containers/macro
shtetl: containers/macro.conf:2: macros are illegal
$ shtetl config show containers/many
shtetl: containers/many.conf: ver: must be a quoted string
shtetl: containers/many.conf: pkgs: invalid key
shtetl: containers/many.conf: src_branch: valid only in foundation recipe
shtetl: containers/many.conf: requires[0]: invalid container name: only letters, digits, - and _ allowed
shtetl: containers/many.conf: pkg.git: dropping values is illegal
shtetl: containers/many.conf: datasets.www.recordsize: must be a quoted string
$ echo $?
1
```

The last file has six problems, and all six are reported: `ver` and
`recordsize` are numbers where strings belong; `pkgs` is no key;
`src_branch` is a foundation's key; `web.1` is no container's name;
and `false` would drop a value (section 5).

shtetl does not refuse bytes outside ASCII in a value; a package's,
a dataset's or a label's name takes only the characters given for it
in section 4.

## 3. shtetl.conf: the settings

shtetl.conf holds two things. Its sections, `properties`, `build`,
`update` and `retention`, are the site's settings: about
this machine, never about a recipe. Every other key at its top is the
default recipe's (section 6). The settings have built-in values; a
setting shtetl.conf gives replaces the built-in one, key by key.

| setting | value | built-in | meaning |
|---------|-------|----------|---------|
| `properties.default` | ZFS properties | `atime = "off"`, `compression = "zstd"`, `aclmode = "passthrough"` | set on every space's root at bootstrap, and inherited below it (planning.md 4.3) |
| `properties.lake`, `.run`, `.config`, `.build` | ZFS properties | none | one space's, each over `properties.default`'s of the same name |
| `build.make_jobs` | a count | `0` | make's `-j` for a foundation's build; 0 is `hw.ncpu` |
| `update.suffix` | a name | `"staging"` | an update of X lands in `X-staging` (planning.md 7.6) |
| `update.depth` | 0, a count or an interval | `0` | the saves an update replays (planning.md 7.6.2); 0 is every save zrun holds |
| `retention.run` | 0, a count or an interval | `0` | the saves zrun keeps (planning.md 6.3); 0 keeps everything |
| `retention.archive` | 0, a count or an interval | `0` | how long a retired tree is kept in zlake |

The forms:

- **ZFS properties**: an object, `{ compression = "zstd-9"; }`. A
  value is a string, or `true` or `false` for `on` and `off`; a
  number is refused (quote `"1M"`). `mountpoint`, `canmount` and
  `shtetl:*` are shtetl's own and refused. A name with a `:` is a
  user property (lowercase letters, digits, `_`, `.`, `:`, `-`); any
  other must be spelled as a ZFS property is. ZFS checks the name and
  the value when shtetl sets them.
- **A count**: an integer, 0 or more.
- **An interval**: a quoted count above 0 and one of the units
  `date -v` takes, `y`, `m`, `w`, `d`, `H`, `M`, `S`: `"30d"`,
  `"6m"`, `"1y"`.
- **A name**: letters, digits, `-` and `_`, beginning with a letter
  or a digit.

There is no `retention.lake`: zlake holds exactly what zrun holds.
M2 uses `properties`; the others are read and checked now and used by
the milestones that need them.

<!-- example: settings -->

```ucl
# shtetl.conf
properties {
    lake { compression = "zstd-9"; }
}
build {
    make_jobs = 8;
}
retention {
    run = 10;
    archive = "1y";
}
```

```console
$ shtetl config show
# the site's settings: shtetl.conf over the built-in ones
properties {
    default {
        atime = "off";  # (built-in)
        compression = "zstd";  # (built-in)
        aclmode = "passthrough";  # (built-in)
    }
    lake {
        compression = "zstd-9";  # shtetl.conf
    }
}
build {
    make_jobs = 8;  # shtetl.conf, over (built-in)
}
update {
    suffix = "staging";  # (built-in)
    depth = 0;  # (built-in)
}
retention {
    run = 10;  # shtetl.conf, over (built-in)
    archive = "1y";  # shtetl.conf, over (built-in)
}
```

Each value says where it came from: `(built-in)`; `shtetl.conf`;
or `shtetl.conf, over (built-in)` where shtetl.conf replaced a
built-in value. `properties.lake` gives the lake's root `zstd-9` over
`properties.default`'s `zstd`; the other spaces keep `zstd`.

<!-- example: settings-refused -->

```ucl
# shtetl.conf
properties {
    default { mountpoint = "/shtetl"; recordsize = 1M; }
}
build {
    make_jobs = -1;
}
update {
    suffix = "x.staging";
}
retention {
    run = 30d;
    lake = 10;
}
```

```console
$ shtetl config show
shtetl: shtetl.conf: properties.default.mountpoint: invalid property: reserved
shtetl: shtetl.conf: properties.default.recordsize: must be a quoted string
shtetl: shtetl.conf: build.make_jobs: must be 0 or more
shtetl: shtetl.conf: update.suffix: invalid name: only letters, digits, - and _ allowed
shtetl: shtetl.conf: retention.run: must be an integer or a quoted interval
shtetl: shtetl.conf: retention.lake: invalid key
```

shtetl.conf is checked whole wherever it is read, so a refused setting refuses every recipe too, each recipe reading
shtetl.conf as its default:

```ucl
# containers/x.conf
pkg = [ "nginx" ];
```

```console
$ shtetl config show containers/x
shtetl: shtetl.conf: properties.default.mountpoint: invalid property: reserved
shtetl: shtetl.conf: properties.default.recordsize: must be a quoted string
shtetl: shtetl.conf: build.make_jobs: must be 0 or more
shtetl: shtetl.conf: update.suffix: invalid name: only letters, digits, - and _ allowed
shtetl: shtetl.conf: retention.run: must be an integer or a quoted interval
shtetl: shtetl.conf: retention.lake: invalid key
```

## 4. Recipe keys

| key | kinds | value | stacks as (section 5) |
|-----|-------|-------|-----------------------|
| `import` | all | a file's path, or a list of them | files merged in before this one |
| `ver` | all | a version constraint, or a list of them | every one kept; all must hold |
| `src_branch` | foundation | a string | one value: files that give it agree |
| `kernconf` | foundation | a config's name or path; or text | the config: one file of the stack; text: a piece from each file |
| `buildopts` | foundation | text | a piece from each file |
| `patches` | foundation | keyed by location | the union; an entry's fields given once |
| `datasets` | all | keyed by path | the union; a property may be overridden, with a warning |
| `pkg` | all | keyed by name | the union |
| `commands` | all | keyed by label | the union; a label's text a piece from each file |
| `derivations` | all | keyed by derived path | the union; each given once, whole |
| `requires` | container | keyed by name | the union |
| `jailconf` | container | text | a piece from each file |

"All" is foundations, containers and systems. A file is checked as a
file of the recipe it is merged into: `common/web.conf`, imported by
a container, is a container's file and takes a container's keys. A
key of another kind is refused there. The default's files are the
exception (section 6).

A string is never empty. Text is a string, usually a heredoc:

    buildopts = <<EOD
    WITHOUT_LIB32=yes
    EOD

### import

`import = "common/web.conf";` or `import = [ "common/web.conf",
"common/backup.conf" ];`. Each is a file's path in the configuration
root, `.conf` included, never absolute and with no `.` or `..`
component. Section 5 says how imports are followed.

### ver

The FreeBSD versions the recipe fits: `">=15"`, `"<16"`, `"=15.1"`,
or a list, `[ ">=15", "<16" ]`. A constraint is one of `>=`, `<=`,
`>`, `<` and `=`, then `N` or `N.N`. `N.N` is that version; a bare
`N` is every `N.x`:

| constraint | versions |
|------------|----------|
| `"=15"` | every 15.x |
| `"=15.1"` | 15.1 |
| `">=15"` | 15.0 and later |
| `">15"` | 16.0 and later |
| `">15.1"` | 15.2 and later |
| `"<=15"` | up to the last 15.x |
| `"<=15.1"` | up to 15.1 |
| `"<15"` | before 15.0 |
| `"<15.1"` | before 15.1 |

Every `ver` of every file of the recipe must hold, a container's
together with its foundation's when it is created. A foundation's
version comes from its `src_branch` (planning.md 6.1).

### src_branch

The FreeBSD branch a foundation is built from: `"releng/N.M"`,
`"stable/N"` or `"main"`; anything else is refused. A foundation must
have one, from some file. The branch names the version (planning.md
6.1: `15.1-releng`, `15-stable`, `16-current`) and is checked against
`ver` before the checkout; after it, the checkout's own REVISION is.

### kernconf

shtetl writes a foundation's kernel config itself: an `include` of
the config to start from, its own `ident`, then the lines. `kernconf`
gives either, by how it is written:

- A string is the config to start from. A bare name (`"GENERIC"`,
  `"MINIMAL"`) is a config in the src tree's `conf` directory for the
  machine; a name with a `/` is a file in the configuration root,
  which must exist (section 7). One file of the stack gives it; the
  shipped shtetl.conf gives `"GENERIC"`, and a foundation may
  override that.
- A heredoc is lines, added after it, and stacks, a piece from each
  file:

      kernconf = <<EOD
      nooptions INVARIANTS
      device vmm
      EOD

A heredoc is lines even when it holds one line; a quoted string is
always the config. A file gives `kernconf` once, so a foundation that
starts from a config of its own and adds lines to it gives one of the
two in a file it imports.

### buildopts

Text: make(1) variables, the lines of the src.conf shtetl writes for
the build (`WITHOUT_LIB32=yes`).

### patches

Patch files applied to the src tree before a foundation's build
(planning.md 7.1.1), keyed by where they come from:

    patches {
        "github.com/eidelbyte/freebsd-patches" { ref = "releng-15.1"; }
        "https://git.example.org/patches.git" { dir = "15"; }
        "patches/local" {}
    }

A location is one of:

- a URL with a scheme, `https://...`, `ssh://...`;
- a host and a path, `github.com/owner/repo`: a first component with
  a `.` is a host, fetched over https;
- a directory in the configuration root, `patches/local`: a first
  component with no `.`; it must exist (section 7).

An scp-style address (`git@host:path`) is refused: write it as an
`ssh://` URL. An entry is `true` (or `{}`), or gives `ref`, the git
ref to fetch (a branch, a tag or a commit; the remote's HEAD when
not given), and `dir`, the directory in the source holding the
patches (its top when not given). A list of locations stands for
`location = true` each. Sources apply in the order the recipe gives
them, each one's `*.patch` files in natural order.

At the build, after the checkout: a patch that applies is applied; one
whose reverse applies is already in, and skipped; any other pauses the
build before anything is built, its merge left in the src tree for a
person, and `shtetl build F --continue` (once fixed), `--skip` or
`--abort` takes it up. The version records the sources and what became
of the patches in `shtetl:patches`. planning.md 7.1.1 has it whole.

### datasets

The recipe's child datasets, created before anything is installed,
keyed by path below the recipe's own dataset: `www`, `usr/local`,
`var/db/pkg`. A path takes what ZFS names take. An entry is `true`
(or `{}`), or ZFS properties for the dataset, in the forms of
`properties` in section 3:

    datasets {
        tmp {}
        "usr/local" {}
        www { readonly = true; recordsize = "1M"; }
    }

A list of paths stands for `path = true` each. A container's
datasets add to its foundation's, which it keeps. A property a
container gives one of its foundation's datasets is its own clone's:
shtetl sets it on the clone when the container is created and on
each update's staging clone (planning.md 7.4, 7.6).

### pkg

Packages installed when the recipe is made, keyed by name:
`pkg = [ "nginx", "git" ];` or `pkg { nginx = true; }`. A name takes
letters, digits and `-_.+@/`, and does not begin with `-`. The only
value of an entry is `true`.

### commands

sh(1) text run once when the recipe is made, after its packages,
never on an update, keyed by a label of letters, digits, `-`, `_` and
`.`:

    commands {
        nginx = <<EOD
    sysrc nginx_enable=YES
    EOD
    }

Labels run in the order they were first given. A label given in
several files stacks: each file's text is a piece of it, run in file
order (section 5).

### derivations

How to remake a file from its text source when a rebase conflicts on
it (planning.md 7.7), keyed by the derived file's path:

    derivations {
        "etc/pwd.db" {
            source = "etc/master.passwd";
            command = "pwd_mkdb -p -d /etc /etc/master.passwd";
        }
    }

`source` (a path) and `command` (a string) are both given, in one
file.

### requires

A container's containers that must exist before it:
`requires = [ "nginx" ];`. Each is a container recipe that exists,
and not the container itself (section 7).

### jailconf

Text: the jail's options. shtetl writes the jail's block in jail.conf
itself, `X { ... }`, with these inside it:

    jailconf = <<EOD
    allow.raw_sockets;
    ip4.addr = "10.0.0.5";
    EOD

An option given twice, in two files, is jail(8)'s to settle, not
shtetl's.

### Every key at once

<!-- example: keys -->

```ucl
# shtetl.conf
ver = ">=15.1";
kernconf = "GENERIC";
```

```ucl
# foundations/generic.conf
src_branch = "releng/15.1";
kernconf = <<EOD
nooptions INVARIANTS
nooptions WITNESS
EOD
buildopts = <<EOD
WITHOUT_LIB32=yes
EOD
patches {
    "github.com/eidelbyte/freebsd-patches" { ref = "releng-15.1"; }
}
datasets {
    tmp {}
    var {}
    "usr/local" { compression = "zstd-9"; }
}
pkg = [ "sudo" ];
derivations {
    "etc/pwd.db" {
        source = "etc/master.passwd";
        command = "pwd_mkdb -p -d /etc /etc/master.passwd";
    }
}
```

```ucl
# containers/nginx.conf
ver = "<16";
requires = [ "db" ];
pkg = [ "nginx" ];
datasets {
    www { readonly = true; }
}
commands {
    nginx = <<EOD
sysrc nginx_enable=YES
service nginx start
EOD
}
jailconf = <<EOD
allow.raw_sockets;
EOD
```

```ucl
# containers/db.conf
pkg = [ "postgresql16-server" ];
```

```console
$ shtetl config show foundations/generic
# foundations/generic.conf: a foundation's recipe, its layers merged
# layers, nearest last: shtetl.conf, foundations/generic.conf
ver = [
    ">=15.1"  # shtetl.conf
]
kernconf {
    file = "GENERIC";  # shtetl.conf
    lines {
        "foundations/generic.conf" = <<EOD
nooptions INVARIANTS
nooptions WITNESS
EOD
    }
}
src_branch = "releng/15.1";  # foundations/generic.conf
buildopts {
    "foundations/generic.conf" = "WITHOUT_LIB32=yes";
}
patches {
    "github.com/eidelbyte/freebsd-patches" {  # foundations/generic.conf
        ref = "releng-15.1";  # foundations/generic.conf
    }
}
datasets {
    tmp {}  # foundations/generic.conf
    var {}  # foundations/generic.conf
    "usr/local" {  # foundations/generic.conf
        compression = "zstd-9";  # foundations/generic.conf
    }
}
pkg {
    sudo = true;  # foundations/generic.conf
}
derivations {
    "etc/pwd.db" {  # foundations/generic.conf
        source = "etc/master.passwd";  # foundations/generic.conf
        command = "pwd_mkdb -p -d /etc /etc/master.passwd";  # foundations/generic.conf
    }
}
$ shtetl config show containers/nginx
# containers/nginx.conf: a container's recipe, its layers merged
# layers, nearest last: shtetl.conf, containers/nginx.conf
ver = [
    ">=15.1",  # shtetl.conf
    "<16"  # containers/nginx.conf
]
requires {
    db = true;  # containers/nginx.conf
}
pkg {
    nginx = true;  # containers/nginx.conf
}
datasets {
    www {  # containers/nginx.conf
        readonly = true;  # containers/nginx.conf
    }
}
commands {
    nginx {  # containers/nginx.conf
        "containers/nginx.conf" = <<EOD
sysrc nginx_enable=YES
service nginx start
EOD
    }
}
jailconf {
    "containers/nginx.conf" = "allow.raw_sockets;";
}
```

The container shows only its own files and the default's. Its
foundation is chosen when it is created (`shtetl create nginx --on
generic`), and then its files stack on the foundation's.

## 5. How the files of a recipe stack

### The layers

A recipe's files merge in this order, nearest last:

1. the default: shtetl.conf, after the files it imports (section 6);
2. each file the recipe names in `import`, in order, each after the
   files it imports in turn;
3. the recipe's own file.

A file is merged once, at the first place it is reached; a file
reached again adds nothing. A cycle of imports is refused, naming its
files, and so is an import that does not exist. Imports nest at most
16 deep.

A foundation and a container are two layers of this kind: a
container's recipe stacks on its foundation's when it is created
(planning.md 7.4), by the same rules, so a container keeps
everything its foundation has.

<!-- example: imports -->

```ucl
# shtetl.conf
pkg = [ "sudo" ];
```

```ucl
# common/base.conf
pkg = [ "tmux" ];
```

```ucl
# common/web.conf
import = "common/base.conf";
pkg = [ "apache24" ];
```

```ucl
# common/tls.conf
import = [ "common/base.conf" ];
pkg = [ "py311-certbot" ];
```

```ucl
# containers/www.conf
import = [ "common/web.conf", "common/tls.conf" ];
pkg = [ "nginx" ];
```

```ucl
# common/a.conf
import = "common/b.conf";
```

```ucl
# common/b.conf
import = "common/a.conf";
```

```ucl
# containers/loop.conf
import = [ "common/a.conf", "common/nope.conf" ];
```

```console
$ shtetl config show containers/www
# containers/www.conf: a container's recipe, its layers merged
# layers, nearest last: shtetl.conf, common/base.conf, common/web.conf, common/tls.conf, containers/www.conf
pkg {
    sudo = true;  # shtetl.conf
    tmux = true;  # common/base.conf
    apache24 = true;  # common/web.conf
    py311-certbot = true;  # common/tls.conf
    nginx = true;  # containers/www.conf
}
$ shtetl config show containers/loop
shtetl: common/b.conf: import: import cycle: common/a.conf, common/b.conf, common/a.conf
shtetl: containers/loop.conf: import[1]: common/nope.conf: no such file
```

`common/base.conf` is imported twice and merged once, before
`common/web.conf`, where it was first reached.

### Everything stacks

Each file adds to what the files below it gave. No file takes
anything away, and none takes another's place: `false` as an entry
is refused, since dropping values is illegal. How each key stacks:

| key | two files giving it |
|-----|---------------------|
| `pkg`, `requires` | the union; an entry listed again adds nothing |
| `datasets`, `patches` | the union by key; an entry's fields compose, each field given once; a dataset's property may be given again, with a warning |
| `derivations` | the union by path; each derivation given once, whole |
| `commands` | the union by label; a label's text stacks, a piece from each file |
| `ver` | every constraint kept; all must hold |
| `buildopts`, `jailconf`, `kernconf`'s lines | a piece from each file, in file order |
| `src_branch` | one value: the files that give it give the same |
| `kernconf`'s config | one file of the stack gives it |

What one file gives, another does not give again: two files that need
the same thing defined two ways are not a composition of the two.
So a second value for a one-value key, a
derivation given in two files, or a field of an entry given twice
(even alike) is refused, naming both files. Listing an entry again
with nothing new is no redefinition. Two things may be given again:
a dataset's property, which the nearer file overrides with a
warning, and a commands label, whose texts stack. The default may be
overridden (section 6).

Within one file, a list may not name an entry twice.

### Collections: the union

<!-- example: union -->

```ucl
# common/tools.conf
pkg = [ "git", "tmux" ];
```

```ucl
# containers/dev.conf
import = "common/tools.conf";
pkg { git = true; go = true; }
```

```ucl
# containers/twice.conf
pkg = [ "go", "git", "go" ];
```

```ucl
# containers/drop.conf
import = "common/tools.conf";
pkg { tmux = false; }
```

```console
$ shtetl config show containers/dev
# containers/dev.conf: a container's recipe, its layers merged
# layers, nearest last: common/tools.conf, containers/dev.conf
pkg {
    git = true;  # common/tools.conf
    tmux = true;  # common/tools.conf
    go = true;  # containers/dev.conf
}
$ shtetl config show containers/twice
shtetl: containers/twice.conf: pkg[2]: duplicate entry "go"
$ shtetl config show containers/drop
shtetl: containers/drop.conf: pkg.tmux: dropping values is illegal
```

`git` comes from `common/tools.conf`, the file that first gave it;
`containers/dev.conf` naming it again adds nothing. A list is
shorthand for an object of `name = true` entries, and the two mix
across files. To be rid of `tmux`, take it out of
`common/tools.conf`, or do not import it.

### Entries: fields compose, each given once

<!-- example: entries -->

```ucl
# common/data.conf
datasets {
    www { readonly = true; }
    db { recordsize = "16k"; }
}
derivations {
    "etc/pwd.db" {
        source = "etc/master.passwd";
        command = "pwd_mkdb -p -d /etc /etc/master.passwd";
    }
}
```

```ucl
# containers/app.conf
import = "common/data.conf";
datasets {
    www { compression = "lz4"; }
    db { recordsize = "8k"; }
    cache {}
}
```

```ucl
# containers/redo.conf
import = "common/data.conf";
derivations {
    "etc/pwd.db" {
        source = "etc/master.passwd";
        command = "pwd_mkdb -p /etc/master.passwd";
    }
}
```

```ucl
# foundations/f.conf
import = "common/src.conf";
src_branch = "releng/15.1";
patches {
    "github.com/eidelbyte/freebsd-patches" { ref = "main"; }
}
```

```ucl
# common/src.conf
patches {
    "github.com/eidelbyte/freebsd-patches" { ref = "releng-15.1"; }
}
```

```console
$ shtetl config show containers/app
shtetl: warning: containers/app.conf: datasets.db.recordsize: 8k overrides 16k from common/data.conf
# containers/app.conf: a container's recipe, its layers merged
# layers, nearest last: common/data.conf, containers/app.conf
datasets {
    www {  # common/data.conf
        readonly = true;  # common/data.conf
        compression = "lz4";  # containers/app.conf
    }
    db {  # common/data.conf
        recordsize = "8k";  # containers/app.conf, over common/data.conf
    }
    cache {}  # containers/app.conf
}
derivations {
    "etc/pwd.db" {  # common/data.conf
        source = "etc/master.passwd";  # common/data.conf
        command = "pwd_mkdb -p -d /etc /etc/master.passwd";  # common/data.conf
    }
}
$ echo $?
0
$ shtetl config show containers/redo
shtetl: containers/redo.conf: derivations."etc/pwd.db": already defined in common/data.conf
$ shtetl config show foundations/f
shtetl: foundations/f.conf: patches."github.com/eidelbyte/freebsd-patches".ref: already defined in common/src.conf
```

`www` gets `readonly` from one file and `compression` from another:
they compose. `db`'s `recordsize` is given twice, and the nearer
file's stands, with a warning; the recipe still resolves (exit 0).
The comment `# containers/app.conf, over common/data.conf` says which
file set the value and which first gave the key. A derivation, or a
patch source's `ref`, given again is refused.

### Commands: pieces

<!-- example: commands -->

```ucl
# common/web.conf
commands {
    setup = <<EOD
sysrc apache24_enable=YES
EOD
}
```

```ucl
# containers/web.conf
import = "common/web.conf";
commands {
    setup = <<EOD
sysrc apache24_flags="-DSSL"
EOD
    motd = "echo web > /etc/motd";
}
```

```console
$ shtetl config show containers/web
# containers/web.conf: a container's recipe, its layers merged
# layers, nearest last: common/web.conf, containers/web.conf
commands {
    setup {  # common/web.conf
        "common/web.conf" = "sysrc apache24_enable=YES";
        "containers/web.conf" = "sysrc apache24_flags=\"-DSSL\"";
    }
    motd {  # containers/web.conf
        "containers/web.conf" = "echo web > /etc/motd";
    }
}
```

`setup` is one label with two pieces, keyed by the files that gave
them, run in that order. It runs where it was first given, before
`motd`. A container does not run a piece its foundation already ran
(the same label, file and text): the default's pieces, which are in
both, run once, when the foundation is built (planning.md 7.4).

### Text: pieces

<!-- example: text -->

```ucl
# common/build.conf
buildopts = "WITHOUT_LIB32=yes";
kernconf = <<EOD
nooptions INVARIANTS
EOD
```

```ucl
# foundations/lean.conf
import = "common/build.conf";
src_branch = "releng/15.1";
buildopts = <<EOD
WITHOUT_TESTS=yes
WITH_META_MODE=yes
EOD
kernconf = <<EOD
nooptions WITNESS
EOD
```

```console
$ shtetl config show foundations/lean
# foundations/lean.conf: a foundation's recipe, its layers merged
# layers, nearest last: common/build.conf, foundations/lean.conf
buildopts {
    "common/build.conf" = "WITHOUT_LIB32=yes";
    "foundations/lean.conf" = <<EOD
WITHOUT_TESTS=yes
WITH_META_MODE=yes
EOD
}
kernconf {
    lines {
        "common/build.conf" = "nooptions INVARIANTS";
        "foundations/lean.conf" = "nooptions WITNESS";
    }
}
src_branch = "releng/15.1";  # foundations/lean.conf
```

`buildopts`, `jailconf` and `kernconf`'s lines keep a piece from
each file that gives them, lower files first; shtetl writes the
pieces into the src.conf, the jail's block and the kernel config in
that order. A one-line heredoc prints as a quoted string under
`lines`.

### ver: all must hold

<!-- example: ver -->

```ucl
# common/new.conf
ver = ">=15";
```

```ucl
# containers/fits.conf
import = "common/new.conf";
ver = [ "<16", ">=15.1" ];
```

```ucl
# containers/never.conf
import = "common/new.conf";
ver = "<14";
```

```ucl
# containers/any15.conf
ver = [ "=15", ">=15.3" ];
```

```ucl
# containers/past15.conf
ver = [ ">15", "<=15.3" ];
```

```console
$ shtetl config show containers/fits
# containers/fits.conf: a container's recipe, its layers merged
# layers, nearest last: common/new.conf, containers/fits.conf
ver = [
    ">=15",  # common/new.conf
    "<16",  # containers/fits.conf
    ">=15.1"  # containers/fits.conf
]
$ shtetl config show containers/never
shtetl: containers/never.conf: ver: >=15 in common/new.conf conflicts with <14 in containers/never.conf
$ shtetl config show containers/any15
# containers/any15.conf: a container's recipe, its layers merged
# layers, nearest last: containers/any15.conf
ver = [
    "=15",  # containers/any15.conf
    ">=15.3"  # containers/any15.conf
]
$ shtetl config show containers/past15
shtetl: containers/past15.conf: ver: >15 in containers/past15.conf conflicts with <=15.3 in containers/past15.conf
```

A `ver` no version can satisfy is refused when the recipe is read,
naming the two constraints that cannot meet and their files. `"=15"`
takes 15.3, being every 15.x; `">15"` begins at 16.0, past every
15.x, and so meets no version `"<=15.3"` allows.

### One value

<!-- example: one-value -->

```ucl
# common/branch.conf
src_branch = "releng/15.1";
```

```ucl
# foundations/same.conf
import = "common/branch.conf";
src_branch = "releng/15.1";
```

```ucl
# foundations/other.conf
import = "common/branch.conf";
src_branch = "stable/15";
```

```ucl
# common/kern.conf
kernconf = "MINIMAL";
```

```ucl
# foundations/kern2.conf
import = [ "common/branch.conf", "common/kern.conf" ];
kernconf = "MINIMAL";
```

```console
$ shtetl config show foundations/same
# foundations/same.conf: a foundation's recipe, its layers merged
# layers, nearest last: common/branch.conf, foundations/same.conf
src_branch = "releng/15.1";  # common/branch.conf
$ shtetl config show foundations/other
shtetl: foundations/other.conf: src_branch: stable/15 conflicts with releng/15.1 in common/branch.conf
$ shtetl config show foundations/kern2
shtetl: foundations/kern2.conf: kernconf: already defined in common/kern.conf
```

`src_branch` may be given in several files when they give the same
value; two values are refused. `kernconf`'s config is given by one
file of the stack, alike or not. The default's value is the
exception: a recipe overrides it, with a warning (section 6).

## 6. The default: shtetl.conf

The default every recipe starts from is shtetl.conf, beside its
settings, after the files it imports. The recipes repository ships it
with:

    ver = ">=15.1";
    kernconf = "GENERIC";

The default is the one file a recipe may override:

- It takes every kind's keys, and each recipe takes those of its
  kind: a container takes no `kernconf` from it, a foundation no
  `jailconf`. Its settings are no recipe's.
- What would be a redefinition of the default's value overrides it,
  with a warning naming the value replaced and its file: a recipe's
  `src_branch` or `kernconf` config, a derivation for the same path, an
  entry's field, a dataset's property. Giving the default's value
  again, alike, is no override and says nothing.
- What stacks, stacks with the default: its `ver`, `pkg`, datasets,
  texts and commands reach every recipe of the kinds that take them.
  A recipe cannot take them away; to be rid of one, take it out of
  shtetl.conf.
- The files shtetl.conf imports are the default's too. They hold
  recipe keys only: the settings are shtetl.conf's own.

<!-- example: default -->

```ucl
# shtetl.conf
retention { run = 10; }
import = "common/site.conf";
ver = ">=15.1";
kernconf = "GENERIC";
jailconf = "allow.raw_sockets;";
pkg = [ "sudo" ];
datasets {
    var { compression = "zstd"; }
}
derivations {
    "etc/pwd.db" {
        source = "etc/master.passwd";
        command = "pwd_mkdb -p -d /etc /etc/master.passwd";
    }
}
```

```ucl
# common/site.conf
pkg = [ "tmux" ];
```

```ucl
# foundations/small.conf
src_branch = "releng/15.1";
kernconf = "MINIMAL";
```

```ucl
# containers/app.conf
pkg = [ "nginx" ];
datasets {
    var { compression = "lz4"; }
}
derivations {
    "etc/pwd.db" {
        source = "etc/master.passwd";
        command = "pwd_mkdb -p /etc/master.passwd";
    }
}
```

```ucl
# containers/old.conf
ver = "<15";
```

```console
$ shtetl config show foundations/small
shtetl: warning: foundations/small.conf: kernconf: MINIMAL overrides GENERIC from shtetl.conf
# foundations/small.conf: a foundation's recipe, its layers merged
# layers, nearest last: common/site.conf, shtetl.conf, foundations/small.conf
pkg {
    tmux = true;  # common/site.conf
    sudo = true;  # shtetl.conf
}
ver = [
    ">=15.1"  # shtetl.conf
]
kernconf {
    file = "MINIMAL";  # foundations/small.conf, over shtetl.conf
}
datasets {
    var {  # shtetl.conf
        compression = "zstd";  # shtetl.conf
    }
}
derivations {
    "etc/pwd.db" {  # shtetl.conf
        source = "etc/master.passwd";  # shtetl.conf
        command = "pwd_mkdb -p -d /etc /etc/master.passwd";  # shtetl.conf
    }
}
src_branch = "releng/15.1";  # foundations/small.conf
$ shtetl config show containers/app
shtetl: warning: containers/app.conf: datasets.var.compression: lz4 overrides zstd from shtetl.conf
shtetl: warning: containers/app.conf: derivations."etc/pwd.db": overrides shtetl.conf
# containers/app.conf: a container's recipe, its layers merged
# layers, nearest last: common/site.conf, shtetl.conf, containers/app.conf
pkg {
    tmux = true;  # common/site.conf
    sudo = true;  # shtetl.conf
    nginx = true;  # containers/app.conf
}
ver = [
    ">=15.1"  # shtetl.conf
]
jailconf {
    "shtetl.conf" = "allow.raw_sockets;";
}
datasets {
    var {  # shtetl.conf
        compression = "lz4";  # containers/app.conf, over shtetl.conf
    }
}
derivations {
    "etc/pwd.db" {  # containers/app.conf, over shtetl.conf
        source = "etc/master.passwd";  # containers/app.conf
        command = "pwd_mkdb -p /etc/master.passwd";  # containers/app.conf
    }
}
$ shtetl config show containers/old
shtetl: containers/old.conf: ver: >=15.1 in shtetl.conf conflicts with <15 in containers/old.conf
```

The foundation overrides `kernconf`'s config, with a warning, and
takes no `jailconf`; the container takes `jailconf` and no
`kernconf`, overrides the property and the derivation, a warning for
each, and keeps `sudo` and `tmux`. `ver` stacks, so
`containers/old` cannot be read with this shtetl.conf.

## 7. Checks after the merge

Some things only the merged recipe can answer:

| check | refused with |
|-------|--------------|
| a foundation has a `src_branch` | missing src_branch |
| every `ver` holds together | A in FILE conflicts with B in FILE |
| a `kernconf` config with a `/` exists in the configuration root | PATH: no such file |
| a `patches` directory exists in the configuration root | PATH: no such directory |
| each `requires` is a container recipe, not the container itself | no such container; requires itself |

<!-- example: after-merge -->

```ucl
# foundations/nobranch.conf
kernconf = "conf/MYKERNEL";
patches { "patches/mine" {} }
```

```ucl
# containers/lonely.conf
requires = [ "lonely", "ghost" ];
```

```console
$ shtetl config show foundations/nobranch
shtetl: foundations/nobranch.conf: missing src_branch
shtetl: foundations/nobranch.conf: kernconf: conf/MYKERNEL: no such file
shtetl: foundations/nobranch.conf: patches."patches/mine": patches/mine: no such directory
$ shtetl config show containers/lonely
shtetl: containers/lonely.conf: requires.lonely: requires itself
shtetl: containers/lonely.conf: requires.ghost: no such container
```

## 8. Messages, warnings and exit status

A refusal is a line on standard error, one for each problem, and the
exit status is 1: nothing is printed and nothing is done. A problem
in a value reads `shtetl: FILE: KEY.PATH: what is wrong`; a problem
in the text itself, `shtetl: FILE:LINE: what is wrong` (a duplicate
key, a macro, a syntax error). A warning, `shtetl: warning: FILE:
KEY.PATH: ...`, is printed and the command goes on, exit 0. A usage
error prints the usage and exits 2.

<!-- example: usage -->

```console
$ shtetl config show containers/a containers/b
shtetl: config show takes one recipe at most
usage: shtetl [-C dir] command ...
       shtetl [-C dir] config show [recipe]
       shtetl [-C dir] spaces
       shtetl [-C dir] bootstrap [--lake ds] [--run ds] [--config ds]
           [--build ds] [--recipes url] [-y]
       shtetl [-C dir] build [-y] [-m msg] [--continue | --skip | --abort]
           foundation
       shtetl [-C dir] pkg-upgrade [-y] [-m msg] foundation
       shtetl [-C dir] beam [-y] [--version v] foundation
       shtetl [-C dir] save [-m msg] container
       shtetl [-C dir] create [-y] [-m msg] container --on foundation
           [--version v]

  -C dir  the configuration root (the shtetl-recipes clone);
          without it, the config root's mountpoint

  config show: the recipe (foundations/F, containers/X, systems/S)
    with its layers merged, or the site's settings
  spaces: each space's root, and where config and build are mounted
  bootstrap: datasets you have made the roots of the spaces, and laid
    out, the recipes cloned into an empty config root, from
    --recipes or shtetl-recipes; shown first, done once confirmed
    (-y confirms)
  build: a foundation built from its recipe into zlake; shown
    first, done once confirmed (-y confirms); a build paused at a
    patch carried on, the fix made or the patch left out, or
    given up
  pkg-upgrade: a foundation's packages upgraded in a chroot of it,
    snapshotted as its last version's -pkg; shown first, done
    once confirmed (-y confirms)
  beam: a foundation's version, its newest by default,
    sent to zrun; shown first, done once confirmed (-y confirms)
  save: the container saved, its message -m's or written in
    $VISUAL or $EDITOR; mirrored into zlake
  create: a container cloned from a foundation's version, its
    newest by default, its jail started and saved; shown first,
    done once confirmed (-y confirms)

exit: 0 done, 1 refused (the reasons are on standard error), 2 usage,
      3 a build paused at a patch
$ echo $?
2
$ shtetl config show nginx
shtetl: nginx: invalid recipe: use foundations/NAME, containers/NAME or systems/NAME
$ echo $?
1
```

The second is a refusal, not a usage error: `nginx` is no recipe's
path.

`config show` prints UCL that reads back as the merged recipe, with
the file each value came from as a comment:

- `# common/data.conf`: the file that gave the value;
- `# containers/app.conf, over common/data.conf`: the file that set
  the value, then the file that first gave the key;
- `ver` and `pkg` and the other collections: each entry with its own
  comment;
- text and commands: an object of pieces keyed by the file that gave
  each.

## 9. Summary: when two files give the same thing

| two files give | result |
|----------------|--------|
| the same package, container or patch source | one entry, from the first file |
| one dataset, different properties | the dataset with both |
| one dataset property, two values | the nearer value, with a warning |
| one entry's field (a patch's `ref`), alike or not | refused |
| one derivation | refused |
| one commands label | both pieces, in file order |
| text (`buildopts`, `jailconf`, `kernconf`'s lines) | both pieces, in file order |
| `ver` | both kept; refused if no version satisfies both |
| `src_branch`, alike | one value |
| `src_branch`, different | refused |
| `kernconf`'s config, alike or not | refused |
| anything, one of the two being the default | the recipe's overrides the default's, with a warning, or stacks with it, as section 6 says |
| `false` | refused: dropping values is illegal |
