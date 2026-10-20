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
| Z10 | set | a user property on a filesystem | its value; below it inherited (m3-roll) |
| Z10 | set | set there already | replaced |
| Z10 | inherit | set there, set above | the ancestor's value |
| Z10 | inherit | not set there | nothing done |
| Z10 | inherit | set there, nothing above | unset: 1 |
| Z10 | set, inherit | no such filesystem | "no such filesystem" |
| Z11 | snapinfo | two snapshots | guids apart, createtxg in order (m4-zfs) |
| Z11 | snapinfo | no such snapshot | "no such snapshot" |
| Z12 | clone | a snapshot, a property | a clone, the property from its birth, its origin read back |
| Z12 | origin | no clone | 0 |
| Z12 | clone | no such snapshot; the name taken; no parent | "no such snapshot"; "exists"; "no parent" |
| Z9 | destroy | a snapshot with a clone | "has clones" (m4-zfs) |
| Z13 | promote | a clone | the origin's snapshots up to the clone's now its, the origin a clone of it |
| Z13 | promote | no clone | "not a clone" |
| Z14 | send | whole | the target made, its snapshot the source's guid |
| Z14 | send | whole onto a filesystem there | "exists" |
| Z14 | send | incremental | the next snapshot, the source's guid |
| Z14 | send | incremental onto a newest snapshot that is no counterpart; onto no filesystem; from no snapshot | "newest snapshot is not FROM's counterpart"; "no such filesystem"; "no such snapshot" |
| Z14 | send | a clone stream | the target a clone of ORIGIN, its snapshot the source's |
| Z14 | send | a clone stream onto an origin no counterpart | "not FROM's counterpart" |
| Z15 | send cut | an incremental | a receive to resume; the target's newest as before |
| Z15 | send | while a receive to resume stands | "a receive to resume" |
| Z15 | resume | an incremental; a whole | the snapshot the source's; nothing left to resume |
| Z15 | resume | nothing to resume; the source destroyed | "no receive to resume"; "SNAP: no such snapshot" |
| Z15 | abort | an incremental; a whole | nothing to resume, the target as before; what the whole made gone |
| Z15 | abort | nothing to resume | "no receive to resume" |
| Z15 | send | after an abort | it lands |
| Z16 | set | a user property on a snapshot | its value there, not its filesystem's (m4-beam) |
| Z16 | set | set there already | replaced |
| Z16 | inherit | set on a snapshot | its filesystem's again |
| Z16 | set, inherit | no such snapshot | "no such snapshot" |
| Z17 | props | a filesystem: one set there, one inherited, a native one set there | the one set there, with its value (m4-beam) |
| Z17 | props | a snapshot with one set there | that one, not its filesystem's |
| Z17 | props | nothing set there | nothing |
| Z17 | props | FN stops | FN's value returned |
| Z17 | props | no such filesystem; no such snapshot | "no such filesystem"; "no such snapshot" |
| Z18 | create, children | made in a forked child, the layer opened before the fork | seen by the parent: the state is the kernel's, the fake's shared as it is (m4-beam) |

The streams' cells cut a stream through the driver (zc_cut): on the
box as probe P1 cuts it, zfs send into head -c into zfs receive -s;
in the fake, zfake_cut. A fake whose abort leaves what a whole made
fails Z15 "a cut whole taken away".

Disposition of every row above: tests/zcells.c, over the fake (make
check) and over libzfs (tests/box/run.sh). Deferred, with what
unblocks them:

- mount "mounted at PATH, not its mountpoint": the cell needs a
  mountpoint changed under a mounted filesystem. shtetl sets only
  user properties (Z10, m3-roll), and libzfs remounts a filesystem
  whose mountpoint it sets, so the cell waits for an issue that
  mounts something itself.
