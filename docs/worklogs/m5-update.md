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
