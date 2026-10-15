# Box trip: M5 (2026-10-14)

What the box has not seen yet. Its last runs were run.sh at 2c89594
(115 ok, 0 failed) and build.sh at 2d1ef76 (101 ok, 0 failed), both
before M5. Since then:

- M5: `shtetl update` into a staging tree with the real zfs_rebase,
  its conflicts gate, `cutover` and `abandon`;
- a cutover or abandon stopped part way carries on when run again,
  with prechecks for holds, outside clones and name clashes;
- `shtetl:config-guid` and `shtetl:replayed-from` dropped;
- config snapshots taken only when the config root changed, named
  three words.

Run it at 0bcc801 or later. The scripts take minutes: run.sh took
seconds last time, and build.sh about three, building from the store
its last run kept; the update cells add a few more.

## 0. Before you start

On KIDON, as your own user:

- FreeBSD 15.1 or later (zfs_rebase links recallocarray; KIDON is
  15.1-RELEASE-p2), with the FreeBSD source in /usr/src: zfs_rebase's
  build takes the OpenZFS headers from it, and build.sh clones it.
- Network: build.sh fetches the branch from git.freebsd.org, clones
  shtetl-recipes from GitHub (B13) and runs pkg upgrade (F33).
- Nothing of a stopped run in the way. Each script puts its own
  leftovers away, but refuses to start beside another pool with a
  shtetl root, or a jail of its names:

      zfs get -H -s local -o name,value shtetl:space
      jls name
      ls /etc/jail.conf.d

  The first should list nothing (build.sh's pool is exported between
  runs); the test jails are shbweb, shbup, shbup-staging, shbab and
  shbab-staging.
- Git credentials for a push as yourself: the logs push themselves
  back (tests/box/send.sh). If sudo drops your ssh agent, the commit
  stays and the script says to `git push`.

## 1. Install zfs_rebase (new for this trip)

`shtetl update` runs the real zfs_rebase from shtetl's own PATH
(/sbin, /bin, /usr/sbin, /usr/bin, /usr/local/sbin, /usr/local/bin).
Install the current one even if an older build is there: shtetl
passes --result, --allow-unrelated --base, -o and --no-merge.

    # once: git clone https://github.com/eidelbyte/zfs_rebase_tool.git
    cd ~/zfs_rebase_tool
    git pull                    # main at bd7ec81 or later
    make freebsd
    sudo make install           # zfs_rebase, zfs_rebase-picker, zfs_rebase.8
    which zfs_rebase            # /usr/local/sbin/zfs_rebase

Without it, build.sh says "W7 zfs_rebase on shtetl's PATH: install
it" and skips U9 and K7; everything else still runs.

## 2. Bring shtetl up to date

    cd ~/shtetl
    git pull                    # 0bcc801 or later
    make freebsd                # shtetl and build/box_zcheck

## 3. run.sh: the ZFS calls (seconds)

    sudo sh tests/box/run.sh

New since the last run, through build/box_zcheck against libzfs:

- Z21 promote: a clone takes its origin's snapshots up to the origin,
  the old dataset becomes its clone; no such filesystem and not a
  clone refused.
- Z22 rename: a filesystem and everything below it, mounts following;
  no such filesystem, the new name taken, a missing parent refused.
- Z23 written@SNAP: a number, 0 when nothing was written.

The bar: the last line `== N ok, 0 failed`, N above last time's 115.

## 4. build.sh: build, container, update, cutover (minutes)

    sudo sh tests/box/build.sh

No --clean: that throws away the kept build root and obj cache and
makes the first build take hours again.

New and changed cells, in the order they run:

- F13: the failed build of nope keeps its config snapshot, now found
  by its message, "build nope", not by its name.
- N9: the config root is atime=off (bootstrap's default), so reading
  the recipes writes nothing to it.
- F11: "no shtetl:config-guid" (it was "shtetl:config-guid is its
  guid"). N9: box's version records F13's config snapshot, since
  nothing was written to zconfig in between.
- N9: F27, after the patch edit, records a new config snapshot whose
  message is "update box".
- X9: the container's first save names a config snapshot whose
  message starts "create shbweb".
- W7: zfs_rebase found, and its output in the update's log.
- U9: shbup is created on box's first version, given an edit to
  /usr/share/misc/bsd-family-tree (which box's update also changes)
  and a later one to /etc/motd.template, each saved. `shtetl update
  -y shbup` with no terminal pauses at the conflict (exit 3). The
  script answers every unanswered line onto in the resolution file,
  as you would by hand, and runs `--continue` through any further
  gates. Then the staging tree is checked: on box's newest, the family
  tree as the update has it, the motd edit replayed, www cloned with
  its file, the commits kept, its jail running, shbup stopped.
- K7: `shtetl cutover -y shbup`: shbup on box's newest, the staging
  tree gone, its jail running again, the old tree in zlake's
  archive/containers/shbup-<old version>. shbab is updated and
  abandoned: the staging tree gone, shbab running, unchanged.

The bar: the last line `== N ok, 0 failed`.

## 5. Tell me

Tell me once both logs are pushed (commits "box: KIDON ...: n ok, m
failed"). I read each log's header first: its "shtetl <sha>" must be
0bcc801 or later, since an older one means the box ran stale code.
Then its ok/failed line, then any failures. On a green run I mark the
five M5 issues tested and planning.md 12 marks M5 built.

## If something fails

The log has every shtetl command with its exit status, and around
the update and cutover cells the command's output. When one of those
fails, its errors follow, then the end of the update's own log, where
shtetl puts zfs_rebase's output: the run's teardown deletes that file,
so the copy in the results log is the one that comes back. A failing
check reads "FAIL <cell> <what>: got [...], wanted [...]". Some likely
readings:

- W7 "no ... on shtetl's PATH": step 1 not done, or installed outside
  that PATH.
- U9 failing at the pause, or in a `--continue`: zfs_rebase's words
  are in the copied update log. A rebase a failed run left open is
  aborted by its teardown, so nothing needs doing by hand.
- K7 refused with "... is held (zfs holds), which keeps it from being
  destroyed": a hold left on the old tree's snapshots after the update
  was done. That is the new cutover precheck catching it before
  anything changes, and it points at zfs_rebase.
- K7 refused with "shbup-staging has no mirror in zlake": the update's
  closing save did not mirror the staging tree.
- N9 "nothing written to zconfig since F13" failing: something wrote
  to the config root between the two builds. Harmless, since each
  build then just takes its own snapshot, but change detection is
  weaker than designed, and I want to know what wrote.
- Z21 - Z23: the libzfs layer for promote, rename or written@; the
  got and wanted say which way it went.

## The second run (after 2026-10-15's)

The first run: run.sh at 83c8e39, 126 ok, 0 failed; build.sh, 125 ok,
1 failed, X9: the create recorded the last build's config snapshot,
written@ having been read before a transaction group committed the
container's recipe. Fixed by a sync of the pool before every written@
read. Again, from step 2 (zfs_rebase stays installed):

    cd ~/shtetl && git pull && make freebsd
    sudo sh tests/box/run.sh        # Z24 new, two more cells
    sudo sh tests/box/build.sh      # X9 again

## What the box does not cover

- A cutover or abandon stopped part way and run again (K5, K8, K9):
  provoked only in the fake.
- The picker at a gate: it needs a terminal, and build.sh answers
  through the resolution file instead.
