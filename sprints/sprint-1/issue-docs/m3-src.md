# m3-src: the src checkout, the patches, the recipe repositories

## What was built

src/core/src.[ch], steps a job runs, each git a child (m3-builder):

- sh_src_steps, roll steps 1 and 2 (plan 6):
  - src: init, fetch, reset, clean: zbuild's src made by git init
    when missing, the branch fetched from remotes.src, reset hard to
    FETCH_HEAD and cleaned with -fdx;
  - for each patch source from a remote: init, fetch (--depth 1, its
    ref or HEAD), reset, into zbuild's patches/N;
  - a call, "patches", which lists each source's *.patch (scandir,
    natural order, hidden names left out) and adds one step per patch:
    git apply --3way, named "LOC: NAME".
- sh_src_reset: the checkout put back at HEAD and cleaned, for m3-roll
  to run after a failure (plan 6).
- sh_src_recipes: each recipe repository remotes.recipes names that is
  missing from zconfig cloned into its directory; one there is never
  touched (ruled 2026-10-20).
- sh_src_url: a host and a path over https, a URL as written.
- sh_natcmp, natural order, from main.
- cascade.c's sh_recipe_rel, exported: where a path a layer gives is in
  zconfig (the recipe's place, or the top for the default's), which a
  patch directory needs as the cascade's check already did.

## Decisions, beyond or against the issue

- zbuild's layout (plan 4 amended): the checkout is zbuild's src, and
  each patch source from a remote is fetched into patches/N, out of
  the checkout, where a nested repository would be the clean's to
  skip. Both are made by git init when missing, which makes a first
  roll no different from any other, and a patch repository is fetched
  again at its ref each roll, so nothing about it is kept to go stale.
  A directory of zconfig is read where it is: there is no pause, so
  nothing has to be frozen.
- The src fetch is by URL, from remotes.src each roll: no "origin" to
  keep in step with shtetl.conf. It is not shallow: --3way needs the
  patch's base blob, which a shallow checkout would lack.
- No retouch code. main's 8b27e68 gave the patched files the time of
  now after a ZFS rollback; main then moved to git (836f668), whose
  reset --hard writes each file a patch changed anew (apply --3way
  updates the index, so the reset sees every one). The probe (git
  2.50) and GS6 show it.
- One git apply --3way per patch, nothing else. git (2.32 on) merges
  three ways where the checkout has the base and applies as it is
  where not, so a patch already in merges clean and goes on: main's
  "already" case (a reverse apply) is gone with no code.
- Hidden names are left out of a source's listing: emacs's lock,
  .#x.patch, ends in .patch. Anything else named *.patch is applied,
  and git says why when it is no patch.
- The rc script is left as it is here (below).

## Verified

- make check: check_src, cells GS1 - GS9, GP1 - GP15 and GR1 - GR4 of
  tests/MATRIX.md, over repositories tests/src-fixture.sh makes, as
  file:// remotes, with git run for real.
- Mutations, each caught: the clean left out (GS2), the reset made a
  checkout (GS2), byte order (GP7), hidden names listed (GP7), apply
  without --3way (GP9), directories always at the top (GP4), the ref
  or the dir passed over (GP3), a clone over a repository there (GR3).
  The shallow fetch is economy, no cell's.
- leaks: none of ours (libucl's known 96 bytes from the parse's
  self-check, once a process).
- gate, xcheck (scandir's comparator is POSIX's on both).
- Owed: the box (GS10, a FreeBSD src fetch over https) in m3-box, and
  check_src on FreeBSD with the next box run's make check-freebsd.

## Gotchas for the next issues

- rc's PATH, and service(8)'s, is /sbin:/bin:/usr/sbin:/usr/bin: git,
  /usr/local/bin's, is not found by a daemon rc started ("src: init:
  exited 127"). m3-roll, which first has the daemon run a job, gives
  the rc script the PATH.
- foundation.conf records the src commit and the patch sources' (plan
  5); m3-roll must read them from git without parsing its words where
  it can (the job only logs a child's output).
- A recipe repository is cloned as root into zconfig, which the person
  edits; its owner may want to be theirs. Not decided; m3-box will
  show whether it matters.

## Commit

The commit after d976523 ("job: a child's argv taken const", which
src.c's argv needed under -Wcast-qual).