- destroy of a snapshot "has holds": shtetl takes no holds. ("has
  clones" is Z9's, from m4-zfs.)
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
| S8 | a row's dataset missing | made, its properties from birth | tests/check_spaces.c |
| S9 | there already | left, nothing said | tests/check_spaces.c |
| S10 | the row's space absent | nothing made | tests/check_spaces.c |
| S11 | making it refused | its words | tests/check_spaces.c |
| S12 | the beam's rows (m4-beam), lake and run found | lake/containers and lake/systems, mountpoint none and readonly; run/foundations canmount off and readonly; run/containers mountpoint /containers; run/systems canmount off | tests/check_spaces.c |

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
| D18 | the rc script | started under daemon(8), stopped by its pidfile | by hand on the box (tests/box/README.md): passed on KIDON, 2026-10-20 |
| D19 | show | the site's settings, from zconfig where it is mounted, each with its layer | tests/check_daemon.c |
| D20 | show RECIPE | the recipe, its layers merged | tests/check_daemon.c |
| D21 | show a refused recipe | its words, nothing shown, 1 | tests/check_daemon.c |
| D22 | show with two recipes | the usage, 2 | tests/check_daemon.c |
| D23 | show, zconfig not mounted | "shtetl: ROOT: not mounted", 1 | tests/check_daemon.c |
| D24 | show, no config space | "shtetl: no config space", 1 | tests/check_daemon.c |
| D25 | the lake and the run found; the run not | the beam begun at the start, beam.log made in the log directory, the lake's version beamed, "beam converged" in status after the foundations; no beam and no line (m4-beam) | tests/check_daemon.c |

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
| A3 | no version at all, mounted, a child | destroyed, children first; no actor; nothing said, no stall (m3-logs) | tests/check_foundation.c; box: tests/box/run.sh |
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
| CS13 | remotes.src: a URL or a host and a path accepted; a directory of zconfig, an scp-style address, empty, a number refused | covered: check_site.c test_remotes |
| CS14 | remotes.recipes: name = remote; a bad name, a bad remote, a list refused; remotes.lake an invalid key (out of scope) | covered: check_site.c test_remotes |
| CS15 | the src remote built in, no recipe repository; shtetl.conf's repositories in the order it gives them (sh_site_repos) | covered: check_site.c test_load |

### CC: the cascade (docs/configuration.md 5, 6; src/core/cascade.c)

Dimensions: layers {the default (shtetl.conf present or absent, its
imports), imports flat, nested, repeated, cyclic, missing, the
target}; where the recipe is {the top, a repository, two places,
none, a repository not cloned}; merge {one value (agree, disagree, over the default), text
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
| CC39 | a recipe in a repository is found there, its file named with it; its imports are read there, the default's at the top | covered: check_cascade.c test_repos |
| CC40 | one recipe in two places (the top and a repository, two repositories) is refused: "B: already defined in A", the top first, then the repositories in order | covered: check_cascade.c test_repos; configuration.md, example repositories |
| CC41 | a recipe in no place: "F: no such file" | covered: check_cascade.c test_repos |
| CC42 | a repository named but not there (not cloned yet) is an empty one | covered: check_cascade.c test_repos |
| CC43 | requires names a container of another repository: found | covered: check_cascade.c test_repos |
| CC44 | a path a recipe gives (kernconf's config, a patches directory) is in its repository, not at the top; one the default gives is at the top | covered: check_cascade.c test_repos |
| CC45 | an import is read in its importer's place, never across to the top: "import: R/F: no such file" | covered: check_cascade.c test_repos |
| CC46 | a house stacked on its foundation version's shaken recipe (show's text, parsed back): the first layer, taken as the default is | covered: check_cascade.c test_stack |
| CC47 | what stacks, stacks: the foundation's packages and datasets first | covered: check_cascade.c test_stack |
| CC48 | the foundation's ver kept with the house's, to hold together | covered: check_cascade.c test_stack |
| CC49 | the foundation's kind alone (src_branch, kernconf) left out | covered: check_cascade.c test_stack |
| CC50 | commands: the shaken blocks' pieces keyed by their files; the default's piece given again, alike, one piece | covered: check_cascade.c test_stack |
| CC51 | a dataset property the house gives again overrides the foundation's, with a warning; a derivation the default gives again, alike, says nothing | covered: check_cascade.c test_stack |
| CC52 | a house's ver the foundation's cannot meet: refused naming the foundation version | covered: check_cascade.c test_stack |

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

## Family I: the shaken recipe in what it built (m2-shake)

Dimensions: writing {into a tree with nothing on the way, over the
file there, a link on a directory of the way, a link at the file's
place}, the flag {one a user may set, schg as root on ZFS}; reading
{back through the snapshot's view, a link on the way, the file a
link, none there, the live tree changed after the snapshot}. The
shaken recipe's text is show's (CW1 holds that it parses back).

| cell | what | expected | disposition |
|------|------|----------|-------------|
| I1 | written into a tree | the text, mode 0444, the flag set; directories made 0755 | tests/check_shake.c |
| I2 | written again | replaced, its flag cleared first and set again | tests/check_shake.c |
| I3 | a link on the way | "var/db: a link", nothing written through it | tests/check_shake.c |
| I4 | a link at the file's place | replaced by the file, the link's target untouched | tests/check_shake.c |
| I5 | read through .zfs/snapshot/SNAP | parsed, the layer named as asked | tests/check_shake.c (a directory standing in) |
| I6 | a link on the way in the snapshot | ".zfs/snapshot/S/var: a link" | tests/check_shake.c |
| I7 | the file itself a link | "PATH: a link" | tests/check_shake.c |
| I8 | no such snapshot or file | the system's words | tests/check_shake.c |
| I9 | written schg, as root on ZFS | the flag on, mode 0444 | box: tests/box/shake.c |
| I10 | schg | root may not write it, nor unlink it: EPERM | box: tests/box/shake.c |
| I11 | written again over schg | replaced, schg again | box: tests/box/shake.c |
| I12 | read back through a real snapshot's .zfs view | parsed | box: tests/box/shake.c |
| I13 | a link in the snapshot on the way | refused | box: tests/box/shake.c |
| I14 | the live file rewritten after the snapshot | the snapshot's read unchanged | box: tests/box/shake.c |
| I15 | read from the tree as it is, no snapshot (m3-pkg); a link on its way | parsed; "var/db: a link" | tests/check_shake.c |

A link on the way is told from other refusals by looking at it once
it is refused (fstatat, not following it): the errno a component
opened without following a link meets is ELOOP on the mac's open,
ENOTDIR on its openat with O_DIRECTORY, EMLINK on FreeBSD.


## Family J: jobs, steps and children (m3-builder)

Dimensions: a step {a child, a call, a call adding steps, a call in a
child of its own (m4-beam)}; how a child ends {exit 0, exit N, a
signal, not run at all}; what it writes {lines, a last line with no
newline, a line past 4096, nothing, many lines}; what it leaves
{nothing, a process of its group}; the daemon's signals {ignored
there}; standard input; the job stopped while a child runs.

| cell | what | expected | disposition |
|------|------|----------|-------------|
| J1 | children in order, ending well | a line "# STEP" as each begins, then each line it writes, in order; the end with no why | tests/check_job.c |
| J2 | a child exiting 3 | "STEP: exited 3"; its lines logged; no later step | tests/check_job.c |
| J3 | a failing child of 20 lines | retired (m3-logs): nothing is told again; a failing child's lines are in its log | - |
| J4 | calls: one adding a child, one failing | the added child runs; "STEP: its words", no later step | tests/check_job.c |
| J5 | a child killed by a signal | "STEP: killed by signal 9" | tests/check_job.c |
| J6 | a program not there | "STEP: exited 127", the reason in the log | tests/check_job.c |
| J7 | a last line with no newline | told at the child's end | tests/check_job.c |
| J8 | a line past 4096 bytes | told in pieces of 4096 | tests/check_job.c |
| J9 | stopped while a child runs | the child and its group killed and reaped, no end | tests/check_job.c |
| J10 | a child leaving a process of its group behind | the leftover sent SIGTERM at the step's end | tests/check_job.c |
| J11 | SIGTERM ignored by the daemon | the child's own again: a child sending itself SIGTERM dies of it | tests/check_job.c |
| J12 | a child reading standard input | /dev/null: it ends | tests/check_job.c |
| J13 | a forked call ending well | "# STEP" logged, then what it writes; the daemon not held while it runs; what it changes not the daemon's; the next step, the end with no why | tests/check_job.c |
| J14 | a forked call failing | "STEP: its words", the last line it wrote; its words logged; no later step | tests/check_job.c |
| J15 | a forked call sending itself SIGTERM, the daemon ignoring it | "STEP: killed by signal 15" | tests/check_job.c |
| J16 | stopped while a forked call runs, a child of its own under it | both killed, reaped, no end | tests/check_job.c |

Mutations caught: the signals not put back (J11), no SIGTERM to a
step's leftovers (J10). On FreeBSD, make check-freebsd (in the box run)
runs the same cells: EVFILT_PROC and a child gone before it is watched
(ESRCH) are kqueue's there.

## Family T: the builder's turns (m3-builder)

| cell | what | expected | disposition |
|------|------|----------|-------------|
| T1 | a turn asked of a free builder | given at once | tests/check_job.c |
| T2 | turns asked while one is out | given in the order asked, one at each give | tests/check_job.c |
| T3 | the last given back | the builder free; the next ask given at once | tests/check_job.c |

## Family G: what a roll fetches (m3-src)

The cells run git for real, over repositories made in the scratch
directory and reached as file:// remotes. Mutations caught: the clean
left out (GS2); the reset made a checkout (GS2); byte order for natural
order, and hidden names listed (GP7); git apply without --3way (GP9);
a directory always at zconfig's top (GP4); the ref or the dir passed
over (GP3); a recipe repository cloned though there (GR3). The patch
sources' shallow fetch is economy, and no cell's: a mutation of it
fails nothing.

### GS: the src checkout (plan 6 step 1)

Dimensions: zbuild's src {not there, a checkout as an earlier roll
left it: at an older head, a tracked change, a patch's new file, an
untracked and an ignored file, a conflict in the index}; the src
remote {there, not}; the branch {on it, not}; the remote's form {a
URL, a host and a path}.

