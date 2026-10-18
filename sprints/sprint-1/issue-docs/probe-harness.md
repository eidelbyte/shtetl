# probe-harness

## What was built

probes/run.sh, probes/lib.sh, probes/README.md: `sudo sh
probes/run.sh [P1 ...]` on the box. Each probe runs in a subshell with
its output in probes/results/<host>-<time>/P<n>.log, on pools made
for it (s2run, s2lake) and destroyed after it. Every check is a line
of summary.txt naming its cell; the first line names the commit, the
last is `== N ok, M failed`. The run's directory is committed and
pushed as the person behind sudo.

## Decisions

- Written fresh after main's probes/ and tests/box/send.sh, smaller:
  no timing helpers, no fixture trees, nothing a probe here does not
  call.
- Pools on swap-backed memory disks labelled shtetl-probe
  (`mdconfig -L`), not sparse files: --clean finds exactly the disks
  it made by their label (mdconfig -l -v prints it as the fifth tab
  field, sbin/mdconfig/mdconfig.c md_list), and there are no image
  files to remove.
- Pools under an altroot (zpool create -R): every mountpoint lands
  below it, and an altroot pool is never in zpool.cache, so a reboot
  never imports a leftover.
- Pool names s2run and s2lake, apart from main's shprun and shplake
  and zfs_rebase_tool's zrt*, so all three harnesses can share a box.
- Cleanup takes away what holds a pool busy first: s2p* jails, then
  any mount under the altroot that is not ZFS (P2's devfs).
- One log line kind per outcome: ok, failed, answer (a fact for the
  plan, neither), failed setup.
- A vm probe runs only when named, with SCRATCH_VM=yes; a
  zfs_rebase probe is skipped without one.

## Verified

sh -n and dash -n over both scripts; on the mac, --list, the usage on
a bad argument (exit 2) and the FreeBSD-only refusal. A box run is
owed: the tracker's check is run.sh with nothing selected printing the
header and `== 0 ok, 0 failed` and leaving no pool or disk.

## Gotchas

- zsh, the mac's shell for tool calls, reads `echo ====` as a
  command lookup; not a problem for the scripts, which are /bin/sh.

## Commit

d0cd4e0
