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
- shtetl-recipes/ sits in this repository until zshtetl-recipes
  exists. Its .gitignore follows 4.4: it ignores everything but what
  it ships.

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