| cell | what | expected | disposition |
|------|------|----------|-------------|
| GS1 | no checkout yet | src at the branch's head | tests/check_src.c |
| GS2 | a checkout at an older head, a tracked change, an untracked and an ignored file | at the new head, nothing else | tests/check_src.c |
| GS3 | a conflict an earlier roll's patch left in the index | gone; at the head | tests/check_src.c |
| GS4 | the src remote not there | "src: fetch: exited 128"; no step after | tests/check_src.c |
| GS5 | the branch not on the remote | "src: fetch: exited 128" | tests/check_src.c |
| GS6 | a file an earlier roll's patch changed, the patch now dropped, its time 2000 | unpatched, its time now's (main 8b27e68) | tests/check_src.c |
| GS7 | sh_src_reset after a failed patch | the checkout at HEAD, clean | tests/check_src.c |
| GS8 | a remote as a host and a path | https://host/path | tests/check_src.c (sh_src_url) |
| GS9 | a remote as a URL | as written | tests/check_src.c (sh_src_url) |
| GS10 | a FreeBSD src fetch over https | the checkout of a real branch | box: tests/box/build.sh R1, R2 |

### GP: the patches (plan 6 step 2)

Dimensions: the sources {none, one from a remote, a directory of
zconfig, several}; a remote source's {ref, HEAD} by {dir, its top}; a
directory's place {zconfig's top, as the default gives it; a recipe
repository, as a recipe in it gives it}; the files {*.patch, others,
hidden, numbered past 9}; a patch {applies, its context moved, merges
three ways, already in, conflicts, its base unknown and not
applying}; a remote source {fetched, not reachable, its dir missing}.

