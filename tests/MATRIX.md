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
| CK23 | src_branch: main, stable/N or releng/N.M; anything else refused: invalid branch | covered: check_schema.c |

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

# Family D: discovery (M2; planning.md 4.1; src/core/spaces.c)

The roots of the four spaces, one to a space, are found by the user
property shtetl:space, set locally on each root. On the mac, a fake ZFS
(tests/zfake.c) stands in: an in-memory set of pools and filesystems
with ZFS's inheritance; on the box, the same cells against pools on
memory disks (tests/box).

Dimensions: what carries the property {nothing, a pool's root
dataset, a dataset deeper, a child that inherits it}; its value {each
space, something else}; how many roots per space {none, one, two in
one pool, two across pools}; roots nested in roots; lookup of a name
under a space's root {there, not there, no root}; ZFS refusing the
walk.

| cell | scenario | disposition |
|------|----------|-------------|
| D1 | nothing carries shtetl:space: every space has no root | covered: check_spaces.c |
| D2 | one root per space, a pool's root dataset among them | covered: check_spaces.c |
| D3 | an inherited value is no root: a root's children are not roots | covered: check_spaces.c |
| D4 | a value that names no space is refused, naming the dataset and the value | covered: check_spaces.c |
| D5 | two roots of a space, any space, in one pool or across pools, are refused, naming both | covered: check_spaces.c |
| D7 | a root inside another root is refused, naming both | covered: check_spaces.c |
| D8 | lookup of a name under a space's root: there gives its name; not there, or no root, says so | covered: check_spaces.c |
| D10 | lookup takes whole names only: foundations/F is not foundations/F2 | covered: check_spaces.c |
| D13 | ZFS refusing the walk refuses the discovery, with ZFS's reason | covered: check_spaces.c |
| D14 | the configuration is read from the config root's mountpoint, where it is mounted | covered: check_spaces.c |
| D15 | the config root refused: none; no mountpoint (none, legacy); not mounted | covered: check_spaces.c |
| D16 | shtetl spaces: each space's root, config and build with where they are mounted, a space with none saying so | covered: check_spaces.c |
| D17 | a portable shtetl, with no -C, says it was built without ZFS | covered: run-cli.sh cl3-noroot, d17-spaces |

# Family B: bootstrap and the property table (M2; planning.md 4.2, 4.3; src/core/boot.c)

shtetl bootstrap names datasets the user already has as roots of the
spaces, one to a space, shows what it will set and create, and on
confirmation lays each root out. Planned and applied over the fake
ZFS on the mac; the same cells on memory-disk pools on the box.

