# m2-language: the configuration language, carried

## What was built

The language of the first history, carried (plan 5, 12): parse
(libucl under shtetl's rules, with its start-up self-check), tree,
root, syntax, schema, site (shtetl.conf's settings), cascade (the two
layers and everything stacking) and show, with their tests
(check_parse, check_schema, check_site, check_cascade, check_show:
1,010 checks), docs/configuration.md, docs/ucl-primer.md and
tools/doccheck.py. Matrix family C carried with them.

Changed in carrying:

- Allocation never fails. tree.c makes nodes with shtetl's allocators;
  sh_arr_push and sh_obj_set return nothing; sh_node_new, sh_node_str
  and sh_node_copy never return NULL. Every allocation check and
  sh_err_nomem went: cascade's mg_nomem flag, ld_load's -1, ld_block's,
  parse's nomem label, root's, site's. libucl's own allocation
  failures (ucl_parser_new, set_filevars, iterate_new) end the process
  through sh_nomem, as shtetl's allocators do (src/core/mem.c).
- err.[ch]: the message count and the key paths come back with the
  language.
- sh_parse_file has one way out (CLAUDE.md, C).
- The settings (docs/configuration.md 3, family CS): update.suffix,
  update.depth, retention.run and retention.archive are gone, and a
  section the model removed is an invalid key (CS10). roll.depth (0,
  a count or an interval), run.keep and lake.keep (a count, 1 or
  more, since the active version counts) are new, with the built-in
  values 0, 2 and 5 (plan 8.2's PROPOSALs). properties stays; that it
  is set on the datasets shtetl makes is a PROPOSAL in the docs, used
  from M3.
- show is the daemon's verb: `shtetl show [RECIPE]`, read from zconfig
  where its root is mounted (sh_show in show.c; the act in daemon.c).
  No config space, or its root not mounted, is said, exit 1. Family D
  gains D19 - D24.
- doccheck runs `$ shtetl show ...` through build/docshow
  (tests/docshow.c), sh_show over the example's root, since there is
  no daemon where the check runs. make check is unit then doc.
- docs/configuration.md says plan for planning.md, house for container
  where it is both, and the new model's words where the old named
  staging, update, cutover, the patch pause or bootstrap. Its usage
  example went: the usage is the daemon's now, and the doc is the
  language's.
- tests/conf and main's command-line family CL are not carried: no
  test read the former, and the latter's cells are the daemon's (D19
  - D24) and the docs' examples.
- The Makefile builds libucl from FREEBSD_SRC's contrib/libucl on the
  mac and links the textproc/libucl port on FreeBSD, as main did;
  LANG_OBJS are the language's objects, which need no ZFS (docshow).

## Decisions

- The settings' shape, `roll { depth }`, `run { keep }`, `lake { keep
  }`, follows the plan's names, run.keep, lake.keep, roll.depth.
  `run` and `lake` as sections beside `properties.run` and
  `properties.lake` are no clash: different levels.
- A keep of 0 is refused: it would prune the active version (plan
  8.2 counts it).

## Verified

- make check: the five carried tests unchanged but check_site (its
  settings cells rewritten) and check_show (its site example);
  check_daemon with the show verb.
- make doc: 36 commands in 15 examples, their outputs regenerated and
  read: only the settings' examples changed.
- leaks on the five tests: only libucl's known 96 bytes a refused
  duplicate key (its error path, from the self-check's duplicate try;
  recorded on the first history), none of ours.
- make gate (49 files), make xcheck (parse.c against the tree's
  libucl header).
- Owed on the box: make check-freebsd, the parse cells against the
  textproc/libucl port.

## Gotchas

- `make` on the mac needs FREEBSD_SRC (a FreeBSD tree) for libucl:
  export FREEBSD_SRC=~/freebsd-development/freebsd.
- recipe roots are still main's single tree (foundations/, containers/,
  systems/ at zconfig's top); m2-remotes changes that to a directory a
  recipe repository.

## Commit

The commit after 6827ab0, "config: the language carried, its settings
the new model's".

## Box run 3

KIDON, 2026-10-20, at 230f87d (results c8df572): 73 ok, 0 failed.
make check-freebsd, now in the box run: every unit test linked against
the textproc/libucl port, the parse cells among them, ok; doccheck ran
there too, 38 commands. Tested.