| cell | what | expected | disposition |
|------|------|----------|-------------|
| GP1 | no patches | the src steps alone; ends well | tests/check_src.c |
| GP2 | a remote source, no ref, no dir | fetched at HEAD into patches/1; its top's patches applied | tests/check_src.c |
| GP3 | a remote source with a ref and a dir | fetched at the ref; the dir's patches, not the top's | tests/check_src.c |
| GP4 | a directory a recipe in a recipe repository gives | read in the repository's place | tests/check_src.c |
| GP5 | a directory shtetl.conf's foundation block gives | read at zconfig's top | tests/check_src.c |
| GP6 | several sources | applied in the recipe's order | tests/check_src.c |
| GP7 | 1, 2 and 10.patch beside README and .#3.patch | applied 1, 2, 10; the others never | tests/check_src.c |
| GP8 | a patch whose context moved | applied | tests/check_src.c |
| GP9 | a patch whose context changed, its base in the checkout | merged three ways | tests/check_src.c |
| GP10 | a patch already in | goes on | tests/check_src.c |
| GP11 | a conflict | "LOC: NAME: exited 1"; no patch after it | tests/check_src.c |
| GP12 | a patch whose base the checkout lacks, not applying | "LOC: NAME: exited 1" | tests/check_src.c |
| GP13 | a remote source not reachable | "LOC: fetch: exited 128" | tests/check_src.c |
| GP14 | a remote source's dir not in it | "patches: PATH: No such file or directory" | tests/check_src.c |
| GP15 | natural order | 2 before 10, a2 before a10, 02 and 2 by their bytes once equal, x before x1 | tests/check_src.c (sh_natcmp) |

### GR: the recipe repositories (plan 4)

| cell | what | expected | disposition |
|------|------|----------|-------------|
| GR1 | none named | no step | tests/check_src.c |
| GR2 | one missing from zconfig | cloned into its directory | tests/check_src.c |
| GR3 | one there, behind its remote | no step; its head as it was | tests/check_src.c |
| GR4 | one whose remote is not there | "NAME: clone: exited 128" | tests/check_src.c |

## Family F: a foundation's roll (m3-roll)

Dimensions: the foundation {no version: a first roll; a version: an
update; stalled}; the ask {idle, waiting, rolling, stalled}; where the
roll ends {well; failing at the recipe, the ver before or after the
checkout, a patch, the version, the snapshot}; the recipe's datasets
{none, a child under a parent it does not give}; the daemon stopped
while a child runs, then the actor started again. The rolls run over
the fake ZFS with git real, on tests/src-fixture.sh's repositories;
what a roll fails with is read from the error sink, "foundation F:
STEP: why". Mutations caught: no stall set (F5); nothing put back
(F5); the head not unmounted (F1); every version of kind create
(F2); a placeholder with canmount on (F1); no zbuild reset (F5); no
ver held to the branch (F7) or to REVISION (F8); a roll asked twice
(F3); a stall not cleared (F4); the default's properties alone (F1);
the commit not the src's (F1).

