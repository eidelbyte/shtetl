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

    .include "default.conf"
    .include(priority=5, duplicate="merge") "site.conf"
    .try_include "/usr/local/etc/local.conf"
    .priority 3

`.include` takes parameters: `try`, `sign`, `glob`, `prefix`, `key`,
`target`, `priority`, and `duplicate`, which is one of `append`
(default), `merge`, `rewrite` or `error`. There are also `.includes`
(signed), `.load` and `.inherit`.

**shtetl refuses every macro.** Includes with priorities make a
recipe's result depend on include order and on numbers scattered
across files. shtetl's cascade is explicit instead (section 4), and a
recipe that writes `.include` gets an error telling it to use
`import`.

libucl has a flag, `UCL_PARSER_DISABLE_MACRO`, that treats macros as
comments. shtetl does not rely on it: a silently ignored `.include`
is worse than an error.

### Variables

`$NAME` and `${NAME}` in strings expand to variables the program
registers. libucl registers `$FILENAME` and `$CURDIR` unless told not
to. shtetl registers none and turns the file variables off, so a `$`
in a value is a literal `$`.

## 4. How shtetl layers files

`shtetl.conf` is about the machine (where things go, properties,
retention) and has its own schema; it is not part of any recipe. A
recipe is built from layers, nearest last:

1. built-in recipe defaults
2. `default.conf`
3. each recipe named in the target's `import`, in order
4. the target recipe (`foundations/F.conf`, `containers/X.conf`,
   `systems/S.conf`)

Each file is parsed alone, strictly (no duplicates, no macros, no
variables). shtetl merges the parsed layers itself:

| value | rule |
|-------|------|
| scalar (string, number, boolean) | the nearer layer replaces |
| object | merged key by key, recursively |
| a collection (`pkg`, `datasets`, `derivations`, `requires`, `commands`) | an object keyed by name: merged key by key |

So editing an inherited collection uses the one rule objects already
follow:

| to | write |
|----|-------|
| add an element | a new key: `pkg { nginx = true; }` |
| change one element | the same key with new fields: `datasets { www { readonly = true; } }` |
| drop one element | set it to `false`: `pkg { tmux = false; }` |
| drop everything inherited | `reset = [ "pkg" ];` at the top of the layer |

An array of names is shorthand for an object of `name = true` entries,
for the common case: `pkg = [ "nginx", "git" ];`.

Order follows the first definition: a `commands` block a later layer
overrides runs where an earlier layer first put it.

Unknown keys, and keys that belong to another kind (`kernconf` in a
container), are errors, not warnings.

`shtetl config show TARGET` (milestone M1) prints the merged result
with the file and line each value came from.

## 5. Examples

`default.conf`

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
    kernconf = "GENERIC";

`foundations/gaming.conf`

    src_branch = "stable/15";
    kernconf = <<EOD
    include         GENERIC
    ident           LEETGAMING
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

`containers/nginx.conf`

    ver = ">=15";

    pkg { nginx = true; tmux = false; }
    datasets {
        www { readonly = true; }
    }
    commands {
        nginx = <<EOD
    sysrc nginx_enable=YES
    EOD
    }

Merged for nginx: pkg = sudo, nginx; datasets = tmp, var, usr/local,
www; commands = nginx. A container cannot drop a dataset its foundation
splits, so `tmp = false` here would be an error (planning.md 14, F);
whether `tmux = false` should be one too, when the foundation installed
tmux, is planning.md Q-H.

A container recipe names no foundation: `ver` says which foundations it
fits, and the foundation is chosen with `shtetl create nginx --on
generic`.

`containers/recipya.conf`

    requires = [ "nginx" ];
    pkg = [ "git", "go", "npm" ];
    datasets {
        "www/recipya" { from = "nginx"; }
    }

`shtetl.conf`

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

### Differences from mishkan's grammar

- `import default.conf` is gone: default.conf is always a layer.
  `import` remains for sharing between recipes
  (`import = [ "common/web.conf" ];`).
- `+=` is gone: collections merge by key; `false` drops one element and
  `reset` drops them all.
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
    - collections are keyed: name = false drops one, reset drops all
    - ASCII only
