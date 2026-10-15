# Rulings carried out after M5 (worklog)

Not tracker issues: code changed by the user's rulings once M5 was on
the box. Each section is a commit of its own; the rulings are quoted
in planning.md 14.

## A failed recipes clone leaves an empty config root (9852320)

Ruled 2026-10-15 (proposal 1): "we should still populate a root. it
would act like a root with no repos." bx_clone, on a failed git clone,
rolls the config root back to the bootstrap-<time> snapshot it took
just before (git removes a partial clone itself, but the rollback
makes it so whatever happened, as the rule over section 7 now asks of
every failure), says "cloning URL failed: ROOT left empty, with no
recipes", and returns 0, so bootstrap goes on to mark every root. It
had refused with no root marked.

Verification: check_boot B13 (the rollback, the directory empty, the
config and lake roots marked; a mutation skipping the rollback
caught); make check, gate, xcheck. The box's B13 clones a real
repository and does not provoke a failure.

## shtetl beam asks nothing (db54301)

Ruled 2026-10-15 (proposal 15): "beaming should be automatic, not
interactive." cmd_beam plans and runs, with no plan shown and no
confirmation; -y went with the question and is now an unknown option
like any other (no special message: nothing has been released).
Create and update already beam on their own when the version they
need is not on zrun, and show it in their plans. The command stays
for bringing a version down by hand.

Verification: run-cli m7-yes added (64 cases) and the 44 recorded
outputs that print the usage re-recorded, beam's two lines the only
change; docs/configuration.md's usage example re-recorded; build.sh's
three beams without -y; make check, gate, xcheck. The box owes M8
with the next build.sh run.

## A clean three-way merge of a patch goes on (a1bda78)

Ruled 2026-10-15: "a clean 3-way should just continue." The three-way
merge moved from fb_pause into the patch loop: a patch that does not
apply, nor in reverse, has the tree written as a git tree (the base
a fix would need) and is merged with git apply --3way; with no
conflict the build goes on, saying "merged three ways without
conflict", and the patch's outcome is the new SH_PO_MERGED ("merged"
in shtetl:patches and the pause record). A merge is not a fix, so
nothing is kept for it in the obj cache's patches. Conflict markers,
or a merge that cannot happen (--reject), pause as before.

Verification: check_build P9 (the --continue after the hand fix runs
to the end, 3-l1 merged; the record "2-x4.patch fixed, 3-l1.patch
merged"; only 2-x4's fix kept; a mutation that pauses anyway caught);
check_patch reads "merged" back and refuses an unknown word; make
check, gate, xcheck. The box's P17 has no clean merge to show.

## The build root without snapshots (m6-build-root)

Ruled 2026-10-15: "we don't need to snapshot the build root while
patching." The build root is not transient (it is the src checkout,
kept so builds are incremental), but it is a git checkout, so git
puts it back: fb_reset is git reset --hard and git clean -fd; at the
end of a build, on a failure (FU_PATCHES) and on --abort (fb_unpatch);
--skip resets and applies the patches before the skipped one again as
the build left them (fb_reapply: as they are, --3way for a merged one,
the kept fix for a fixed one). @shtetl-patch-N, fb_psnap and the
retouch are gone: git writes every file a patch changed anew, so its
time is now's, which is what the retouch did by hand after a ZFS
rollback gave the old times back (box build 6).

Verification: check_build P5, P10, P11, P15 (no build root snapshot,
none rolled back to; a patched file's time now's after the build; the
--skip, --abort and failure paths leave the checkout clean); four
mutations caught (no reset, no clean, no reapply, no undo). The test
hook no longer copies the build root. Box: build.sh's P17 counts no
snapshot of the build root and finds the checkout clean, and its
teardown resets a stopped build's checkout with git; owed with the
next run.
