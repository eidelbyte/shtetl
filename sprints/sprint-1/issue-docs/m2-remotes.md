# m2-remotes: the remotes, and a recipe found across repositories

## What was built

- shtetl.conf's `remotes` section (site.c): `src`, the FreeBSD src
  remote, built in as https://git.freebsd.org/src.git; `recipes`,
  name = remote, each the recipe repository cloned into the directory
  of zconfig of its name. A remote is a URL with a scheme or a host
  and a path, as a patch's location is; a directory of zconfig is not
  one. sh_site_repos gives the repositories' names in order.
- The cascade (cascade.c) finds a recipe at zconfig's top and in each
  repository (rs_find): in one, its file named with the place
  (`web/containers/nginx.conf`) and the place kept in sr_root; in two,
  the second is refused as a redefinition is, "B: already defined in
  A", the top first and the repositories in their order; in none, "F:
  no such file" as before. A recipe's imports are read in its place
  (ld_import), shtetl.conf's at the top; the paths a layer gives
  (kernconf's config, a patches directory) are read in that layer's
  place (rs_kind); requires finds a container in any place (ck_any).
  sh_recipe_resolve takes the repositories; sh_show reads them from
  the site.
- docs/configuration.md: the layout with repositories, the example
  "repositories", the remotes settings and the remote form.
- Matrix: CS13 - CS15, CC39 - CC45.

## Decisions

- Ruled 2026-10-20 (plan 18): shtetl never updates a recipe
  repository, and the lake on another host is out of scope, so
  `remotes.lake` is an invalid key.
- PROPOSAL: zconfig's top holds the site's own recipes beside the
  repositories, laid out the same, and is looked in first. It keeps
  every example of the docs as it was and lets a site keep a recipe
  of its own without a repository for it; one name at the top and in
  a repository is refused like any two.
- The patch repositories are the locations recipes give in `patches`,
  as on main, not a list in shtetl.conf: one thing named in one place.
- Cloning a missing repository is not here: a clone is a child the
  daemon watches, which comes with m3-builder, so it moved to m3-src
  (its description says so). A repository named and not cloned is an
  empty place (CC42).
- No src/core/recipes.[ch], which the issue named: the lookup is a
  part of the cascade's resolve, and two functions there.

## Verified

- make check: check_site 165 checks (CS13 - CS15), check_cascade 305
  (CC39 - CC45), make doc 38 commands in 16 examples.
- Two mutations of the lookup, each caught: imports read at the top
  (CC39), a second place taken instead of refused (CC40).
- leaks, gate, xcheck.
- No box part: all of it is files.

## Commit

The commit after 7191421, "config: the remotes, and recipes found across
repositories".
