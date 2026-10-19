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
| M7 | sh_memstream | gives | a stream into the buffer | tests/check_daemon.c (every reply) |
| M8 | sh_memstream | refuses | as M2 | deferred as M6: the path is M2's |

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
| Z2 | user | a native property | 1: none of the user properties |
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

## Family S: the spaces and their skeleton (m1-spaces)

Dimensions: how many filesystems set shtetl:space locally to each
value (none, one, two), the values (a space's, another), where the
roots sit (apart, one inside another), whether the walk is refused;
then for the skeleton, each row's space found or not, its dataset
there or not, its making refused or not.

| cell | what is there | expected | disposition |
|------|---------------|----------|-------------|
| S1 | nothing marked | every space absent, nothing said | tests/check_spaces.c |
| S2 | one root of each, in two pools | each found | tests/check_spaces.c |
| S3 | a root with a filesystem below it, inheriting the value | the root only | tests/check_spaces.c |
| S4 | two roots of one space | "two lake roots: A and B", sorted; none taken | tests/check_spaces.c |
| S5 | a value no space has | "DS: invalid shtetl:space \"V\""; none taken | tests/check_spaces.c |
| S6 | a root inside another's | "DS: run root inside lake root R" | tests/check_spaces.c |
| S7 | the walk refused | its words; none taken | tests/check_spaces.c |
| S8 | a row's dataset missing | made, its property from birth | tests/check_spaces.c |
| S9 | there already | left, nothing said | tests/check_spaces.c |
| S10 | the row's space absent | nothing made | tests/check_spaces.c |
| S11 | making it refused | its words | tests/check_spaces.c |

On the box, discovery and the skeleton run inside the daemon's start,
which m1-foundation-start's box cells exercise; the ZFS calls under
them are family Z's.

## Family D: the daemon and the command (m1-daemon)

Dimensions: the daemon's start (the lock free or held, the socket's
path empty or holding something, the spaces found or refused); a
request (a verb, none, an unknown one, too many words, whole or in
pieces, larger than a socket takes, something that is no request);
the client (reads its reply, leaves before it, says nothing, reads
slowly); the end (SIGTERM, no daemon at all). A daemon is forked
over the fake on a socket listened on before the fork.

| cell | what happens | expected | disposition |
|------|--------------|----------|-------------|
| D1 | status | the spaces on standard output, nothing on standard error, 0 | tests/check_daemon.c |
| D2 | no verb | the usage on standard error, 2 | tests/check_daemon.c |
| D3 | an unknown verb | "shtetl: V: unknown command" and the usage, 2 | tests/check_daemon.c |
| D4 | status with a word more | the usage, 2 | tests/check_daemon.c |
| D5 | a second daemon while one runs | "already running" | tests/check_daemon.c |
| D6 | a request in two writes, the header cut | answered whole: out, err, exit, nothing after | tests/check_daemon.c |
| D7 | a client that asks and leaves | the next answered | tests/check_daemon.c |
| D8 | a client that says nothing and leaves | the next answered | tests/check_daemon.c |
| D9 | a record that is no request | hung up on, nothing sent; the next answered | tests/check_daemon.c |
| D10 | SIGTERM | exit 0, the socket removed | tests/check_daemon.c |
| D11 | no daemon | "shtetl: SOCK: " and connect's words, 1, nothing on standard output | tests/check_daemon.c |
| D12 | the socket's path holding a file a dead daemon left | replaced, answered | tests/check_daemon.c |
| D13 | the spaces refused | -1 before the loop, their words, the socket removed | tests/check_daemon.c |
| D14 | the pidfile | the daemon's pid in it | tests/check_daemon.c |
| D15 | the socket | mode 0600 | tests/check_daemon.c |
| D16 | a request and a reply of 256 KiB | both whole, through partial writes | tests/check_daemon.c |
| D17 | a client that does not read a large reply | another answered meanwhile; the reply whole when read | tests/check_daemon.c |
| D18 | the rc script | started under daemon(8), stopped by its pidfile | deferred to the box by hand: tests/box/README.md |

Also run by hand on the mac (not in make check, since leaks(1) hangs
on a forked child that exits): a live daemon over the fake after 600
commands, no leaks.