| cell | what | expected | disposition |
|------|------|----------|-------------|
| F1 | a first roll ending well | F made with the site's properties, lake's over default's; usr a placeholder, canmount off; usr/local; var with its atime; every dataset snapshotted at once, <ver>-<date>-create, shtetl:kind create and shtetl:commit the src commit; foundation.conf in the head, the record over the shaken recipe; F idle at it, unmounted; the builder free | tests/check_roll.c |
| F2 | an update ending well | a new version, -update, kind update | tests/check_roll.c |
| F3 | asked while it rolls | "F: rolling" | tests/check_roll.c |
| F4 | asked while stalled | the stall inherited away; it rolls | tests/check_roll.c |
| F5 | an update failing at a patch | what the roll wrote gone; shtetl:stall and status say the step and why; zbuild reset; the builder free | tests/check_roll.c |
| F6 | a first roll failing | its datasets destroyed; no status line, no stall | tests/check_roll.c |
| F7 | ver refused by src_branch | "recipe: FILE: ver: releng/15.1 is not >=16"; nothing made | tests/check_roll.c |
| F8 | ver refused by the checkout's REVISION | "version: FILE: ver: 15.1 is not >=16" | tests/check_roll.c |
| F9 | a checkout with no newvers.sh | "version: sys/conf/newvers.sh: No such file or directory" | tests/check_roll.c |
| F10 | stopped while a child runs, an update; started again | F at its newest version, what the roll wrote gone, no stall; then it rolls | tests/check_roll.c |
| F11 | stopped while a child runs, a first roll; started again | no foundation | tests/check_roll.c |
| F12 | the snapshot refused | rolled back, stalled "snapshot: why" | tests/check_roll.c |
| F13 | a second foundation asked while one rolls | "waiting" in status; it rolls when the first's turn is given back | tests/check_roll.c |
| F14 | status while it rolls | "foundation F rolling STEP" | tests/check_roll.c |
| F15 | a recipe that is not there | "recipe: ..."; nothing made | tests/check_roll.c |
| F16 | shtetl roll over the socket: a bad name, a space missing, no argument | "NAME: invalid name: why", "no build space", the usage | tests/check_daemon.c |
| F17 | the rc script starting the daemon, git found under service(8) | a roll's git runs | box: tests/box/build.sh R1 |
| F18 | foundation.conf schg, a roll over libzfs | as F1, on ZFS | box: tests/box/build.sh R1 - R4 |
| F19 | asked while zbuild is reset after a failure | rolling still: "F: rolling"; stalled only once the reset is over | tests/check_roll.c |
| F20 | a roll ending well; one failing | the beam told of the version, once; told nothing (m4-beam) | tests/check_roll.c |

## Family K: the build and the install (m3-install)

