# shtetl recipes

The configuration root of shtetl (zconfig, planning.md 4.4), as
upstream ships it: `shtetl bootstrap --recipes <url>` clones it into
the config root.

- `shtetl.conf`: the site's settings and the default of every recipe.
- `foundations/`, `containers/`, `systems/`: recipes, a file each.
  Upstream ships none yet.

This directory is the seed of the zshtetl-recipes repository until
that repository exists; it lives in shtetl's own tree meanwhile.