Dimensions: what is named {nothing, each space, a space twice}; each
named dataset {missing, already a
root of this space or another, inside or around another root, with
children not shtetl's, with a partial layout of shtetl's, config or
build without a mountpoint}; the settings {built-in, shtetl.conf's
properties.default, properties.<space>}; the pool {ashift as
recommended or not, autotrim off or on}; the apply {complete,
interrupted and run again}; a layout child already there {as shtetl
makes it, otherwise}; the recipes clone {asked, not asked, the root not empty, failing};
confirmation {-y, a terminal's answer, no terminal}.

| cell | scenario | disposition |
|------|----------|-------------|
| B1 | nothing named, an unknown option, an option without its operand: a usage error | covered: run-cli.sh b1-none, b1-option, b1-operand, b1-recipes; b1-nozfs on the portable flavor |
| B2 | a named dataset that does not exist is refused: shtetl uses datasets the user has | covered: check_boot.c |
| B3 | a dataset that is already a root, of its space or another, is refused | covered: check_boot.c |
| B4 | a space named twice, or named where it has a root, is refused: one root to a space | covered: check_boot.c; run-cli.sh for the option given twice |
| B5 | a named root inside or around another root, existing or named alongside, is refused | covered: check_boot.c |
| B6 | a lake or run root with a child that is not shtetl's layout is refused, naming it | covered: check_boot.c |
| B7 | a config or build root with no mountpoint is refused | covered: check_boot.c |
| B8 | the table: each root's properties, default, shtetl.conf's, the value used and the value now, the changes marked; the pool's ashift reported, with a warning when not 12, and autotrim | covered: check_boot.c |
| B9 | a property's value: built-in, then properties.default, then properties.<space> | covered: check_boot.c |
| B10 | a lake root laid out: foundations, foundation-builds, containers, systems, archive and its two; the root's properties; shtetl:space last | covered: check_boot.c |
| B11 | the run root laid out: foundations at /shtetl/foundations and never mounted, containers at /containers and mounted, systems | covered: check_boot.c |
| B13 | the config root: with a URL and an empty directory, a snapshot and then git clone; not empty, no clone; no URL, no clone; a failed clone leaves the root unmarked. The command line's URL: --recipes's, or shtetl-recipes' (SH_RECIPES_URL) | covered: check_boot.c; box: run.sh with a repository of its own, build.sh with shtetl-recipes from GitHub |
| B14 | an apply interrupted (a create fails): no root marked; the same command again finishes, taking the layout already made | covered: check_boot.c |
| B15 | a layout child already there but not as shtetl makes it is refused, naming the difference | covered: check_boot.c |
| B16 | without -y and no terminal: refused, nothing done; -y: done; a terminal answers y or yes, anything else is no | covered: check_boot.c (the decision, sh_confirm); the command itself on the box, tests/box |
| B17 | the pool: autotrim set only when it differs; ashift never set | covered: check_boot.c |
| B18 | a pool imported with an altroot: the layout's mountpoints are read below it | covered: check_boot.c |

On the box, tests/box/run.sh runs D1, D2, D5, D14, D16, B3, B4, B6,
B8, B10, B11, B13, B14 and B16 - B18 again, as commands against two pools on
memory disks, and logs each by its cell. The script as it was passed
on KIDON (15.1-RELEASE-p2, OpenZFS 2.4.2) at 1ef5e85, 2026-10-12: 53
checks, tests/box/results/KIDON-20261012T111021Z.log; as it is, with
one root to a space, it has not run there yet. B13 ran with git; B8
with the ashift warning, the memory disks having another ashift than
12.

# Family V: a foundation version's name and its ver check (M3; planning.md 6.1, 7.1; src/core/version.c)

A version is named from the recipe's src_branch, the checkout's
REVISION (newvers.sh's, for main's major), the day of the build in
UTC and the checkout's commit; the recipe's ver is checked against the
branch before the checkout and against REVISION after it. All of it
portable, over values the build hands in.

Dimensions: the branch {main, stable/N, releng/N.M, malformed}; the
REVISION line {as newvers.sh -V prints it, anything else}; the day
{any, across midnight in a zone east or west of UTC}; the commit
{40 hex digits, shorter, not hex}; names F's snapshots have {none,
the same name, .2 already, the -pkg twin, another day's}; the ver
check {before the checkout: releng exact, stable any N.x, main
anything; after it: REVISION exact} against constraints that hold or
do not.

| cell | scenario | disposition |
|------|----------|-------------|
| V1 | branch spellings: main, stable/15, releng/15.1 read with their numbers; releng/15, stable/15.1, stable/, releng/a.b, Main, main/x refused | covered: check_version.c |
| V2 | the name for each kind of branch: 15.1-releng, 15-stable, 16-current with main's major from REVISION | covered: check_version.c |
| V3 | the day is the UTC day, whatever TZ says, either side of midnight | covered: check_version.c |
| V4 | the commit: its first 12 hex digits; one shorter than 12, or not hex, refused | covered: check_version.c |
| V5 | a second build the same day from the same commit takes .2, a third .3; the -pkg twin and another day's names do not count | covered: check_version.c |
| V6 | REVISION read from newvers.sh -V's one line, REVISION="16.0"; anything else refused | covered: check_version.c |
| V7 | before the checkout: releng/N.M must satisfy every ver, stable/N must leave some N.x every ver allows, main passes; the refusal names the constraint and its file | covered: check_version.c |
| V8 | after the checkout: REVISION must satisfy every ver, the refusal naming the constraint and its file | covered: check_version.c |

# Family Z: what the build asks of ZFS (M3; src/core/zfs.h; tests/zfake.c, src/fbsd/zfs.c)

The calls a build makes beyond M2's: snapshots of several filesystems
at once with properties, a dataset's snapshots in order, rollback,
destroy, inherit, unmount, and properties of snapshots. The build's
tests run over the fake, so the fake must answer as ZFS does; these
cells hold it to that. The libzfs side compiles with the rest
(tools/xcheck-freebsd.sh) and answers on the box.

Dimensions: the call; its target {a filesystem, a snapshot, missing,
malformed}; several names at once {all good, one taken, one missing,
one twice}; a snapshot's property {identity, its own user property,
its filesystem's, none}; rollback {to the newest, to an older one};
destroy {a snapshot, a leaf filesystem, one with children, one with
snapshots}; ZFS refusing.

| cell | scenario | disposition |
|------|----------|-------------|
| Z1 | snapshots of several filesystems at once share one createtxg, each with the properties given, and come back oldest first | covered: check_zfs.c |
| Z2 | all or none: a name taken, a filesystem missing, a name twice, a malformed name; nothing taken | covered: check_zfs.c |
| Z3 | a snapshot's type, name, createtxg and guid; its own user property, local; its filesystem's, inherited; none, "-" | covered: check_zfs.c |
| Z4 | a user property set on a snapshot after it is taken; a native one, or a missing snapshot, refused | covered: check_zfs.c |
| Z5 | a dataset's snapshots: its own only, not its children's, oldest first; FN's stop returned | covered: check_zfs.c |
| Z6 | rollback to the newest snapshot; to an older one, or of a missing filesystem, refused | covered: check_zfs.c |
| Z7 | destroy: a snapshot; a filesystem with no children and no snapshots; one with either refused, with both for its snapshots (ZFS's order); a missing filesystem or snapshot refused | covered: check_zfs.c |
| Z8 | inherit: the local value gone, the inherited one back | covered: check_zfs.c |
| Z9 | unmount after mount | covered: check_zfs.c |
| Z10 | each call refused when ZFS refuses, with ZFS's reason, in the same words over libzfs and the fake | covered: check_zfs.c; the words over libzfs, Z11 |
| Z11 | the same cells against libzfs on real pools, each refusal by its words | box: tests/box/zcheck.c, run by tests/box/run.sh; make check links it against the stub and runs its cells over the fake (tests/box/zcheck-fake.c). Box run 1: Z8's cell wrong (3b50a35), refusals in errno words; run 2 (c46e87c): 89 ok, 0 failed; run 3 (dc2be99, Z12 and Z13 too): 95 ok, 0 failed; M4's Z14 - Z19 with them, across the two pools (box_zcheck POOL/DATASET OTHER/DATASET); run 4 (f04c6e7, dc592a7): 111 ok, 3 failed, all of them Z16's newest-snapshot refusal, which ZFS's receive does not make (Z16); run 5 (18b78bf, 0904925): 111 ok, 3 failed, Z16's base that is no earlier, said as the newest refusal: the real send's checks now go in the fake's order; run 6 (4c30413, 00912eb): 114 ok, 0 failed; run 7 (8fa7022, 57e132d): 115 ok, 0 failed |
| Z12 | a mountpoint set or inherited moves mounts as zfs set does: the dataset and those taking their mountpoint from it unmounted, then mounted where it now says if they were mounted or canmount is on; canmount off, none or legacy, unmounted. A set mounts what was not | covered: check_zfs.c; box: zcheck.c, run 3 green (the first real build failed on it, F11) |
| Z13 | mount: mounted at its mountpoint already, nothing done; mounted elsewhere, no mountpoint, canmount off: refused | covered: check_zfs.c; box: zcheck.c, all but mounted elsewhere (deferred: only zfs set -u makes one, which the interface does not do) |
| Z14 | a clone (M4): of a snapshot, its properties set from its birth, its origin read back, a filesystem that is no clone's "-" (the box's build 9 met it: libzfs has no value to give, which zfs get shows as "-"); refused: no such snapshot, no parent, the name taken | covered: check_zfs.c; box: zcheck.c |
| Z15 | a whole send: a snapshot received as a new filesystem holding it, in another pool; nothing mounted; no property travels, the snapshot's user ones nor the filesystem's local ones (no -p); refused: the target there already | covered: check_zfs.c; box: zcheck.c, across two pools |
| Z16 | an incremental send: from an earlier snapshot of the same filesystem, onto the target at that snapshot's counterpart, the new one received; refused: the target missing, its newest snapshot not the counterpart. Over libzfs shtetl looks at the newest itself: the receive finds FROM's counterpart anywhere among the target's snapshots and refuses only a change since it (dmu_recv.c), so it took one onto a newer snapshot of no change (box run 4) | covered: check_zfs.c; box: zcheck.c |
| Z17 | a clone stream: from the snapshot the source is a clone of, received as a clone of the given origin in the target's pool, holding only the delta; refused: the origin missing | covered: check_zfs.c; box: zcheck.c |
| Z18 | a snapshot's own user properties walked, not what it inherits; FN's stop returned; set again on a received snapshot (6.2's copy) | covered: check_zfs.c; box: zcheck.c |
| Z19 | the refusals of Z14 - Z17 in the same words over libzfs and the fake | covered: check_zfs.c; box: zcheck.c |
| Z20 | a mounted filesystem not destroyed: refused for its mount, before its snapshots, as its mount holds it (dsl_destroy_head_check_impl's long holds); unmounted, destroyed. An undo that skips an unmount fails here over the fake, not first on the box | covered: check_zfs.c; box: zcheck.c |

# Family F: building a foundation (M3; planning.md 7.1; src/core/build.c)

shtetl build F: planned in full and checked, shown, confirmed, then
run. The plan's cells run over the fake ZFS and a configuration root in
the scratch directory; the run's over the same, with make, git, pkg,
chroot and mount played by scripts that log what they were asked.

Dimensions: the target {F, foundations/F, foundations/F.conf,
another kind's path, a bad name}; the roots {lake, build, config
there or not}; the recipe {accepted, refused}; src_branch against ver
{met, not}; the build root {free, a build under way, no mountpoint,
not mounted}; F {new, there already}; the recipe's datasets {none,
nested with the parent named, nested without, a parent named after
its child, with properties, more than fit}; the obj cache {there,
not}; F {new, there with a version (an update), there without one};
an update's datasets {as F's, one added, one gone}; the securelevel
{0, above}; installworld's backups {none, each kind, flagged
immutable}; an update's end {built, failed, paused and aborted}.

