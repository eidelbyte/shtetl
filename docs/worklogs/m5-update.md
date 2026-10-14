# M5: updates (worklog)

Update into staging, history replay, the conflicts gate with
derivations, cutover and abandon (planning.md 7.6 - 7.10, 9).
Tracker: tracker/shtetl.json, m5-zfs, m5-rebase, m5-update, m5-gate,
m5-cutover. The matrix came first (f5571f7): families W, U, G, K and
cells Z21 - Z23, S10.

## m5-zfs

The ZFS calls cutover and abandon need, in src/core/zfs.h:

- sh_zfs_promote, over lzc_promote: refused for no such filesystem and
  for one that is no clone, looked at first so said in the contract's
  words; EEXIST, a snapshot name the origin shares, names it.
- sh_zfs_rename, over zfs_rename, whose changelist moves the mounts:
  refused for no such filesystem, the new name there already and its
  parent missing, each looked at first.
- written@SNAP through sh_zfs_get, by zfs_prop_get_written_int: a
  number, the binary source of it, printed; a snapshot that is not
  there refused.

The fake: promote moves the origin's snapshots up to the clone's by
transaction, clones of them following, and swaps the two origins, as
dsl_dataset_promote does; rename moves a filesystem, its children,
their snapshots and every origin naming them, remounting what was
mounted where its mountpoint now says; zfake_write records bytes
written, which written@ counts after a snapshot's transaction.

zcheck gains the cells for the box. Its written@ cell writes 64 KiB at
the dataset's mountpoint and waits up to 30 s for a transaction group
to count it; over the fake, zcheck-fake.c points the write at
zfake_write (zcheck_write).

Verification: check_zfs 227 checks; zcheck over the fake 75 cells;
make check, gate, xcheck. Box owed (run.sh). tracker: m5-zfs testing.

## m5-rebase

zfs_rebase as shtetl runs it, src/core/rebase.c (planning.md 9,
7.6.1, 7.7).

- An injected rebaser, struct sh_rebaser: start, continue and abort.
  The real one, sh_zfs_rebaser, runs zfs_rebase by its name on
  SH_RUN_PATH (the port installs it in /usr/local/sbin), its output in
  the caller's log; its exits are the tool's (0 done, 1 at the gate,
  2 refused, 3 failed or drift) and anything else is -1, sh_run's word
  on it passed on. A start takes --from, --onto, --result and -o, with
  --allow-unrelated --base for a replay step. --continue -i, the
  picker, is forked on the terminal shtetl has, as an editor is; every
  other call goes through sh_run (stdin /dev/null). sh_run_find is
  sh_run's PATH lookup, exported for that.
- A rebase is named by its result dataset in both forms: the clone
  form's record is on the clone, the dataset form's on onto, which is
  the same staging dataset. The tool's run directory, and so its
  private mount, is /var/db/zfs_rebase/<that dataset>/mnt (run.c's
  rundir_of), sh_rb_privmnt.
- The resolution (v4-manifest.md 8): read line by line into the tree,
  each directory's decoded name pushed until its "..", a name's
  \\ooo escapes decoded, conflict N and drift lines with their choice;
  #unanswered held to the lines, as the tool holds it. sh_res_keep
  answers the lines of given paths still "-" and counts #unanswered
  again; every other line goes back as it came. sh_res_write writes
  PATH.shtetl and renames it over, as the tool writes its own.
- tests/rbfake.c, the fake rebaser over the fake ZFS (the clone form's
  result unmounted, mountpoint none, readonly; the dataset form's
  pre-apply snapshot; a test's conflicts stopping a start at the gate
  with a resolution in the tool's grammar and the record on the
  result; --continue done when complete and not --no-merge; --abort).
  It is a test object; the box link (zcheck over the fake) takes the
  fake ZFS alone.

