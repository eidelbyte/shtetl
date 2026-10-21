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

## Box run 1

KIDON, 2026-10-19, at 68bc7f8 (results 1c23d85).
The whole harness on the box: the summary, the cleanup after a probe's
failed setup (P2 left a devfs mount under the altroot, which
pools_down took away before destroying the pools, and P5 then ran on
fresh pools), and the send as the person behind sudo. Tested.

## The VM: probes/vm.sh (2026-10-20)

Asked for, so that the author can run P3 and P4. On a FreeBSD host
with bhyve, as root:

- make: FreeBSD's own ZFS VM image of the host's release, fetched
  from download.freebsd.org/releases/VM-IMAGES and held to its
  CHECKSUM.SHA256 file, unpacked and grown to 20G (the image grows its
  partition and pool at its first boot, growfs_enable in its rc.conf).
  Its pool is imported here by its own disk alone, under a name of the
  host's (zpool import -t: the disk keeps zroot, which the host's own
  root pool may be called too), and given a hostname for the results'
  directories, the serial console, this checkout cloned into
  /home/shtetl (outside the boot environments, as the probes ask), and
  git-lite through pkg -r for the summaries' commit line.
- run: bhyve with the UEFI firmware, 2 CPUs and 8G (P4's memory disk
  is twice the root), the console on the terminal, so the loader's
  menu (P3.6) and outer space's words are seen; booted again after
  each reboot, which bhyve ends with status 0, until it powers off.
  P4's reboot -r is no VM reboot.
- results: the VM off, its pool imported again, its probes' result
  directories not here yet copied into probes/results, and sent as
  run.sh sends (lib.sh's send, as the person behind sudo).
- destroy: the pool exported and the disk detached if a crash left
  them, the directory gone.

The VM has no network: the image's packages go in from the host, and
the results come out through its disk, so no credentials go into it.

Decisions: the layout P3 and P4 assume, zroot/ROOT/default at /
canmount=noauto with /home a dataset of its own, is the image's
(release/tools/vmimage.subr); bhyve's -A -H -P are in its options in
15 and in CURRENT.

Verified: sh -n, gate. Not run: there is no bhyve here; the author's
first make and run on KIDON is its test.

## vm.sh's first run, and send (2026-10-21)

On KIDON the checksum's name was wrong (3ae9d48: CHECKSUM.SHA256), the
console was the terminal that ran bhyve and could not be come back to
(1d19568: a null modem, cu, the boot loop under daemon(8)), and P3's
note said reboot where a plain one was meant (f551fb7). Then P3 and P4
ran, 25 of 25 (eb1e88b).

That commit also took 39 files the checkout held untracked, the
compressed install logs of main's probes P6, P12 and P15 from
2026-10-11 and 10-12 (main has their folders, not these files): vm.sh
gave send the results directory whole. They stay: they are in v2's
history now, and taking them out would delete the box's only copies at
its next pull. send now takes the summary line first and then the
paths, commits those and nothing else, and vm.sh gives it the
directories it copied; run.sh, tests/box/run.sh and build.sh give
theirs. Checked against a scratch repository with su a function: the
two directories given committed and pushed, a stray one left
untracked.
