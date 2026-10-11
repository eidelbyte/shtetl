The rule this file follows: plot each phase's problem space before
writing its tests. Enumerate the dimensions, cross them into cells,
and give every cell a disposition: planned (names the test that will
close it), covered (the test exists and names the cell), or deferred
(with the reason and the work that unblocks it). A cell with no row
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
| CP23 | the start-up check passes on the linked libucl: a macro refused at line 1, `$` literal, a duplicate refused | covered: check_parse.c |
| CP24 | every refusal names the file | covered: check_parse.c |
| CP25 | a value nested more than 64 deep is refused, naming its key path | covered: check_parse.c |

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
| CK3 | a foundation-only key (src_branch, kernconf, buildopts, patches, the twins) in a container and in a system is refused, naming the kind | covered: check_schema.c |
| CK4 | a container-only key (requires, jailconf, jailconf_file) in a foundation and in a system is refused | covered: check_schema.c |
| CK5 | a string key given a number or a bool is refused; a number says to quote it | covered: check_schema.c |
| CK6 | ver: `>=15`, `=15.1`, `<16`, `<=15.1`, `>14` accepted; ``, `15.1`, `>=`, `=>15`, `>=15.x`, `>=15.` refused | covered: check_schema.c |
| CK7 | kernconf and kernconf_file in one file are refused; so for buildopts and jailconf | covered: check_schema.c |
| CK8 | a `_file` value: relative paths accepted; absolute, `..`, empty components refused | covered: check_schema.c |
| CK9 | import: an array of strings, or one string; a non-string element or a bad path refused | covered: check_schema.c |
| CK10 | reset: collection names only; an unknown key or a scalar key refused | covered: check_schema.c |
| CK11 | pkg: array shorthand; entries true and false; an entry with fields refused; bad names (empty, leading `-`, a space) refused | covered: check_schema.c |
| CK12 | datasets: paths by ZFS's characters; leading or trailing `/`, `//`, `.`, `..`, `@` refused | covered: check_schema.c |
| CK13 | dataset entries: true, false, or properties with string or bool values; a number refused (quote it); mountpoint, canmount and shtetl:* refused; a bad property name refused | covered: check_schema.c |
| CK14 | commands: a string or false; true, an object or an array of names refused; an empty string refused; label syntax | covered: check_schema.c |
| CK15 | derivations: {source, command} or false; an unknown field, a non-string field refused; key path syntax | covered: check_schema.c |
| CK16 | requires: container names, array shorthand; a bad name refused | covered: check_schema.c |
| CK17 | patches: array shorthand; {ref, dir}; the three location forms (a URL, host/path, a zconfig directory); an absolute path, `..`, an scp-style address and an unknown field refused | covered: check_schema.c |
| CK18 | a collection given as a scalar is refused | covered: check_schema.c |
| CK19 | every error in a layer is reported, not only the first | covered: check_schema.c |
| CK20 | the key path quotes a component that is not a bare word (`datasets."usr/local".readonly`) | covered: check_schema.c |

## CS: shtetl.conf (planning.md 5.2, 6.3; src/core/site.c)

Dimensions: section {spaces, properties, build, update, retention,
an unknown one}; value {the right type, each wrong one}; the policy
forms of 6.3 {0, a count, an interval, each malformed one}; layers
{built-in only, shtetl.conf over it}.

| cell | scenario | disposition |
|------|----------|-------------|
| CS1 | 5.2's example is accepted as written | covered: check_site.c |
| CS2 | an unknown section, or an unknown key in a section, is refused | covered: check_site.c |
| CS3 | spaces: lake and run take `default`, a dataset name; config or build with one is refused; a bad name is refused | covered: check_site.c |
| CS4 | properties: sections default, lake, run, config, build; string or bool values; a number refused; mountpoint, canmount, shtetl:* refused | covered: check_site.c |
| CS5 | build.make_jobs: 0 or more; negative, a string, a float refused | covered: check_site.c |
| CS6 | update.suffix: a name; empty or a bad character refused | covered: check_site.c |
| CS7 | retention.run, retention.archive, update.depth: 0, 10, "7d", "6m", "1y", "12H" accepted; -1, "7x", "d7", "", "0d", 1.5 and an unquoted 30d refused, the last saying to quote it | covered: check_site.c |
| CS8 | with no shtetl.conf the settings are the built-in ones (4.3's properties, 6.3's zeros, suffix staging) | covered: check_site.c |
| CS9 | shtetl.conf sets one property; the rest stay built-in, each with its layer | covered: check_site.c |
| CS10 | retention.lake is refused: zlake keeps what zrun keeps | covered: check_site.c |

## CC: the cascade (planning.md 5.4; src/core/cascade.c)

Dimensions: layers {built-in, default.conf present or absent,
imports flat, nested, repeated, cyclic, missing, the target};
merge {scalar, twin, collection add, change, drop, array shorthand,
true over fields, reset}; the drop rule {kind, the collection, the
layer that added the element}; order; checks after the merge;
the target's spelling.