Verification: check_rebase 93 checks (W1 - W6: the real rebaser
against a script logging its words, every exit, not found, the
picker path; the resolution read, refused, answered and written byte
for byte; the fake held to the tool's contract); make check, gate,
xcheck. The real tool on the box comes with the update (W7).
tracker: m5-rebase testing.

## m5-update

shtetl update [-y] [--to v] X, --continue X, --abort X (planning.md
7.6, 7.6.2), src/core/update.c; three commits before it: the jail's
block in src/core/jail.c (create's, now shared), save taking a staging
tree's clones of another container's datasets (S10), sh_tools' jls.

- The plan: X's foundation and version from its root's origin; the
  target and its checks (U1); X's datasets classified: replayed (F's,
  not a placeholder), cloned from the target (F's placeholder), cloned
  from the from-save (X's own); X's saves to replay, update.depth's
  newest but the from-save, which the run takes.
- The run: config snapshot, beam, X stopped, the from-save, F mounted,
  each dataset in order, F unmounted, X-staging@V, its jail, its save.
  A replay is a loop that reads where the dataset stands from ZFS each
  time round: the leading saves the staging dataset holds (step 1 left
  none, step k left the pre-apply s(k-1)) and whether the tool's record
  is on it (a step open at its gate). So a paused update, or one cut
  short, is taken up from what is there, not from a counter.
- A gate: the picker on a terminal, then --continue once complete; or
  a pause, exit 3, the record written with state paused. --abort and
  every failure share uq_undo.
- Decided beyond the plan (PROPOSAL, 7.6 as built): the pause and its
  record; X stopped for the whole update, started by --abort; the
  staging jail named X-staging with X's hostname; depth's interval
  read as every save for now.

Gotchas:
- A mutation written f() * 0 or (f() != 0 && 0) still calls f; only
  (0 && f() != 0) skips it. Two such were redone.
- A finished update keeps its record, state staged, until cutover or
  abandon: a test's world clears it.

Verification: check_update 392 checks (U1 - U7: refusals; the plan and
its showing; a whole run with three saves replayed, a placeholder and
an own dataset; depth 1 and 2; a gate paused, taken up unanswered,
answered and continued; the picker on a terminal; --abort; a refused
and a failed rebase undone); seven mutations of update.c caught;
run-cli U8 (59 cases); doccheck; make check, gate, xcheck. The box
owed (U9). tracker: m5-update testing.

## m5-gate

Conflicts and derivations, the text first (planning.md 7.7), in
update.c's gate.

- uq_derivs: the recipe's derivations whose derived path is in the
  dataset at hand (uq_owner, the longest dataset path in front of it),
  their paths as that dataset's tree spells them; a source in another
  dataset is warned of and left out (7.7's open detail).
- uq_gate: the derived paths still "-" answered keep (sh_res_keep,
  written whole); the picker or a pause; then, the document complete,
  uq_derive runs each noted derivation whose source is not answered
  from, by chroot into the private mount (sh_rb_privmnt), one run per
  distinct command; a source answered from leaves a marker,
  d<i>.<k>.derive beside the manifest, and its derivations run after
  done, by chroot into the placed dataset (uq_owing). The replay loop
  pays every owed step first, so a pause or a crash between done and
  the run loses nothing.
- A derivation failing pauses the update with the step's rebase still
  at its gate; --continue runs it again.

Verification: check_update 537 checks (G1 - G5: pwd.db kept and made
again in the private mount; its source answered from, made after done
in the placed tree; var's /db/x.db; a failing one paused and run
again; a source in another dataset warned of); five mutations of the
gate caught; make check, gate, xcheck. The box owed (U9).
tracker: m5-gate testing.

## m5-cutover

shtetl cutover [-y] X and shtetl abandon [-y] X (planning.md 7.9,
7.10), src/core/cutover.c; jail.c gains sh_jail_running and
sh_jail_do, update.c exports its record (sh_update_record).

- The plan: X and X-staging there, the update's record saying staged;
  for a cutover, the staging mirror in zlake, X's written@ its
  from-save per dataset, and X-staging's clones of X's datasets (the
  promotes). Shown before the confirmation, writes and all.
- The cutover: a final save if X wrote; X-staging stopped, its clones
  promoted; X stopped, unmounted, destroyed; X-staging renamed X, the
  jail's block rewritten; in zlake the same promotes, the old X to
  archive/containers/X-<date>, the rename; X started and saved, its
  save going onto the renamed mirror incrementally. A failure stops and
  says what is done. The abandon: the staging tree gone on both sides,
  X started.
- The tests are check_update.c's (test_cutover): a cutover needs an
  update done, and that file has the world for one.

Verification: check_update 704 checks (K1 - K5: refusals, paused and
mirrorless; a cutover after the container was written to, its final
save in the archive, the archive's www a clone of the live www at the
from-save as P11 found, the next save mirrored; an abandon; a rename
failing, said); four mutations caught; run-cli K6 (63 cases); make
check, gate, xcheck. The box owed (K7). tracker: m5-cutover testing.

## The box (U9, W7, K7)

tests/box/build.sh gains updates(), after the container cells:
shbup created on box's first version, an admin's line added to
/usr/share/misc/bsd-family-tree (which box's update changes, F27) and
a later edit to /etc/motd.template, each saved; shtetl update -y with
no terminal pauses at the conflict; the script answers every unanswered
line onto, as a person would in the file, and --continues, through as
many gates as come (the jail's rc writes /var between saves, and the
foundation's update may meet them); then the staging tree is checked
(box's newest under it, the family tree onto's, the motd edit replayed,
www cloned with its file, the commits kept, its jail running, the
tool's output in the log), cut over (its archive in zlake, zlake's
shbup on box's newest) and, for shbab, updated and abandoned. The
real zfs_rebase must be on shtetl's PATH (/usr/local/sbin, where make
install puts it); without it the script says so and skips. teardown
aborts any rebase a stopped run left open, whose holds would keep the
pool's datasets from being destroyed, and stops every test jail.

## The rulings on M5's proposals (2026-10-14)

- No record of shtetl's: update.conf is gone. sh_update_state reads an
  update's standing from ZFS (a staging tree with a rebase open, or
  without its shtetl:kind version snapshot, is under way; with it,
  done); X's from-save is marked shtetl:update with the target, which
  --continue and cutover read; the config snapshot is taken at the end;
  a pause leaves only the tool's manifest and resolution, which are
  removed when the update is done or given up.
- X stays stopped until the update is done or fails; a failure and
  --abort start it again and take the mark off the from-save (only a
  mark this run set: an earlier update's, copied onto a replayed save,
  stays).
- The archive is archive/containers/X-<old version> (X's origin's
  snapshot before the update); a cutover is refused while that name is
  taken.

Verification: check_update 731 checks (U1's refusals from ZFS, the
state after a run, a pause and an abort, the mark; K's archive name
and its refusal); make check, gate, xcheck.

## A cutover carried on (2026-10-14)

Ruled 2026-10-14 (planning.md 14): a cutover or an abandon stopped part
way is carried on by the same command. src/core/cutover.c rewritten
around it; nothing records progress, every step asks ZFS whether it
is done.

- Where it stands, from the four trees (X, X-staging, and their zlake
  mirrors) and X's shtetl:update mark. X-staging there: from the start,
  or (X gone) from the rename. X-staging gone from zrun: X's newest
  marked snapshot names the target, and X's newest shtetl:kind version
  snapshot is that target when X is the staging tree renamed (a
  cutover stopped after the rename), or is not (an abandon stopped
  after the staging tree's destroy). The replayed from-save carries the
  mark because the replay copies shtetl:* (uq_snapprops), so the
  renamed tree is recognisable; the last step takes every mark off X.
  zlake's half: the old mirror archived while both mirrors are there,
  the staging mirror renamed while it is.
- sh_update_state now answers DONE with FROM "" when X is gone (a
  cutover stopped between the destroy and the rename); before, it
  refused on the missing snapshots.
- Idempotent steps: jails stopped or started only when jls says so;
  the jail blocks written each run; promotes only of datasets still
  clones of the old tree's; the closing save skipped when X's newest
  save already carries "cutover to V" (otherwise a run stopped at the
  unmark would save twice); X's written@ read only before any promote,
  since a promote takes the snapshot it is measured from (found by the
  destroy cell: the re-plan asked written@ of a snapshot that had
  moved).
- Refused in the plan, every reason said, nothing done: a hold on a
  snapshot of the old X, or a clone of one outside it (walking the run
  root), unless the promotes take that snapshot along (kp_moves: a
  staging clone of the same dataset at or after it); a staging
  dataset's snapshot named like one its promote would take over, on
  zrun and in zlake; the archive's name taken. An abandon is refused
  once a cutover has begun promoting or destroyed X (sh_zw_destroy
  would otherwise destroy half of the staging tree and stop on a
  snapshot the old X's datasets are now clones of), and on a hold or
  outside clone on the staging tree.
- Dropped: unmarking zlake. Its snapshots never carry the mark (a save
  sends only the newest snapshot, and X's from-save is marked after its
  save), and a mutation removing that step showed no test could see it.
  Also a run-time look for zlake's X before the archive rename, which
  the plan already decides.
- The fake: zfake_hold, userrefs, and a destroy of a held snapshot
  refused (EBUSY in dsl_destroy_snapshot_check_impl). The test jail
  fails a start while a scratch file jailfails is there.

Verification: check_update 1296 checks. K5 runs a cutover stopped by
a failure at each of: the zrun rename, the archive rename, the staging
mirror's rename, either promote, the destroy, X's jail start, the
save, the unmark; each run again from a fresh plan (which says where
it takes up) to the whole run's end, with one closing save. K8: abandon
refused after a promote and after the destroy; an abandon stopped at
the jail start, and one with the zlake mirror left, taken up; done,
nothing to take up. K9: the plan's refusals, the held and cloned
snapshots a promote takes along not refused. 15 mutations, all caught
but two, both dead code and removed (the zlake unmark, the zlake
look). make check, gate, xcheck. The box owed: build.sh's K7 runs the
whole cutover; a stopped one is not provoked there.
