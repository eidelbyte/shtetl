# UCL primer for shtetl

UCL ("universal configuration language") is the format of FreeBSD's
`pkg.conf` and of the repo files under `/etc/pkg/`; base also links it
into `ctld(8)`, `bhyve(8)` and `iovctl(8)`. It is a superset of JSON
with a lighter, nginx-like syntax. shtetl reads all of its
configuration with libucl: every file in the zshtetl-recipes clone,
`shtetl.conf` included. Why UCL rather than TOML, JSON or YAML is in
planning.md section 5.1.

Facts below were checked against the libucl that FreeBSD carries in
`contrib/libucl` (ucl_parser.c, ucl_util.c, ucl_hash.c, include/ucl.h).
Every key shtetl takes, and what it prints when files stack, is in
configuration.md, whose examples are run.
shtetl links the `textproc/libucl` port: base builds its copy as
PRIVATELIB (`lib/libucl/Makefile`), which third-party programs cannot
link.

## 1. The shape of a file

A file is an object: keys and values. The outer braces are optional.

    # a comment
    src_branch = "stable/15";

    update {
        suffix = "staging";
    }

- `key = value;` and `key: value;` are the same thing. The `;` (or `,`)
  is optional at the end of a line.
- An object value needs no `=`: `update { ... }`.
- Keys may be unquoted, or quoted when they contain odd characters
  (`"usr/local" {}`).
- Comments are `#` to end of line, and `/* ... */`, which may nest.
  There is no `//` comment.

The same file in JSON, which UCL also accepts:

    { "src_branch": "stable/15", "update": { "suffix": "staging" } }

### Named sections

    jail "nginx" {
        ip = "10.0.0.5";
    }

is shorthand for

    jail {
        nginx {
            ip = "10.0.0.5";
        }
    }

### Arrays

    pkg = [ "nginx", "tmux" ];

Elements are separated by commas; a trailing comma is allowed.

### Multi-line strings (heredocs)

    commands {
        nginx = <<EOD
    sysrc nginx_enable=YES
    service nginx start
    EOD
    }

The terminator is `<<` followed by capital letters only, the text
starts on the next line, and the terminator must stand alone on its
own line. Nothing inside is interpreted: no escapes, no comments. This
is how a kernel config or a shell snippet sits inside a recipe.

### Key order

libucl keeps an object's keys in the order they were written, and a
key that is replaced keeps its place (`DL_APPEND` and
`DL_REPLACE_ELEM` in ucl_hash.c). shtetl relies on this for
`commands`, which run in order.

## 2. Values and how they are typed

UCL types bare values as it reads them, before shtetl ever sees them.
This is the part that bites.

| written | becomes |
|---------|---------|
| `"stable/15"` | string |
| `stable/15` | string (an unquoted atom) |
| `8` | integer 8 |
| `15.1` | float 15.1 |
| `"15.1"` | string "15.1" |
| `true`, `yes`, `on` | boolean true |
| `false`, `no`, `off` | boolean false |
| `null` | null |
| `10k` | integer 10000 (`k`, `m`, `g` are powers of 1000) |
| `10kb` | integer 10240 (`kb`, `mb`, `gb` are powers of 1024) |
| `10min`, `30s`, `2h`, `1d`, `1w`, `1y`, `500ms` | a time, stored as float seconds |

Consequences for shtetl:

- **Versions must be quoted.** `ver = 15.10` is the float 15.1. The
  schema requires a string and says so when it gets a number.
- **Retention and depth intervals must be quoted.** shtetl reads them
  with date(1) units, where `m` is a month; UCL reads a bare `6m` as
  the integer 6000000 and a bare `30d` as a time in seconds. Write
  `archive = "6m";`.
- **ZFS property values may arrive as booleans.** `atime = off;` is
  boolean false, not the string "off". shtetl's schema maps a boolean
  to `on`/`off` for properties that take those words, and refuses it
  elsewhere.
- **Quote anything that looks like a number with a suffix.** A package
  or dataset named like `10k` would be turned into a number.
- When in doubt, quote. A quoted string is always a string.

Double-quoted strings take JSON escapes (`\n`, `\"`, `\\`); shtetl
files are ASCII only.

## 3. Duplicate keys, priorities and macros

These are libucl's own merge mechanisms. shtetl does **not** use them,
but you need to know them because they explain the strictness shtetl
imposes.

### Duplicate keys

By default, writing a key twice at the same level does not replace it.
libucl keeps both values as an *implicit array*:

    pkg = "nginx";
    pkg = "git";