| cell | scenario | disposition |
|------|----------|-------------|
| CC1 | layer order: built-in, default.conf, the imports in order, the target | covered: check_cascade.c |
| CC2 | no default.conf: the recipe resolves without it | covered: check_cascade.c |
| CC3 | an import's own imports merge before it | covered: check_cascade.c |
| CC4 | a recipe imported twice merges once, at its first place | covered: check_cascade.c |
| CC5 | an import cycle is refused, naming the files in it | covered: check_cascade.c |
| CC6 | an import of a missing file is refused, naming the importing file | covered: check_cascade.c |
| CC7 | a scalar: the nearer layer wins and keeps the key's place | covered: check_cascade.c |
| CC8 | a twin: a nearer kernconf_file replaces a lower kernconf, and the other way | covered: check_cascade.c |
| CC9 | collections: add a key, change one field (fields merge), drop with false | covered: check_cascade.c |
| CC10 | array shorthand merges as name = true; true over an entry with fields keeps the fields | covered: check_cascade.c |
| CC11 | a drop of something no lower layer added is refused | covered: check_cascade.c |
| CC12 | a container or system dropping a pkg or dataset that built-in, default.conf or one of its imports added is refused (5.4's tmp and tmux) | covered: check_cascade.c |
| CC13 | a container dropping what its own import added is fine (5.4's apache24) | covered: check_cascade.c |
| CC14 | a foundation dropping a default.conf element is fine | covered: check_cascade.c |
| CC15 | reset empties an inherited collection before the layer merges; a container's reset of default.conf's pkg is refused by the drop rule | covered: check_cascade.c |
| CC16 | order: keys keep their first place across layers; an overridden commands block stays where it was first defined | covered: check_cascade.c |
| CC17 | 5.4's worked example merges to pkg sudo, tmux, py311-certbot, nginx; datasets tmp, var, usr/local, www; commands certbot, nginx | covered: check_cascade.c |
| CC18 | a foundation with no src_branch after the merge is refused | covered: check_cascade.c |
| CC19 | a derivation lacking source or command after the merge is refused | covered: check_cascade.c |
| CC20 | a `_file` or a patches directory that does not exist in the config root is refused | covered: check_cascade.c |
| CC21 | requires naming a container with no recipe, or the container itself, is refused | covered: check_cascade.c |
| CC22 | targets: `containers/nginx` and `containers/nginx.conf` alike; an unknown kind directory, `..`, a bad name, a nested path, a missing recipe refused | covered: check_cascade.c |
| CC23 | errors in several layers are all reported, and nothing is merged | covered: check_cascade.c |
| CC24 | each merged entry names the layer that added it and the one that last set it | covered: check_cascade.c |
| CC25 | patches: a lower layer's sources come first; a source a nearer layer changes keeps its place (7.1.1) | covered: check_cascade.c |
| CC26 | every layer is checked against the target's kind: kernconf in a fragment a container imports is refused, naming the fragment | covered: check_cascade.c |

## CW: what config show prints (src/core/show.c)

| cell | scenario | disposition |
|------|----------|-------------|
| CW1 | a merged recipe prints as UCL that parses back to the same keys, values and order | covered: check_show.c |
| CW2 | every value names its layer; one changed by a later layer names both | covered: check_show.c |
| CW3 | quotes, backslashes and control characters are escaped, and come back the same | covered: check_show.c |
| CW4 | a multi-line string prints as a heredoc; one ending in a newline, or holding the terminator line, still comes back the same | covered: check_show.c |
| CW5 | keys that are not bare words are quoted (`"usr/local"`, a URL) | covered: check_show.c |
| CW6 | an emptied collection prints as an empty object | covered: check_show.c |
| CW7 | the site settings print with built-in or shtetl.conf on each value | covered: check_show.c |

## CL: the command line (src/main.c; tests/run-cli.sh)

| cell | scenario | disposition |
|------|----------|-------------|
| CL1 | no arguments, or -h: the usage, exit 2 (-h: exit 0) | covered: run-cli.sh cl1-none, cl1-help |
| CL2 | an unknown command or option, or -C with no directory: the usage, exit 2 | covered: run-cli.sh cl2-command, cl2-option, cl2-optarg |
| CL3 | no -C: refused, exit 2 | covered: run-cli.sh cl3-noroot |
| CL4 | config show RECIPE: the merged recipe on stdout, exit 0 | covered: run-cli.sh cl4-nginx, cl4-generic |
| CL5 | config show: the site settings, exit 0 | covered: run-cli.sh cl5-site, cl5-builtin |
| CL6 | a refused configuration, a missing recipe, a root that is not a directory: the reasons on stderr, nothing on stdout, exit 1 | covered: run-cli.sh cl6-bad, cl6-drop, cl6-notdir, cl6-norecipe |
| CL7 | config show with two recipes: the usage, exit 2 | covered: run-cli.sh cl7-two |
