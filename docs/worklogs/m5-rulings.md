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

## Gate merges through zfs_rebase -m (m6-gate-merge)

Ruled 2026-10-15: a clean three-way merge goes on, "true of zfs_rebase
too", by a new "--merge-non-conflict (-m)" in the tool. The tool's half
is zfs_rebase_tool 69d8223 (its worklog: freebsd-development's
sprints/sprint-6/issue-docs/merge-non-conflict.md). shtetl's half: the
rebaser takes SH_RB_MERGE on a --continue and rs_merge on a start, and
the update always asks for both, so a step whose every conflict merges
clean goes to done and one that does not pauses on what is left. The
fake rebaser models it (rbfake_merges: the paths that merge clean;
-m answers them keep, and a start left with nothing unanswered goes to
done).

Verification: check_rebase W8 (the start's argv ends -o ... -m; the
--continue's -m before --no-merge and -i); check_update G6 (every
conflict merged: no pause, the staging tree made; one merged and one
not: paused on the one, the other keep, then on with the -m
--continue) and the fake logs re-recorded with -m (the two checks
that /usr and /www are never rebased kept honest with the suffix);
five mutations caught. make check, gate, xcheck.

The box: build.sh's U9 counted on a pause at the family tree, which
-m now merges, since box's update changes it near the top and the
admin's line is at the end. So shbup now shows the merge (no pause,
the admin's line kept beside the update's change, the tool's merged
line in the update log, W8), and shbab gets an edit to the very line
the update takes out, which still pauses and is answered onto before
the abandon. The box needs zfs_rebase 69d8223 or later installed.

## Cutover and abandon, put back on failure (m6-cutover)

Ruled 2026-10-15: no longer special resumable verbs; "simplify", and
"pruning is the last step of cutover". This overturns 2026-10-14's
cutover that read where it stood from the trees and carried on when
run again (7c71389).

What was built (src/core/cutover.c, rewritten). The run is a straight
sequence. Each step that changes something takes a note first (struct
kc_undo: a rename, a jail stopped or started, a file's bytes, a mark
taken off), and a step that fails, or finds nothing to do, takes its
note back. The cutover, in order:

1. X's jail stopped, and its final save if it wrote since its newest
   save.
2. X-staging's jail stopped.
3. X renamed aside as X-<its version>, its previous root.
4. X-staging renamed X.
5. X-staging's block removed and X's written for the new X.
6. In zlake, X archived and the staging mirror renamed X.
7. The mark taken off.
8. X started, and its closing save.

A failure anywhere in that sequence puts every noted step back, last
first (kc_back), and the run says "cutover of x failed and was put
back". If putting back fails too, it stops there and says each step
not put back. The prune follows, and only warns (into the new W sink
of sh_cutover_run). The new X's clones of the previous root's datasets
are promoted off it. The previous root is then destroyed, unless
kp_destroyable finds a hold or an outside clone, in which case nothing
is destroyed and the root is left whole. In zlake, the renamed
mirror's clones are promoted off the archive. An abandon is the same
shape: X-staging stopped, its block removed, the mark off and X
started are all noted, and X-staging's destroys come last and only
warn.

Decisions beyond the plan:

- The closing save is the one step that can half-succeed: made on
  zrun, its mirror into zlake failing. That is a save's own exception
  (7: the next save brings the mirror up to date), so the cutover
  counts it done. The save runs into a memory sink (kc_close), and its
  messages go to W when the zrun snapshot is there, to E otherwise. A
  final save that half-succeeds still fails the cutover: the point of
  that save is that the archive holds X's writes.
- Gone with the resuming: the plan's refusal on a hold or an outside
  clone of the old X (only the prune meets those now, so the cutover
  still goes through); kp_promoted, kp_stopped, kc_saved; the
  abandon's refusal once a cutover has promoted; kp_resumed. The
  refusal on a snapshot name a promote would meet stays in the plan,
  so the prune does not fail on it, and so does the archive's name.
  New: the refusal on the previous root's name taken.
- The previous root is a sibling on zrun, containers/X-<version>, as
  X-staging is. A previous root the prune left behind is not taken up
  by anything yet: M7's prune and retention are where it belongs.
- The undo restores both jail blocks byte for byte, rather than
  writing them again from the recipe, which may have changed since X
  was created.

Verification: check_update.c, K1 - K4 updated, and K5, K8, K9 new
(test_cutover_back, test_abandon, test_prune):

- K5: a failure at each changing step, with X running or not and
  written or not, puts back the trees, their mounts, both jails, both
  blocks byte for byte and both marks; run again, it cuts over. Also
  covered: putting back stopped part way, and the closing save's
  mirror failing.
- K8: an abandon put back, and its destroy failing.
- K9: the plan's refusals, and what only the prune meets (hold,
  outside clone, either promote failing).

Fourteen mutations caught (no put-back; a stop not noted; bytes not
kept or not written back; a mark not noted; renames not put back; the
prune destroying past a hold; no prune; mirror lag counted a failure;
zlake's promote skipped; no abandon destroys; the previous root's name
unchecked; X-staging's block left; put-back not stopping at its first
failure). make check (1642 checks in check_update), the gate, xcheck.

The box: build.sh's K7 now also checks that shbup's previous root is
gone, with no warning, and that none of its datasets is a clone of it.
The box run is owed.
