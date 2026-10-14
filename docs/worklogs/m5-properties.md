# Fewer properties, fewer config snapshots (worklog)

Not a tracker issue: three cuts the user asked for on 2026-10-14
(planning.md 14, "fewer properties, fewer config snapshots"), each a
commit of its own. Every shtetl:* property's readers were surveyed
first; the survey's findings are what each section rests on.

## shtetl:config-guid dropped

Written on every version, -pkg, save and staging version; read only
to be carried to the next of them (sv_lastconfig, sh_pkg_run). Nothing
compared it with the guid of the snapshot shtetl:config names, so it
guarded nothing. Gone from save.c, build.c and update.c.

Two guid reads were also existence checks, and stay as such, by type:
sh_save_plan's of a given config snapshot ("no such config snapshot")
and fb_resume's of the paused build's (gone, the build stays paused;
check_build's resume-after-destroy cell still covers it).

Verification: check_save, check_build and box F11 now assert the
property is not set; make check, gate, xcheck. The box owes F11's
new line with the next build.sh run.

## shtetl:replayed-from dropped

Set by the update on each replayed save (the original's guid) and read
by nothing. A replayed save keeps its original's name in the staging
dataset, which is the link; after a cutover the originals are in
zlake's archive under the same names. uq_snapprops was the guid and
the copy; the copy (sh_shtetl_copy) is called in its place.

tools/story.py and the story pages still show it; they have been
stale since draft 4 and are regenerated separately.

Verification: check_update asserts the replayed save carries its
original's shtetl:commit and no shtetl:replayed-from; make check,
gate, xcheck.

## Config snapshots only on a change, named three words

src/core/cfgsnap.c, sh_cfgsnap, in place of the three copies in
fb_begin, cr_config and uq_config. It finds the config root's newest
shtetl:kind config snapshot (by creation, not name; other snapshots on
the root, bootstrap's or a person's, passed over) and asks ZFS for
written@ it on the root: 0, and that one is recorded, nothing taken,
nothing said. Otherwise, or with none yet, one is taken.

- The name: three different words of a 256-word list (short ASCII
  nouns), drawn from splitmix64 of the time and a try count. Not
  random: the same second and try give the same name, so the tests
  know it (sh_cfgsnap_name), and a taken name moves to the next try,
  up to 100, then a refusal.
- Beyond what was asked: the new snapshot carries shtetl:commit, "build
  F", "update F", "create X on F@V", "update X to F@V", since its name
  no longer says what took it (and planning's 2026-10-08 ruling puts a
  message on every snapshot). A snapshot recorded again keeps the
  first one's message.
- Bootstrap's own snapshot of the empty root before it clones the
  recipes is left as it was (@bootstrap-<time>, no kind): it is a
  place to go back to, not a record of recipes.
- written@ is any write: a recipe edited and edited back, or git
  touching .git, takes a new one. Reads write nothing with atime=off,
  which bootstrap sets on the config root by default; a shtetl.conf
  setting atime=on there would make every run take one.
- Tests: the command suites write to tank/zconfig where a recipe
  changes (check_build's config() switching roots counts as a write,
  check_create's world writes the containers' recipes after the
  build's snapshot), so reuse and change are both exercised; F26's
  paused update had been relying on a fresh snapshot per build and
  failed until config() counted its switch.

Verification: check_cfgsnap 162 checks (family N1 - N7, written
first); check_build, check_create, check_update carry N8 (a second
build after a recipe change takes the second name, a failed build
after that reuses it; a create after another reuses its snapshot, a
create right after the build the build's; an update with nothing
changed the build's, after a write its own); 11 mutations of cfgsnap.c
all caught. make check, gate, xcheck. Box: build.sh gains N9 (atime
off on the config root; P17's build records F13's failed build's
snapshot, nothing having been written; F27's after the patch edit a
new one, "update box") and X9 checks the create's snapshot by its
message; owed with the next run.
