# UCL primer for shtetl

UCL ("universal configuration language") is the format of FreeBSD's
`pkg.conf` and of the repo files under `/etc/pkg/`. It is a superset of
JSON with a lighter, nginx-like syntax. shtetl reads all of its
configuration with libucl: `zconfig/shtetl.conf` and every file in
zshtetl-recipes.

Facts below were checked against the libucl that FreeBSD carries in
`contrib/libucl` (ucl_parser.c, ucl_util.c, include/ucl.h). shtetl
links the `textproc/libucl` port: base builds its copy as PRIVATELIB
(`lib/libucl/Makefile`), which third-party programs cannot link.

## 1. The shape of a file

A file is an object: keys and values. The outer braces are optional.

    # a comment
    src_branch = "stable/15";
    make_jobs = 8;

    build {
        kernconf = "GENERIC";
        make_jobs = 8;
    }

- `key = value;` and `key: value;` are the same thing. The `;` (or `,`)
  is optional at the end of a line.
- An object value needs no `=`: `build { ... }`.
- Keys may be unquoted, or quoted when they contain odd characters.
- Comments are `#` to end of line, and `/* ... */`, which may nest.
  There is no `//` comment.

The same file in JSON, which UCL also accepts:

    { "src_branch": "stable/15", "make_jobs": 8,
      "build": { "kernconf": "GENERIC", "make_jobs": 8 } }

### Named sections

    container "nginx" {
        jailconf = "...";
    }

is shorthand for

    container {
        nginx {
            jailconf = "...";
        }
    }

### Arrays

    pkg = [ "nginx", "py311-certbot" ];

Elements are separated by commas; a trailing comma is allowed.

### Multi-line strings (heredocs)

    commands = <<EOD
    sysrc nginx_enable=YES
    service nginx start
    EOD

The terminator is `<<` followed by capital letters only, the text
starts on the next line, and the terminator must stand alone on its
own line. Nothing inside is interpreted: no escapes, no comments. This
is how a kernel config or a shell snippet sits inside a recipe.

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
| `10k` | integer 10000 |
| `10kb` | integer 10240 (`kb`, `mb`, `gb` are powers of 1024) |
| `10min`, `30s`, `2h`, `1d`, `1w`, `1y`, `500ms` | a time, stored as float seconds |

Consequences for shtetl:

- **Versions must be quoted.** `ver = 15.10` is the float 15.1. The
  schema requires a string and says so when it gets a number.
- **ZFS property values may arrive as booleans.** `atime = off;` is
  boolean false, not the string "off". shtetl's schema maps a boolean
  to `on`/`off` for properties that take those words, and refuses it
  elsewhere.
- **Quote anything that looks like a number with a suffix.** A dataset
  named `10k` or a package version `1d` would be turned into a number.
- When in doubt, quote. A quoted string is always a string.

Double-quoted strings take JSON escapes (`\n`, `\"`, `\\`, `\uXXXX`);
shtetl files are ASCII only, so `\u` has no use here.

## 3. Duplicate keys, priorities and includes

These are libucl's own merge mechanisms. shtetl does **not** use them
for its cascade (section 4), but you need to know them because they
explain the strictness shtetl imposes.

### Duplicate keys

By default, writing a key twice at the same level does not replace it.
libucl keeps both values as an *implicit array*:

    pkg = "nginx";
    pkg = "git";

reads back as `pkg = ["nginx", "git"]` to code that asks for all
values, and as `"nginx"` to code that asks for one. Easy to write by
accident, invisible when read. **shtetl parses every file with
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
`target`, `priority`, and `duplicate`, which is one of
`append` (default: same priority makes an array), `merge` (objects and
arrays are merged into the existing value), `rewrite` (always replace)
or `error`. There are also `.includes` (signed), `.load` and
`.inherit`.

**shtetl refuses every macro.** Includes with priorities make the
result of a recipe depend on the order files happened to be included
and on numbers scattered across files. shtetl's cascade is explicit
instead (section 4), and a recipe that writes `.include` gets an error
telling it to use `import`.