## Family A: an actor's start (m1-foundation-start)

Dimensions, for a foundation as a crash left it: its head's
versions (some, none), what was written since the newest (nothing,
something), its children (none, one with versions, one with none),
what is mounted (nothing, the head, the head and a child inside it),
shtetl:stall (unset, set), a step refused or not; and across
foundations, how many and in what order ZFS walks them, and the walk
of <lake>/foundations refused. Expected throughout: every dataset
rolled back to its newest snapshot or destroyed where it has none,
children first, unmounted; no stall but shtetl:stall's or a refused
step's.

| cell | what the crash left | expected | disposition |
|------|---------------------|----------|-------------|
| A1 | versions, nothing written | idle at the newest; "foundation F idle V"; nothing said | tests/check_foundation.c; box: tests/box/run.sh |
| A2 | written since the newest, mounted | the writes undone, unmounted | tests/check_foundation.c; box: tests/box/run.sh |
| A3 | no version at all, mounted, a child | destroyed, children first; no actor; "foundation F: a first build undone" said | tests/check_foundation.c; box: tests/box/run.sh |
| A4 | a version on the head, a child with none | the child destroyed, the head idle | tests/check_foundation.c |
| A5 | the head mounted, a child mounted inside it, written | the child unmounted first (busy otherwise), the writes undone | tests/check_foundation.c |
| A6 | shtetl:stall set | stalled with its text, its newest kept; "foundation F stalled TEXT" | tests/check_foundation.c; box: tests/box/run.sh |
| A7 | a step refused | that one stalled with the refusal, said; the others started | tests/check_foundation.c |
| A8 | several | by name in status, whatever the walk's order | tests/check_foundation.c |
| A9 | the walk of <lake>/foundations refused | no start: the daemon does not start | tests/check_foundation.c |
| A10 | no foundations | none | tests/check_foundation.c |

Mutations of src/core/foundation.c, each caught: the parent unmounted
before its children (A3), nothing unmounted (A2), the stall never read
(A1), a head with no version kept (A3). On the box the daemon also
gives D10 (SIGTERM, its socket removed) on real pools.

## Family C: the configuration language (m2-language)

Carried from the first history with the language, every cell kept
but those the new model retires (marked so, with the reason). A
cell is closed by a return value, the tree a function built, or the
text an error sink caught; all of it runs on the mac in make check,
and its parse cells hold for whichever libucl is linked (the
contrib/libucl the portable build compiles, the textproc/libucl port
make check-freebsd links).

### CP: one file parsed (docs/configuration.md 2; src/core/parse.c)

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

### CK: a recipe layer against its kind (docs/configuration.md 4; src/core/schema.c)

