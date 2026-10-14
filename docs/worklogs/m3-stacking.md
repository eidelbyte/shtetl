# M3, first part: everything stacks (worklog)

2026-10-12, branch impl/m3, from impl/m2. Before M3 proper (version
naming, patches, the foundation build), the configuration rulings of
2026-10-12 change what M1 built: the drops and `reset` go, every key
stacks, a redefinition is refused, and shtetl.conf becomes the default
layer. Built and tested on the mac; nothing here needs the box, the
cascade being portable code.

## What was built

| commit | unit |
|--------|------|
| 5fe0046 | tracker/shtetl.json and tools/tracker.py, with Q-X's issue (patch-location-hosts); the stacking rulings recorded in the plan |
| 91a16b8 | cascade: everything stacks; no `false` entries, no `reset`; every `ver` kept; buildopts and jailconf appended |
| e28da90 | Q-AC: src_branch and kernconf_file must agree, kernconf_lines stacks; a redefinition refused; dataset properties overridden with a warning; a commands label's pieces keyed by file; a `ver` no version meets refused |
| 6b0b80c | shtetl.conf is the default layer: any kind's keys, its values overridable, what stacks stacking with it; buildopts_file gone; shtetl-recipes/ seeded |

- **How each key merges** is a column of the key table now
  (`rk_merge` in src/core/schema.c): `SH_M_ALL` for ver (a list),
  `SH_M_TEXT` for buildopts, kernconf_lines and jailconf, `SH_M_AGREE`
  for src_branch and kernconf_file, `SH_M_COLL` for the collections,
  `SH_M_LAYERS` for import. src/core/cascade.c's `mg_layer` switches
  on it.
- **Text and commands are pieces keyed by file.** A text key merges
  to an object `{ "<file>" = <text>; ... }` in layer order, and so
  does each commands label. config show prints a piece of lines as a
  heredoc. The key is what 7.4 needs to run each piece once: a piece
  of the foundation's (same label, same file, same text) is not run
  again in the container.
- **`ver`** is a list of every constraint with its file. After the
  merge, `ck_ver` turns each into a range (`sh_ver_range`,
  major * 100000 + minor) and refuses the recipe when the highest
  floor is above the lowest ceiling, naming the two.
- **The default layer.** `sl_default` marks the layers loaded from
  shtetl.conf and its imports. `mg_default` asks whether a merged
  value came from one of them, by its `sn_from`. Where the value
  already there is the default's, a redefinition becomes an override,
  with no warning. The default's files are checked by
  `sh_schema_default`, every kind's keys at once, with shtetl.conf's
  settings sections skipped; site.c checks the sections and leaves
  the rest to it.
- **Keys that are gone** (reset, kernconf, jailconf_file,
  buildopts_file) are refused with a message saying what to write
  instead, from a `gone[]` table in schema.c.

## Decisions beyond the plan

The rulings are quoted in planning.md 14. What I settled where they
were silent:

- Q-AD (planning.md 15):
  - the files shtetl.conf imports are of the default too;
  - an override of a default's dataset property gives no warning;
  - `ver` stacks with the default, so a recipe whose `ver` cannot meet
    shtetl.conf's `>=15.1` is refused, not let through. The user's
    words were "all stackables simply stack. user needs to delete a
    stackable value if they want it overriden".
- A field given again with the same value is still refused; only
  src_branch and kernconf_file accept a repeat that agrees. That
  follows "Both redefinitions of a key are errors", which ruled
  derivations given twice with the same text.
- A derivation is given whole in one file: source and command
  together. Before this, the two could come from different files.
- A `kernconf_file` with a `/` is a path in zconfig and must exist
  there. Without one it names a config in the src tree, which M3's
  build checks once the tree is checked out.
- shtetl-recipes/ sits in this repository until the recipes have a
  repository of their own (since 2026-10-14,
  github.com/eidelbyte/shtetl-recipes). Its .gitignore follows 4.4: it
  ignores everything but what it ships.

## Verification

