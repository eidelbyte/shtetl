# Probes (M0)

The probes of `docs/planning.md` section 13, as scripts. Each one
answers a question the design depends on, on pools made for it on
memory disks and destroyed after it.

## On the box

    git pull
    sudo sh probes/run.sh

That runs the thirteen probes that need only ZFS (and zfs_rebase for
four of them), in a few minutes. Then send the results back:

    git add probes/results
    git commit -m "probes: $(hostname -s) run"
    git push

The installworld probes are a second, longer run (below).

## What it needs

- FreeBSD, root.
- `zfs_rebase`, the FreeBSD build, for P4, P5, P14 and P16. The first
  of these that exists is used: `ZFS_REBASE=/path/to/zfs_rebase`,
  `zfs_rebase` on root's PATH, or a `zfs_rebase_tool` checkout beside
  this one (`make freebsd` there). Without one those four are skipped.
  The portable build is refused.
- Room in `TMPDIR` (default `/tmp`) for sparse pool images: 1 to 4 GB
  each for the ZFS probes, of which they write tens of MB.
- P5 and P14 copy `/usr/include` and `/usr/share/man` into their
  foundations; `PAYLOAD="usr/include"` (paths under `/`) changes that.

## The installworld probes

P6, P12 and P15 install a real world into foundations:

    sudo env SRC=/usr/src OLD_REL=15.0-RELEASE sh probes/run.sh --world

- `SRC` (default `/usr/src`) must have a finished `make buildworld`.
  No kernel is needed. Only installworld, distribution and delete-old
  run from it, each into a dataset on the probe pool. If the world was
  built with `MAKEOBJDIRPREFIX`, `SRCCONF` or the like in the
  environment, pass them through `sudo env` too.
- P6 and P15 start from an older release: `OLD_REL` names one whose
  `base.txz` is fetched once from download.freebsd.org and kept in
  `/var/cache/shtetl-probes`; `OLD_BASE=/path/to/base.txz` uses a
  file you have. It should be older than `SRC`.
- `TMPDIR` must not be tmpfs: these pools are 40 GB sparse and write
  a few GB each.
- `kern.securelevel` must be 0 or below (installworld sets schg
  flags, and zfs_rebase can clear them only there).
- Expect 30 to 90 minutes: six installworlds in all. `WORLD_J=8` runs
  them with `make -j8`.

`--world` adds them to the default set; naming one runs it on its own:
`sudo env OLD_REL=15.0-RELEASE sh probes/run.sh P15`.

## Running

    sudo sh probes/run.sh               every probe that needs only ZFS
    sudo sh probes/run.sh P1 P14        just these (world ones too)
    sudo sh probes/run.sh --world       all of them
    sh probes/run.sh --list             the probes, their tier and question
    sudo sh probes/run.sh --clean       remove what a crash or KEEP=1 left
    sudo env KEEP=1 sh probes/run.sh P7 leave P7's pools for a look

It touches only what it makes: the pools `shprun`, `shplake` and
`shplake2`, the memory disks backed by their images in `TMPDIR`, the
mounts under `TMPDIR/shtetl-probe` (`PROBE_MNT` moves them), and
zfs_rebase's run directories for those pools under
`/var/db/zfs_rebase`. It will not start while one of those pools
exists.

## Reading the results

`probes/results/<host>-<UTC time>/`:

- `summary.txt`: the host, ZFS and zfs_rebase versions, then each
  probe's lines;
- `P<n>.log`: the transcript, every command as it ran (`# ...`) with
  its output and any non-zero exit;
- `P<n>-*.manifest` and `.resolution`: zfs_rebase's documents for
  each rebase a probe ran;
- `P6-remains.txt`, `P6-missing.txt`, `P12-still-changed.txt`: the
  long lists;
- `P<n>-*.log.gz`: make's output, kept out of git by
  `probes/.gitignore`.

The lines:

- `answer:` the probe's answer to its question;
- `check ok` / `check DIFFERS`: a value against what the plan expects.
  DIFFERS is a finding about the design, not a broken script;
- `ERROR`: a setup step failed, and the log says which; the probe
  stopped there and the next one ran;
- `skipped`: why the probe could not run here.

## The probes

| probe | plan | asks | needs |
|-------|------|------|-------|
| P1  | 7.5 | does a first save's clone stream land in zlake as a clone of the zlake foundation, without `-o origin`? | |
| P2  | 7.10 | in zlake, does www moved under a new X (a clone of F@v2) still take `send -i www@s1 www@s2`? | |
| P3  | 7.6, 7.9 | on zrun, after clone and promote, does the promoted www keep s1 and its incrementals? | |
| P4  | 7.6, 7.6.2 | rebases on children of a placed, mounted X-staging; a dataset form step on a parent with a child mounted | zfs_rebase |
| P5  | 7.2 | a synthetic installworld rewrite: is rc.conf, edited in X and untouched by v1 -> v2, a conflict? the time, the bytes, the send | zfs_rebase |
| P6  | 7.2 | an update over an older release, in 7.2's order: what remains that a fresh install lacks? | `--world` |
| P7  | 4.3, 4.7, 7.9 | mountpoints by inheritance, canmount's inheritance, a clone across pools, zlake's mountpoints, a rename, the import order | |
| P8  | 7.9 | what moves `written@<from>` on a stopped container: a remount, reads with atime on, a write | |
| P9  | 7.4 | cloning children: parent first, `clone -p`, the placeholder's canmount, what a clone keeps of its origin | |
| P10 | 6.2 | does `shtetl:commit` travel with `send -i`, with and without `-p`? | |
| P11 | 7.9, 7.10 | the whole cutover on zrun and its mirror in zlake: does the next save receive? | |
| P12 | 7.2 | installworld over itself, plain and with `install -C`: what changes, the send, the rebase (and P5 on a real world) | `--world`, zfs_rebase |
| P13 | 4.1 | discovery of roots, the exactly-one-match rule, a mirror in the root holding its foundation | |
| P14 | 7.6.2 | history replay: step 1 clone form, steps 2-3 with `--allow-unrelated --base`; the derived base's conflicts; the cost per step | zfs_rebase |
| P15 | 7.2, 7.6 | a container on an older release rebased onto the new world, updated over it in 7.2's order: which names conflict, and the time | `--world`, zfs_rebase |
| P16 | 7.3, 9 | the zrun foundation mounted read-only for an update and unmounted after; the usr placeholder refused as a side and cloned | zfs_rebase |

Each `P<n>-*.sh` says in its header comment what it builds and what it
looks at. `lib.sh` holds shtetl's moves as the plan describes them
(build, beam to zrun, create, save, mirror, place), so the probes read
as the lifecycle does.
