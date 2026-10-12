The rule this file follows: plot each phase's problem space before
writing its tests. Enumerate the dimensions, cross them into cells,
and give every cell a disposition: planned (names the test that will
close it), covered (the test exists and names the cell), deferred
(with the reason and the work that unblocks it). A behavior a ruling
takes away goes from the matrix with its cells. A cell with no row
is a hole. Each test's comment names the cells it closes. When a cell
flips from planned to covered, the row changes with it.

# Test matrix for shtetl

Phases, from planning.md 11: config and cascade (M1, family C here),
then discovery, bootstrap, patches and the pause, foundation build,
beam-down, create, save, update, history replay, gate handling, soak,
cutover, abandon, mirror, restore, prune. Each gets its family when
its milestone starts.

## Observation mechanism

M1 is the configuration only, and all of it runs on the mac with
make check: no ZFS, no jail, no program run. A cell is closed by a
return value, the tree a function built, the text an error sink
caught (an open_memstream in the tests), or the command's output and
exit status.

- unit: tests/check_*.c, each linked against the core and libucl.
- command line: tests/run-cli.sh runs ./shtetl over the
  configuration trees in tests/conf and compares stdout, stderr and
  the exit status with tests/cli/*.

Both builds of libucl count: the portable build compiles FreeBSD's
contrib/libucl (0.9.4), and make check-freebsd links the
textproc/libucl port on the box. The parse cells hold for whichever
is linked, which is the point of running them twice.

# Family C: configuration (M1)

## CP: one file parsed (planning.md 5.3; src/core/parse.c)

Dimensions: input shape {empty, an object, a top-level array or
scalar}; UCL value kind {bool, int, float, time, string, heredoc,
null}; nesting {object in object, array, array of objects}; the
duplicate rule {scalar, object, nested, keys differing in case};
macros {top level, nested, each built-in name, an unknown name, the
same text where no macro can be}; variables {$NAME, ${NAME}, the
file variables}; failures {syntax, NUL byte, missing, not a regular
file, too large, nested too deep}; the library itself {the start-up
check}.

| cell | scenario | disposition |
|------|----------|-------------|
| CP1 | an empty file is an empty object | covered: check_parse.c |
| CP2 | a top-level array or scalar is refused | covered: check_parse.c |
| CP3 | every scalar kind arrives typed: bool (true, false, yes, off), int (10, 0x10, 1k), float (15.10), string, null | covered: check_parse.c |
| CP4 | an unquoted time (30d, 10min) arrives as a number, never a string | covered: check_parse.c |
| CP5 | a heredoc is verbatim: no escapes, no expansion, a `.include` line inside is text | covered: check_parse.c |
| CP6 | nesting: objects, arrays, arrays of objects; keys in the order written | covered: check_parse.c |
| CP7 | a scalar key written twice is refused, with its line | covered: check_parse.c |
| CP8 | an object key written twice is refused (libucl's default would merge them) | covered: check_parse.c |
| CP9 | a duplicate inside a nested object is refused | covered: check_parse.c |
| CP10 | keys differing only in case are two keys | covered: check_parse.c |
| CP11 | `.include` at the top is refused as a macro, with its line, and no file is read | covered: check_parse.c |
| CP12 | `.priority` inside an object is refused as a macro, with its line | covered: check_parse.c |
| CP13 | `.try_include`, `.includes`, `.load` and `.inherit` are each refused | covered: check_parse.c |
| CP14 | an unknown `.foo` is refused as a macro | covered: check_parse.c |
| CP15 | `.include` as an array element or as a value is plain text | covered: check_parse.c |
| CP16 | `$FOO`, `${CURDIR}` and `$FILENAME` stay literal, in strings and heredocs | covered: check_parse.c |
| CP17 | a syntax error is refused, naming the file | covered: check_parse.c |
| CP18 | a NUL byte in the input is refused | covered: check_parse.c |
| CP19 | a missing file is refused, naming it | covered: check_parse.c |
| CP20 | a directory is refused: not a regular file | covered: check_parse.c |
| CP21 | a file over the size limit is refused | covered: check_parse.c |
| CP22 | every node of a parsed tree names its file as its layer | covered: check_parse.c |
| CP23 | the start-up check passes on the linked libucl: a macro refused at line 1, `$` literal, a duplicate refused, a heredoc marked and a string not | covered: check_parse.c |
| CP24 | every refusal names the file | covered: check_parse.c |
| CP25 | a value nested more than 64 deep is refused, naming its key path | covered: check_parse.c |
| CP26 | a heredoc is marked as lines, one line or many; a quoted string is not, though its text be the same; a copy keeps the mark | covered: check_parse.c |

## CK: a recipe layer against its kind (planning.md 5.5; src/core/schema.c)

Dimensions: key {each key of 5.5, the `_file` twins}; kind
{foundation, container, system}; value {the right type, each wrong
type}; collection form {object, array shorthand, entry true, false,
object, string}; entry key syntax per collection; error reporting
{one, several, the path's spelling}.

| cell | scenario | disposition |
|------|----------|-------------|
| CK1 | every key of 5.5 is accepted in each kind that has it | covered: check_schema.c |
| CK2 | an unknown key is refused with the file and its key path | covered: check_schema.c |
| CK3 | a foundation-only key (src_branch, kernconf, buildopts, patches) in a container and in a system is refused: valid only in foundation recipe | covered: check_schema.c |
| CK4 | a container-only key (requires, jailconf) in a foundation and in a system is refused | covered: check_schema.c |
| CK5 | a string key given a number or a bool is refused: must be a quoted string | covered: check_schema.c |
| CK6 | ver, one constraint or a list of them: `>=15`, `=15.1`, `<16`, `<=15.1`, `>14` accepted; ``, `15.1`, `>=`, `=>15`, `>=15.x`, `>=15.` refused | covered: check_schema.c, check_cascade.c test_ver |
| CK7 | kernconf: a string is the config (a name, or a path in zconfig), a heredoc lines, even one line; a quoted string with a newline is a path and refused | covered: check_schema.c |
| CK8 | a `_file` value: relative paths accepted; absolute, `..`, empty components refused | covered: check_schema.c |
| CK9 | import: an array of strings, or one string; a non-string element or a bad path refused | covered: check_schema.c |
| CK11 | pkg: array shorthand; entries true; false refused (dropping values is illegal); an entry with fields refused; bad names (empty, leading `-`, a space) refused | covered: check_schema.c |
| CK12 | datasets: paths by ZFS's characters; leading or trailing `/`, `//`, `.`, `..`, `@` refused | covered: check_schema.c |
| CK13 | dataset entries: true, or properties with string or bool values; false refused; a number refused (must be a quoted string); mountpoint, canmount and shtetl:* refused; a bad property name refused | covered: check_schema.c |
| CK14 | commands: a string; true, false, an object or an array of names refused; an empty string refused; label syntax | covered: check_schema.c |
| CK15 | derivations: {source, command}, both, in one file; false, an unknown field, a non-string field, half an entry refused; key path syntax | covered: check_schema.c |
| CK16 | requires: container names, array shorthand; false and a bad name refused | covered: check_schema.c |
| CK17 | patches: array shorthand; {ref, dir}; the three location forms (a URL, host/path, a zconfig directory); false, an absolute path, `..`, an scp-style address and an unknown field refused | covered: check_schema.c |
| CK18 | a collection given as a scalar is refused | covered: check_schema.c |
| CK19 | every error in a layer is reported, not only the first | covered: check_schema.c |
| CK20 | the key path quotes a component that is not a bare word (`datasets."usr/local".readonly`) | covered: check_schema.c |
| CK22 | a file of the default layer takes every kind's keys; shtetl.conf's sections of settings are no recipe keys, and a file it imports may have none; an unknown key is refused | covered: check_schema.c test_default |

## CS: shtetl.conf (planning.md 5.2, 6.3; src/core/site.c)

Dimensions: section {spaces, properties, build, update, retention,
an unknown one}; value {the right type, each wrong one}; the policy
forms of 6.3 {0, a count, an interval, each malformed one}; layers
{built-in only, shtetl.conf over it}.

| cell | scenario | disposition |
|------|----------|-------------|
| CS1 | 5.2's example is accepted as written | covered: check_site.c |
| CS2 | an unknown section, or an unknown key in a section, is refused; a key at the top that is neither a section nor a recipe's key is refused | covered: check_site.c |
| CS3 | spaces: lake and run take `default`, a dataset name; config or build with one is refused; a bad name is refused | covered: check_site.c |
| CS4 | properties: sections default, lake, run, config, build; string or bool values; a number refused; mountpoint, canmount, shtetl:* refused | covered: check_site.c |
| CS5 | build.make_jobs: 0 or more; negative, a string, a float refused | covered: check_site.c |
| CS6 | update.suffix: a name; empty or a bad character refused | covered: check_site.c |
| CS7 | retention.run, retention.archive, update.depth: 0, 10, "7d", "6m", "1y", "12H" accepted; -1, "7x", "d7", "", "0d", 1.5 and an unquoted 30d refused | covered: check_site.c |
| CS8 | with no shtetl.conf the settings are the built-in ones (4.3's properties, 6.3's zeros, suffix staging) | covered: check_site.c |
| CS9 | shtetl.conf sets one property; the rest stay built-in, each with its layer | covered: check_site.c |
| CS10 | retention.lake is refused: zlake keeps what zrun keeps | covered: check_site.c |
| CS11 | the default recipe's keys beside the settings: checked by the recipe schema, and out of the settings' view | covered: check_site.c |

## CC: the cascade (planning.md 5.4; src/core/cascade.c)

Dimensions: layers {the default (shtetl.conf present or absent, its
imports), imports flat, nested, repeated, cyclic, missing, the
target}; merge {one value (agree, disagree, over the default), text
pieces, ver, collection add, a field given again (over a recipe file,
over the default), a dataset property overridden, a derivation given
again, a commands piece, array shorthand, true over fields, false};
the default's keys {of the recipe's kind, of another
kind, settings}; order; checks after the merge; the target's
spelling.

| cell | scenario | disposition |
|------|----------|-------------|
| CC1 | layer order: shtetl.conf (the default) after what it imports, then the recipe's imports in order, then the recipe | covered: check_cascade.c |
| CC2 | no shtetl.conf: the recipe resolves with no default layer | covered: check_cascade.c |
| CC3 | an import's own imports merge before it | covered: check_cascade.c |
| CC4 | a recipe imported twice merges once, at its first place | covered: check_cascade.c |
| CC5 | an import cycle is refused, naming the files in it | covered: check_cascade.c |
| CC6 | an import of a missing file is refused, naming the importing file | covered: check_cascade.c |
| CC7 | ver: every file's constraints kept, in layer order, each with its file, to hold together; src_branch, one value: given again only alike, else refused naming both files | covered: check_cascade.c test_stacked |
| CC8 | buildopts, jailconf and kernconf's lines append: a piece from each file, keyed by it, lower layers first | covered: check_cascade.c test_stacked |
| CC9 | collections: a key added; an entry's fields compose across files, each given once | covered: check_cascade.c |
| CC10 | array shorthand merges as name = true; true over an entry with fields keeps the fields | covered: check_cascade.c |
| CC11 | false as an entry is refused in every kind of recipe: dropping values is illegal | covered: check_cascade.c test_nodrops |
| CC12 | what stacks in the default (shtetl.conf and its imports), every recipe keeps, commands included | covered: check_cascade.c test_nodrops, test_default |
| CC16 | order: keys keep their first place across layers; an overridden commands block stays where it was first defined | covered: check_cascade.c |
| CC17 | 5.4's worked example, its default in shtetl.conf: pkg sudo, tmux, apache24, py311-certbot, nginx; datasets tmp, var, usr/local, www; commands certbot, nginx | covered: check_cascade.c |
| CC18 | a foundation with no src_branch after the merge is refused | covered: check_cascade.c |
| CC19 | a derivation given in a second file, alike or not, is refused, naming the first | covered: check_cascade.c |
| CC20 | a kernconf config path or a patches directory that does not exist in the config root is refused, naming the key | covered: check_cascade.c |
| CC21 | requires naming a container with no recipe, or the container itself, is refused | covered: check_cascade.c |
| CC22 | targets: `containers/nginx` and `containers/nginx.conf` alike; an unknown kind directory, `..`, a bad name, a nested path, a missing recipe refused | covered: check_cascade.c |
| CC23 | errors in several layers are all reported, and nothing is merged | covered: check_cascade.c |
| CC24 | each merged entry names the layer that added it and the one that last set it | covered: check_cascade.c |
| CC25 | patches: a lower layer's sources come first; a source a nearer layer changes keeps its place (7.1.1) | covered: check_cascade.c |
| CC26 | every layer is checked against the target's kind: kernconf in a fragment a container imports is refused, naming the fragment | covered: check_cascade.c |
| CC27 | the ver constraints of all the files must leave a version: >=16 with <15, =15.1 with >15.1, refused naming the two | covered: check_cascade.c test_ver |
| CC28 | a dataset property a nearer file gives again, differently, overrides, with a warning naming both values and the first file | covered: check_cascade.c |
| CC29 | a commands block given in several files stacks: a piece from each, keyed by its file, lower first | covered: check_cascade.c |
| CC30 | an entry's field a second file gives again, alike or not, is refused (a patch source's ref); a field the first did not give composes (dir) | covered: check_cascade.c |
| CC31 | a recipe takes from the default only the keys of its kind (a container no kernconf, a foundation no jailconf), and none of shtetl.conf's settings | covered: check_cascade.c test_default |
| CC32 | what a recipe gives again of the default's one-value keys, derivations, entries' fields and dataset properties overrides it, each with a warning naming the value replaced and its file; the default's value given again alike is no override and warns nothing | covered: check_cascade.c test_default; run-cli.sh cl4-generic (GENERIC again, alike) |
| CC33 | what stacks, stacks with the default: pkg, ver, text, commands | covered: check_cascade.c test_default |
| CC34 | the files shtetl.conf imports are of the default too | covered: check_cascade.c test_default |
| CC35 | shtetl.conf is checked whole wherever it is read: a refused setting refuses a recipe's resolve, naming the setting; a file shtetl.conf imports holds no settings | covered: check_cascade.c test_site_checked; configuration.md, example settings-refused |
| CC36 | in ver a bare major is every minor of it: =15 any 15.x, <=15 takes 15.x, >15 begins at 16.0; N.N is that version; each form's range, and the meets they make (=15 with >=15.3; =15 with <15 and >15 with <16 refused) | covered: check_cascade.c test_ver; configuration.md, example ver |
| CC37 | kernconf: the config from one file of the stack, a second file giving it refused, alike or not; the default's overridden with a warning; lines stack with the default's and the imports'; merged as { file; lines } | covered: check_cascade.c test_stacked, test_default |

## CW: what config show prints (src/core/show.c)

| cell | scenario | disposition |
|------|----------|-------------|
| CW1 | a merged recipe prints as UCL that parses back to the same keys, values and order | covered: check_show.c |
| CW2 | every value names its layer; one changed by a later layer names both | covered: check_show.c |
| CW3 | quotes, backslashes and control characters are escaped, and come back the same | covered: check_show.c |
| CW4 | a multi-line string prints as a heredoc; one ending in a newline, or holding the terminator line, still comes back the same | covered: check_show.c |
| CW5 | keys that are not bare words are quoted (`"usr/local"`, a URL) | covered: check_show.c |
| CW6 | a collection written empty prints as an empty object | covered: check_show.c |
| CW7 | the site settings print with built-in or shtetl.conf on each value | covered: check_show.c |
| CW8 | a key that stacks prints its values with their layers: ver a value a line, each ending in its layer (a comment never between [ and the first value, which libucl would read as part of it); buildopts, jailconf and each commands block as an object of pieces keyed by their files, heredocs where they hold lines | covered: check_show.c |
| CW9 | kernconf prints as { file; lines { pieces } }; only a collection's entries carry a layer comment, so lines prints as text does | covered: run-cli.sh cl4-generic; configuration.md, examples keys and text |

## CL: the command line (src/main.c; tests/run-cli.sh)

| cell | scenario | disposition |
|------|----------|-------------|
| CL1 | no arguments, or -h: the usage, exit 2 (-h: exit 0) | covered: run-cli.sh cl1-none, cl1-help |
| CL2 | an unknown command or option, or -C with no directory: the usage, exit 2 | covered: run-cli.sh cl2-command, cl2-option, cl2-optarg |
| CL3 | no -C: the configuration root is looked for by discovery (D14); a portable shtetl, with no ZFS, refuses, exit 1 | covered: run-cli.sh cl3-noroot |
| CL4 | config show RECIPE: the merged recipe on stdout, exit 0 | covered: run-cli.sh cl4-nginx, cl4-generic |
| CL5 | config show: the site settings, exit 0 | covered: run-cli.sh cl5-site, cl5-builtin |
| CL6 | a refused configuration, a missing recipe, a root that is not a directory: the reasons on stderr, nothing on stdout, exit 1 | covered: run-cli.sh cl6-bad, cl6-drop, cl6-notdir, cl6-norecipe |
| CL7 | config show with two recipes: the usage, exit 2 | covered: run-cli.sh cl7-two |

# Family R: running a program (M2; src/core/run.c)

shtetl runs other programs (git from M2, then make, pkg, jail and
zfs_rebase) one way: an argument list, never a shell (planning.md
10). Observed on the mac with the system's own programs as stand-ins,
and a small sh script where a case needs a child that misbehaves.

Dimensions: finding the program {by name on shtetl's fixed PATH, by
path, missing, not executable}; its end {exit 0, exit non-zero,
killed by a signal}; output {logged (stdout and stderr together),
echoed to the terminal, stdout captured, over the capture limit, more
than a pipe holds}; what the child gets {stdin, working directory,
environment, open descriptors}; interruption {shtetl signalled while
the child runs, the child's own children, shtetl's handlers after};
a child that leaves a process holding its output open.

| cell | scenario | disposition |
|------|----------|-------------|
| R1 | a program named without a '/' is found on the fixed PATH; one with a '/' is run as given | covered: check_run.c |
| R2 | a program on no directory of the PATH is refused before anything is forked, naming it | covered: check_run.c |
| R3 | a file that cannot be executed is refused with exec's reason | covered: check_run.c |
| R4 | exit 0 is success; exit 3 is a refusal naming the program and the status | covered: check_run.c |
| R5 | a child killed by a signal is a refusal naming the signal | covered: check_run.c |
| R6 | logged: stdout and stderr both reach the log | covered: check_run.c |
| R7 | captured: stdout comes back byte for byte; stderr goes to the log | covered: check_run.c |
| R8 | captured output over its limit is refused | covered: check_run.c |
| R9 | 8 MiB of output passes through the pipe whole | covered: check_run.c |
| R10 | the child's stdin is /dev/null | covered: check_run.c |
| R11 | the working directory is the one asked for | covered: check_run.c |
| R12 | an environment given replaces shtetl's; none given, the child inherits it | covered: check_run.c |
| R13 | the child holds no descriptor of shtetl's beyond 0, 1 and 2 | covered: check_run.c |
| R14 | shtetl interrupted (SIGINT, SIGTERM) while a child runs: the child and its own children are stopped and reaped, and the run is refused as interrupted | covered: check_run.c |
| R15 | shtetl's signal handlers and mask are as they were after a run | covered: check_run.c |
| R16 | a child that exits leaving a process holding its output: the run returns when the child exits | covered: check_run.c |
| R17 | echo: the output also reaches standard error | covered: check_run.c |

# Family D: discovery and multi-rooting (M2; planning.md 4.1; src/core/spaces.c)

The roots of the four spaces are found by the user property
shtetl:space, set locally on each root. On the mac, a fake ZFS
(tests/zfake.c) stands in: an in-memory set of pools and filesystems
with ZFS's inheritance; on the box, the same cells against pools on
memory disks (tests/box).

Dimensions: what carries the property {nothing, a pool's root
dataset, a dataset deeper, a child that inherits it}; its value {each
space, something else}; how many roots {none, one, several in one
pool, several across pools} per space {lake and run take several,
config and build one}; roots nested in roots; lookup of a name under
a space's roots {no match, one, two}; the default root for something
new {one root, several with and without spaces.X.default, a default
that is no root of its space}; ZFS refusing the walk.

| cell | scenario | disposition |
|------|----------|-------------|
| D1 | nothing carries shtetl:space: every space has no root | covered: check_spaces.c |
| D2 | one root per space, a pool's root dataset among them | covered: check_spaces.c |
| D3 | an inherited value is no root: a root's children are not roots | covered: check_spaces.c |
| D4 | a value that names no space is refused, naming the dataset and the value | covered: check_spaces.c |
| D5 | two config roots, or two build roots, are refused, naming both | covered: check_spaces.c |
| D6 | several lake and run roots, in one pool and across pools, come back sorted by name | covered: check_spaces.c |
| D7 | a root inside another root is refused, naming both | covered: check_spaces.c |
| D8 | lookup of a name under a space's roots: one match gives its name; none says so | covered: check_spaces.c |
| D9 | lookup with the name under two roots is refused, naming both: one name, one place | covered: check_spaces.c |
| D10 | lookup takes whole names only: foundations/F is not foundations/F2 | covered: check_spaces.c |
| D11 | the default root: the only root; with several, spaces.X.default when it names one of them | covered: check_spaces.c |
| D12 | the default refused: no root; several and no default; a default that is no root of that space | covered: check_spaces.c |
| D13 | ZFS refusing the walk refuses the discovery, with ZFS's reason | covered: check_spaces.c |
| D14 | the configuration is read from the config root's mountpoint, where it is mounted | covered: check_spaces.c |
| D15 | the config root refused: none; no mountpoint (none, legacy); not mounted | covered: check_spaces.c |
| D16 | shtetl spaces: each space's roots, the default lake and run roots marked, config and build with where they are mounted, a space with none saying so | covered: check_spaces.c |
| D17 | a portable shtetl, with no -C, says it was built without ZFS | covered: run-cli.sh cl3-noroot, d17-spaces |

# Family B: bootstrap and the property table (M2; planning.md 4.2, 4.3; src/core/boot.c)

shtetl bootstrap names one or more datasets the user already has as
roots of the spaces, shows what it will set and create, and on
confirmation lays each root out. Planned and applied over the fake
ZFS on the mac; the same cells on memory-disk pools on the box.

Dimensions: what is named {nothing, each space, several lake and run
roots, config or build twice}; each named dataset {missing, already a
root of this space or another, inside or around another root, with
children not shtetl's, with a partial layout of shtetl's, config or
build without a mountpoint}; the settings {built-in, shtetl.conf's
properties.default, properties.<space>}; the pool {ashift as
recommended or not, autotrim off or on}; the apply {complete,
interrupted and run again}; a layout child already there {as shtetl
makes it, otherwise}; the run roots {the first, a later one};
the recipes clone {asked, not asked, the root not empty, failing};
confirmation {-y, a terminal's answer, no terminal}.

| cell | scenario | disposition |
|------|----------|-------------|
| B1 | nothing named, an unknown option, an option without its operand: a usage error | covered: run-cli.sh b1-none, b1-option, b1-operand, b1-recipes; b1-nozfs on the portable flavor |
| B2 | a named dataset that does not exist is refused: shtetl uses datasets the user has | covered: check_boot.c |
| B3 | a dataset that is already a root, of its space or another, is refused | covered: check_boot.c |
| B4 | config or build named twice, or named where one exists, is refused | covered: check_boot.c |
| B5 | a named root inside or around another root, existing or named alongside, is refused | covered: check_boot.c |
| B6 | a lake or run root with a child that is not shtetl's layout is refused, naming it | covered: check_boot.c |
| B7 | a config or build root with no mountpoint is refused | covered: check_boot.c |
| B8 | the table: each root's properties, default, shtetl.conf's, the value used and the value now, the changes marked; the pool's ashift reported, with a warning when not 12, and autotrim | covered: check_boot.c |
| B9 | a property's value: built-in, then properties.default, then properties.<space> | covered: check_boot.c |
| B10 | a lake root laid out: foundations, foundation-builds, containers, systems, archive and its two; the root's properties; shtetl:space last | covered: check_boot.c |
| B11 | the first run root laid out: foundations at /shtetl/foundations and never mounted, containers at /containers and mounted, systems | covered: check_boot.c |
| B12 | a later run root: its containers inherits /containers but does not mount (canmount=off) | covered: check_boot.c |
| B13 | the config root: with --recipes and an empty directory, a snapshot and then git clone; not empty, no clone; no --recipes, no clone; a failed clone leaves the root unmarked | covered: check_boot.c |
| B14 | an apply interrupted (a create fails): no root marked; the same command again finishes, taking the layout already made | covered: check_boot.c |
| B15 | a layout child already there but not as shtetl makes it is refused, naming the difference | covered: check_boot.c |
| B16 | without -y and no terminal: refused, nothing done; -y: done; a terminal answers y or yes, anything else is no | covered: check_boot.c (the decision, sh_boot_confirm); the command itself on the box, tests/box |
| B17 | the pool: autotrim set only when it differs; ashift never set | covered: check_boot.c |
| B18 | a pool imported with an altroot: the layout's mountpoints are read below it | covered: check_boot.c |

On the box, tests/box/run.sh runs D1, D2, D5, D6, D14, D16, B3, B6,
B8, B10 - B14 and B16 - B18 again, as commands against two pools on
memory disks, and logs each by its cell. All passed on KIDON
(15.1-RELEASE-p2, OpenZFS 2.4.2) at 1ef5e85, 2026-10-12: 53 checks,
tests/box/results/KIDON-20261012T111021Z.log. B13 ran with git; B8
with the ashift warning, the memory disks having another ashift than
12.