| cell | scenario | disposition |
|------|----------|-------------|
| F1 | the target: generic, foundations/generic and foundations/generic.conf alike; containers/x and a bad name refused | covered: check_build.c |
| F2 | no lake, build or config root: each refused, by name | covered: check_build.c |
| F3 | the recipe refused: its reasons, and nothing planned | covered: check_build.c |
| F4 | src_branch not meeting ver, refused naming the constraint and its file | covered: check_build.c |
| F5 | a build under way (shtetl:build set on the build root) refused, naming it | covered: check_build.c |
| F6 | the build root with no mountpoint, or not mounted, refused | covered: check_build.c |
| F7 | a foundation of that name already there: with a version, an update (F20); without one, refused | covered: check_build.c |
| F8 | F's datasets: F first, the recipe's in its order, a placeholder for each parent it does not name, a parent named after its child made real, properties carried; more than 32 refused | covered: check_build.c |
| F9 | the obj cache: made when missing, taken when there | covered: check_build.c |
| F10 | the plan shown: the branch and the version's pattern, each dataset with what it is set to, the obj cache, the checkout, the kernel and its lines, src.conf, make -j, the install, pkg, commands, the snapshot | covered: check_build.c |
| F11 | a whole run, in order: the build root marked, the config root snapshotted (shtetl:kind=config), F's datasets made with their properties, the checkout, newvers.sh asked, the configs written, the obj cache and F mounted below the run directory, make buildworld buildkernel with -j, SRCCONF, KERNCONFDIR, KERNCONF and MAKEOBJDIRPREFIX in its environment, installworld, installkernel and distribution with DESTDIR and install -C, devfs, pkg -c, each command piece, everything unmounted and inherited back, the snapshot of every dataset with the version's properties, the mark cleared | covered: check_build.c, over the fake's mounts as libzfs makes them (Z12); box: tests/box/build.sh, build 1 failed on a mount after a set (fixed with Z13); build 2 (dc2be99) green, box@15.1-releng-2026-10-13-e38a7085f3a8 |
| F12 | the version: REVISION's, the day of the run, the commit; REVISION not meeting ver after the checkout refused and undone | covered: check_build.c |
| F13 | a failure at each step that can fail (git, make at each target, pkg, a command, the snapshot) undone: F's datasets destroyed, every mount and mountpoint back, the mark cleared; the config snapshot and the obj cache kept; the log named | covered: check_build.c |
| F14 | the build root with changes of its own refused | covered: check_build.c |
| F15 | no packages: no resolv.conf copied; no packages and no commands: no devfs; a resolv.conf F has is left alone | covered: check_build.c |
| F16 | the kernel config: an include of a name in the checkout's conf directory for this machine, or of a path in the configuration root; the ident; the lines in order; src.conf, buildopts' pieces in order | covered: check_build.c |
| F17 | shtetl:commit: the build's own message, or the one given | covered: check_build.c |
| F18 | build on the command line: no foundation, two, an unknown option, -m with no message, usage errors; the portable shtetl refuses for want of ZFS; -C not the config root's mountpoint refused | covered: run-cli.sh f18-none, f18-two, f18-option, f18-msg, f18-nozfs; the -C refusal and the run itself on the box |
| F19 | on a pool imported with an altroot, make and the install are given where the obj cache and F are mounted, below the altroot, not the mountpoint set | box: tests/box/build.sh, its pool imported with one; the fake reports a mountpoint as set |
| F20 | an update (planning.md 7.2): F there with a version, the build is its next, into the same datasets; the plan says so, each dataset kept, the last version named | covered: check_build.c |
| F21 | an update's datasets are F's: one the recipe adds, or one F has that the recipe no longer names, refused, each named; F there with no version refused | covered: check_build.c |
| F22 | an update at a securelevel above 0, where schg cannot be cleared, refused | covered: check_build.c |
| F23 | the update's run: no dataset made; make delete-old delete-old-libs with DESTDIR and BATCH_DELETE_OLD_FILES=yes before installworld; the installs with install -C; then installworld's backups removed, flags cleared first: boot/kernel.old, boot/*.old, libexec/ld-elf*.so.1.old, sbin/init.bak, and nothing else; pkg and the commands run again, as the installs undo what they changed (ruled 2026-10-13) | covered: check_build.c (the user's immutable flag; schg needs root: the box) |
| F24 | an update the same day as the last version: the name with .2 | covered: check_build.c |
| F25 | an update that fails: F's datasets rolled back to the last version, not destroyed; mounts, mountpoints and the mark undone as for a new build | covered: check_build.c |
| F26 | an update paused at a patch: --abort rolls F back to its last version; --continue goes on to the update | covered: check_build.c |
| F27 | for real: a second build of box on the box, an update over the first, on the kept obj cache: the version .2, delete-old run, no backup left, the command's /etc/motd.template written again over distribution's; init and rtld changed by a patch each, so installworld makes backups to remove | box: tests/box/build.sh. Build 4 (63df125, at 97d62c7) green, the update 1 minute on the kept obj cache; but the same commit made no backup, so their removal went unseen: F27 now changes init and rtld. Build 5 (e820db9, at 993bd7a) green: ld-elf.so.1.old, ld-elf32.so.1.old and init.bak made and removed, schg cleared as root; the command run again |
| F28 | pkg-upgrade planned (planning.md 7.2, 6.1): from F's last snapshot, a version or a -pkg of one; the snapshot to take its version's -pkg, then -pkg.2 and on; shown | covered: check_build.c |
| F29 | pkg-upgrade refused: no lake or build root, no such foundation, F with no version, a build under way or paused | covered: check_build.c |
| F30 | pkg-upgrade's run: the build root's mark; F and its datasets mounted below the run directory, placeholders not; devfs and the host's resolv.conf while pkg runs; pkg -c <root> upgrade -y; all of it undone; snapshot -r with shtetl:kind=pkg, shtetl:commit (-m, or its own), and the version's shtetl:config, shtetl:config-guid and shtetl:patches carried over | covered: check_build.c |
| F31 | pkg-upgrade that fails: F back to its last snapshot, the mark cleared, the log kept | covered: check_build.c |
| F32 | pkg-upgrade on the command line: no foundation, two, an unknown option, -m with no message; the portable shtetl refuses for want of ZFS | covered: run-cli.sh |
| F33 | for real: pkg-upgrade of box after F27, its -pkg snapshot on the box | box: tests/box/build.sh; build 4 (63df125) green |
| F34 | the kernel (planning.md 7.2 as built): installkernel into the obj cache's kernel-install, then synced into F: a file alike kept, its object too; a changed or new one copied in; boot/kernel and usr/lib/debug/boot/kernel mirrored, a module gone and a debug directory the new kernel has not removed; the counts said; installkernel leaving no boot/kernel fails the build; kernel-install removed after | covered: check_build.c; box: tests/box/build.sh, build 6 (70962a2, at 8d18312): 1715 kernel files copied into the new F, all 1715 kept by the update, a module's object the same |

On the box, tests/box/build.sh builds a foundation for real (sudo sh
tests/box/build.sh; hours): one pool on a sparse file imported with
an altroot, bootstrapped by shtetl, the build root a shared clone of
the box's /usr/src fetching from git.freebsd.org. It runs F13 (a
fetch that fails, undone on real ZFS), then F11, F12, F16, F17 and
F19 on a real build of releng/15.1, and logs each by its cell to
tests/box/results/<host>-build-<time>.log. Build 1 failed on a mount
after a set (Z12); build 2 green (b12f0c2). Since, the build is P17's,
paused at a patch and carried on. The pool's file stays between
runs, exported, keeping the build root and box's obj cache for the
branch, so a later build is incremental; --clean drops them. Build 4
was its first: the first build whole in 30 minutes, the update over
its obj cache in 1. Build 5 started from the store: the first build 1
minute too.


# Family P: patches and the pause (M3; planning.md 7.1.1; src/core/patch.c, src/core/build.c)

A foundation recipe's patches, applied to the src tree after the
checkout and before the build, and the pause a patch that does not
apply leaves for a person. The pieces (order, locations, the files a
patch touches, what a fix leaves) are checked alone in check_patch.c;
the run in check_build.c, with real git for everything git does to a
patch and to the tree (skipped, and said so, where there is none),
patch sources fetched over file:// URLs, and the fake ZFS's snapshots
and rollbacks of the build root carried out on the scratch tree by a
hook, so a rollback really puts the files back.

Dimensions: the source {a URL with a scheme, a host and path (https),
a directory in the configuration root}; its options {ref none, a
branch, a tag, a commit; dir none, a subdirectory}; the sources {none,
one, several; an array or an object; from two layers}; a source's
files {none, several in natural order, not .patch, in a subdirectory};
the fetch {done, failing}; each patch {applies, already in, conflicts,
merges without conflict, no base to merge against}; the verb {build,
--continue, --skip, --abort}; at --continue {fixed, a marker left, a
.rej left}; the build after the patches {succeeds, fails}.

| cell | scenario | disposition |
|------|----------|-------------|
| P1 | natural order: runs of digits compare as numbers (2 before 10, a2b before a10b), leading zeros, ties byte by byte; only the files ending .patch at the top of the directory, others and subdirectories left | covered: check_patch.c |
| P2 | a location: a URL as written, a host and path as https, a directory in the configuration root | covered: check_patch.c |
| P3 | the fetch: a git source at its ref (none: the remote's HEAD; a branch; a tag; a commit) into the run directory, its commit noted; a directory's patches copied; dir taking a subdirectory; a repository, ref or directory missing refused and the run undone | covered: check_build.c |
| P4 | the list: sources in the recipe's order, each whole, its patches in natural order; the lower layer's sources first; the array and the object alike | covered: check_build.c |
| P5 | the snapshots: @shtetl-patch-0 after the checkout, @shtetl-patch-N before patch N | covered: check_build.c |
| P6 | a patch that applies: applied, said, on to the next | covered: check_build.c |
| P7 | a patch already in, its reverse applying: skipped, said | covered: check_build.c |
| P8 | a patch that does not apply: merged three ways, conflict markers left; merged without conflict, paused all the same for a person to look; no base to merge against, applied with --reject, .rej files left. The pause: the patch, the files to fix and the three commands said; exit 3; the mark and F's datasets kept, nothing mounted, the record written in the run directory | covered: check_build.c |
| P9 | --continue: refused while a file the patch touches holds a conflict marker or has a .rej, naming it; otherwise the fix written out (the tree's difference from the patch's snapshot) as fixed/<source>/<name>, the next patch, the build to its end | covered: check_build.c |
| P10 | --skip: the tree back to the patch's snapshot, the patch left out, the build on | covered: check_build.c |
| P11 | --abort: the build root back to @shtetl-patch-0, the patch snapshots destroyed, F's datasets destroyed, the mark cleared, the record removed; the log kept | covered: check_build.c |
| P12 | refused: --continue, --skip or --abort with no build paused, or with one of another foundation paused; a build while one is paused (F5); two verbs at once | covered: check_build.c; run-cli.sh for the usage |
| P13 | a resumed build reads the recipe in the config snapshot the build began with, not the configuration root as it is now | covered: check_build.c |
| P14 | the record: shtetl:patches on every dataset of the version, each source with the commit it was fetched at, each patch in already, skipped or fixed by hand; none when there are no patches; a fix kept in the obj cache under patches/<version>/ | covered: check_build.c |
| P15 | the end: the build root rolled back to @shtetl-patch-0 and every patch snapshot destroyed, after a build that succeeds and after one that fails after the patches; then every file the patches, and a fix by hand, touched given the time of now, so that the next build does not take objects of a dropped patch for up to date | covered: check_build.c; box: build 6 found it (F27); build 7 (63f6a8f, from --clean) green, the time set on 1 file and then 3; build 8 (b6baf3a), from the kept store where build 6 failed, green: the first build rebuilt init without the patch, the update made its backups |
| P16 | the plan shows each source, with its ref and dir | covered: check_build.c |
| P17 | for real: git apply on a FreeBSD tree, ZFS snapshots and rollbacks of the build root; a patch that applies, one already in, one that conflicts, paused, fixed by the script, --continue, built | box: tests/box/build.sh, the patches made on the box from the branch's own share/misc/bsd-family-tree; rehearsed on the mac against the file; box build 3 (461df60, at 5802782) green: paused, refused, fixed, built in 30 minutes, both lines installed |
| P18 | the command line: build F --continue, --skip, --abort; usage errors | covered: run-cli.sh |

# Family M: a foundation's version brought to zrun (M4; planning.md 7.3; src/core/beam.c)

shtetl beam F: a version of F, in zlake, sent to the run root's
foundations/F, whole the first time and incremental after, the
version's shtetl:* copied onto what was received. Over the fake ZFS,
with a lake root and a run root in two pools.

Dimensions: the roots {lake, run, there or not}; the target {F,
foundations/F, a bad name}; F {there with versions, there without,
not there}; the version {the newest by default, a -pkg the newest,
given and there, given and not one of F's}; zrun {without F, with an
older version, with V already, with a newer one}; F's datasets {as
zrun's, differing}; a placeholder {there}; zrun's snapshots {all
with shtetl:kind, one without, an interrupted copy}; the sends {all
received, one failing}.

| cell | scenario | disposition |
|------|----------|-------------|
| M1 | the plan: F's newest version or -pkg by default, the one given otherwise; a first beam whole, a later one incremental from zrun's newest; shown | covered: check_beam.c |
| M2 | refused: no lake or run root; a bad name; no such foundation; F with no version; the version given not one of F's; it on zrun already; zrun's newest newer than it; F's datasets not zrun's | covered: check_beam.c |
| M3 | a first beam: each dataset of F whole, parents first, into the run root's foundations/F; readonly=on on F, its datasets inheriting it; canmount=noauto on each, off on a placeholder; the mountpoint inherited; nothing mounted | covered: check_beam.c |
| M4 | a later beam: each dataset incremental from zrun's newest, the version received on each; zrun's earlier versions kept | covered: check_beam.c |
| M5 | 6.2: the version's shtetl:* copied onto each received snapshot, nothing else of it; a zrun snapshot without shtetl:kind, an interrupted copy, copied again from zlake first | covered: check_beam.c |
| M6 | a send failing part way: what this beam received destroyed, the first beam's whole tree, a later one's new snapshots; zrun as it was | covered: check_beam.c |
| M7 | the command line: beam with no foundation, two, an unknown option, --version with none; the portable shtetl refuses for want of ZFS | covered: run-cli.sh |
| M8 | for real: box beamed after its build, then again after its update, the run root in the build pool | box: tests/box/build.sh, build 10 (f271e2d, at 8fa7022) green |

# Family S: a container's save, and its zlake mirror (M4; planning.md 7.5, 6.2, 7.10; src/core/save.c, src/core/edit.c)

shtetl save X: every dataset of the container snapshotted at once,
with its message, then mirrored into the lake root's containers/X as
deltas. Over the fake ZFS: a lake root and a run root in two pools, a
foundation version on both, a container cloned from it by hand as
create will clone one.

Dimensions: the roots {lake, run}; X {there, not, a bad name}; the
message {given, from an editor, empty, the editor failing}; X's
datasets {a clone of the foundation's, a placeholder, X's own}; the
save {the first, a later one, two in a second}; the mirror {there,
partly there after a failure, failing}; shtetl:config {from X's
newest save, given by create, neither}.

| cell | scenario | disposition |
|------|----------|-------------|
| S1 | refused: no lake or run root; a bad name; no such container; an empty message; nothing to take shtetl:config from (no save, none given) | covered: check_save.c |
| S2 | the snapshot: every dataset of X at once, save-<date>-<time> UTC (-2 for a second in the same second), shtetl:kind=save, shtetl:commit the message, shtetl:config and its guid from X's newest save, or given | covered: check_save.c |
| S3 | the first mirror: a dataset cloned from the foundation received as a clone of zlake's counterpart of its origin, holding the delta; a placeholder likewise; X's own whole; parents first | covered: check_save.c |
| S4 | a later mirror: each dataset from its own newest mirrored save | covered: check_save.c |
| S5 | 6.2: the save's shtetl:* copied onto each mirrored snapshot, nothing of -p | covered: check_save.c |
| S6 | a mirror failing part way: the save stays on zrun, said; the next save brings every dataset's mirror up to it, each from its own newest | covered: check_save.c |
| S7 | the message from an editor, as git commit takes one: $VISUAL, $EDITOR or vi, on a file with a comment saying what it is for; lines beginning # left out, trailing blank lines too; empty refused; the editor failing refused | covered: check_save.c |
| S8 | the command line: save with no container, two, an unknown option, -m with no message; the portable shtetl refuses for want of ZFS | covered: run-cli.sh |
| S9 | for real: a container cloned from box on zrun saved twice, the mirror a clone of zlake's box and then incremental | box: tests/box/build.sh, build 10 (f271e2d, at 8fa7022) green |

# Family X: a new container (M4; planning.md 7.4; src/core/create.c)

shtetl create X --on F: a container cloned from a version of F on
zrun, its own datasets made, its jail written and started, its
packages and commands run, and its first save. Over the fake ZFS, a
lake, a run and a config root, with jail, jexec and pkg played by
scripts that log what they were asked.

Dimensions: the target {X, containers/X, a bad name, digits only};
X {new, there already}; the recipe {accepted, refused, ver met by
the version or not, requires there or not}; F {there with versions,
not}; the version {the newest, a -pkg, given, not on zrun}; the
datasets {F's, a placeholder, F's with the recipe's properties, X's
own, X's own under a parent nobody names}; the jail {its block,
jailconf, started}; the commands {X's own pieces, the foundation's
own run already}; resolv.conf {there, not}; a failure {at each step}.

| cell | scenario | disposition |
|------|----------|-------------|
| X1 | refused: no lake, run or config root; a bad name, digits only; X there already; X's recipe refused; no such foundation; F with no version; the version given not F's; X's ver not met by the version's shtetl:revision; a version without one; a requires not a container on zrun | covered: check_create.c |
| X2 | the plan: the version, F's newest version or -pkg by default; beamed first when zrun lacks it; F's datasets cloned, X's own made, a placeholder for a parent nobody names; the recipe's properties on F's datasets set on the clones; shown | covered: check_create.c |
| X3 | the run's ZFS: the config root snapshotted; the version beamed if asked; each of F's datasets cloned parents first, a placeholder canmount=off, the recipe's properties local on the clone; X's own made; X mounted, placeholders not | covered: check_create.c |
| X4 | the jail: its block written whole to the jail directory (PROPOSAL /etc/jail.conf.d/X.conf), path the container's real mount, host.hostname X, rc's start and stop, exec.clean, mount.devfs, devfs_ruleset 4, ip4 and ip6 inherited, the recipe's jailconf after; started with jail -f ... -c X | covered: check_create.c |
| X5 | inside: the host's resolv.conf copied in where X has none, kept; pkg -j X install -y; each command piece by jexec X /bin/sh -e -c, a piece the foundation ran as of its version's shtetl:config (same label, file and text) left out | covered: check_create.c |
| X6 | the first save: shtetl:config the create's config snapshot, the message -m's or "create X on F@v", mirrored into zlake | covered: check_create.c |
| X7 | a failure at each step undone: the jail stopped, its block removed, X's datasets destroyed, the config snapshot kept; a failure of the save's mirror after the save leaves X created, said | covered: check_create.c |
| X8 | the command line: create with no container, two, no --on, an unknown option; the portable shtetl refuses for want of ZFS | covered: run-cli.sh |
| X9 | for real: a container on box created on the box, its jail started and stopped, saved again (family S's S9 with it) | box: tests/box/build.sh, build 9 (a460f91) failed on a non-clone's origin (Z14), build 10 (f271e2d, at 8fa7022) green |