- mac, make check:
  - check_parse 169, check_schema 347, check_site 186,
    check_cascade 215, check_show 52, check_run 107, check_spaces 89,
    check_boot 277;
  - 21 command-line cases, re-recorded for the default layer.
- tests/MATRIX.md has 159 cells, none planned, 5 retired with the
  drops.
- tools/xcheck-freebsd.sh: every source compiles for FreeBSD.
- make gate: clean (cstyle on 39 files, ASCII over docs, src, tests,
  tracker and shtetl-recipes).
- The primer's examples (docs/ucl-primer.md 5) were written out as a
  config root and run through `shtetl config show`. All four recipes
  resolve, merged as the primer says.

Not yet verified, or not built:

- `config show containers/X --on F` (agreed, Q-AC 8) is not built.
  It needs the foundation's recipe as of its version's
  `shtetl:config`, which M3's builds record.
- Each command piece running once is M4's create; the pieces are
  keyed for it.
- Setting a container's dataset property on its clones (7.4, 7.6) is
  M4's.
- docs/story.html and docs/slides.html (tools/story.py) still tell
  planning.md draft 4: default.conf, `kernconf`, `retention.lake`,
  and a config show that is not M1's. Regenerating them is its own
  piece of work.

## Gotchas

- libucl reads a comment between `[` and the first element as part
  of the value, so config show puts each list element's comment at
  the end of its line, after the comma.
- libucl takes no heredoc inside an array, which is why texts and
  commands are objects of pieces keyed by file, not lists.
- `mg_default` trusts the value node's `sn_from`, the file it was
  parsed from, which `sh_node_copy` keeps (the pair's `pr_from` is
  the merging layer's, not the value's). A merge step that builds a
  value some other way must carry the file over, or a recipe's value
  passes for the default's and becomes overridable.
- The default is loaded first, so a file shtetl.conf imports stays
  the default's even when a recipe imports it again (each file merges
  once, at its first place).

## Later the same day: Q-AD ruled, and the configuration reference

| commit | unit |
|--------|------|
| 28681db | Q-AD ruled ("1: sounds good 2: give warning for all layers including default 3: agreed"): a dataset property's override warns over the default's value too; the default's imports and its stacking `ver` stay as built |
| 28bd341 | the header comments of cascade.c, schema.c, show.c and tree.h brought up to the rulings: they still spoke of default.conf, buildopts_file, derivations completed across files and the drop rule |
| 96eaa0b | docs/configuration.md, every setting and recipe key with run examples; tools/doccheck.py and `make doc`, part of `make check` |

- **Q-AD**: `mg_entry` drops its `mg_default` test before the
  warning; the other overrides of the default stay without a word, as
  the ruling was about dataset properties. CC32 now expects the
  warning. The second item above supersedes "an override of a
  default's dataset property gives no warning" in the decisions
  earlier in this log.
- **The reference's examples are run.** Each `<!-- example: NAME -->`
  starts an empty root; each ucl block whose first line is
  `# PATH.conf` is that file; each console block's `$ shtetl ...`
  runs with `-C` the root, standard error and output together, and
  `$ echo $?` shows the exit status. `--update` writes the outputs in;
  every one was read against the prose before the commit. A changed
  message makes `make check` fail, naming the example and the line.
- Writing it turned up one thing the plan does not say: a recipe's
  resolve checks shtetl.conf's recipe keys but not its settings, so
  `config show containers/x` resolves with a refused setting, which
  `config show`, `spaces` and `bootstrap` refuse. The reference shows
  it as it is; whether a recipe should be refused too is the user's
  call.

Verification: make check (the 8 suites, 21 command-line cases, 33
doc commands), make gate clean. doccheck run on a copy with one
output changed: it failed, naming entries, line 819.

## Later still: three rulings on the reference's questions

"1. yes, all should warn. 2. shtetl.conf should be checked for
correctness in its own right 3. =15 matches 15.x"

