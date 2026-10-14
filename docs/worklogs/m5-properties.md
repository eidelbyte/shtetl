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