Dimensions: the kernel's sync {a file alike, changed, gone from the
new kernel, new; a link alike or changed; a directory where a file was;
in a kernel directory or outside one; a kernel directory the new
kernel has none of}; installworld's backups {each kind, and names that
are none}; a tree removed {with flags, not there}; the make steps {a
first roll, an update}; the configs {kernconf a name or a path,
buildopts, make_jobs}; a make {failing, stopped}. The unit cells run
on files in the scratch directory (tests/check_install.c); the roll's
over the fake ZFS with tests/bin/make, which leaves what each target
would and logs its words (tests/check_roll.c). Mutations caught: no
delete-old on an update, or one on a first roll; installworld without
install -C; no removal in the kernel's directories, or a kernel
directory the new kernel lacks kept; a file alike copied anyway; no
backups step; .old alone taken for a backup; flags not cleared (on
the mac, when the flag was uchg; now the box's R5); no
MAKEOBJDIRPREFIX; the ident not in capitals; a kernconf path at
zconfig's top; make_jobs passed over; the scratch directory kept;
kernconf's lines or buildopts left out.

| cell | what | expected | disposition |
|------|------|----------|-------------|
| K1 | a kernel file alike | kept: its inode the same, its mode set where it differs | tests/check_install.c |
| K2 | a kernel file changed | a copy in its place: a new inode, the content and mode | tests/check_install.c |
| K3 | a file the new kernel has not, in boot/kernel; one outside | removed; kept | tests/check_install.c |
| K4 | a kernel directory the new kernel has none of | removed | tests/check_install.c |
| K5 | a link alike; a link changed | kept; replaced | tests/check_install.c |
| K6 | a directory where a file was | the file removed, the directory made | tests/check_install.c |
| K7 | installworld's backups | boot's *.old, kernel.old, libexec's ld-elf*.so.1.old, sbin/init.bak removed; boot/.old, ld-elf.so.1, init, loader kept | tests/check_install.c |
| K8 | a tree with flags; a path not there | removed; done | tests/check_install.c (nodump, a user's flag: FreeBSD's ZFS has no uchg); schg, root's, box: tests/box/build.sh R5, installworld's backups |
| K9 | the build | env MAKEOBJDIRPREFIX=OBJ make -C SRC -jN buildworld buildkernel SRCCONF KERNCONFDIR KERNCONF | tests/check_roll.c |
| K10 | a first roll's install | installworld (install -C) into the head, installkernel into the scratch directory, distribution; no delete-old, no backups | tests/check_roll.c |
| K11 | an update's install | delete-old before installworld; the backups removed after | tests/check_roll.c |
| K12 | the configs | the kernel config: include of the checkout's GENERIC for this machine, ident in capitals, kernconf's lines; src.conf: buildopts | tests/check_roll.c |
| K13 | kernconf a path | include of the file of zconfig in the recipe's place | tests/check_roll.c |
| K14 | make_jobs | -jN from the site | tests/check_roll.c |
| K15 | a make failing | "installworld: exited 2"; make's words in the foundation's log; rolled back and stalled | tests/check_roll.c |
| K16 | stopped in the build; started again | the head at its newest version | tests/check_roll.c |
| K17 | the obj cache | made on a first roll with the site's properties, mounted; the scratch directory gone after | tests/check_roll.c |
| K18 | an update's kernel | synced into the head: the kernel replaced, a.ko kept, b.ko and the debug files gone, c.ko new | tests/check_roll.c |
| K19 | a real build and install | a FreeBSD world and kernel in the head | box: tests/box/build.sh R3 |

## Family X: the packages and the commands (m3-commands)

Dimensions: what the recipe gives {commands-external, pkg, commands,
none of them, commands alone}; the head's resolv.conf {none, its own};
a step {ending well, failing}; devfs {mounted, left by a failure or a
crash}. The rolls run as family F's, with tests/bin's fake mount,
umount, pkg and chroot logging to the fakes' log, which the
commands-external of the recipes write to as well, so the log holds
the order; a fake chroot runs nothing. Mutations caught: no
commands-external, or no SHTETL_KIND; no devfs, or devfs left
mounted; nothing lent, or a lent resolv.conf kept, or one lent over
the head's own; no commands; a chroot for a recipe with neither pkg
nor commands; a label's last piece alone.

| cell | what | expected | disposition |
|------|------|----------|-------------|
| X1 | commands-external | on the host, before anything in the chroot, SHTETL_ROOT, SHTETL_NAME and SHTETL_KIND set | tests/check_roll.c |
| X2 | pkg and commands | devfs mounted on the head's dev; pkg -c HEAD install -y -- the packages; each command piece by chroot HEAD /bin/sh -e -c, a label's pieces in file order (an import's first); devfs unmounted | tests/check_roll.c |
| X3 | pkg, the head with no resolv.conf | the host's lent for pkg, taken back after | tests/check_roll.c |
| X4 | pkg, the head with its own | not lent; kept | tests/check_roll.c |
| X5 | no pkg, no commands | no chroot, no devfs | tests/check_roll.c (K10's exact log of r1) |
| X6 | commands alone | devfs and the chroot; no pkg, nothing lent | tests/check_roll.c |
| X7 | a command failing | "commands: LABEL: exited 1"; rolled back, stalled | tests/check_roll.c |
| X8 | devfs left mounted by a failure, a stop or a crash | unmounted by sh_foundation_idle before the head | box: tests/box/build.sh R9 (the mac mounts no devfs) |
| X9 | real pkg and chroot | the packages in the head | box: tests/box/build.sh R3 |

## Family U: a pkg roll (m3-pkg)

Dimensions: the foundation {a version, none, stalled}; its recipe
{as the version was built, changed since}; the upgrade {ending well,
failing}. Run as family F's, the fakes logging. Mutations caught: a
failed pkg roll resetting zbuild (U3); kind update (U1); the version's
datasets not mounted (U1); the commit not the version's (U1); no
refusal without a version (U2, a crash); no list given the upgrade, or its names dropped (U1); install for upgrade, and no
resolv.conf lent for it (U1).

| cell | what | expected | disposition |
|------|------|----------|-------------|
| U1 | a pkg roll | F and the datasets its version's foundation.conf gives mounted; devfs, the resolv.conf lent, pkg -c HEAD upgrade -y -- the packages that file lists (ruled 2026-10-20), devfs off; nothing built; F@<ver>-<date>-pkg on every dataset, kind pkg, the version's shtetl:commit | tests/check_roll.c |
| U2 | a foundation with no version | "F: no version" | tests/check_roll.c |
| U3 | the upgrade failing | "pkg: exited 1"; rolled back, stalled; zbuild untouched | tests/check_roll.c |
| U4 | the recipe given a dataset and a package since the version | neither made nor upgraded: the version's recipe is the pkg roll's | tests/check_roll.c |
| U8 | a version whose foundation.conf lists no packages | no chroot, no pkg; the -pkg version made | tests/check_roll.c |
| U5 | asked of a stalled foundation | the stall cleared; it rolls | tests/check_roll.c |
| U6 | shtetl pkg over the socket: no argument, no such foundation | the usage; "NAME: no such foundation" | tests/check_daemon.c |
| U7 | a real pkg upgrade | the packages upgraded in a version of kind pkg | box: tests/box/build.sh R8 |

## Family R: real rolls on the box (m3-box)

tests/box/build.sh: the daemon, installed and started by rc, rolling
a real foundation from the FreeBSD src remote on a pool kept between
runs, asked as a person asks and watched through shtetl status, what
it did read from ZFS and the head. The cells the families above defer
to the box are these.

| cell | what | expected | disposition |
|------|------|----------|-------------|
| R1 | shtetl roll box, a first roll, the daemon rc's (F17, GS10) | idle at <ver>-<date>-create | tests/box/build.sh |
| R2 | the version | every dataset kind create; shtetl:commit zbuild's HEAD; usr a placeholder; its log, /var/log/shtetl/box.log, the build's step line and the version's | tests/box/build.sh |
| R3 | the head (K19, X9) | bin/sh and boot/kernel/kernel; the package in usr/local; the command's motd; no resolv.conf left | tests/box/build.sh |
| R4 | foundation.conf on ZFS (F18) | schg, 0444; the head unmounted | tests/box/build.sh |
| R5 | an update | idle at -update, kind update | tests/box/build.sh |
| R6 | two patches of one line, a three-way conflict | stalled "patches/local: 2-b.patch: exited 1", shtetl:stall, the newest version unchanged, zbuild clean | tests/box/build.sh |
| R7 | the patch taken out, rolled again | the stall cleared, idle at -update, the first patch in the checkout | tests/box/build.sh |
| R8 | shtetl pkg box (U7) | idle at -pkg, kind pkg, the version's commit | tests/box/build.sh |
| R9 | the daemon stopped in a command, started again (X8, K16, F10) | devfs left on the head, a marker the roll wrote in it, then devfs and the marker gone; idle at the newest, no stall; unmounted | tests/box/build.sh |
| R10 | every roll of the run | syslog one line, R6's stall, and nothing else (m3-logs) | tests/box/build.sh |

## Family L: where a roll's output goes (m3-logs)

Ruled 2026-10-20 (plan 18): a roll's output in its foundation's log,
syslog one line for each roll that fails.

| cell | what | expected | disposition |
|------|------|----------|-------------|
| L1 | a roll ending well | its log, F.log: a line "# STEP" as each program begins, their output, "# version SNAP" last | tests/check_roll.c |
| L2 | a second roll | the log made anew: the first roll's lines gone | tests/check_roll.c |
| L3 | a roll failing | syslog one line, "foundation F: STEP: why", and nothing else; the log "# failed: why", then zbuild's reset | tests/check_roll.c |
| L4 | a log that cannot be made | the ask refused, "PATH: why"; the foundation as it was | tests/check_roll.c |
| L5 | a roll ending well | syslog nothing | tests/check_roll.c |
| L6 | the daemon's start | the log directory made | tests/check_daemon.c |
| L7 | the box: every roll of a run, the log of the last | box: tests/box/build.sh R2, R10 | box |

## Family B: the beam (m4-beam)

Plan 8.1: every dataset's copy converges to its owner's, foundation
versions from lake/foundations to run/foundations, house versions from
run/containers and run/systems to lake/containers and lake/systems.
Each snapshot the copy lacks after the newest of the dataset's history
whose guid the copy's space holds, wherever it is there, is sent, in
origin order, oldest first: an incremental onto a copy there, a clone
stream onto the space's snapshot of that guid for a copy that is not,
a full stream where no guid of the history is held. A receive to
resume goes first; nothing is destroyed. The shtetl:* properties of
each filesystem and snapshot are made the owner's once its version is
all there, the version's top last: a version whose top snapshot holds
them has arrived. The actor converges at its start, when told, and on
a timer, backing off while a convergence fails.

Dimensions: the pair {foundations, containers, systems}; a dataset's
history on the owner's side {its own snapshots only, a clone of a
snapshot outside the pair, a clone of one inside it, promoted, its
origin's filesystem destroyed}; the copy's side {nothing of the
history, its origin's snapshot only, behind, up to date, a receive to
resume, its newest no counterpart, promoted, a snapshot pruned}; a cut
at each stream of a convergence; the properties {a snapshot's, a
filesystem's, changed or taken away on the owner}; the actor {start,
told, told while converging, the timer, failing, a space gone, stop};
what it says {status, the log}.

The cells run over the fake, whose state is shared with the children
the beam forks for its streams, as ZFS's is in the kernel (Z18); a
stream is cut by zfake_cut_nth, a space made unreachable by
zfake_export.

| cell | what | expected | disposition |
|------|------|----------|-------------|
| B1 | a foundation's first version, the run without it | a full stream of each dataset, a parent before its children; each copy's snapshot the owner's guid; the properties of each copied; "# arrived" the top's copy | tests/check_beam.c |
| B2 | later versions | an incremental each, oldest first, from the newest the copy holds | tests/check_beam.c |
| B3 | up to date | nothing sent, nothing logged | tests/check_beam.c |
| B4 | a version with a dataset new in it | that dataset a full stream, the others incrementals; the version arrived once all of it is there | tests/check_beam.c |
| B5 | a house version on the run, cloned from a foundation copy | on the lake a clone stream onto the lake's own foundation snapshot of that guid; the house's own dataset a full stream | tests/check_beam.c |
| B6 | a house's own dataset cloned from the version before's | a clone stream onto the lake's copy of that snapshot | tests/check_beam.c |
| B7 | a promote on the run before the beam | the promoted clone a clone stream onto the lake's copy of its newest held guid, sent from the owner's snapshot where the promote moved it; nothing else sent | tests/check_beam.c |
| B8 | a promote on the run after the beam | nothing sent; its next snapshot an incremental | tests/check_beam.c |
| B9 | the lake's copy promoted | nothing sent again; the owner's next snapshot lands | tests/check_beam.c |
| B10 | the old version destroyed on the run, before the beam and after | nothing sent, nothing destroyed on the lake | tests/check_beam.c |
| B11 | a snapshot the copy lacks older than its newest held, pruned there | not sent again | tests/check_beam.c |
| B12 | a cut at each stream of B1, B2, B4, B5, B6 and B7 | the convergence failing with the stream's words, a receive to resume left, nothing destroyed; the next resuming it first, then going on; converged, every guid and property the owner's, each version arrived once | tests/check_beam.c |
| B13 | a receive to resume whose stream's snapshot is gone | "SNAP: no such snapshot"; failing; the receive left | tests/check_beam.c |
| B14 | a copy with a snapshot of its own since | "newest snapshot is not FROM's counterpart"; failing; nothing destroyed; no stream after it | tests/check_beam.c |
| B15 | a filesystem's properties | the owner's shtetl:* set on the copy; one the owner lets go let go on the copy; any other property of the copy's left | tests/check_beam.c |
| B16 | a version cut after its top's stream; its properties failing part way | the top's copy without the properties, nothing arrived; both once the rest is there | tests/check_beam.c |
| B17 | a convergence after an arrival | "# arrived" not again | tests/check_beam.c |
| B18 | told while converging | converging once more at its end | tests/check_beam.c |
| B19 | the timer after a convergence ending well | the next after the period; told meanwhile, at once | tests/check_beam.c |
| B20 | failing again and again, then ending well | the next after the retry, doubled each time to the period; then the period | tests/check_beam.c |
| B21 | the run's pool exported, then imported | failing with the walk's words; converged once it is back | tests/check_beam.c |
| B22 | stopped while converging | its child killed and reaped, nothing ended; a beam begun again converges | tests/check_beam.c |
| B23 | status | "beam converging STEP", "beam converged", "beam failing WHY" | tests/check_beam.c |
| B24 | the log | "# send SNAP" and "# resume DS" as each stream begins, "# failed: why", "# arrived SNAP"; nothing for a convergence with nothing to do | tests/check_beam.c |
| B25 | a receive to resume outside the beam's roots | left alone | tests/check_beam.c |
| B26 | both pairs lacking | the foundations' streams first | tests/check_beam.c |
| B27 | a system version | as B5, into lake/systems | tests/check_beam.c |
| B28 | the box: the M3 foundation beamed to a run pool, a stream cut and resumed, a house stand-in beamed back | box: m4-box | box |
| B29 | a clone the walk finds before its origin's dataset (a top cloned from a deeper one) | the origin's streams first, the clone a clone stream | tests/check_beam.c |