| commit | unit |
|--------|------|
| c3fee4d | every override of the default warns: src_branch, kernconf_file and a derivation joined the dataset property and the entry's field |
| 650729d | shtetl.conf is checked whole wherever it is read: the loader checks it with sh_site_check, so a refused setting refuses every recipe; MATRIX CC35 |
| 7458ef9 | in ver a bare major is every minor of it: =15 is any 15.x, <=15 takes 15.x, >15 begins at 16.0; MATRIX CC36 |
| 272a413 | MATRIX CK3 and CC26 named buildopts_file and kernconf still; text only |

- **A correction.** 28681db's message, and the reference as first
  written, said the default's other overrides stayed silent. An
  entry's field over the default's (a patch's `ref`) warned from
  28681db on, since that commit dropped the default test from the
  branch every field goes through; test_default did not look at the
  warnings of the foundation it resolved. It looks at all five kinds
  of override now. The lesson for the next test of a rule with
  exceptions: count the warnings of every resolve, not only the one
  the change was about.
- **ver.** The ruling named `=`; `<=` and `>` follow the same
  principle, a bare N naming every N.x, so `>15` is 16.0 and later.
  That turned test_ver's case e (`>=15`, `<=15.1`, `>15`) from
  resolving to refused. Asked about `>`, the user confirmed: ">15
  starts at 16."
- **Gotcha, again**: a Python heredoc that writes C must be raw
  (`r'''...'''`), or `\n` lands in the source as a newline. And a
  command chain with `;` after the gate commits whatever the gate
  says; 650729d was amended before any push to fix a long line the
  gate had refused.

Verification: make check (8 suites, check_cascade 250 checks; 21
command-line cases; 35 doc commands in 14 examples), make gate
clean, xcheck below.

## Simple messages, no legacy, kernconf one key

Asked on reading the reference: simple, declarative messages; no
message, test or doc for a key that was removed, nothing having been
released; no ruling dates; and kernconf one key again, a heredoc
lines that stack, a string the config, one per stack. Quoted in
planning.md 14.

| commit | unit |
|--------|------|
| d63f471 | the configuration's messages: "invalid key", "valid only in foundation recipe", "dropping values is illegal", "must be a quoted string", "already defined in F", "A in F conflicts with B in G", "missing src_branch", "no such file"; libucl's errors become "F:N: duplicate key", "macros are illegal" or "syntax error"; the removed-keys table and its tests and cells gone |
| 24d9ea2 | the same for spaces, bootstrap and running programs; tests/box/run.sh's B3, B6, B16 take the new text |
| dcb2afc | kernconf: a heredoc is lines, a string the config, one file of the stack giving it; the parse keeps libucl's heredoc mark (sn_block), the self-check refuses a libucl without it; config show comments only a collection's entries |
| 3f7bb14 | the reference, the primer, the plan's design sections, comments and the matrix without history or ruling dates |

- **Messages.** The rule I wrote by: the key path says where, the
  message says what is wrong, in as few words as are true; a hint
  only where the fix is not the obvious one ("must be a quoted
  string", since UCL took the word for a number). libucl's own text
  is no longer passed on: its error code says whether a key was
  written twice (UCL_EMERGE, both of its merge errors), and the line
  is libucl's.
- **kernconf and one key per file.** UCL takes a key once in a file,
  so a foundation that starts from a config of its own and adds lines
  gives one of the two in an imported file. Raised with the user; the
  reference says so.
- **One per stack** read as: a second file giving the config is
  refused even when alike, unlike src_branch, which may repeat alike.
  The default's is overridden, with a warning.
- **config show**: a layer comment on an object now only where it is
  a collection's entry, so kernconf's lines print as buildopts does.

Verification: make check: check_parse 189, check_schema 325,
check_site 186, check_cascade 251, check_show 52, check_run 107,
check_spaces 89, check_boot 278; 21 command-line cases; 36 doc
commands in 14 examples. MATRIX 159 cells, none planned. make gate
clean; xcheck: every source passes. Not on the box: tests/box/run.sh
needs a run for B3, B6 and B16's new text.