(libucl has a flag, `UCL_PARSER_DISABLE_MACRO`, that treats macros as
comments. shtetl does not use it that way: a silently ignored
`.include` is worse than an error.)

### Variables

`$NAME` and `${NAME}` in strings expand to variables the program
registers. libucl registers `$FILENAME` and `$CURDIR` unless told not
to. shtetl registers none and turns the file variables off, so a `$`
in a value is a literal `$`.

## 4. How shtetl layers files

Each file is parsed alone, strictly (no duplicates, no macros). shtetl
then merges the parsed files itself, nearest layer last:

1. built-in defaults
2. `zconfig/shtetl.conf`
3. `zshtetl-recipes/default.conf`
4. each file named in the target recipe's `import`, in order
5. the target recipe (`foundations/F.conf`, `containers/X.conf`,
   `systems/S.conf`)

Merge rules come from shtetl's schema, by key:

| key type | rule |
|----------|------|
| scalar (string, number, boolean) | the nearer layer replaces |
| object | merged key by key, recursively |
| list | the nearer layer appends |

(How a recipe replaces a list instead of appending is open: planning.md
Q5.)

Unknown keys, and keys that belong to another kind (`kernconf` in a
container), are errors, not warnings.

`shtetl config show TARGET` (milestone M1) prints the merged result
with the file and line each value came from.

## 5. Mishkan's examples in UCL

mishkan's design doc had its own grammar. The same recipes in UCL:

`default.conf`

    derivations = [
        { source = "etc/master.passwd"; derived = "etc/pwd.db";
          command = "pwd_mkdb -p -d /etc /etc/master.passwd"; },
        { source = "etc/master.passwd"; derived = "etc/spwd.db";
          command = "pwd_mkdb -p -d /etc /etc/master.passwd"; },
        { source = "etc/login.conf"; derived = "etc/login.conf.db";
          command = "cap_mkdb /etc/login.conf"; },
        { source = "etc/mail/aliases"; derived = "etc/mail/aliases.db";
          command = "newaliases"; },
    ];

    datasets = [ "tmp", "var", "usr/local" ];

`foundations/generic.conf`

    kernconf = "GENERIC";

`foundations/gaming.conf`

    kernconf = <<EOD
    include         GENERIC
    ident           LEETGAMING
    device          drm
    options         SCHED_ULE
    EOD

    pkg = [ "linux-steam-utils" ];
    datasets = [ "home" ];          # appended to default.conf's list

    commands = <<EOD
    sysrc linux_enable="YES"
    EOD

`containers/nginx.conf`

    pkg = [ "nginx" ];
    datasets = [ { name = "www"; readonly = true; } ];

`containers/recipya.conf`

    requires = [ "nginx" ];
    pkg = [ "git", "go", "npm" ];
    datasets = [ { from = "nginx"; name = "www/recipya"; } ];

Differences from mishkan's grammar:

- `import default.conf` is gone: default.conf is always a layer.
  `import` remains for sharing between recipes
  (`import = [ "common/web.conf" ];`).
- `+=` is gone: lists append by default.
- `()` line lists and `[]` word lists are both UCL arrays; quoting
  makes the elements unambiguous.
- `{}` text literals are heredocs.
- `<nginx>/www` becomes an object naming the owning container.
- Mount types (`ro`, `rw`) become fields on a dataset object.

## 6. Quick reference

    key = "string";             string
    key = 8;                    integer
    key = true;                 boolean (also yes/on, no/off/false)
    key = [ "a", "b" ];         array
    key { sub = 1; }            object
    key "name" { sub = 1; }     same as key { name { sub = 1; } }
    key = <<EOD                 multi-line string, verbatim
    ...
    EOD
    # comment   /* comment */   (no //)

    Rules in shtetl:
    - quote versions and anything number-like
    - no key twice in one file
    - no macros (.include, .priority, ...); use import
    - no $variables
    - ASCII only