reads back as `["nginx", "git"]` to code that asks for all values, and
as `"nginx"` to code that asks for one. Easy to write by accident,
invisible when read. **shtetl parses every file with
`UCL_DUPLICATE_ERROR`: a repeated key is an error with file and
line.**

### Priorities

Every value carries a priority from 0 to 15 (4 bits; default 0). When
a key is seen twice under the default strategy (`append`):

- equal priority: implicit array, as above;
- the new value's priority is higher: it replaces the old;
- the new value's priority is lower: it is dropped.

### Macros

Lines starting with `.` are macros:

    .include "common.conf"
    .include(priority=5, duplicate="merge") "site.conf"
    .try_include "/usr/local/etc/local.conf"
    .priority 3

`.include` takes parameters: `try`, `sign`, `glob`, `prefix`, `key`,
`target`, `priority`, and `duplicate`, which is one of `append`
(default), `merge`, `rewrite` or `error`. There are also `.includes`
(signed), `.load` and `.inherit`.

**shtetl refuses every macro.** Includes with priorities make a
recipe's result depend on include order and on numbers scattered
across files. shtetl's cascade is explicit instead (section 4): a
recipe that writes `.include` gets an error naming the macro and its
line, and `import` does what it was after.

libucl's header says its flag `UCL_PARSER_DISABLE_MACRO` treats macros
as comments. In libucl 0.9.4 it does not: a macro becomes a key that
does not begin with a letter, a parse error. shtetl sets the flag and,
since a silently ignored `.include` is worse than an error, checks at
start-up that the linked libucl refuses a macro, and reads nothing
with one that does not (planning.md 5.3).

### Variables

`$NAME` and `${NAME}` in strings expand to variables the program
registers. libucl registers `$FILENAME` and `$CURDIR` unless told not
to. shtetl registers none and turns the file variables off, so a `$`
in a value is a literal `$`.

## 4. How shtetl layers files

`shtetl.conf` holds two things: the machine's settings (where things
go, properties, retention), in their own schema, and beside them the
default recipe, the layer every recipe starts from. A recipe is
built from layers, nearest last:

1. the default: `shtetl.conf`, after what it imports
2. each recipe named in the target's `import`, in order, each after
   what it imports in turn (a recipe reached twice merges once, at
   its first place)
3. the target recipe (`foundations/F.conf`, `containers/X.conf`,
   `systems/S.conf`)

Each file is parsed alone, strictly (no duplicates, no macros, no
variables). shtetl merges the parsed layers itself:

| value | rule |
|-------|------|
| `ver` | every file's constraint kept; all must hold together |
| `buildopts`, `jailconf`, `kernconf`'s lines (a heredoc) | text: a piece from each file, kept in order, lower files first |
| `pkg`, `requires` | the union |
| `datasets`, `patches` | an object keyed by name: the union, an entry's fields composing |
| `derivations` | the union; each one given whole, in one file |
| `commands` | the union by label; one label's texts stack, a piece from each file |
| `src_branch` | one value: every file that gives it gives the same |
| `kernconf`'s config (a string) | one value, given by one file |

Everything stacks: a file adds to what the files below gave, and
nothing takes anything away or takes another file's place. `false`
as an entry is refused: dropping values is illegal. So whatever its
foundation provides, a container keeps.

What one file gives, another does not give again: a second value for
a one-value key, a derivation given in two files, a patch source's
`ref` given twice, are each refused, naming both files. Listing an
entry again with nothing new (`sudo` in two `pkg` lists) is fine. A
dataset's property is the exception: a nearer file may give it again,
with a warning, the property being the container's own clone's.

The default is the one layer a recipe may override. It may hold any
kind's keys, and each recipe takes those of its kind (a container
takes no `kernconf`). What a recipe gives again of its one-value
keys, derivations, fields or dataset properties replaces the
default's, with a warning. What stacks, stacks with it: its `pkg`
and `ver` reach every recipe, and the way to be rid of one is to take
it out of `shtetl.conf`.

| to | write |
|----|-------|
| add an element | a new key: `pkg { nginx = true; }` |
| add to an element | the same key with new fields: `datasets { www { readonly = true; } }` |
| change a default | give it again: `kernconf = "MINIMAL";` |

An array of names is shorthand for an object of `name = true` entries,
for the common case: `pkg = [ "nginx", "git" ];`.

Order follows the first definition: a `commands` label a later file
gives again runs where an earlier file first put it, its pieces in
file order. A container does not run a piece its foundation already
ran (same label, same file, same text): `shtetl.conf` is a layer of
both, and its pieces run once, when the foundation is built.