Dimensions: key {each key of docs/configuration.md 4}; kind
{foundation, container, system}; value {the right type, each wrong
type}; collection form {object, array shorthand, entry true, false,
object, string}; entry key syntax per collection; error reporting
{one, several, the path's spelling}.

| cell | scenario | disposition |
|------|----------|-------------|
| CK1 | every key of docs/configuration.md 4 is accepted in each kind that has it | covered: check_schema.c |
| CK2 | an unknown key is refused with the file and its key path | covered: check_schema.c |
| CK3 | a foundation-only key (src_branch, kernconf, buildopts, patches) in a container and in a system is refused: valid only in foundation recipe | covered: check_schema.c |
| CK4 | a container-only key (requires, jailconf) in a foundation and in a system is refused | covered: check_schema.c |
| CK5 | a string key given a number or a bool is refused: must be a quoted string | covered: check_schema.c |
| CK6 | ver, one constraint or a list of them: `>=15`, `=15.1`, `<16`, `<=15.1`, `>14` accepted; ``, `15.1`, `>=`, `=>15`, `>=15.x`, `>=15.` refused | covered: check_schema.c, check_cascade.c test_ver |
| CK7 | kernconf: a string is the config (a name, or a path in zconfig), a heredoc lines, even one line; a quoted string with a newline is a path and refused | covered: check_schema.c |
| CK8 | a path (kernconf's config): relative paths accepted; absolute, `.` or `..`, empty components, a trailing `/`, a control character refused | covered: check_schema.c |
| CK9 | import: an array of strings, or one string; a non-string element or a bad path refused | covered: check_schema.c |
| CK11 | pkg: array shorthand; entries true; false refused (dropping values is illegal); an entry with fields refused; bad names (empty, leading `-`, a space) refused | covered: check_schema.c |
| CK12 | datasets: paths by ZFS's characters; leading or trailing `/`, `//`, `.`, `..`, `@` refused | covered: check_schema.c |
| CK13 | dataset entries: true, or properties with string or bool values; false refused; a number refused (must be a quoted string); mountpoint, canmount and shtetl:* refused; a bad property name refused | covered: check_schema.c |
| CK14 | commands and commands-external: a string; true, false, an object or an array of names refused; an empty string refused; label syntax | covered: check_schema.c |
| CK15 | derivations: {source, command}, both, in one file; false, an unknown field, a non-string field, half an entry refused; key path syntax | covered: check_schema.c |
| CK16 | requires: container names, array shorthand; false and a bad name refused | covered: check_schema.c |
| CK17 | patches: array shorthand; {ref, dir}; the three location forms (a URL, host/path, a zconfig directory); false, an absolute path, `..`, an scp-style address and an unknown field refused | covered: check_schema.c |
| CK18 | a collection given as a scalar is refused | covered: check_schema.c |
| CK19 | every error in a layer is reported, not only the first | covered: check_schema.c |
| CK20 | the key path quotes a component that is not a bare word (`datasets."usr/local".readonly`) | covered: check_schema.c |
| CK22 | a file of the default layer takes every kind's keys; shtetl.conf's sections of settings are no recipe keys, and a file it imports may have none; an unknown key is refused | covered: check_schema.c test_default |
| CK23 | src_branch: main, stable/N or releng/N.M; anything else refused: invalid branch | covered: check_schema.c |
| CK24 | a block of one kind's keys (foundation, container, system) in a file of the default: its keys checked as that kind's, another kind's refused; no import in it; not an object refused; a block in a block, or in a recipe's own file, an invalid key | covered: check_schema.c test_blocks |

### CS: shtetl.conf (docs/configuration.md 3; src/core/site.c)

Dimensions: section {properties, build, roll, run, lake, a removed
one, an unknown one}; value {the right type, each wrong one};
roll.depth's forms {0, a count, an interval, each malformed one};
a keep {1 or more, 0, a string}; layers {built-in only, shtetl.conf
over it}.

| cell | scenario | disposition |
|------|----------|-------------|
| CS1 | every section, as docs/configuration.md 3 shows them, is accepted | covered: check_site.c |
| CS2 | an unknown section, or an unknown key in a section, is refused; a key at the top that is neither a section nor a recipe's key is refused | covered: check_site.c |
| CS3 | retired: the spaces section went with one root to a space, on the first history | - |
| CS4 | properties: sections default, lake, run, config, build; string or bool values; a number refused; mountpoint, canmount, shtetl:* refused | covered: check_site.c |
| CS5 | build.make_jobs: 0 or more; negative, a string, a float refused | covered: check_site.c |
| CS6 | retired: update.suffix, with the staging tree it named (plan 11) | - |
| CS7 | roll.depth: 0, 10, "7d", "6m", "1y", "12H" accepted; -1, "7x", "d7", "", "0d", 1.5 and an unquoted 30d refused | covered: check_site.c |
| CS8 | with no shtetl.conf the settings are the built-in ones (the properties, make_jobs 0, roll.depth 0, run.keep 2, lake.keep 5) | covered: check_site.c |
| CS9 | shtetl.conf sets one property; the rest stay built-in, each with its layer | covered: check_site.c |
| CS10 | a section the new model removed, update or retention, is an invalid key | covered: check_site.c |
| CS11 | the default recipe's keys beside the settings: checked by the recipe schema, and out of the settings' view | covered: check_site.c |
| CS12 | run.keep and lake.keep: 1 or more (the active version counts); 0, negative, a string refused | covered: check_site.c |

### CC: the cascade (docs/configuration.md 5, 6; src/core/cascade.c)

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
| CC17 | the first history's worked example, its default in shtetl.conf: pkg sudo, tmux, apache24, py311-certbot, nginx; datasets tmp, var, usr/local, www; commands certbot, nginx | covered: check_cascade.c |
| CC18 | a foundation with no src_branch after the merge is refused | covered: check_cascade.c |
| CC19 | a derivation given in a second file, alike or not, is refused, naming the first | covered: check_cascade.c |
| CC20 | a kernconf config path or a patches directory that does not exist in the config root is refused, naming the key | covered: check_cascade.c |
| CC21 | requires naming a container with no recipe, or the container itself, is refused | covered: check_cascade.c |
| CC22 | targets: `containers/nginx` and `containers/nginx.conf` alike; an unknown kind directory, `..`, a bad name, a nested path, a missing recipe refused | covered: check_cascade.c |
| CC23 | errors in several layers are all reported, and nothing is merged | covered: check_cascade.c |
| CC24 | each merged entry names the layer that added it and the one that last set it | covered: check_cascade.c |
| CC25 | patches: a lower layer's sources come first; a source a nearer layer changes keeps its place | covered: check_cascade.c |
| CC26 | every layer is checked against the target's kind: kernconf in a fragment a container imports is refused, naming the fragment | covered: check_cascade.c |
| CC27 | the ver constraints of all the files must leave a version: >=16 with <15, =15.1 with >15.1, refused naming the two | covered: check_cascade.c test_ver |
| CC28 | a dataset property a nearer file gives again, differently, overrides, with a warning naming both values and the first file | covered: check_cascade.c |
| CC29 | a commands block given in several files stacks: a piece from each, keyed by its file, lower first | covered: check_cascade.c |
| CC30 | an entry's field a second file gives again, alike or not, is refused (a patch source's ref); a field the first did not give composes (dir) | covered: check_cascade.c |
| CC31 | a recipe takes from the default only the keys of its kind (a container no kernconf, a foundation no jailconf), and none of shtetl.conf's settings | covered: check_cascade.c test_default |
| CC32 | what a recipe gives again of the default's one-value keys, derivations, entries' fields and dataset properties overrides it, each with a warning naming the value replaced and its file; the default's value given again alike is no override and warns nothing | covered: check_cascade.c test_default; configuration.md, example default |
| CC33 | what stacks, stacks with the default: pkg, ver, text, commands | covered: check_cascade.c test_default |
| CC34 | the files shtetl.conf imports are of the default too | covered: check_cascade.c test_default |
| CC35 | shtetl.conf is checked whole wherever it is read: a refused setting refuses a recipe's resolve, naming the setting; a file shtetl.conf imports holds no settings | covered: check_cascade.c test_site_checked; configuration.md, example settings-refused |
| CC36 | in ver a bare major is every minor of it: =15 any 15.x, <=15 takes 15.x, >15 begins at 16.0; N.N is that version; each form's range, and the meets they make (=15 with >=15.3; =15 with <15 and >15 with <16 refused) | covered: check_cascade.c test_ver; configuration.md, example ver |
| CC37 | kernconf: the config from one file of the stack, a second file giving it refused, alike or not; the default's overridden with a warning; lines stack with the default's and the imports'; merged as { file; lines } | covered: check_cascade.c test_stacked, test_default |
| CC38 | a kind's block of a default file: a recipe of that kind takes it as a layer of its own after the file's ("container in shtetl.conf"), its values saying so; what it gives stacks with the file's, a commands label a piece of its own; a one-value key given again overrides the file's, with a warning; another kind's block not taken; a file shtetl.conf imports may hold blocks | covered: check_cascade.c test_blocks; running a block's pieces is m5-create's |

### CW: what show prints (src/core/show.c)

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
| CW9 | kernconf prints as { file; lines { pieces } }; only a collection's entries carry a layer comment, so lines prints as text does | covered: configuration.md, examples keys and text |

### CL: retired

main's command line (`shtetl -C DIR config show`, tests/run-cli.sh)
is gone: show is the daemon's verb, its cells D19 - D24 of family D,
and the doc examples run the daemon's show itself (tests/docshow.c).