Unknown keys, and keys that belong to another kind (`src_branch` in
a container's files), are errors, not warnings: `invalid key`, `valid
only in foundation recipe`.

`shtetl -C <zconfig> config show TARGET` (built in M1) prints the
merged result as UCL, each value with the file it came from. libucl
keeps no line on a value, so a refusal names the file and the key
path (`datasets."usr/local".readonly`) instead.

## 5. Examples

`shtetl.conf`, the default recipe beside the settings

    properties {
        default { compression = "zstd"; }
        lake    { compression = "zstd-9"; }
    }
    update {
        suffix = "staging";
        depth = 0;          # replay every save zrun holds
    }
    retention {
        run = 10;
        archive = "1y";
    }

    ver = ">=15.1";
    kernconf = "GENERIC";

    datasets {
        tmp {}
        var {}
        "usr/local" {}
    }

    derivations {
        "etc/pwd.db" {
            source = "etc/master.passwd";
            command = "pwd_mkdb -p -d /etc /etc/master.passwd";
        }
        "etc/spwd.db" {
            source = "etc/master.passwd";
            command = "pwd_mkdb -p -d /etc /etc/master.passwd";
        }
        "etc/login.conf.db" {
            source = "etc/login.conf";
            command = "cap_mkdb /etc/login.conf";
        }
        "etc/mail/aliases.db" {
            source = "etc/mail/aliases";
            command = "newaliases";
        }
    }

    pkg = [ "sudo", "tmux" ];

`foundations/generic.conf`

    src_branch = "releng/15.1";

`foundations/gaming.conf`

    src_branch = "stable/15";
    kernconf = <<EOD
    device          drm
    options         SCHED_ULE
    EOD

    pkg = [ "linux-steam-utils" ];
    datasets { home {} }

    commands {
        linux = <<EOD
    sysrc linux_enable="YES"
    EOD
    }

shtetl writes the kernel config itself: `include GENERIC` (the
default's `kernconf`), an `ident` of its own, then the lines. A
heredoc is lines; a quoted string is the config to start from.

`common/web.conf`

    pkg = [ "apache24", "py311-certbot" ];
    commands {
        certbot = <<EOD
    sysrc certbot_enable=YES
    EOD
    }

`containers/nginx.conf`

    ver = "<16";
    import = [ "common/web.conf" ];

    pkg { nginx = true; }
    datasets {
        www { readonly = true; }
    }
    commands {
        nginx = <<EOD
    sysrc nginx_enable=YES
    EOD
    }
    jailconf = <<EOD
    allow.raw_sockets;
    EOD

Merged for nginx: pkg = sudo, tmux, apache24, py311-certbot, nginx;
datasets = tmp, var, usr/local, www; commands = certbot, then nginx;
ver `>=15.1` and `<16`, both to hold. Everything stacks, so a
container ends with everything its foundation provides, `shtetl.conf`
being a file of the foundation's recipe too (planning.md 7.4).
`jailconf` is only the options: shtetl writes the `nginx { ... }`
block around them.

A container recipe names no foundation: `ver` says which foundations it
fits, and the foundation is chosen with `shtetl create nginx --on
generic`.

`containers/recipya.conf`

    requires = [ "nginx" ];
    pkg = [ "git", "go", "npm" ];
    datasets {
        "www/recipya" { from = "nginx"; }
    }

### Differences from mishkan's grammar

- `import default.conf` is gone: the default is `shtetl.conf`, always
  a layer. `import` remains for sharing between recipes
  (`import = [ "common/web.conf" ];`).
- `+=` is gone: collections merge by key, and everything stacks;
  nothing takes an element away.
- `()` line lists and `[]` word lists are both keyed objects, with an
  array as shorthand for plain names.
- `{}` text literals are heredocs.
- `<nginx>/www` becomes a dataset object naming the owning container.
- Mount types (`ro`, `rw`) become fields on a dataset object.

## 6. Quick reference

    key = "string";             string
    key = 8;                    integer
    key = true;                 boolean (also yes/on, no/off/false)
    key = [ "a", "b" ];         array (in a collection: a { } b { })
    key { sub = 1; }            object
    key "name" { sub = 1; }     same as key { name { sub = 1; } }
    key = <<EOD                 multi-line string, verbatim
    ...
    EOD
    # comment   /* comment */   (no //)

    Rules in shtetl:
    - quote versions, retention intervals, anything number-like
    - no key twice in one file
    - no macros (.include, .priority, ...); use import
    - no $variables
    - collections are keyed, and everything stacks: nothing is dropped
    - ASCII only
